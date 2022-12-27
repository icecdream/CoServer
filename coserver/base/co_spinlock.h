#ifndef _CO_SPINLOCK_H_
#define _CO_SPINLOCK_H_

#include <atomic>
#include <sched.h>

namespace coserver
{

class CoSpinlock 
{
public:
    CoSpinlock() { m_lock.clear(); }
    CoSpinlock(const CoSpinlock&) = delete;
    ~CoSpinlock() = default;

    void lock() 
    {
        if (!m_lock.test_and_set(std::memory_order_acquire)) {
            return ;
        }

        while (1) {
            for (int32_t i=0; i<4; ++i) {
                if (!m_lock.test_and_set(std::memory_order_acquire)) {
                    return ;
                }
            }

            sched_yield();  // usleep(1)
        }

        return ;
    }
    
    bool try_lock() 
    {
        return !m_lock.test_and_set(std::memory_order_acquire);
    }
    
    void unlock() 
    {
        m_lock.clear(std::memory_order_release);
    }

private:
    std::atomic_flag m_lock;
};

}

#endif //_CO_SPINLOCK_H_

