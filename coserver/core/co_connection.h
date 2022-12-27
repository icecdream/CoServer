#ifndef _CO_CONNECTION_H_
#define _CO_CONNECTION_H_

#include <list>
#include <vector>
#include <unordered_set>
#include "core/co_event.h"
#include "base/co_tcp.h"
#include "base/co_buffer.h"
#include "core/co_server_control.h"
#include "core/co_callback_event.h"


namespace coserver
{

/*
    针对hook第三方socket套接字
    到底使用一个新的连接承载 还是 使用原本的连接承载，各有优缺点
    使用新连接：代码上更清晰；但是会浪费连接资源 并且原连接各种状态需要进行处理
    使用原本连接：代码会复杂些，增加block相关变量；但是会更省资源（协程内存），逻辑也好理解些，连接只是暂时管理了第三方socket，而且也是阻塞了当前连接
*/

struct CoCycle;
struct CoRequest;
struct CoBackend;
class CoUpstream;


// 连接信息
struct CoConnection
{
    uint32_t        m_connId = 0;           // 连接id, 从1开始
    uint32_t        m_version = 0;          // 连接版本信息 用于检查连接是否过期
    uint32_t        m_epollCurEvents = 0;   // epoll中现在的事件events
    uint32_t        m_epollCurVersion = 0;  // epoll中现在的version

    CoEvent*        m_readEvent  = NULL;    // 读事件
    CoEvent*        m_writeEvent = NULL;    // 写事件
    CoEvent*        m_sleepEvent = NULL;    // sleep事件

    CoCycle*        m_cycle = NULL;         // cycle指针
    CoBuffer*       m_coBuffer = NULL;      // 读socket填充, 结束后清空; decode写入, 写事件结束后清空
    
    // socket相关
    CoTCP*          m_coTcp = NULL;             // 网络socket
    int32_t         m_scoketConnTimeout = -1;   // socket上连接超时时间（hook使用）
    int32_t         m_socketRcvTimeout = -1;    // socket上读超时时间（hook使用）
    int32_t         m_socketSndTimeout = -1;    // socket上写超时时间（hook使用）
    int32_t         m_keepaliveTimeout = -1;    // keepalive超时时间（保活时间）
    CoServerControl* m_serverControl = NULL;    // 连接所属的server

    // request
    CoRequest*      m_request = NULL;           // 指向连接对应的请求request
    uint32_t        m_requestCount = 0;         // 建立连接后处理请求的次数

    // upstream
    CoUpstream*     m_upstream = NULL;
    CoBackend*      m_backend = NULL;
    uint64_t        m_startTimestamp = 0;   // 连接开始时间

    CoCoroutine*    m_coroutine = NULL;     // 连接的协程
    std::function<void (CoConnection* connection)> m_handler = NULL;    // 连接可读/可写时的回调函数
    std::function<int32_t (CoConnection* connection)> m_handlerException = CoCallbackEvent::event_exception;   // 事件发生异常时回调函数, 比如超时/对端关闭socket
    std::vector<std::function<void (CoConnection* connection)>> m_handlerCleanups;

    // 调用第三方模块中使用了阻塞socket
    CoConnection*   m_blockConn = NULL;         // 第三方阻塞网络socket的连接 flagBlockSocket为0时有效
    CoConnection*   m_blockOriginConn = NULL;   // 当前连接是第三方阻塞socket时 的原始socket连接

    // flags
    unsigned        m_flagBlockConn:1;      // 连接是否为阻塞的连接
    unsigned        m_flagUseBlockConn:1;   // 是否使用了阻塞连接
    
    unsigned        m_flagPendingEof:1;     // 为1时表示触发异常 需要关闭
    unsigned        m_flagTimedOut:1;       // 为1时表示这个事件已经超时  提示需要做超时处理
    unsigned        m_flagDying:1;          // 连接即将销毁
    unsigned        m_flagParentDying:1;    // 父请求连接即将销毁

    unsigned        m_flagThirdFuncBlocking:1; // 为1表示第三方函数阻塞中, 比如sleep/mutex


// functions
    CoConnection(uint32_t id, CoCycle* cycle);
    CoConnection() = delete;
    ~CoConnection();

    void reset(bool keepalive = false);
    void reset_block();
};


class CoConnectionPool 
{
public:
    CoConnectionPool(CoCycle* cycle);
    CoConnectionPool() = delete;
    ~CoConnectionPool();

    int32_t init(int32_t minConnectionSize, int32_t maxConnectionSize);
    int32_t expand_connections(int32_t expandSize = 16);

    // 获取连接
    CoConnection* get_connection(const std::string &ip, uint16_t port, bool listen = false);
    CoConnection* get_connection(int32_t socketFd);
    // 释放连接  置回连接池
    void free_connection(CoConnection* connection);

    CoConnection* get_connection_accord_id(int32_t connId);
    bool is_inner_socketfd(int32_t socketFd);
    
    // 主动关闭所有连接
    void close_all_connection();


private:
    CoCycle*    m_cycle = NULL;

    // 连接池的最小 最大 当前连接数量
    int32_t     m_minConnectionSize = 1;
    int32_t     m_maxConnectionSize = 4;
    int32_t     m_curConnectionSize = 0;

    std::vector<CoConnection*>  m_connections;      // 指向所有连接对象数组
    std::list<CoConnection*>    m_freeConnections;  // 可用连接数组 (指向m_connections中空闲连接)

    // sockets
    std::unordered_set<int32_t> m_innerSockets;
};

}

#endif //_CO_CONNECTION_H_

