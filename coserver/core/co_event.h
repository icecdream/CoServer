#ifndef _CO_EVENT_H_
#define _CO_EVENT_H_

#include "base/co_timer.h"
#include "base/co_coroutine.h"


namespace coserver 
{

struct CoConnection;

const int32_t EVENT_TYPE_READ = 0;
const int32_t EVENT_TYPE_WRITE = 1;
const int32_t EVENT_TYPE_SLEEP = 2;

struct CoEvent 
{
    int32_t         m_eventType   = EVENT_TYPE_READ; // 事件类型

    CoConnection*   m_connection  = NULL;   // 事件所属的CoConnection
    TimerNode       m_timerNode;            // 定时器节点

    std::function<void (CoEvent* event)> m_timerPreHandler = NULL;    // 超时时预处理函数

    // flag
    unsigned    m_flagActive:1;           // 为1时表示当前事件是活跃的（epoll监听的） 在添加、删除和处理事件时 active的标志位不同对应着不同的处理方式
    unsigned    m_flagTimerSet:1;         // 为1时表示这个事件存在于定时器中


    CoEvent(int32_t type, CoConnection* connection);
    CoEvent() = delete;

    void reset();
};

}

#endif //_CO_EVENT_H_

