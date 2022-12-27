#include "core/co_connection.h"
#include "base/co_log.h"
#include "base/co_common.h"
#include "core/co_cycle.h"


namespace coserver
{

const int32_t MAX_LISTEN_BACKLOG = 512;

const int32_t MIN_CONNECTION_SIZE = 4;
const int32_t MAX_CONNECTION_SIZE = 65535;


CoConnection::CoConnection(uint32_t id, CoCycle* cycle)
: m_connId(id)
, m_cycle(cycle)
, m_flagBlockConn(0)
, m_flagUseBlockConn(0)
, m_flagPendingEof(0)
, m_flagTimedOut(0)
, m_flagDying(0)
, m_flagParentDying(0)
, m_flagThirdFuncBlocking(0)
{
}

CoConnection::~CoConnection()
{
    SAFE_DELETE(m_coBuffer);
    SAFE_DELETE(m_readEvent);
    SAFE_DELETE(m_writeEvent);
    SAFE_DELETE(m_sleepEvent);
    SAFE_DELETE(m_coroutine);
    SAFE_DELETE(m_coTcp);
}

void CoConnection::reset(bool keepalive) 
{
    // connection重置信息
    m_version ++;

    m_handler = NULL;
    m_request = NULL;

    m_flagUseBlockConn = 0;
    m_flagPendingEof = 0;
    m_flagTimedOut = 0;
    m_flagDying = 0;
    m_flagParentDying = 0;
    m_flagThirdFuncBlocking = 0;

    m_readEvent->reset();
    m_writeEvent->reset();
    m_sleepEvent->reset();

    m_coBuffer->reset();
    m_coroutine->reset();

    // keepalive连接 socket/servercontrol/upstream/backend不需要清空
    if (!keepalive) {
        if (m_coTcp->get_socketfd() > 0) {
            CO_SERVER_LOG_ERROR("connection tcp is using by socketfd:%d, now close socket", m_coTcp->get_socketfd());
            m_coTcp->tcp_close();
        }

        m_startTimestamp = 0;
        m_serverControl = NULL;
        m_upstream = NULL;
        m_backend = NULL;
        m_requestCount = 0;
        m_handlerCleanups.clear();
    }

    // blockconn重置信息
    reset_block();
}

void CoConnection::reset_block() 
{
    // block connection重置信息
    m_blockConn->m_readEvent->reset();
    m_blockConn->m_writeEvent->reset();
    m_blockConn->m_coTcp->reset();

    m_blockConn->m_flagPendingEof = 0;
    m_blockConn->m_flagTimedOut = 0;
    m_blockConn->m_flagDying = 0;
    m_blockConn->m_flagParentDying = 0;
    m_blockConn->m_flagUseBlockConn = 0;
    m_blockConn->m_flagThirdFuncBlocking = 0;
}


CoConnectionPool::CoConnectionPool(CoCycle* cycle)
: m_cycle(cycle)
{
}

CoConnectionPool::~CoConnectionPool()
{
    for (auto &itr : m_connections) {
        SAFE_DELETE(itr);
    }
    m_connections.clear();
}

int32_t CoConnectionPool::init(int32_t minConnectionSize, int32_t maxConnectionSize)
{
    m_minConnectionSize = minConnectionSize < MIN_CONNECTION_SIZE ? MIN_CONNECTION_SIZE : minConnectionSize;
    m_maxConnectionSize = maxConnectionSize > MAX_CONNECTION_SIZE ? MAX_CONNECTION_SIZE : maxConnectionSize;
    m_maxConnectionSize += 3;

    // 客户端需要的连接数 需要+3（内部监听连接、异步协程需要的读写连接）
    int32_t expandSize = m_minConnectionSize + 3;

    CO_SERVER_LOG_DEBUG("connectionpool param minsize:%d maxsize:%d, inner minsize:%d maxsize:%d expandsize:%d", minConnectionSize, maxConnectionSize, m_minConnectionSize, m_maxConnectionSize, expandSize);
    return expand_connections(expandSize);
}

int32_t CoConnectionPool::expand_connections(int32_t expandSize)
{
    if (m_curConnectionSize >= m_maxConnectionSize) {
        CO_SERVER_LOG_ERROR("connections cur:%d large max:%d, expand failed", m_curConnectionSize, m_maxConnectionSize);
        return CO_ERROR;
    }

    int32_t startPos = m_curConnectionSize * 2;

    m_curConnectionSize += expandSize;
    m_curConnectionSize = m_curConnectionSize > m_maxConnectionSize ? m_maxConnectionSize : m_curConnectionSize;

    // 需要block connection  连接数*2
    m_connections.resize(m_curConnectionSize * 2);  // vector的0位置没有使用

    for (int32_t i=startPos; i<m_curConnectionSize * 2; i += 2) {
        CoConnection* connection = new CoConnection(i + 1, m_cycle);
        CoConnection* blockConn = new CoConnection(i + 2, m_cycle);

        connection->m_coTcp = new CoTCP;
        connection->m_coBuffer = new CoBuffer;
        connection->m_readEvent = new CoEvent(EVENT_TYPE_READ, connection);
        connection->m_writeEvent = new CoEvent(EVENT_TYPE_WRITE, connection);
        connection->m_sleepEvent = new CoEvent(EVENT_TYPE_SLEEP, connection);
        connection->m_coroutine = new CoCoroutine;

        connection->m_coBuffer->set_userdata((void* )connection);
        connection->m_blockConn = blockConn; 

        // 阻塞连接
        blockConn->m_coTcp = new CoTCP;
        blockConn->m_readEvent = new CoEvent(EVENT_TYPE_READ, blockConn);
        blockConn->m_writeEvent = new CoEvent(EVENT_TYPE_WRITE, blockConn);
        blockConn->m_flagBlockConn = 1;
        blockConn->m_blockOriginConn = connection;

        // set connection
        m_connections[i] = connection;
        m_connections[i + 1] = blockConn;

        // free connection save, non blockconn
        m_freeConnections.emplace_back(connection);
    }
    
    CO_SERVER_LOG_INFO("connections expand from id:%d to id:%d (contain half block conneciton)", startPos, m_curConnectionSize * 2);
    return CO_OK;
}

CoConnection* CoConnectionPool::get_connection(const std::string &ip, uint16_t port, bool listen)
{
    // 在cycle所属线程中调用 无需加锁
    if (m_freeConnections.empty()) {
        expand_connections();
        if (m_freeConnections.empty()) {
            CO_SERVER_LOG_ERROR("%u connections are not enough, no connection", m_maxConnectionSize);
            return NULL;
        }
    }

    CoConnection* connection = m_freeConnections.front();
    if (CO_OK != connection->m_coTcp->init_ipport(ip, port)) {
        CO_SERVER_LOG_ERROR("tcp init ipport failed, ip:%s port:%d", ip.c_str(), port);
        return NULL;
    }
    if (CO_OK != connection->m_coTcp->set_nonblock()) {
        CO_SERVER_LOG_ERROR("tcp socket:%d set nonblock error", connection->m_coTcp->get_socketfd());
        return NULL;
    }

    if (listen) {
        // listen socket attributes
        if (CO_OK != connection->m_coTcp->set_reused()) {
            CO_SERVER_LOG_ERROR("listen tcp socket:%d set resued error", connection->m_coTcp->get_socketfd());
            return NULL;
        }
        if (CO_OK != connection->m_coTcp->server_bind()) {
            CO_SERVER_LOG_ERROR("listen tcp socket:%d bind error", connection->m_coTcp->get_socketfd());
            return NULL;
        }
        if (CO_OK != connection->m_coTcp->server_listen(MAX_LISTEN_BACKLOG)) {
            CO_SERVER_LOG_ERROR("listen tcp socket:%d listen error", connection->m_coTcp->get_socketfd());
            return NULL;
        }
    }

    m_freeConnections.pop_front();
    connection->m_startTimestamp = GET_CURRENTTIME_MS(); 

    m_innerSockets.insert(connection->m_coTcp->get_socketfd());
    return connection;
}

CoConnection* CoConnectionPool::get_connection(int32_t socketFd)
{
    // 在cycle所属线程中调用 无需加锁
    if (m_freeConnections.empty()) {
        expand_connections();
        if (m_freeConnections.empty()) {
            CO_SERVER_LOG_ERROR("%u connections are not enough, no connection", m_maxConnectionSize);
            return NULL;
        }
    }

    CoConnection* connection = m_freeConnections.front();
    if (CO_OK != connection->m_coTcp->init_client_socketfd(socketFd)) {
        return NULL;
    }

    if (CO_OK != connection->m_coTcp->set_nonblock()) {
        CO_SERVER_LOG_ERROR("tcp socketfd:%d set nonblock error", connection->m_coTcp->get_socketfd());
        return NULL;
    }

    m_freeConnections.pop_front();
    connection->m_startTimestamp = GET_CURRENTTIME_MS();

    m_innerSockets.insert(socketFd);
    return connection;
}

void CoConnectionPool::free_connection(CoConnection* connection)
{
    // 函数肯定在CoServer内部线程中调用 无需加锁
    int32_t socketFd = connection->m_coTcp->get_socketfd();
    auto itrFind = m_innerSockets.find(socketFd);
    if (itrFind != m_innerSockets.end()) {
        m_innerSockets.erase(itrFind);

    } else {
        CO_SERVER_LOG_FATAL("connection free fd:%d, not find inner sockets", socketFd);
    }

    if (socketFd > 0) {
        connection->m_coTcp->tcp_close();

    } else {
        CO_SERVER_LOG_WARN("connection socket:%d already close", socketFd);
    }

    // cleanup
    for (auto &itrFunc : connection->m_handlerCleanups) {
        itrFunc(connection);
    }
    connection->m_handlerCleanups.clear();
    
    // 置于队列头 方便下次使用
    connection->reset();
    m_freeConnections.emplace_front(connection);
}

CoConnection* CoConnectionPool::get_connection_accord_id(int32_t connId)
{
    return m_connections[connId - 1];   // conn id是从1开始
}

bool CoConnectionPool::is_inner_socketfd(int32_t socketFd)
{
    if (m_innerSockets.find(socketFd) != m_innerSockets.end()) {
        return true;
    }
    return false;
}

void CoConnectionPool::close_all_connection()
{
    for (auto &itr : m_connections) {
        CoConnection* connection = itr;

        if (!(connection->m_flagBlockConn)) {
            if (connection->m_coTcp->get_socketfd() > 0) {
                // 连接使用中 调用读事件handler来处理主动关闭标识  注：客户端处于连接中 肯定有读事件及回调函数存在
                connection->m_flagPendingEof = 1;
                connection->m_cycle->m_dispatcher->m_delayConnections.push(std::make_pair(connection, connection->m_version));
            }
        }
    }
}

}

