#include "core/co_callback_request.h"
#include "base/co_log.h"
#include "core/co_cycle.h"
#include "core/co_callback_event.h"


namespace coserver
{

void CoCallbackRequest::request_init(CoConnection* connection)
{
    // 如果是复用连接 删除之前的keepalive定时器
    if (connection->m_readEvent->m_flagTimerSet) {
        connection->m_cycle->m_timer->del_timer(connection->m_readEvent);
    }

    CoRequest* request = new CoRequest(CO_REQUEST_NORMAL);
    int32_t ret = request->init(connection, connection->m_serverControl->m_confServer->m_serverType);
    if (ret != CO_OK) {
        CO_SERVER_LOG_ERROR("(cid:%u rid:%u) request init request failed, ret:%d", connection->m_connId, request->m_requestId, ret);
        return request_write(request, CO_ERROR);
    }
    
    return request_read(request);
}

void CoCallbackRequest::request_read(CoRequest* request)
{
    CoCycle* cycle = request->m_cycle;
    CoConnection* connection = request->m_connection;
    CoBuffer* coBuffer = connection->m_coBuffer;

    co_use_defer_return();

    // 客户端数据读取完毕或出错 删除事件epoll 只删除读事件监听 还需要监听异常 防止客户端主动断开连接
    co_defer(
        cycle->m_timer->del_timer(connection->m_readEvent);
        if (CO_OK != cycle->m_coEpoll->modify_connection(connection, EPOLL_EVENTS_DEL, CO_EVENT_IN)) {
            CO_SERVER_LOG_FATAL("(cid:%u) request epoll del event failed", connection->m_connId);
        }
    )
    
    // 添加读事件epoll和定时器
    cycle->m_timer->add_timer(connection->m_readEvent, connection->m_socketRcvTimeout);
    if (CO_OK != cycle->m_coEpoll->modify_connection(connection, EPOLL_EVENTS_ADD, CO_EVENT_READ)) {
        CO_SERVER_LOG_FATAL("(cid:%u) request epoll add event failed", connection->m_connId);
        return co_defer_return(request_write(request, CO_ERROR));
    }

    // start read data
    int32_t ret = CO_OK;
    for ( ; ; ) {
        // 申请接下来的缓冲区空间
        ret = coBuffer->buffer_expand(BUFFER_SIZE_4096);
        if (CO_OK != ret) {
            CO_SERVER_LOG_ERROR("(cid:%u rid:%u) buffer expand:%d failed", connection->m_connId, request->m_requestId, BUFFER_SIZE_4096);
            break;
        }

        ret = connection->m_coTcp->tcp_read(coBuffer->get_bufferdata() + coBuffer->get_buffersize(), BUFFER_SIZE_4096);
        // 不需要处理EAGAIN  hook保证返回数据或出错
        if (ret < CO_OK) {
            CO_SERVER_LOG_ERROR("(cid:%u rid:%u) socket tcpread ret:%d error", connection->m_connId, request->m_requestId, ret);
            break;
        }

        // 增加缓冲区 有效数据长度
        int32_t expandSize = ret;
        ret = coBuffer->buffer_size_expand(expandSize);
        if (CO_OK != ret) {
            CO_SERVER_LOG_ERROR("(cid:%u rid:%u) coBuffer expand size:%d failed", connection->m_connId, request->m_requestId, expandSize);
            break;
        }

        // 读到数据 进行协议解析
        ret = request->m_protocol->decode(coBuffer);
        if (CO_AGAIN == ret) {
            // 数据不足
            CO_SERVER_LOG_DEBUG("(cid:%u rid:%u) protocol decode need more data, read again", connection->m_connId, request->m_requestId);
            continue;
        }
        
        if (CO_OK == ret) {
            request->m_readUs = GET_CURRENTTIME_US() - request->m_startUs;
            break;
        }
        
        // 解析协议失败
        CO_SERVER_LOG_ERROR("(cid:%u rid:%u) buffer protocol parse failed, ret:%d", connection->m_connId, request->m_requestId, ret);
        break;
    }

    // 处理失败
    if (CO_OK != ret) {
        return co_defer_return(request_write(request, CO_ERROR));
    }

    // 请求协议解析成功  开始处理请求
    return co_defer_return(request_process(request));
}

void CoCallbackRequest::request_process(CoRequest* request)
{
    CoTimer* timer = request->m_cycle->m_timer;
    CoEvent* readEvent = request->m_connection->m_readEvent;

    // 请求最大处理时间为keepalive时间  超时后清理请求
    timer->add_timer(readEvent, request->m_connection->m_keepaliveTimeout);

    // 业务函数处理
    int32_t ret = request->m_userProcess(request->m_userData);
    request->m_processUs = GET_CURRENTTIME_US() - request->m_startUs;
    CO_SERVER_LOG_DEBUG("(cid:%u rid:%u) business handler process complete, ret:%d", request->m_connection->m_connId, request->m_requestId, ret);

    timer->del_timer(readEvent);
    return request_write(request, ret);
}

void CoCallbackRequest::request_write(CoRequest* request, int32_t retCode)
{
    CoCycle* cycle = request->m_cycle;
    CoConnection* connection = request->m_connection;
    CoEvent* writeEvent = connection->m_writeEvent;

    co_use_defer_return();
    // 客户端数据读取完毕 删除事件epoll 只删除读事件监听 还需要监听异常 防止客户端主动断开连接
    co_defer(
        cycle->m_timer->del_timer(writeEvent);
        if (CO_OK != cycle->m_coEpoll->modify_connection(connection, EPOLL_EVENTS_DEL, CO_EVENT_OUT)) {
            CO_SERVER_LOG_FATAL("(cid:%u) request epoll del event failed", connection->m_connId);
        }
    )

    // 添加写事件超时定时器和epoll
    cycle->m_timer->add_timer(writeEvent, connection->m_socketSndTimeout);
    if (cycle->m_coEpoll->modify_connection(connection, EPOLL_EVENTS_ADD, CO_EVENT_OUT)) {
        CO_SERVER_LOG_FATAL("(cid:%u rid:%u) request epoll add event failed", connection->m_connId, request->m_requestId);
        return co_defer_return(request_finalize(request, CO_ERROR));
    }

    // 构建响应
    CoBuffer* coBuffer = connection->m_coBuffer;
    if (coBuffer->get_buffersize() > 0) {
        coBuffer->reset();
    }
    request->m_protocol->encode(coBuffer);

    // start write data
    for ( ; ; ) {
        if (coBuffer->get_buffersize() > 0) {
            int32_t ret = connection->m_coTcp->tcp_write(coBuffer->get_bufferdata(), coBuffer->get_buffersize());
            if (ret > 0) {
                coBuffer->buffer_erase(ret);
                CO_SERVER_LOG_DEBUG("(cid:%u rid:%u) response write bytes:%d, remain bytes:%lu", connection->m_connId, request->m_requestId, ret, coBuffer->get_buffersize());
                continue;
            }

            // 对端关闭连接或者发送出错  直接结束当前请求
            CO_SERVER_LOG_ERROR("(cid:%u rid:%u) buffer write socket fd:%d failed, ret:%d errno:%d", connection->m_connId, request->m_requestId, connection->m_coTcp->get_socketfd(), ret, errno);
            return co_defer_return(request_finalize(request, CO_ERROR));
        }
        break;
    }

    request->m_writeUs = GET_CURRENTTIME_US() - request->m_startUs;
    CO_SERVER_LOG_DEBUG("(cid:%u rid:%u) response write success", connection->m_connId, request->m_requestId);

    return co_defer_return(request_finalize(request, retCode));
}

void CoCallbackRequest::request_finalize(CoRequest* request, int32_t retCode)
{
    CoConnection* connection = request->m_connection;

    // 调用结束客户端的请求
    if (request->m_userDestroy) {
        request->m_userDestroy(request->m_userData);
    }
    CO_SERVER_LOG_DEBUG("(cid:%u rid:%u) request finalize, timeus start:%lu, diffstart read:%lu process:%lu write:%lu", connection->m_connId, request->m_requestId, request->m_startUs, request->m_readUs, request->m_processUs, request->m_writeUs);

    SAFE_DELETE(request);

    // keepalive内部判断
    return free_request_connection(connection, retCode);
}

void CoCallbackRequest::free_request_connection(CoConnection* connection, int32_t retCode)
{
    CoCycle* cycle = connection->m_cycle;
    CoEvent* readEvent = connection->m_readEvent;
    CoEvent* writeEvent = connection->m_writeEvent;

    // 清理连接上的事件和定时器
    if (writeEvent->m_flagTimerSet) {
        connection->m_cycle->m_timer->del_timer(writeEvent);
    }
    if (readEvent->m_flagTimerSet) {
        connection->m_cycle->m_timer->del_timer(readEvent);
    }

    if (CO_OK != retCode) {
        // 执行出错 真正关闭连接
        if (cycle->m_coEpoll->del_connection(connection)) {
            CO_SERVER_LOG_FATAL("(cid:%u) epoll del connection failed", connection->m_connId);
        }
        cycle->m_connectionPool->free_connection(connection);
        CO_SERVER_LOG_INFO("(cid:%u) free connection, ret:%d", connection->m_connId, retCode);
        return ;
    }

    // reuse连接信息  tcp连接保留(清空后再添加epoll/timer 防止连接版本不对)
    connection->reset(true);

    // 复用连接 添加epoll、定时器等  需要添加epoll_in 继续读取新请求数据
    if (cycle->m_coEpoll->modify_connection(connection, EPOLL_EVENTS_ADD, CO_EVENT_IN)) {
        CO_SERVER_LOG_FATAL("(cid:%u) client keepalive, epoll del event failed", connection->m_connId);
    }
    if (cycle->m_coEpoll->modify_connection(connection, EPOLL_EVENTS_DEL, CO_EVENT_OUT)) {
        CO_SERVER_LOG_FATAL("(cid:%u) client keepalive, epoll del event failed", connection->m_connId);
    }
    // 读超时时间重置为keepalive时间
    cycle->m_timer->add_timer(readEvent, connection->m_keepaliveTimeout);

    // 重置读事件回调函数为event_init
    connection->m_handler = request_init;
    CO_SERVER_LOG_DEBUG("(cid:%u) free client connection keepalive", connection->m_connId);
    return ;
}

}

