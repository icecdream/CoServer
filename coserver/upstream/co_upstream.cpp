#include "upstream/co_upstream.h"
#include "base/co_log.h"
#include "core/co_cycle.h"
#include "core/co_request.h"
#include "base/co_common.h"
#include "upstream/co_upstream_backend.h"
#include "upstream/co_callback_upstream.h"


namespace coserver
{

CoUpstream::CoUpstream()
{
}

CoUpstream::~CoUpstream() 
{
    SAFE_DELETE(m_backendStrategy);
}

int32_t CoUpstream::init(CoConfUpstream* confUpstream, CoConnectionPool* connectionPool)
{
    m_confUpstream = confUpstream;
    m_connectionPool = connectionPool;

    if (m_confUpstream->m_loadBalance == UPSTREAM_LOAD_BALANCE_WRR) {
        m_backendStrategy = new CoBackendStrategyWRR(m_confUpstream->m_name);

    } else {
        CO_SERVER_LOG_ERROR("upstream name:%s load balance:%d unknown", m_confUpstream->m_name.c_str(), m_confUpstream->m_loadBalance);
        return CO_ERROR;
    }

    // 添加upstream server
    for(auto &itr : m_confUpstream->m_upstreamServers) {
        CoConfUpstreamServer &confUpstreamServer = itr;
        int32_t ret = m_backendStrategy->add_backend(m_confUpstream, &confUpstreamServer);
        if(ret != CO_OK) {
            CO_SERVER_LOG_ERROR("upstream name:%s add upstream server %s:%d ret:%d failed", m_confUpstream->m_name.c_str(), confUpstreamServer.m_host.c_str(), confUpstreamServer.m_port, ret);
            return CO_ERROR;
        }
    }

    if(m_backendStrategy->init() != CO_OK) {
        CO_SERVER_LOG_ERROR("upstream name:%s init failed", m_confUpstream->m_name.c_str());
        return CO_ERROR;
    }

    return CO_OK;
}

CoConnection* CoUpstream::get_connection()
{
    // 判断是否达到最大连接
    CoBackend* backend = m_backendStrategy->get_backend();
    if (!backend) {
        CO_SERVER_LOG_ERROR("upstream name:%s get backend failed", m_confUpstream->m_name.c_str());
        return NULL;
    }

    CoConnection* connection = NULL;

    // 查看是否可以复用老连接
    std::list<CoConnection*> &resueConnections = m_reuseConnections[backend->m_confUpstreamServer->m_serverkey];
    if (!resueConnections.empty()) {
        connection = resueConnections.front();
        resueConnections.pop_front();
        CO_SERVER_LOG_DEBUG("(cid:%u) upstream name:%s get reuse connection", connection->m_connId, m_confUpstream->m_name.c_str());
    }

    if (!connection) {
        // 没有可复用的连接 需要申请新的连接

        // 是否还可以申请新连接
        if (m_curConnectionSize >= m_confUpstream->m_maxConnections) {
            CO_SERVER_LOG_ERROR("upstream name:%s get connection failed, used connection:%d >= conf maxconnection:%d", m_confUpstream->m_name.c_str(), m_curConnectionSize, m_confUpstream->m_maxConnections);
            return NULL;
        }

        connection = m_connectionPool->get_connection(backend->m_confUpstreamServer->m_host, backend->m_confUpstreamServer->m_port);
        if (!connection) {
            CO_SERVER_LOG_ERROR("upstream name:%s get connection failed", m_confUpstream->m_name.c_str());
            return NULL;
        }
        m_curConnectionSize ++;
        connection->m_startTimestamp = GET_CURRENTTIME_MS();
        CO_SERVER_LOG_DEBUG("(cid:%u) upstream name:%s get new connection, cur connections size:%d", connection->m_connId, m_confUpstream->m_name.c_str(), m_curConnectionSize);
    }

    connection->m_upstream = this;
    connection->m_backend = backend;

    return connection;
}

void CoUpstream::free_connection(CoConnection* connection, int32_t retCode)
{
    CoCycle* cycle = connection->m_cycle;
    CoBackend* backend = connection->m_backend;

    bool closeConnection = false;
    if (CO_EXCEPTION == retCode) {
        // 异常检查 判断连接是否等待复用 如果等待复用 不在错误计数并删除
        std::list<CoConnection*> &resueConnections = m_reuseConnections[backend->m_confUpstreamServer->m_serverkey];
        for (auto itr = resueConnections.rbegin(); itr != resueConnections.rend(); ++itr) {
            if ((*itr)->m_connId == connection->m_connId) {
                closeConnection = true;
                resueConnections.erase(std::next(itr).base());
                CO_SERVER_LOG_WARN("(cid:%u) upstream name:%s exception ret:%d, reuse remove, free it", connection->m_connId, m_confUpstream->m_name.c_str(), retCode);
                break;
            }
        }
    }

    // 连接出错
    if (!closeConnection) {
        if (CO_OK != retCode) {
            backend->comm_failed();
            closeConnection = true;
            CO_SERVER_LOG_WARN("(cid:%u) upstream name:%s ret:%d, free it", connection->m_connId, m_confUpstream->m_name.c_str(), retCode);
        }
    }

    // 连接请求数过大 关闭连接
    if (!closeConnection) {
        if (0 != m_confUpstream->m_connectionMaxRequest && (int32_t)(connection->m_requestCount) >= m_confUpstream->m_connectionMaxRequest) {
            closeConnection = true;
            CO_SERVER_LOG_INFO("(cid:%u) upstream name:%s requestcount:%d >= maxcount:%d, free it", connection->m_connId, m_confUpstream->m_name.c_str(), connection->m_requestCount, m_confUpstream->m_connectionMaxRequest);
        }
    }

    // 连接建立过久 关闭连接
    if (!closeConnection) {
        if (0 != m_confUpstream->m_connectionMaxTime) {
            uint64_t timestamp = GET_CURRENTTIME_MS();
            if ((int32_t)(timestamp - connection->m_startTimestamp) >= m_confUpstream->m_connectionMaxTime) {
                closeConnection = true;
                CO_SERVER_LOG_INFO("(cid:%u) upstream name:%s curtime:%lu starttime:%lu maxtime:%d, free it", connection->m_connId, m_confUpstream->m_name.c_str(), timestamp, connection->m_startTimestamp, m_confUpstream->m_connectionMaxTime);
            }
        }
    }

    // 连接释放 定时器
    CoEvent* readEvent = connection->m_readEvent;
    if (readEvent->m_flagTimerSet) {
        cycle->m_timer->del_timer(readEvent);
    }
    CoEvent* writeEvent = connection->m_writeEvent;
    if (writeEvent->m_flagTimerSet) {
        cycle->m_timer->del_timer(writeEvent);
    }

    if (closeConnection) {
        // 执行出错  真正关闭连接
        m_curConnectionSize--;
        if (cycle->m_coEpoll->del_connection(connection)) {
            CO_SERVER_LOG_FATAL("(cid:%u) upstream del connection failed", connection->m_connId);
        }
        m_connectionPool->free_connection(connection);
        return ;
    }

    // reset在epoll前(清空后再添加epoll/timer 防止连接版本不对)
    connection->reset(true);

    // 保留EPOLLRDHUP 异常检测
    if (cycle->m_coEpoll->modify_connection(connection, EPOLL_EVENTS_DEL, CO_EVENT_IN | CO_EVENT_OUT)) {
        CO_SERVER_LOG_FATAL("(cid:%u) upstream epoll del event failed", connection->m_connId);
    }
    // 添加空闲超时时间的定时器
    cycle->m_timer->add_timer(readEvent, connection->m_keepaliveTimeout);

    // 复用连接
    std::list<CoConnection*> &resueConnections = m_reuseConnections[backend->m_confUpstreamServer->m_serverkey];
    resueConnections.push_front(connection);    // 加入队列头 方便继续复用

    CO_SERVER_LOG_DEBUG("(cid:%u) free upstream connection keepalive, upstream serverkey:%s", connection->m_connId, backend->m_confUpstreamServer->m_serverkey.c_str());
    return ;
}


CoUpstreamPool::CoUpstreamPool() 
{
}

CoUpstreamPool::~CoUpstreamPool() 
{
    for (auto &itr : m_upstreams) {
        SAFE_DELETE(itr.second);
    }
    m_upstreams.clear();
}

int32_t CoUpstreamPool::init(CoCycle* cycle)
{
    const CoConfig* conf = cycle->m_conf;

    for(auto &itr : conf->m_confUpstreams) {
        CoConfUpstream* confUpstream = itr;
        if (m_upstreams.find(confUpstream->m_name) != m_upstreams.end()) {
            CO_SERVER_LOG_ERROR("upstream name:%s init many times", confUpstream->m_name.c_str());
            continue;
        }

        CoUpstream* upstream = new CoUpstream();
        int32_t ret = upstream->init(confUpstream, cycle->m_connectionPool);
        if (ret != CO_OK) {
            CO_SERVER_LOG_ERROR("upstream name:%s init upstream failed, ret:%d", confUpstream->m_name.c_str(), ret);
            continue;
        }

        m_upstreams.insert(std::make_pair(confUpstream->m_name, upstream));
    }

    return CO_OK;
}

CoConnection* CoUpstreamPool::get_upstream_connection(const std::string &name) 
{
    auto itr = m_upstreams.find(name);
    if (itr == m_upstreams.end()) {
        CO_SERVER_LOG_ERROR("get upstream connection name:%s not found", name.c_str());
        return NULL;
    }

    CoUpstream* upstream = itr->second;
    CoConnection* connection = upstream->get_connection();
    if (!connection) {
        CO_SERVER_LOG_ERROR("upstream name:%s get connection failed", name.c_str());
        return NULL;
    }
    connection->m_scoketConnTimeout = upstream->m_confUpstream->m_connTimeout;
    connection->m_socketRcvTimeout = upstream->m_confUpstream->m_readTimeout;
    connection->m_socketSndTimeout = upstream->m_confUpstream->m_writeTimeout;
    connection->m_keepaliveTimeout = upstream->m_confUpstream->m_keepaliveTimeout;

    connection->m_handler = CoCallbackUpstream::upstream_init;

    return connection;
}

void CoUpstreamPool::free_upstream_connection(CoConnection* connection, int32_t retCode) 
{
    connection->m_upstream->free_connection(connection, retCode);
}

void CoUpstreamPool::retry_upstream(CoRequest* upstreamRequest, const std::string &upstreamName)
{
    CoCycle* cycle = upstreamRequest->m_cycle;
    upstreamRequest->m_retryTimes ++;

    // 申请upstream connection
    CoConnection* upstreamConnection = cycle->m_upstreamPool->get_upstream_connection(upstreamName);
    if (!upstreamConnection) {
        CO_SERVER_LOG_ERROR("(rid:%u) retry upstream name:%s get upstream connection failed", upstreamRequest->m_requestId, upstreamName.c_str());
        upstreamRequest->m_connection = NULL;
        return CoCallbackUpstream::upstream_finalize_request(upstreamRequest, CO_ERROR);
    }
    
    // upstream request重置协议响应数据 / 重置连接数据
    upstreamRequest->reset_connection(upstreamConnection);
    upstreamRequest->m_protocol->reset_respmsg();

    // 添加到dispatch进行下次协程执行
    cycle->m_dispatcher->m_delayConnections.push(std::make_pair(upstreamConnection, upstreamConnection->m_version));

    CO_SERVER_LOG_DEBUG("(cid:%u rid:%u) add upstream request success, reqeust count:%u, retry:%d", upstreamConnection->m_connId, upstreamRequest->m_requestId, upstreamRequest->m_count, upstreamRequest->m_retryTimes);
    return ;
}

int32_t CoUpstreamPool::add_upstream(CoUserHandlerData* userData, const std::string &upstreamName, int32_t protocolType)
{
    CoConnection* connection = (CoConnection*)(userData->m_coroutineData.first);
    CoRequest* request = connection->m_request;
    CoCycle* cycle = connection->m_cycle;

    // 申请upstream connection
    CoConnection* upstreamConnection = cycle->m_upstreamPool->get_upstream_connection(upstreamName);
    if (!upstreamConnection) {
        CO_SERVER_LOG_ERROR("upstream name:%s get upstream connection failed", upstreamName.c_str());
        return CO_ERROR;
    }

    CoProtocol* protocol = cycle->m_protocolFactory->create_protocol(protocolType);
    if (!protocol) {
        CO_SERVER_LOG_ERROR("upstream name:%s create protocol:%d failed", upstreamName.c_str(), protocolType);
        cycle->m_upstreamPool->free_upstream_connection(upstreamConnection, CO_ERROR);
        return CO_ERROR;
    }

    // 子请求状态数据初始化
    CoUpstreamInfo* upstreamInfo = new CoUpstreamInfo();
    upstreamInfo->m_protocol = protocol;
    userData->m_upstreamInfos.emplace_back(upstreamInfo);

    // 申请upstream request
    CoRequest* upstreamRequest = new CoRequest(CO_REQUEST_UPSTREAM);
    int32_t ret = upstreamRequest->init(upstreamConnection, protocolType, protocol);
    if (ret != CO_OK) {
        CO_SERVER_LOG_ERROR("upstream name:%s init request protocol:%d failed", upstreamName.c_str(), protocolType);
        CoCallbackUpstream::upstream_finalize(upstreamRequest, CO_ERROR);
        return CO_ERROR;
    }
    upstreamRequest->m_userProcess = NULL;   // 不需要处理业务逻辑
    upstreamRequest->m_parent = request;
    upstreamRequest->m_upstreamInfo = upstreamInfo;

    // 设置父请求相关参数
    request->m_upstreamRequests[upstreamRequest->m_requestId] = upstreamRequest;
    request->m_count ++;

    // 添加到dispatch进行下次协程执行
    cycle->m_dispatcher->m_delayConnections.push(std::make_pair(upstreamConnection, upstreamConnection->m_version));

    CO_SERVER_LOG_DEBUG("(cid:%u rid:%u) add upstream request success, reqeust count:%u, parent request count:%d", upstreamConnection->m_connId, upstreamRequest->m_requestId, upstreamRequest->m_count, request->m_count);
    return CO_OK;
}

int32_t CoUpstreamPool::run_upstreams(CoUserHandlerData* userData)
{
    CoConnection* connection = (CoConnection*)(userData->m_coroutineData.first);
    if (userData->m_upstreamInfos.empty()) {
        CO_SERVER_LOG_ERROR("(cid:%u) run upstreams, no upstream info", connection->m_connId);
        return CO_ERROR;
    }

    // 需要执行的upstream已经在add_upstream添加完成  随后会在waitEvents中进行执行 这里切出当前协程即可 等待子请求完成
    CO_SERVER_LOG_DEBUG("(cid:%u) dispactch upstream yield, swap out START", connection->m_connId);
    int32_t ret = CoDispatcher::yield(userData->m_coroutineData);
    CO_SERVER_LOG_DEBUG("(cid:%u) dispactch upstream yield, swap out END, ret:%d", connection->m_connId, ret);

    // 切回协程（子请求全部处理完成 或者 出错）
    return ret;
}

CoUserHandlerData* CoUpstreamPool::add_upstream_detach(const std::string &upstreamName, int32_t protocolType, std::function<int32_t (CoUserHandlerData* userData)> userProcess, void* userData)
{
    CoThreadLocalInfo* threadInfo = GET_TLS();
    if (!(threadInfo->m_coCycle)) {
        CO_SERVER_LOG_ERROR("detach upstream not inner thread");
        return NULL;
    }

    // 申请upstream connection
    CoCycle* cycle = threadInfo->m_coCycle;
    CoConnection* connection = cycle->m_upstreamPool->get_upstream_connection(upstreamName);
    if (!connection) {
        CO_SERVER_LOG_ERROR("detach upstream name:%s get upstream connection failed", upstreamName.c_str());
        return NULL;
    }

    CoProtocol* protocol = cycle->m_protocolFactory->create_protocol(protocolType);
    if (!protocol) {
        CO_SERVER_LOG_ERROR("upstream name:%s create protocol:%d failed", upstreamName.c_str(), protocolType);
        cycle->m_upstreamPool->free_upstream_connection(connection, CO_ERROR);
        return NULL;
    }

    // 申请upstream request
    CoRequest* request = new CoRequest(CO_REQUEST_UPSTREAM | CO_REQUEST_DETACH);
    int32_t ret = request->init(connection, protocolType, protocol);
    if (ret != CO_OK) {
        CO_SERVER_LOG_ERROR("detach upstream name:%s init request protocol:%d failed", upstreamName.c_str(), protocolType);
        CoCallbackUpstream::upstream_finalize(request, CO_ERROR);
        return NULL;
    }
    CoUpstreamInfo* upstreamInfo = new CoUpstreamInfo();
    upstreamInfo->m_protocol = protocol; // 调用方填充协议

    request->m_userData->m_upstreamInfos.emplace_back(upstreamInfo);
    request->m_userData->m_userData = userData;
    request->m_userDestroy = userProcess;  // 赋值给userDestroy 异常结束时 也保证调用
    request->m_upstreamInfo = upstreamInfo;

    // 添加到dispatch进行下次协程执行
    cycle->m_dispatcher->m_delayConnections.push(std::make_pair(connection, connection->m_version));

    CO_SERVER_LOG_DEBUG("(cid:%u rid:%u) init detach upstream request success, reqeust count:%u", connection->m_connId, request->m_requestId, request->m_count);
    return request->m_userData;
}

}

