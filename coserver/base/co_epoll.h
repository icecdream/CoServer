#ifndef _CO_EPOLL_H_
#define _CO_EPOLL_H_

#include <sys/epoll.h>
#include "core/co_connection.h"


namespace coserver
{

/*
EPOLLIN:1
EPOLLOUT:4
EPOLLRDHUP:8192
EPOLLPRI:2
EPOLLERR:8
EPOLLHUP:16
EPOLLONESHOT:1073741824
*/

#define CO_EVENT_IN       EPOLLIN               // 1
#define CO_EVENT_OUT      EPOLLOUT              // 4
#define CO_EVENT_HUP      EPOLLRDHUP            // 8192
#define CO_EVENT_READ     EPOLLIN|EPOLLRDHUP
#define CO_EVENT_WRITE    EPOLLOUT|EPOLLRDHUP
// CO_EVENT_READ和CO_EVENT_WRITE hook使用, 第三方阻塞socket也需要监听EPOLLRDHUP状态

const uint32_t EPOLL_EVENTS_ADD = 0;
const uint32_t EPOLL_EVENTS_DEL = 1;


class CoEpoll
{
public:
    CoEpoll();
    ~CoEpoll();

    int32_t init(int32_t maxConnSizes, int32_t eventSize = 256);

    int32_t add_connection(CoConnection* connection);
    int32_t del_connection(CoConnection* connection);
    int32_t modify_connection(CoConnection* connection, uint32_t eventType, uint32_t eventOP);

    int32_t process_events(uint32_t timerMs);

private:
    int32_t      m_epollFd    = -1;
    int32_t      m_eventsSize = 1024;
    epoll_event* m_events     = NULL;
};

}

#endif //_CO_EPOLL_H_

