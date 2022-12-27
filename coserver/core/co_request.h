#ifndef _CO_REQUEST_H_
#define _CO_REQUEST_H_

#include "core/co_event.h"
#include "core/co_cycle.h"
#include "core/co_connection.h"
#include "protocol/co_protocol.h"


namespace coserver 
{

struct CoUpstreamInfo;
struct CoUserHandlerData;
typedef std::function<int32_t (CoUserHandlerData* userData)> CoFuncUserProcess;
typedef std::function<int32_t (CoUserHandlerData* userData)> CoFuncUserDestroy;

struct CoUserFuncs 
{
    CoFuncUserProcess   m_userProcess = NULL;       // 业务处理函数
    CoFuncUserDestroy   m_userDestroy = NULL;       // 业务销毁函数
    void*               m_userData    = NULL;
};


struct CoUserHandlerData
{
    CoProtocol* m_protocol  = NULL;
    void*       m_userData  = NULL;

    // 子请求的 状态数据（请求响应 耗时 状态等）
    std::vector<CoUpstreamInfo*> m_upstreamInfos;   // 访问上游服务的请求和响应

    // 支持协程的变量
    std::pair<void*, uint32_t> m_coroutineData;


    CoUserHandlerData();
    ~CoUserHandlerData();
};

struct CoRequest 
{
    CoCycle*                m_cycle         = NULL;     // 框架数据结构
    CoConnection*           m_connection    = NULL;     // 请求使用的连接
    CoProtocol*             m_protocol      = NULL;     // 消息接口

    //upstreams
    int32_t                 m_count         = 0;        // 引用计数, 增加子请求+1, finalize子请求-1
    CoRequest*              m_parent        = NULL;     // 当前请求的父请求
    CoUpstreamInfo*         m_upstreamInfo  = NULL;     // 当前请求为子请求时 请求的相关数据信息（无需释放内存 使用子请求方进行释放）
    std::unordered_map<uint64_t, CoRequest*>   m_upstreamRequests;  // 当前请求的所有子请求（执行顺序不依赖当前顺序）

    // 业务处理/销毁函数指针
    CoFuncUserProcess       m_userProcess   = NULL;
    CoFuncUserDestroy       m_userDestroy   = NULL;
    CoUserHandlerData*      m_userData      = NULL;

    uint32_t                m_requestId     = 0;
    int32_t                 m_requestType   = 0;
    int32_t                 m_retryTimes    = 0;

    // debug
    uint64_t                m_startUs       = 0;        // 开始时间
    uint64_t                m_connectUs     = 0;        // 连接完成时间和开始时间差值
    uint64_t                m_readUs        = 0;        // 读数据完成时间和开始时间差值
    uint64_t                m_processUs     = 0;        // 处理业务完成时间和开始时间差值
    uint64_t                m_writeUs       = 0;        // 写数据完成时间和开始时间差值

    unsigned                m_flagNeedFreeProtocol:1;   // 为1时表示需要释放协议 为0表示外部传入 外部释放


    CoRequest(int32_t requestType);
    CoRequest() = delete;
    ~CoRequest();

    int32_t init(CoConnection* connection, int32_t protocolType, CoProtocol* protocol = NULL);
    void reset_connection(CoConnection* connection);
};

}

#endif //_CO_REQUEST_H_

