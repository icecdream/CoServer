#include <atomic>
#include "core/co_request.h"
#include "base/co_log.h"


namespace coserver 
{

static thread_local std::atomic<uint64_t>  g_requestId(0);


CoUserHandlerData::CoUserHandlerData()
{
}

CoUserHandlerData::~CoUserHandlerData()
{
    for (auto &itr : m_upstreamInfos) {
        SAFE_DELETE(itr);
    }
    m_upstreamInfos.clear();
}


CoRequest::CoRequest(int32_t requestType) 
: m_count(0)
, m_requestType(requestType)
, m_flagNeedFreeProtocol(0)
{
}

CoRequest::~CoRequest()
{
    SAFE_DELETE(m_userData);
    
    if (m_flagNeedFreeProtocol) {
        SAFE_DELETE(m_protocol);
    }
}

int32_t CoRequest::init(CoConnection* connection, int32_t protocolType, CoProtocol* protocol)
{
    CoCycle* cycle = connection->m_cycle;

    // 关联连接请求
    connection->m_request = this;
    ++(connection->m_requestCount);

    if (!protocol) {
        m_flagNeedFreeProtocol = 1;
        protocol = cycle->m_protocolFactory->create_protocol(protocolType);
        if (!protocol) {
            return CO_ERROR;
        }
    }

    m_connection = connection;
    m_cycle = cycle;

    m_protocol = protocol;
    m_protocol->set_clientip(connection->m_coTcp->get_ip());
    m_startUs = GET_CURRENTTIME_US();
    m_requestId = ++g_requestId;
    m_count ++;

    // request business
    m_userData = new CoUserHandlerData;
    m_userData->m_protocol = m_protocol;
    m_userData->m_coroutineData = std::make_pair((void *)connection, connection->m_version);

    // 非upstream
    if (connection->m_serverControl) {
        m_userProcess = connection->m_serverControl->m_userFuncs->m_userProcess;
        m_userDestroy = connection->m_serverControl->m_userFuncs->m_userDestroy;
        m_userData->m_userData = connection->m_serverControl->m_userFuncs->m_userData;
    }

    CO_SERVER_LOG_DEBUG("(cid:%u rid:%u) init request success, protocol(%d):%d, connection count:%u", connection->m_connId, m_requestId, m_flagNeedFreeProtocol, protocolType, connection->m_requestCount);
    return CO_OK;
}

void CoRequest::reset_connection(CoConnection* connection)
{
    // 关联连接请求
    connection->m_request = this;
    ++(connection->m_requestCount);
    
    m_connection = connection;

    // request business
    m_userData->m_coroutineData = std::make_pair((void *)connection, connection->m_version);
}

}

