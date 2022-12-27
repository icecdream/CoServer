#include "core/co_single.h"
#include "base/co_log.h"
#include "base/co_config.h"
#include "core/co_cycle.h"


namespace coserver
{

extern thread_local int32_t g_innerThreadId;

// init single
CoSingle CoSingle::m_single;

CoSingle::CoSingle()
{
}

CoSingle::~CoSingle()
{
}

CoSingle* CoSingle::get_instance() 
{
    return &m_single;
}

/*
    函数作用: 防止在add_block_mutex中yield_timer后  其他线程unlock锁 异步resume, 导致后续异常问题
    每次异步resume single时 都find_block_mutex检查一下是否真正的唤醒
*/
bool CoSingle::find_block_mutex(CoConnection* connection)
{
    bool findMutex = false;
    uint64_t threadConnectionId = GEN_U64(g_innerThreadId, connection->m_connId);

    m_mtxSpinlock.lock();
    if (m_threadConnectionIds.find(threadConnectionId) != m_threadConnectionIds.end()) {
        // 可能连接在等其他mutex 影响不大 trylock会重试
        findMutex = true;
    }
    m_mtxSpinlock.unlock();

    return findMutex;
}

void CoSingle::add_block_mutex(pthread_mutex_t* mutex, CoConnection* connection)
{
    uint64_t threadConnectionId = GEN_U64(g_innerThreadId, connection->m_connId);

    m_mtxSpinlock.lock();
    if (m_threadConnectionIds.find(threadConnectionId) == m_threadConnectionIds.end()) {
        std::queue<std::pair<CoConnection*, uint32_t>> &blockMutexs = m_blockMutexs[(void*)mutex];
        blockMutexs.push(std::make_pair(connection, connection->m_version));
        m_threadConnectionIds.insert(threadConnectionId);
        CO_SERVER_LOG_DEBUG("(cid:%u) add_block_mutex mutex:%p, threadid:%d connid:%u insert set", connection->m_connId, mutex, g_innerThreadId, connection->m_connId);

    } else {
        CO_SERVER_LOG_DEBUG("(cid:%u) add_block_mutex mutex:%p, threadid:%d connid:%u already wait, not insert", connection->m_connId, mutex, g_innerThreadId, connection->m_connId);
    }
    m_mtxSpinlock.unlock();

    CO_SERVER_LOG_DEBUG("(cid:%u) add_block_mutex mutex:%p", connection->m_connId, mutex);

    // 防止多线程死锁 加一个超时时间检查并进行trylock  timer中处理了flagThirdFuncBlocking
    CoDispatcher::yield_timer(connection, connection->m_cycle->m_conf->m_confHook.m_mutexRetryTime);

    m_mtxSpinlock.lock();
    auto itr = m_threadConnectionIds.find(threadConnectionId);
    if (itr != m_threadConnectionIds.end()) {
        m_threadConnectionIds.erase(itr);
    }
    m_mtxSpinlock.unlock();
    return ;
}

// 可能多线程调用
void CoSingle::free_block_mutex(pthread_mutex_t* mutex)
{
    m_mtxSpinlock.lock();

    auto itr = m_blockMutexs.find((void*)mutex);
    if (itr != m_blockMutexs.end()) {
        std::queue<std::pair<CoConnection*, uint32_t>> &waitLockMutexs = itr->second;

        CoConnection* connection = NULL;
        std::pair<CoConnection*, uint32_t> mutexInfo;

        while (1) {
            mutexInfo = waitLockMutexs.front();
            waitLockMutexs.pop();

            connection = mutexInfo.first;
            int32_t innerThreadId = connection->m_cycle->m_innerThreadId;
            uint64_t threadConnectionId = GEN_U64(innerThreadId, connection->m_connId);
            if (m_threadConnectionIds.find(threadConnectionId) == m_threadConnectionIds.end()) {
                CO_SERVER_LOG_DEBUG("free_block_mutex mutex:%p, threadid:%d connid:%u not found in set, not resume", mutex, innerThreadId, connection->m_connId);
                connection = NULL;
            }
            // 不要删除m_threadConnectionIds里的数据  因为resume时需要检查是否在threadConnectionIds里

            if (waitLockMutexs.empty()) {
                m_blockMutexs.erase(itr);
                CO_SERVER_LOG_DEBUG("free_block_mutex mutex:%p, has no wait mutex, erase it", mutex);
                break;
            }

            if (connection) {
                // find need resume connection
                break;
            }
        }
        m_mtxSpinlock.unlock();

        if (connection) {
            // 可能是其他线程 需要加锁 这里使用异步恢复唤醒
            CO_SERVER_LOG_DEBUG("free_block_mutex, resume wait mutex:%p connection id:%u", mutex, connection->m_connId);
            connection->m_cycle->m_dispatcher->resume_single_async(mutexInfo);
        }

    } else {
        m_mtxSpinlock.unlock();
        CO_SERVER_LOG_DEBUG("free_block_mutex not found wait block mutex:%p", mutex);
    }

    return ;
}

}

