#include "core/co_callback_event.h"
#include "base/co_log.h"
#include "core/co_cycle.h"
#include "core/co_event.h"
#include "core/co_callback_request.h"
#include "upstream/co_callback_upstream.h"


namespace coserver 
{

int32_t CoCallbackEvent::event_exception(CoConnection* connection)
{
    if (!(connection->m_flagTimedOut || connection->m_flagPendingEof || connection->m_flagDying)) {
        return CO_OK;
    }

    // debug
    if (connection->m_request) {
        CoRequest* request = connection->m_request;
        CO_SERVER_LOG_WARN("(cid:%u) connection exception, timedout:%d pendingeof:%d dying:%d, socket ipport:%s, request us start:%lu, diffstart read:%lu process:%lu write:%lu", 
                            connection->m_connId, connection->m_flagTimedOut, connection->m_flagPendingEof, connection->m_flagDying, connection->m_coTcp->get_ipport().c_str(), request->m_startUs, request->m_readUs, request->m_processUs, request->m_writeUs);

    } else {
        CO_SERVER_LOG_WARN("(cid:%u) connection exception, timedout:%d pendingeof:%d dying:%d, socket ipport:%s, no request", connection->m_connId, connection->m_flagTimedOut, connection->m_flagPendingEof, connection->m_flagDying, connection->m_coTcp->get_ipport().c_str());
    }

    connection->m_flagDying = 1;

    // 针对第三方函数阻塞中时 等待第三方函数返回
    if (connection->m_flagThirdFuncBlocking) {
        CO_SERVER_LOG_WARN("(cid:%u) connection exception, wait third func blocking, timedout:%d pendingeof:%d dying:%d", connection->m_connId, connection->m_flagTimedOut, connection->m_flagPendingEof, connection->m_flagDying);
        return CO_EXCEPTION;
    }

    // 设置当前阻塞的连接的销毁标志   防止第三方阻塞socket 使用了临时资源 先恢复阻塞处代码 尽快结束当前连接
    if (connection->m_flagUseBlockConn || connection->m_flagBlockConn) {
        if (connection->m_flagUseBlockConn) {
            connection->m_blockConn->m_flagDying = 1;
        }

        CO_SERVER_LOG_WARN("(cid:%u) connection useblock:%d flagblock:%d, socket:%d exception, resume dying client, quick finish, timedout:%d pendingeof:%d dying:%d", 
                            connection->m_connId, connection->m_flagUseBlockConn, connection->m_flagBlockConn, connection->m_coTcp->get_socketfd(), connection->m_flagTimedOut, connection->m_flagPendingEof, connection->m_flagDying);
        return CO_OK;
    }

    if (connection->m_request) {
        CoRequest* request = connection->m_request;
        CO_SERVER_LOG_WARN("(cid:%u rid:%u rt:%d) request connection timedout:%d pendingeof:%d dying:%d", connection->m_connId, request->m_requestId, request->m_requestType, connection->m_flagTimedOut, connection->m_flagPendingEof, connection->m_flagDying);

        //  有子请求 立即调用所有子请求切入   设置子请求销毁标志 恢复子请求  等待所有子请求处理完毕后再唤醒父请求
        if (!request->m_upstreamRequests.empty()) {
            for (auto &itr : request->m_upstreamRequests) {
                CoRequest* upstreamRequest = itr.second;
                CoConnection* upstreamConnection = upstreamRequest->m_connection;

                upstreamConnection->m_flagDying = 1;
                upstreamConnection->m_flagParentDying = 1; 
                connection->m_cycle->m_dispatcher->m_delayConnections.push(std::make_pair(upstreamConnection, upstreamConnection->m_version));
                CO_SERVER_LOG_WARN("(cid:%u scid:%d srid:%u) connection exception, resume dying upstreams, quick finish", connection->m_connId, upstreamConnection->m_connId, upstreamRequest->m_requestId);
            }

            return CO_EXCEPTION;
        }

    } else {
        // 一般是在keepalive中
        if (!(connection->m_upstream)) {
            CO_SERVER_LOG_WARN("(cid:%u) no request, free connection timedout:%d pendingeof:%d dying:%d", connection->m_connId, connection->m_flagTimedOut, connection->m_flagPendingEof, connection->m_flagDying);
            CoCallbackRequest::free_request_connection(connection, CO_EXCEPTION);

        } else {
            CO_SERVER_LOG_WARN("(cid:%u) no request, free upstream connection timedout:%d pendingeof:%d dying:%d", connection->m_connId, connection->m_flagTimedOut, connection->m_flagPendingEof, connection->m_flagDying);
            connection->m_cycle->m_upstreamPool->free_upstream_connection(connection, CO_EXCEPTION);
        }

        return CO_EXCEPTION;
    }

    // 其他异常继续已经设置dying标志位 继续处理即可
    return CO_OK;
}

}

