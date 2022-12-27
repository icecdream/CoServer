#ifndef _CO_TIMER_H_
#define _CO_TIMER_H_

#include <map>


namespace coserver
{

struct CoEvent;
typedef std::multimap<uint64_t, void*>::iterator TimerNode;

class CoTimer
{
public:
    CoTimer() {}
    ~CoTimer() {}

    void add_timer(CoEvent* event, uint32_t timerMs);
    void del_timer(CoEvent* event);

    // 找出最近定时器的超时时间和当前时间的 时间差
    uint64_t find_timer();
    // 处理已经超时的定时器
    void process_timer();

    bool empty();
    size_t size();


private:
    std::multimap<uint64_t, void*>  m_timers;
};

}

#endif //_CO_TIMER_H_

