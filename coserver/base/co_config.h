#ifndef _CO_CONFIG_H_
#define _CO_CONFIG_H_

#include <vector>
#include "base/co_common.h"

namespace coserver
{

// conf config
const std::string CONF_CONFIG = "conf";
const int32_t LOG_LEVEL = 1;
const int32_t WORKER_THREADS = 4;

// conf global
const std::string HOOK_CONFIG = "hook";
const int32_t MUTEX_RETRY_TIME = 100;

// server config
const std::string SERVER_CONFIG = "server";
const std::string SERVER_IP = "";
const int32_t SERVER_PORT = 15678;
const std::string SERVER_HANDLER_NAME = "handler";

const int32_t PROTOCOL_TCP_SERVER = 1;
const int32_t PROTOCOL_HTTP_SERVER = 2;
const int32_t PROTOCOL_TCP_CLIENT = 101;
const int32_t PROTOCOL_HTTP_CLIENT = 102;
const int32_t PROTOCOL_MAX = 5;

const int32_t SERVER_MAX_CONNECTIONS = 65536;
const int32_t SERVER_READ_TIMEOUT = 1000;
const int32_t SERVER_WRITE_TIMEOUT = 1000;
const int32_t SERVER_KEEPALIVE_TIMEOUT = 60000;

// upstream config
const std::string UPSTREAM_CONFIG = "upstream";
const int32_t UPSTREAM_MAX_CONNECTIONS = 65535;
const int32_t UPSTREAM_LOAD_BALANCE_WRR = 1;
const int32_t UPSTREAM_LOAD_BALANCE_IPHASH = 2;
const int32_t UPSTREAM_CONN_TIMEOUT = 1000;
const int32_t UPSTREAM_READ_TIMEOUT = 1000;
const int32_t UPSTREAM_WRITE_TIMEOUT = 1000;
const int32_t UPSTREAM_KEEPALIVE_TIMEOUT = 60000;
const int32_t UPSTREAM_FAIL_TIMEOUT = 5000;
const int32_t UPSTREAM_FAIL_MAX_NUM = 10;
const int32_t UPSTREAM_CONNECTION_MAX_REQUEST = 10240;
const int32_t UPSTREAM_CONNECTION_MAX_TIME = 60000;
const int32_t UPSTREAM_RETRY_MAX_NUM = 3;


struct CoConf
{
    int32_t m_logLevel      = LOG_LEVEL;
    int32_t m_workerThreads = WORKER_THREADS;
};

// hook
struct CoConfHook
{
    int32_t m_mutexRetryTime    = MUTEX_RETRY_TIME;             // mutex加锁失败后 重试时的超时时间ms
};

// server
struct CoConfServer
{
    std::string m_listenIP      = SERVER_IP;
    uint16_t    m_listenPort    = SERVER_PORT;

    int32_t     m_serverType    = PROTOCOL_TCP_SERVER;
    std::string m_handlerName   = SERVER_HANDLER_NAME;

    int32_t     m_readTimeout       = SERVER_READ_TIMEOUT;       // 读超时时间 (ms)
    int32_t     m_writeTimeout      = SERVER_WRITE_TIMEOUT;      // 写超时时间 (ms)
    int32_t     m_keepaliveTimeout  = SERVER_KEEPALIVE_TIMEOUT;  // 连接最长保活时间, 过后将清理连接

    int32_t     m_maxConnections    = SERVER_MAX_CONNECTIONS;    // 最大连接数
};

// upstream conf
struct CoConfUpstreamServer
{
    std::string m_host;
    int32_t m_port = 0;
    int32_t m_weight = 0;

    std::string m_serverkey;    // ip + : + port


    CoConfUpstreamServer(std::string &host, int32_t port, int32_t weight) : m_host(host), m_port(port), m_weight(weight)
    {
        m_serverkey = m_host + ":" + std::to_string(m_port);
    }
    CoConfUpstreamServer() = delete;
};

struct CoConfUpstream
{
    std::string m_name;
    std::vector<CoConfUpstreamServer> m_upstreamServers;    // 后端服务组

    int32_t m_loadBalance      = UPSTREAM_LOAD_BALANCE_WRR; // 后端负载均衡方式

    int32_t m_connTimeout      = UPSTREAM_CONN_TIMEOUT;     // 连接超时时间 (ms)
    int32_t m_readTimeout      = UPSTREAM_READ_TIMEOUT;     // 读超时时间 (ms)
    int32_t m_writeTimeout     = UPSTREAM_WRITE_TIMEOUT;    // 写超时时间 (ms)
    int32_t m_keepaliveTimeout = UPSTREAM_KEEPALIVE_TIMEOUT;// 复用连接的空闲时间(ms)
    
    int32_t m_failTimeout      = UPSTREAM_FAIL_TIMEOUT;     // 后端问题判断周期时间 (ms)
    int32_t m_failMaxnum       = UPSTREAM_FAIL_MAX_NUM;     // 后端问题连接最大数 用于判断是否后端服务有问题

    int32_t m_maxConnections   = UPSTREAM_MAX_CONNECTIONS;  // 当前upstream的最大连接数

    int32_t m_connectionMaxRequest  = UPSTREAM_CONNECTION_MAX_REQUEST;  // 一次连接最大的请求数
    int32_t m_connectionMaxTime     = UPSTREAM_CONNECTION_MAX_TIME;     // 一次连接最大时间 (ms)

    int32_t m_retryMaxnum      = UPSTREAM_RETRY_MAX_NUM;    // 重试次数
};

struct CoConfig
{
    CoConf      m_conf;
    CoConfHook  m_confHook;
    std::vector<CoConfServer*>    m_confServers;
    std::vector<CoConfUpstream*>  m_confUpstreams;

// funs
    CoConfig()
    {
    }

    ~CoConfig() 
    {
        for (auto &itr : m_confServers) {
            SAFE_DELETE(itr);
        }
        m_confServers.clear();

        for (auto &itr : m_confUpstreams) {
            SAFE_DELETE(itr);
        }
        m_confUpstreams.clear();
    }
};

}

#endif //_CO_CONFIG_H_

