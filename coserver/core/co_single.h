#ifndef _CO_SINGLE_H_
#define _CO_SINGLE_H_

#include <unordered_map>
#include <queue>
#include "base/co_spinlock.h"
#include "core/co_connection.h"

namespace coserver
{

// CoCycle管理整个生命周期的数据结构
class CoSingle
{
public:
    CoSingle();
    ~CoSingle();

    static CoSingle* get_instance();

    bool find_block_mutex(CoConnection* connection);
    void add_block_mutex(pthread_mutex_t* mutex, CoConnection* connection);
    void free_block_mutex(pthread_mutex_t* mutex);


private:
    // mutex级别的自旋锁
    CoSpinlock  m_mtxSpinlock;

    // key: threadID + "_" + connId, 同一线程同一个连接只能wait block在一个mutex上
    std::unordered_set<uint64_t> m_threadConnectionIds;
    // <mutex*, queue<connections> >
    std::unordered_map<void*, std::queue<std::pair<CoConnection*, uint32_t>>> m_blockMutexs;

    // init single
    static CoSingle m_single;
};

}

#endif //_CO_SINGLE_H_

