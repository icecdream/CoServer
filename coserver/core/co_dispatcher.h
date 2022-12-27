#ifndef _CO_DISPATCHER_H_
#define _CO_DISPATCHER_H_

#include <vector>
#include <queue>
#include "base/co_spinlock.h"


namespace coserver
{

struct CoCycle;
struct CoConnection;
class CoServerControl;


// 总体调度
class CoDispatcher 
{
public:
    CoDispatcher();
    ~CoDispatcher();

    int32_t init(CoCycle* cycle);

    int32_t start(CoCycle* cycle);
    int32_t stop();

    static void func_dispatcher(CoConnection* connection);
    static void func_proc_coroutine(CoConnection* connection);

public:
    /*
        函数功能: 对阻塞的调用, 切出当前协程

        参数: 
            connection: socket对应的连接
        
        返回值: CO_OK成功 其他错误
    */
    static int32_t yield(CoConnection* connection);

    /*
        函数功能: 对第三方阻塞Socket IO调用, 切出当前协程

        参数: 
            socketFd: 第三方阻塞socket fd
            eventOP: epoll事件
            timeoutMs: 超时时间
            connection: 第三方阻塞socket被调用时的当前连接
            socketPhase: 第三方socket所处阶段(connect/read/write)
        
        返回值: CO_OK成功 其他错误
    */
    static int32_t yield_thirdsocket(int32_t socketFd, uint32_t eventOP, int32_t timeoutMs, CoConnection* connection, int32_t socketPhase = 0);

    /*
        函数功能: 内部定时器 配合阻塞sleep/usleep

        参数: 
            connection: socket对应的连接
            sleepMs: 定时器时间 ms
        
        返回值: CO_OK成功 其他错误
    */
    static int32_t yield_timer(CoConnection* connection, uint32_t sleepMs);

    // 切出当前事件的协程（客户端可以配合独立线程使用）
    static int32_t yield(std::pair<void*, uint32_t> &coroutineData);

    // 异步切入当前事件的协程（客户端可以配合独立线程使用）
    static int32_t resume_async(std::pair<void*, uint32_t> &coroutineData);

    // 异步切入当前事件的协程 全局single使用
    static int32_t resume_single_async(std::pair<CoConnection*, uint32_t> &coroutineData);

private:
    // yield后需要resume的连接 通信socket
    int32_t init_yieldresume_comm(CoCycle* cycle);
    int32_t process_events_and_timers(CoCycle* cycle);


private:
    bool    m_run = false;


public:
    std::vector<CoServerControl*> m_serverControls;

    // 在一个事件的协程中触发其他时间  因为其他事件也需要协程支持  所以其他事件暂存 等待处理
    std::queue<std::pair<CoConnection*, uint32_t>>  m_delayConnections;

    // resume
    CoSpinlock                m_mtxResume;
    CoConnection*             m_writeResumeConnection = NULL;
    // 用于内部恢复协议的通信socket
    std::queue<std::pair<CoConnection*, uint32_t>>  m_waitResumes;     // 等待resume的连接协程数据
    // 用于全局single的处理
    std::queue<std::pair<CoConnection*, uint32_t>>  m_waitSingles;     // 等待resume的single连接
};

}

#endif //_CO_DISPATCHER_H_

