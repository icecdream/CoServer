#ifndef _CO_UPSTREAM_BACKEND_H_
#define _CO_UPSTREAM_BACKEND_H_

#include "base/co_config.h"
#include "core/co_connection.h"


namespace coserver
{

// 后端服务信息
struct CoBackend 
{
    CoConfUpstreamServer* m_confUpstreamServer = NULL;

    std::string m_upstreamName;

    int32_t m_confFailTime         = 0; // fail判断周期时间
    int32_t m_confFailNum          = 0; // fail连接最大数量 用于判断是否服务有问题

    bool    m_fail          = false;
    bool    m_failCheck     = false;    // 是否检查后端服务是否可用

    int32_t  m_errNum            = 0;   // 后端错误次数
    uint64_t m_errFirstTimestamp = 0;   // 后端第一次错误时间戳
    uint64_t m_downTimestamp     = 0;   // 后端down开始时间


    CoBackend(const std::string &name);
    CoBackend() = delete;
    virtual ~CoBackend();

    virtual bool avaliable();
    virtual void comm_failed();
};

struct CoBackendWRR : public CoBackend
{
    int32_t m_weight = 0;            // 配置文件中的权重值
    int32_t m_currentWeight = 0;     // 当前权重 一开始为0 后续动态调整
    int32_t m_effectiveWeight = 0;   // 有效权重 会因为失败而降低

    
    CoBackendWRR(const std::string &name);
    CoBackendWRR() = delete;
    virtual void comm_failed();
};


// 后端策略选择
class CoBackendStrategy 
{
public:
    CoBackendStrategy(const std::string &name);
    CoBackendStrategy() = delete;
    virtual ~CoBackendStrategy();

    virtual int32_t init() = 0;
    virtual int32_t add_backend(CoConfUpstream* confUpstream, CoConfUpstreamServer* confUpstreamServer) = 0;
    virtual CoBackend* get_backend() = 0;

    std::string m_upstreamName;
};

// 加权轮询
class CoBackendStrategyWRR : public CoBackendStrategy 
{
public:
    CoBackendStrategyWRR(const std::string &name);
    CoBackendStrategyWRR() = delete;
    ~CoBackendStrategyWRR();

    virtual int32_t init();
    virtual int32_t add_backend(CoConfUpstream* confUpstream, CoConfUpstreamServer* confUpstreamServer);
    virtual CoBackend* get_backend();

private:
    CoBackendWRR* choose_one();
    CoBackendWRR* choose_one_weight();   // 针对master服务集群


private:
    int32_t m_totalWeight   = 0;     // 服务集群总权重
    int32_t m_weighted      = 0;        // 服务集群是否使用权重值

    size_t m_curBackendsIndex   = 0;
    std::vector<CoBackendWRR*> m_backends;
};

}

#endif //_CO_UPSTREAM_BACKEND_H_

