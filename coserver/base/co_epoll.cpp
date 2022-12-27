#include "base/co_epoll.h"
#include "base/co_log.h"
#include "base/co_common.h"
#include "core/co_cycle.h"
#include "core/co_dispatcher.h"


namespace coserver
{

const int32_t MIN_EV_NUMBER = 32;
const int32_t MAX_EV_NUMBER = 1024;

const uint32_t TIMER_INFINITE = -1;


CoEpoll::CoEpoll()
{
}

CoEpoll::~CoEpoll()
{
    SAFE_CLOSE(m_epollFd);
    SAFE_DELETE_ARRAY(m_events);
}

int32_t CoEpoll::init(int32_t maxConnSizes, int32_t eventSize) 
{
    m_eventsSize = eventSize > MAX_EV_NUMBER ? MAX_EV_NUMBER : eventSize;
    m_eventsSize = eventSize < MIN_EV_NUMBER ? MIN_EV_NUMBER : eventSize;

    m_events = new epoll_event[m_eventsSize];
    m_epollFd = epoll_create(maxConnSizes / 2);

    return m_epollFd < 0 ? m_epollFd : 0;
}

int32_t CoEpoll::add_connection(CoConnection* connection)
{
    uint32_t epollCurEvents = EPOLLIN | EPOLLOUT | EPOLLRDHUP;
    if (connection->m_epollCurEvents == epollCurEvents && connection->m_epollCurVersion == connection->m_version) {
        CO_SERVER_LOG_DEBUG("(cid:%u) events(in/out/hup):1/1/1 not change", connection->m_connId);
        return CO_OK;
    }

    struct epoll_event epollEvent;
    epollEvent.events = epollCurEvents | EPOLLET;
    epollEvent.data.u64 = GEN_U64(connection->m_connId, connection->m_version);

    int32_t socketFd = connection->m_coTcp->get_socketfd();
    if (epoll_ctl(m_epollFd, EPOLL_CTL_ADD, socketFd, &epollEvent) == -1) {
        CO_SERVER_LOG_FATAL("(cid:%u) epoll_ctl add connection failed, fd:%d errno:%d op:ADD events(in/out/hup):1/1/1", connection->m_connId, socketFd, errno);
        return CO_ERROR;
    }

    connection->m_epollCurEvents = epollCurEvents;
    connection->m_epollCurVersion = connection->m_version;
    connection->m_readEvent->m_flagActive = 1;
    connection->m_writeEvent->m_flagActive = 1;
    CO_SERVER_LOG_DEBUG("(cid:%u) epoll_ctl add connection success, fd:%d op:ADD events(in/out/hup):1/1/1", connection->m_connId, socketFd);
    return CO_OK;
}

int32_t CoEpoll::del_connection(CoConnection* connection)
{
    if (connection->m_epollCurEvents == 0) {
        CO_SERVER_LOG_DEBUG("(cid:%u) events(in/out/hup):0/0/0 not change", connection->m_connId);
        return CO_OK;
    }

    epoll_event epollEvent;
    epollEvent.events = 0;
    epollEvent.data.u64 = 0;

    int32_t socketFd = connection->m_coTcp->get_socketfd();
    if (epoll_ctl(m_epollFd, EPOLL_CTL_DEL, socketFd, &epollEvent) == -1) {
        CO_SERVER_LOG_FATAL("(cid:%u) epoll_ctl del connection failed, fd:%d errno:%d op:DEL events(in/out/hup):0/0/0", connection->m_connId, socketFd, errno);
        return CO_ERROR;
    }

    connection->m_epollCurEvents = 0;
    connection->m_epollCurVersion = 0;
    connection->m_readEvent->m_flagActive = 0;
    connection->m_writeEvent->m_flagActive = 0;
    CO_SERVER_LOG_DEBUG("(cid:%u) epoll_ctl del connection success, fd:%d op:DEL events(in/out/hup):0/0/0", connection->m_connId, socketFd);
    return CO_OK;
}

int32_t CoEpoll::modify_connection(CoConnection* connection, uint32_t eventType, uint32_t eventOP) 
{
    uint32_t epollPreEvents = connection->m_epollCurEvents;
    uint32_t epollCurEvents = epollPreEvents | eventOP; // add
    if (EPOLL_EVENTS_DEL == eventType) {
        epollCurEvents = epollPreEvents & (~eventOP); // del
    }

    // epoll events没有变化 不需要操作
    if (epollCurEvents == epollPreEvents && connection->m_epollCurVersion == connection->m_version) {
        CO_SERVER_LOG_DEBUG("(cid:%u) events(in/out/hup):%d/%d/%d not change", connection->m_connId, (epollCurEvents & EPOLLIN) > 0, (epollCurEvents & EPOLLOUT) > 0, (epollCurEvents & EPOLLRDHUP) > 0);
        return CO_OK;
    }

    /*
        epoll针对一个socket 如果添加过 后续需要时修改 不能再是添加操作
        修改操作的话 需要带上上次已经添加过的事件
    */
    int32_t epollOP = EPOLL_CTL_ADD;
    if (epollPreEvents != 0) {
        if (epollCurEvents == 0) {
            epollOP = EPOLL_CTL_DEL;

        } else {
            epollOP = EPOLL_CTL_MOD;
        }
    }

    epoll_event epollEvent;
    epollEvent.events = epollCurEvents | EPOLLET;
    epollEvent.data.u64 = GEN_U64(connection->m_connId, connection->m_version);

    int32_t socketFd = connection->m_coTcp->get_socketfd();
    if (epoll_ctl(m_epollFd, epollOP, socketFd, &epollEvent) == -1) {
        CO_SERVER_LOG_FATAL("(cid:%u) epoll_ctl(%d) modify failed, fd:%d errno:%d op:%d events(in/out/hup):%d/%d/%d", connection->m_connId, m_epollFd, 
                                socketFd, errno, epollOP, (epollCurEvents & EPOLLIN) > 0, (epollCurEvents & EPOLLOUT) > 0, (epollCurEvents & EPOLLRDHUP) > 0);
        return CO_ERROR;
    }

    connection->m_epollCurEvents = epollCurEvents;
    connection->m_epollCurVersion = connection->m_version;
    // 处理事件active
    if (epollCurEvents & (CO_EVENT_READ)) {
        connection->m_readEvent->m_flagActive = 1;
    } else {
        connection->m_readEvent->m_flagActive = 0;
    }

    if (epollCurEvents & CO_EVENT_OUT) {
        connection->m_writeEvent->m_flagActive = 1;
    } else {
        connection->m_writeEvent->m_flagActive = 0;
    }

    CO_SERVER_LOG_DEBUG("(cid:%u) epoll_ctl(%d) modify success, fd:%d op:%d events(in/out/hup):%d/%d/%d", connection->m_connId, m_epollFd, 
                        socketFd, epollOP, (epollCurEvents & EPOLLIN) > 0, (epollCurEvents & EPOLLOUT) > 0, (epollCurEvents & EPOLLRDHUP) > 0);
    return CO_OK;
}

int32_t CoEpoll::process_events(uint32_t timerMs)
{
    int32_t epollSize = epoll_wait(m_epollFd, m_events, m_eventsSize, timerMs);
    if (epollSize == -1) {
        if (errno == EINTR) {
            return CO_OK;
        }
        CO_SERVER_LOG_FATAL("epoll_wait failed, errno:%d", errno);
        return CO_ERROR;
    }
    
    // timeout
    if (epollSize == 0) {
        if (timerMs != TIMER_INFINITE) {
            return CO_OK;
        }

        CO_SERVER_LOG_WARN("epoll_wait return no events without timeout");
        return CO_ERROR;
    }

    CoThreadLocalInfo* threadInfo = GET_TLS();
    CoCycle* cycle = threadInfo->m_coCycle;

    // process events
    for (int32_t i=0; i<epollSize; ++i) {
        uint32_t connId = GET_U64_HIGH32(m_events[i].data.u64);
        uint32_t connVersion = GET_U64_LOW32(m_events[i].data.u64);

        CoConnection* connection = cycle->m_connectionPool->get_connection_accord_id(connId);
        if (connection->m_version != connVersion) {
            /*
                背景:
                    在处理数组前面连接的时候, 将数组后面连接也处理了, 导致再处理数组后面连接时, 其实已经处理过了, 导致version不一致

                场景举例: 
                    1. 连接1在执行业务逻辑时, 遇到阻塞socket, 协程阻塞在block socket上, 切出协程
                    2. block socket响应数据, 同时 原始连接1收到了断开连接的数据
                    3. epoll_wait响应, block socket数据消息在数组0中, 连接1断开消息在数组1中
                    4. 此时, 先处理数组0消息, block socket处理完毕, 业务正常处理完毕, 连接1正常处理完毕, 连接1 version++, 重新注册到epoll中
                    5. 接着处理数组1消息, 因为还在上一次的epoll_wait中, 所以数据还是老的, 此时 连接1的version还未++, 所以出现此情况

                解决:
                    忽略当前通知即可, 因为在第四步重新注册了epoll, 即使ET模式下 (因为socket重新注册), epoll还会再处理一遍socket上的数据, 继续通知
            */
            CO_SERVER_LOG_ERROR("(cid:%u) epoll index:%d size:%d stale event, old version:%d  now version:%d", connection->m_connId, i, epollSize, connVersion, connection->m_version);
            continue;
        }

        uint32_t revents = m_events[i].events;
        CO_SERVER_LOG_DEBUG("(cid:%u) epoll index:%d size:%d ev:%u u64:%lu", connection->m_connId, i, epollSize, revents, m_events[i].data.u64);
        if (revents & (EPOLLERR|EPOLLHUP)) {
            CO_SERVER_LOG_WARN("(cid:%u) epoll index:%d error on ev:%u u64:%lu", connection->m_connId, i, revents, m_events[i].data.u64);
        }

        if ((revents & (EPOLLERR|EPOLLHUP)) && (revents & (EPOLLIN|EPOLLOUT)) == 0) {
            /*
             ** if the error events were returned without EPOLLIN or EPOLLOUT,
             ** then add these flags to handle the events at least in one
             ** active handler
             ** 如：非阻塞connect连接失败时
             **/
            revents |= EPOLLIN|EPOLLOUT;
        }

        if ((revents & EPOLLIN || revents & EPOLLRDHUP) && connection->m_readEvent->m_flagActive) {
            if (revents & EPOLLRDHUP) {
                // 对端关闭连接, 或者建立连接refused时触发
                connection->m_flagPendingEof = 1;
            }

            CoDispatcher::func_dispatcher(connection);
        }

        if ((revents & EPOLLOUT) && connection->m_writeEvent->m_flagActive) {
            CoDispatcher::func_dispatcher(connection);
        }
    }

    return CO_OK;
}

}

