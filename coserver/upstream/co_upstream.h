#ifndef _CO_UPSTREAM_H_
#define _CO_UPSTREAM_H_

#include <list>
#include <unordered_map>
#include "protocol/co_protocol.h"
#include "upstream/co_upstream_backend.h"


namespace coserver
{

struct CoRequest;
struct CoUserHandlerData;


struct CoUpstreamInfo
{
    CoProtocol* m_protocol = NULL;

    int32_t     m_status = 0;
    uint32_t    m_useTimeUs = 0;

    void*       m_userData = NULL;


    CoUpstreamInfo();
    ~CoUpstreamInfo();
};

class CoUpstream
{
public:
    CoUpstream();
    ~CoUpstream();

    int32_t init(CoConfUpstream* confUpstream, CoConnectionPool* connectionPool);

    // 获取连接
    CoConnection* get_connection();
    // 释放连接
    void free_connection(CoConnection* connection, int32_t retCode);


public:
    CoConfUpstream*     m_confUpstream = NULL;

private:
    CoBackendStrategy*  m_backendStrategy = NULL;   // 后端选择策略
    
    CoConnectionPool*   m_connectionPool = NULL;    // 全局连接池

    // 复用的缓存连接
    int32_t m_curConnectionSize = 0;
    std::unordered_map<std::string, std::list<CoConnection*>> m_reuseConnections;
};

class CoUpstreamPool
{
public:
    CoUpstreamPool();
    ~CoUpstreamPool();

    int32_t init(CoCycle* cycle);

    CoConnection* get_upstream_connection(const std::string &name);
    void free_upstream_connection(CoConnection* connection, int32_t retCode = 0);

public:
    /*
        函数功能: 添加一个upstream子请求

        参数: 
            userData: 请求关联的业务数据
            upstreamName: upstream的name（关联上游服务器地址）
            protocolType: 通信协议（比如tcp/http）

        返回值: CO_OK成功 其他错误
    */
    static int32_t add_upstream(CoUserHandlerData* userData, const std::string &upstreamName, int32_t protocolType);

    /*
        函数功能: 执行添加的所有upstream

        参数: 
            userData: 请求关联的业务数据

        返回值: CO_OK成功 其他错误
    */
    static int32_t run_upstreams(CoUserHandlerData* userData);

    /*
        函数功能: 添加一个detach upstream子请求

        参数:
            upstreamName: upstream的name（关联上游服务器地址）
            protocolType: 通信协议（比如tcp/http）
            userProcess: upstream请求处理完成时的回调函数
            userData: 回调时关联的业务数据

        返回值: 请求的用户相关信息, 在里面填充请求协议
    */
    static CoUserHandlerData* add_upstream_detach(const std::string &upstreamName, int32_t protocolType, std::function<int32_t (CoUserHandlerData* userData)> userProcess, void* userData = NULL);

    /*
        函数功能: 请求重试  申请upstreamName下的新连接 重置request的连接 和 协议中的响应相关数据

        参数:
            upstreamRequest: 重试的upstream请求
            upstreamName: 重试的upstream名称
    */
    static void retry_upstream(CoRequest* upstreamRequest, const std::string &upstreamName);


private:
    std::unordered_map<std::string, CoUpstream*>    m_upstreams;
};


}

#endif //_CO_UPSTREAM_H_

