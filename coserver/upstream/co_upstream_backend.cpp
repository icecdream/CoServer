#include "upstream/co_upstream_backend.h"
#include "base/co_log.h"


namespace coserver
{

const int32_t UPSTREAM_ERR_MAX_NUM = 10;
const int32_t UPSTREAM_ERR_MAX_TIME = 5000;


CoBackend::CoBackend(const std::string &name)
: m_upstreamName(name)
{
}

CoBackend::~CoBackend()
{
}

bool CoBackend::avaliable()
{
    if (!m_failCheck || !m_fail) {
        return true;
    }

    // backend down
    uint64_t timestamp = GET_CURRENTTIME_MS();
    if ((int32_t)(timestamp - m_downTimestamp) >= m_confFailTime) {
        m_fail = false;
        m_errNum = 0;
        CO_SERVER_LOG_DEBUG("upstream:%s backend:%s recover(cur:%lu down:%lu fail:%d)", m_upstreamName.c_str(), m_confUpstreamServer->m_serverkey.c_str(), timestamp, m_downTimestamp, m_confFailTime);
    }

    return m_fail ? false : true;
}

void CoBackend::comm_failed()
{
    if (!m_failCheck) {
        return ;
    }

    uint64_t timestamp = GET_CURRENTTIME_MS();
    if ((int32_t)(timestamp - m_errFirstTimestamp) > m_confFailTime) {
        m_errFirstTimestamp = timestamp;
        m_errNum = 0;
    }

    m_errNum ++;

    if (m_errNum >= m_confFailNum) {
        m_fail = true;
        m_downTimestamp = timestamp;
        CO_SERVER_LOG_WARN("upstream:%s backend:%s down (num:%d failnum:%d)", m_upstreamName.c_str(), m_confUpstreamServer->m_serverkey.c_str(), m_errNum, m_confFailNum);
    }

    return ;
}

CoBackendWRR::CoBackendWRR(const std::string &name)
: CoBackend(name)
{
}

void CoBackendWRR::comm_failed()
{
    if (!m_failCheck) {
        return ;
    }

    if (m_confFailNum) {
        // 上游服务器出现错误 降低上游服务器权重
        m_effectiveWeight -= m_weight / m_confFailNum;
        if (m_effectiveWeight < 0) {
            m_effectiveWeight = 0;
        }
    }

    CoBackend::comm_failed();
    return ;
}


CoBackendStrategy::CoBackendStrategy(const std::string &name)
: m_upstreamName(name)
{
}

CoBackendStrategy::~CoBackendStrategy()
{
}

CoBackendStrategyWRR::CoBackendStrategyWRR(const std::string &name)
: CoBackendStrategy(name)
{
}

CoBackendStrategyWRR::~CoBackendStrategyWRR()
{
    for (auto &itr : m_backends) {
        SAFE_DELETE(itr);
    }
    m_backends.clear();
}

int32_t CoBackendStrategyWRR::add_backend(CoConfUpstream* confUpstream, CoConfUpstreamServer* confUpstreamServer)
{
    CoBackendWRR* backend = new CoBackendWRR(m_upstreamName);
    backend->m_confUpstreamServer = confUpstreamServer;

    // 安全检查
    if (confUpstream->m_failTimeout == 0 || confUpstream->m_failMaxnum == 0) {
        backend->m_failCheck = false;
    } else {
        backend->m_failCheck = true;
    }

    if (confUpstream->m_failTimeout < 0) {
        backend->m_confFailTime = UPSTREAM_ERR_MAX_TIME;
    } else {
        backend->m_confFailTime = confUpstream->m_failTimeout;
    }

    if (confUpstream->m_failMaxnum < 0) {
        backend->m_confFailNum = UPSTREAM_ERR_MAX_NUM;
    } else {
        backend->m_confFailNum = confUpstream->m_failMaxnum;
    }

    if (backend->m_confUpstreamServer->m_weight <= 0) {
        backend->m_confUpstreamServer->m_weight = 1;
    }

    backend->m_effectiveWeight = backend->m_confUpstreamServer->m_weight;
    backend->m_currentWeight = 0;

    // 添加backend
    m_backends.push_back(backend);
    return CO_OK;
}

int32_t CoBackendStrategyWRR::init()
{
    int32_t backendSize = m_backends.size();
    int32_t backendTotalWeight = 0;

    for (auto &itr : m_backends) {
        backendTotalWeight += itr->m_weight;
    }

    m_totalWeight = backendTotalWeight;
    m_weighted = (backendTotalWeight != backendSize);   // 权重和服务器数量相等 表示weight全为1 不使用权重值

    // 只有一台上游服务器 不使用权重值
    if (backendSize == 1) {
        m_weighted = 0;
    }

    CO_SERVER_LOG_DEBUG("CoBackendStrategyWRR name:%s, init totalweight:%d size:%d", m_upstreamName.c_str(), backendTotalWeight, backendSize);
    return CO_OK;
}

CoBackend* CoBackendStrategyWRR::get_backend() 
{
    if (m_backends.empty()) {
        CO_SERVER_LOG_ERROR("backends empty, strategy WRR get connection failed");
        return NULL;
    } 

    CoBackendWRR* backend = NULL;
    if (m_weighted) {
        backend = choose_one_weight();
    } else {
        backend = choose_one();
    }

    if (!backend) {
        CO_SERVER_LOG_ERROR("upstrem:%s WRR backend server all down", m_upstreamName.c_str());
        return NULL;
    }

    return backend;
}

CoBackendWRR* CoBackendStrategyWRR::choose_one()
{
    for (size_t i=0; i<m_backends.size(); ++i) {
        m_curBackendsIndex ++;
        m_curBackendsIndex = m_curBackendsIndex % m_backends.size();
        
        CoBackendWRR* backend = m_backends[m_curBackendsIndex];
        if (backend->avaliable()) {
            return backend;
        }
    }

    return NULL;
}

CoBackendWRR* CoBackendStrategyWRR::choose_one_weight()
{
    CoBackendWRR* bestBackend = NULL;
    int32_t total = 0;

    for (auto itr : m_backends) {
        CoBackendWRR* backend = itr;
        if (!(backend->avaliable())) {
            continue;
        }

        backend->m_currentWeight += backend->m_effectiveWeight; // 对每个上游服务器 增加有效权重
        total += backend->m_effectiveWeight;                // 计算所有上游服务器有效权重

        // 如果之前服务器出现失败 会减小effective_weight值来降低它的权重  在选取新服务的过程中 通过增加它effective_weight的值来恢复它的权重
        if (backend->m_effectiveWeight < backend->m_weight) {
            backend->m_effectiveWeight ++;
        }

        // 选取当前权重最大者  作为本次请求的上游服务器
        if (bestBackend == NULL || backend->m_currentWeight > bestBackend->m_currentWeight) {
            bestBackend = backend;
        }

        CO_SERVER_LOG_DEBUG("WRR choose_one_weight key:%s weight:%d eff:%d curr:%d total:%d", backend->m_confUpstreamServer->m_serverkey.c_str(), backend->m_weight, backend->m_effectiveWeight, backend->m_currentWeight, total);
    }

    if (bestBackend == NULL) {
        return NULL;
    }

    CO_SERVER_LOG_DEBUG("WRR choose_one_weight sucess best key:%s", bestBackend->m_confUpstreamServer->m_serverkey.c_str());
    bestBackend->m_currentWeight -= total;  // 对选取的上游服务器降低其权重
    return bestBackend;
}

}

