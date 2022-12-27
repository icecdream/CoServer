
#include "upstream/co_callback_upstream.h"
#include "base/co_log.h"
#include "core/co_cycle.h"
#include "core/co_event.h"
#include "core/co_request.h"
#include "core/co_callback_request.h"


namespace coserver
{

CoUpstreamInfo::CoUpstreamInfo()
{
}

CoUpstreamInfo::~CoUpstreamInfo()
{
    SAFE_DELETE(m_protocol);
}

void CoCallbackUpstream::upstream_init(CoConnection* connection)
{
    // 建立连接
    if (connection->m_readEvent->m_flagActive) {
        // 复用连接 删除之前的keepalive定时器
        connection->m_cycle->m_timer->del_timer(connection->m_readEvent);

    } else {
        CoCycle* cycle = connection->m_cycle;

        co_use_defer_return();
        // 需要继续写 不需要删除write epoll
        co_defer(cycle->m_timer->del_timer(connection->m_writeEvent);)

        // 非复用连接 添加定时器 epoll事件（只需要监听hup 防止断开连接）
        cycle->m_timer->add_timer(connection->m_writeEvent, connection->m_scoketConnTimeout);
        if (cycle->m_coEpoll->modify_connection(connection, EPOLL_EVENTS_ADD, CO_EVENT_WRITE)) {
            CO_SERVER_LOG_FATAL("(cid:%u rid:%uz) upstream epoll add event failed", connection->m_connId, connection->m_request->m_requestId);
            return co_defer_return(upstream_finalize(connection->m_request, CO_ERROR));
        }

        // 读事件没在epoll中 说明是新连接 不是复用连接 需要连接服务器
        int32_t ret = connection->m_coTcp->client_connect();
        if (ret != CO_OK) {
            CO_SERVER_LOG_ERROR("(cid:%u) upstream init client connect failed, ret: %d", connection->m_connId, ret);
            return co_defer_return(upstream_finalize(connection->m_request, CO_ERROR));
        }
    }

    // 建立连接后发送数据
    connection->m_request->m_connectUs = GET_CURRENTTIME_US() - connection->m_request->m_startUs;
    return upstream_write(connection);
}

void CoCallbackUpstream::upstream_write(CoConnection* connection)
{
    CoCycle* cycle = connection->m_cycle;
    CoRequest* request = connection->m_request;
    CoEvent* writeEvent = connection->m_writeEvent;

    co_use_defer_return();
    co_defer(
        cycle->m_timer->del_timer(writeEvent);
        if (cycle->m_coEpoll->modify_connection(connection, EPOLL_EVENTS_DEL, CO_EVENT_OUT)) {
            CO_SERVER_LOG_FATAL("(cid:%u rid:%u) upstream epoll del event failed", connection->m_connId, request->m_requestId);
            return upstream_finalize(request, CO_ERROR);
        }
    )

    // 添加写事件超时定时器和epoll
    cycle->m_timer->add_timer(writeEvent, connection->m_socketSndTimeout);
    if (cycle->m_coEpoll->modify_connection(connection, EPOLL_EVENTS_ADD, CO_EVENT_OUT)) {
        CO_SERVER_LOG_FATAL("(cid:%u rid:%u) upstream epoll add event failed", connection->m_connId, request->m_requestId);
        return co_defer_return(upstream_finalize(request, CO_ERROR));
    }

    // 构建请求
    CoBuffer* coBuffer = connection->m_coBuffer;
    if (coBuffer->get_buffersize() > 0) {
        coBuffer->reset();
    }
    request->m_protocol->encode(coBuffer);

    // start send data
    for ( ; ; ) {
        if (coBuffer->get_buffersize() > 0) {
            int32_t ret = connection->m_coTcp->tcp_write(coBuffer->get_bufferdata(), coBuffer->get_buffersize());
            if (ret > 0) {
                coBuffer->buffer_erase(ret);
                CO_SERVER_LOG_DEBUG("(cid:%u rid:%u) upstream write bytes:%d, remain bytes:%lu", connection->m_connId, request->m_requestId, ret, coBuffer->get_buffersize());
                continue;
            }

            // 对端关闭连接或者发送出错  直接结束当前请求
            CO_SERVER_LOG_ERROR("(cid:%u rid:%u) upstream buffer write socket fd:%d failed, ret:%d errno:%d", connection->m_connId, request->m_requestId, connection->m_coTcp->get_socketfd(), ret, errno);
            return co_defer_return(upstream_finalize(request, CO_ERROR));
        }
        break;
    }
    request->m_writeUs = GET_CURRENTTIME_US() - request->m_startUs;

    // 请求发送完毕 开始读取响应数据
    return co_defer_return(upstream_read(connection));
}

void CoCallbackUpstream::upstream_read(CoConnection* connection)
{
    CoCycle* cycle = connection->m_cycle;
    CoRequest* request = connection->m_request;
    CoEvent* readEvent = connection->m_readEvent;
    CoBuffer* coBuffer = connection->m_coBuffer;

    co_use_defer_return();
    co_defer(
        // 删除读epoll/timer
        cycle->m_timer->del_timer(readEvent);
        if (CO_OK != cycle->m_coEpoll->modify_connection(connection, EPOLL_EVENTS_DEL, CO_EVENT_IN)) {
            CO_SERVER_LOG_FATAL("(cid:%u rid:%u) upstream epoll del event failed", connection->m_connId, request->m_requestId);
        }
    )

    // 读事件和定时器处理
    cycle->m_timer->add_timer(readEvent, connection->m_socketRcvTimeout);
    if (cycle->m_coEpoll->modify_connection(connection, EPOLL_EVENTS_ADD, CO_EVENT_IN)) {
        CO_SERVER_LOG_FATAL("(cid:%u rid:%u) upstream epoll add event failed", connection->m_connId, request->m_requestId);
        return co_defer_return(upstream_finalize(request, CO_ERROR));
    }

    // start read data
    int32_t ret = CO_OK;
    for ( ; ; ) {
        // 申请接下来的缓冲区空间
        ret = coBuffer->buffer_expand(BUFFER_SIZE_4096);
        if (CO_OK != ret) {
            CO_SERVER_LOG_ERROR("(cid:%u rid:%u) upstream buffer expand:%d failed", connection->m_connId, request->m_requestId, BUFFER_SIZE_4096); 
            return co_defer_return(upstream_finalize(request, CO_ERROR));
        }

        ret = connection->m_coTcp->tcp_read(coBuffer->get_bufferdata() + coBuffer->get_buffersize(), BUFFER_SIZE_4096);
        // 不需要处理EAGAIN  hook保证返回数据或出错
        if (ret < CO_OK) {
            CO_SERVER_LOG_ERROR("(cid:%u rid:%u) upstream socket tcpread ret:%d error", connection->m_connId, request->m_requestId, ret);
            break;
        }

        // 增加缓冲区 有效数据长度
        int32_t expandSize = ret;
        ret = coBuffer->buffer_size_expand(expandSize);
        if (CO_OK != ret) {
            CO_SERVER_LOG_ERROR("(cid:%u rid:%u) upstream coBuffer expand size:%d failed", connection->m_connId, request->m_requestId, expandSize);
            break;
        }

        // 读到数据 进行协议解析
        ret = request->m_protocol->decode(coBuffer);
        if (CO_AGAIN == ret) {
            // 数据不足
            CO_SERVER_LOG_DEBUG("(cid:%u rid:%u) upstream protocol decode need more data, read again", connection->m_connId, request->m_requestId);
            continue;
        }
        
        if (CO_OK == ret) {
            // 响应协议解析成功
            request->m_readUs = GET_CURRENTTIME_US() - request->m_startUs;
            CO_SERVER_LOG_DEBUG("(cid:%u rid:%u) upstream protocol decode success", connection->m_connId, request->m_requestId);

        } else {
            // 解析协议失败
            CO_SERVER_LOG_ERROR("(cid:%u rid:%u) upstream buffer protocol parse failed", connection->m_connId, request->m_requestId);
        }
        
        break;
    }

    if (CO_OK == ret) {
        return co_defer_return(upstream_process(connection));
    }
    return co_defer_return(upstream_finalize(request, ret));
}

void CoCallbackUpstream::upstream_process(CoConnection* connection)
{
    CoRequest* request = connection->m_request;
    int32_t ret = CO_OK;

    if (request->m_userProcess) {
        // 请求最大处理时间为keepalive时间
        connection->m_cycle->m_timer->add_timer(connection->m_readEvent, connection->m_keepaliveTimeout);

        // 用户逻辑处理
        ret = request->m_userProcess(request->m_userData);
        request->m_processUs = GET_CURRENTTIME_US() - request->m_startUs;

        connection->m_cycle->m_timer->del_timer(connection->m_readEvent);
    }

    return upstream_finalize(request, ret);
}

void CoCallbackUpstream::upstream_finalize(CoRequest* request, int32_t retCode)
{
    CoConnection* connection = request->m_connection;
    CoUpstream* upstream = connection->m_upstream;

    bool needRetry = false;
    if (!(connection->m_flagParentDying)) {
        // 非异常情况下 在尝试重试
        if (CO_OK != retCode && upstream->m_confUpstream->m_retryMaxnum > request->m_retryTimes) {
            needRetry = true;
        }
    }

    if (!needRetry) {
        upstream_finalize_request(request, retCode);
        // 内部判断keepalive
        connection->m_cycle->m_upstreamPool->free_upstream_connection(connection, retCode);

    } else {
        // upstream需要重试
        CO_SERVER_LOG_WARN("(cid:%u rid:%u) upstream:%s backend:%s finalize, need retry", connection->m_connId, request->m_requestId, upstream->m_confUpstream->m_name.c_str(), connection->m_backend->m_confUpstreamServer->m_serverkey.c_str());

        connection->m_cycle->m_upstreamPool->free_upstream_connection(connection, retCode);
        CoUpstreamPool::retry_upstream(request, upstream->m_confUpstream->m_name);
    }

    return ;
}

void CoCallbackUpstream::upstream_finalize_request(CoRequest* request, int32_t retCode)
{
    CoUpstreamInfo* upstreamInfo = request->m_upstreamInfo;
    // 记录upstream耗时 响应状态
    upstreamInfo->m_useTimeUs = GET_CURRENTTIME_US() - request->m_startUs;
    upstreamInfo->m_status = retCode;

    if (request->m_userDestroy) {
        request->m_userDestroy(request->m_userData);
    }

    // 正常子请求结束 检查父请求（detach请求没有父请求 独立的 不检查）
    if (!(request->m_requestType & CO_REQUEST_DETACH)) {
        CoRequest* parentRequest = request->m_parent;

        auto itr = parentRequest->m_upstreamRequests.find(request->m_requestId);
        if (itr != parentRequest->m_upstreamRequests.end()) {
            parentRequest->m_upstreamRequests.erase(itr);

        } else {
            CO_SERVER_LOG_FATAL("(rid:%u prid:%u) upstream finalize parent request not find subrequestid", request->m_requestId, parentRequest->m_requestId);
        }

        (parentRequest->m_count) --;
        if (parentRequest->m_count == 1) {
            // 子请求全部处理完成 唤醒父请求
            request->m_cycle->m_dispatcher->m_delayConnections.push(std::make_pair(parentRequest->m_connection, parentRequest->m_connection->m_version));
        }
        CO_SERVER_LOG_DEBUG("(rid:%u prid:%u) upstream finalize parent count:%d", request->m_requestId, parentRequest->m_requestId, parentRequest->m_count);
    }

    CO_SERVER_LOG_DEBUG("(rid:%u) upstream finalize success, timeus start:%lu, diffstart conn:%lu write:%lu read:%lu process:%lu", request->m_requestId, request->m_startUs, request->m_connectUs, request->m_writeUs, request->m_readUs, request->m_processUs);
    SAFE_DELETE(request);

    return ;
}

}

