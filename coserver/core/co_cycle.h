#ifndef _CO_CYCLE_H_
#define _CO_CYCLE_H_

#include <thread>
#include "base/co_epoll.h"
#include "base/co_timer.h"
#include "base/co_config.h"
#include "core/co_single.h"
#include "core/co_dispatcher.h"
#include "upstream/co_upstream.h"
#include "protocol/co_protocol_factory.h"


namespace coserver
{

// CoCycle管理整个生命周期的数据结构
struct CoCycle 
{
    int32_t             m_innerThreadId = -1;       // 对应的内部线程id

    const CoConfig*     m_conf      = NULL;         // 配置项

    CoConnectionPool*   m_connectionPool = NULL;    // 连接池
    CoEpoll*            m_coEpoll   = NULL;         // IO事件管理
    CoTimer*            m_timer     = NULL;         // 超时事件管理
    CoCoroutineMain*    m_coCoroutineMain= NULL;    // 协程遍历

    CoDispatcher*       m_dispatcher     = NULL;    // 任务分配管理
    CoUpstreamPool*     m_upstreamPool   = NULL;    // upstream管理
    CoProtocolFactory*  m_protocolFactory = NULL;   // 请求响应协议管理
    //  CoPool*             m_memoryPool;           // 内存池


// funcs
    CoCycle();
    ~CoCycle();
};


// thread local info
struct CoThreadLocalInfo
{
    std::thread::id     m_threadId;

    CoCycle*            m_coCycle = NULL;

    /*
        线程正在处理的连接 hook处使用
        线程处理的连接变化时, 需要替换为处理的连接

        这里不使用事件 是因为hook使用连接信息已经足够
        如果使用事件 在处理读事件无阻塞到响应时 curEvent事件还没换 但是已经处理写事件了
    */
    CoConnection*       m_curConnection = NULL;


    CoThreadLocalInfo();
    ~CoThreadLocalInfo();
};

CoThreadLocalInfo* GET_TLS();

}

#endif //_CO_CYCLE_H_

