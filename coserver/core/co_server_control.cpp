#include "core/co_server_control.h"
#include "base/co_log.h"
#include "core/co_cycle.h"
#include "base/co_common.h"
#include "core/co_callback_request.h"


namespace coserver
{

const int32_t MAX_ACCEPT_NUM = 64;
std::unordered_map<std::string, CoUserFuncs*>  g_userFuncs;


CoServerControl::CoServerControl(CoConfServer* confServer)
: m_confServer(confServer)
{
}

CoServerControl::~CoServerControl() 
{
}

int32_t CoServerControl::init(CoCycle* cycle)
{
    // find handler funcs
    auto itr = g_userFuncs.find(m_confServer->m_handlerName);
    if (itr == g_userFuncs.end()) {
        CO_SERVER_LOG_ERROR("server control not find handler funcs, name:%s", m_confServer->m_handlerName.c_str());
        return CO_ERROR;
    }
    m_userFuncs = itr->second;

    //  listen socket connection
    m_listenConnection = cycle->m_connectionPool->get_connection(m_confServer->m_listenIP, m_confServer->m_listenPort, true);
    if (m_listenConnection == NULL) {
        CO_SERVER_LOG_ERROR("server control get connection failed");
        return CO_ERROR;
    }

    // 监听连接 读事件处理函数
    m_listenConnection->m_handler = [=](CoConnection* connection) {
        while(1) {
            int32_t maxAcceptSize = limit();
            if (0 != maxAcceptSize) {
                accept(connection, maxAcceptSize);

            } else {
                // 不能接受新连接 不再监听epoll
                modify_listening();
                break;
            }
        }
    };

    // 监听连接 读事件异常处理函数
    m_listenConnection->m_handlerException = [=](CoConnection* connection) -> int32_t {
        if (connection->m_flagPendingEof) {
            CO_SERVER_LOG_ERROR("listen socket exception, pendingeof:%d", connection->m_flagPendingEof);
        }
        return CO_OK;
    };

    // add to epoll
    modify_listening();
    return CO_OK;
}

void CoServerControl::accept(CoConnection* connection, int32_t maxAcceptSize)
{
    for (int32_t i=0; i<maxAcceptSize; ++i) {
        int32_t clientSocket = -1;
        int32_t ret = connection->m_coTcp->accept(clientSocket);
        if (ret != CO_OK) {
            CO_SERVER_LOG_ERROR("accept failed ret:%d, errno:%d", ret, errno);
            continue;
        }

        // 初始化连接数据 地址等
        if (init_connection(connection->m_cycle, clientSocket) != CO_OK) {
            CO_SERVER_LOG_ERROR("init connection failed, client socket:%d", clientSocket);
            return ;
        }
    }

    return ;
}

int32_t CoServerControl::init_connection(CoCycle* cycle, int32_t socketFd)
{
    // 获取连接
    CoConnection* connection = cycle->m_connectionPool->get_connection(socketFd);
    if (connection == NULL) {
        CO_SERVER_LOG_ERROR("init connection, get connection failed");
        return CO_ERROR;
    }
    // server及超时时间
    connection->m_serverControl = this;
    connection->m_socketRcvTimeout = m_confServer->m_readTimeout;
    connection->m_socketSndTimeout = m_confServer->m_writeTimeout;
    connection->m_keepaliveTimeout = m_confServer->m_keepaliveTimeout;
    
    connection->m_handler = CoCallbackRequest::request_init;
    // 挂载cleanup 释放连接时需要计数
    connection->m_handlerCleanups.push_back(CoServerControl::func_cleanup);

    m_curConnectionSize ++;
    cycle->m_dispatcher->m_delayConnections.push(std::make_pair(connection, connection->m_version));
    CO_SERVER_LOG_DEBUG("(cid:%d) accept one client socketfd:%d", connection->m_connId, socketFd);
    return CO_OK;
}

int32_t CoServerControl::limit()
{
    if (m_curConnectionSize >= m_confServer->m_maxConnections) {
        CO_SERVER_LOG_ERROR("server ip:%s port:%d  cur connectionsize:%d large maxsize:%d", m_confServer->m_listenIP.c_str(), m_confServer->m_listenPort, m_curConnectionSize, m_confServer->m_maxConnections);
        return 0;
    }

    int32_t newConnectionSize = MAX_ACCEPT_NUM;
    int32_t maxNewConnectionSize = m_confServer->m_maxConnections - m_curConnectionSize;
    if (maxNewConnectionSize < newConnectionSize) {
        newConnectionSize = maxNewConnectionSize;
    }

    return newConnectionSize;
}

void CoServerControl::func_cleanup(CoConnection* connection)
{
    CoServerControl* serverControl = connection->m_serverControl;
    serverControl->m_curConnectionSize --;

    return connection->m_serverControl->modify_listening();
}

void CoServerControl::modify_listening()
{
    if (m_curConnectionSize >= m_confServer->m_maxConnections) {
        if (m_listening) {
            m_listening = false;
            m_listenConnection->m_cycle->m_coEpoll->modify_connection(m_listenConnection, EPOLL_EVENTS_DEL, CO_EVENT_READ);
            CO_SERVER_LOG_ERROR("listen socket limit, no accept size, epoll del event");
        }

    } else {
        if (!m_listening) {
            m_listening = true;
            m_listenConnection->m_cycle->m_coEpoll->modify_connection(m_listenConnection, EPOLL_EVENTS_ADD, CO_EVENT_READ);
            CO_SERVER_LOG_INFO("listen socket listening, epoll add event");
        }
    }

    return ;
}

}

