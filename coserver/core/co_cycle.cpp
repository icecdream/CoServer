#include <sstream>
#include <atomic>
#include "core/co_cycle.h"
#include "base/co_log.h"


namespace coserver
{

static std::atomic<uint32_t>  g_threadId(0);
thread_local int32_t g_innerThreadId;


CoCycle::CoCycle() 
: m_innerThreadId(g_innerThreadId)
{
}

CoCycle::~CoCycle()
{
    m_connectionPool->close_all_connection();
    SAFE_DELETE(m_dispatcher);
    
    SAFE_DELETE(m_connectionPool);
    SAFE_DELETE(m_coEpoll);
    SAFE_DELETE(m_timer);
    SAFE_DELETE(m_coCoroutineMain);
    SAFE_DELETE(m_upstreamPool);
    SAFE_DELETE(m_protocolFactory);
}

CoThreadLocalInfo* GET_TLS()
{
    static thread_local CoThreadLocalInfo threadLocalInfo;
    return &threadLocalInfo;
}

CoThreadLocalInfo::CoThreadLocalInfo()
{
    m_threadId = std::this_thread::get_id();
    g_innerThreadId = g_threadId++;

    std::ostringstream oss;
    oss << m_threadId;
    CO_SERVER_LOG_INFO("new CoThreadLocalInfo threadid:%s, inner threadid:%u", oss.str().c_str(), g_innerThreadId);
}

CoThreadLocalInfo::~CoThreadLocalInfo()
{
    std::ostringstream oss;
    oss << m_threadId;
    CO_SERVER_LOG_INFO("delete CoThreadLocalInfo threadid:%s, inner threadid:%u", oss.str().c_str(), g_innerThreadId);
    SAFE_DELETE(m_coCycle);
}

}

