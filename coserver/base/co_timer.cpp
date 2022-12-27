#include "base/co_timer.h"
#include "base/co_log.h"
#include "core/co_event.h"
#include "base/co_common.h"
#include "core/co_connection.h"
#include "core/co_dispatcher.h"


namespace coserver
{

const uint64_t MAX_EPOLL_WAIT_TIME = 1000;


void CoTimer::add_timer(CoEvent* event, uint32_t timerMs) 
{
    CO_SERVER_LOG_DEBUG("(cid:%u et:%d) add timer, flagtimerset:%d -> 1 ms:%u", event->m_connection->m_connId, event->m_eventType, event->m_flagTimerSet, timerMs);

    if (event->m_flagTimerSet) {
        CO_SERVER_LOG_ERROR("(cid:%u et:%d) timer flagtimerset is true", event->m_connection->m_connId, event->m_eventType);
        return;
    }

    uint64_t timer = GET_CURRENTTIME_MS() + timerMs;
    TimerNode iter = m_timers.insert(std::make_pair(timer, (void*)event));

    event->m_timerNode = iter;
    event->m_flagTimerSet = 1;
}

void CoTimer::del_timer(CoEvent* event) 
{
    CO_SERVER_LOG_DEBUG("(cid:%u et:%d) del timer, flagtimerset:%d -> 0", event->m_connection->m_connId, event->m_eventType, event->m_flagTimerSet);

    if (!event->m_flagTimerSet) {
        // 定时器已经超时 删除时告警 忽略
        if (event->m_connection->m_flagTimedOut) {
            CO_SERVER_LOG_WARN("(cid:%u et:%d) timer flagtimerset is false, connection timeout, ignore", event->m_connection->m_connId, event->m_eventType);
        } else {
            CO_SERVER_LOG_ERROR("(cid:%u et:%d) timer flagtimerset is false", event->m_connection->m_connId, event->m_eventType);
        }
        return;
    }

    m_timers.erase(event->m_timerNode);
    event->m_flagTimerSet = 0;
}

uint64_t CoTimer::find_timer() 
{
    if (m_timers.empty()) {
        // 没有定时任务
        return MAX_EPOLL_WAIT_TIME;
    }

    uint64_t timer = 0;
    uint64_t now = GET_CURRENTTIME_MS();
    TimerNode iter = m_timers.begin();
    if (iter->first > now) {
        timer = iter->first - now;
    }
    return timer;
} 

void CoTimer::process_timer() 
{
    uint64_t now = GET_CURRENTTIME_MS();

    while (!m_timers.empty()) {
        TimerNode iter = m_timers.begin();

        if (iter->first <= now) {
            CoEvent* event = (CoEvent* )(iter->second);
            m_timers.erase(iter);
            event->m_flagTimerSet = 0;

            CoConnection* connection = event->m_connection;
            connection->m_flagTimedOut = 1;

            if (event->m_timerPreHandler) {
                event->m_timerPreHandler(event);
            }

            CO_SERVER_LOG_DEBUG("(cid:%u et:%d) process timers remain size:%lu, connection timeout now:%lu timer:%lu", connection->m_connId, event->m_eventType, m_timers.size(), now, iter->first);
            CoDispatcher::func_dispatcher(connection);
            continue;
        }

        break;
    }
}

bool CoTimer::empty()
{
    return m_timers.empty();
}

size_t CoTimer::size()
{
    return m_timers.size();
}

}

