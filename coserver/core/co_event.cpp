#include "core/co_event.h"
#include "core/co_connection.h"


namespace coserver
{

CoEvent::CoEvent(int32_t type, CoConnection* connection): m_eventType(type), m_connection(connection), m_flagActive(0), m_flagTimerSet(0) 
{
    if (EVENT_TYPE_SLEEP == m_eventType) {
        m_timerPreHandler = [=](CoEvent* event) {
            // sleep事件超时不置标志位 正常继续执行后续流程即可
            event->m_connection->m_flagTimedOut = 0;
            event->m_connection->m_flagThirdFuncBlocking = 0;   // 防止event_exception函数处理时 同时还有网络错误 导致连接假死 无法处理
        };
    }
}

void CoEvent::reset() 
{
    m_flagActive = 0;
    m_flagTimerSet = 0;
}

}

