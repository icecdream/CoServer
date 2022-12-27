#include <sys/socket.h>
#include <sys/un.h>
#include "core/co_dispatcher.h"
#include "base/co_log.h"
#include "core/co_cycle.h"
#include "base/co_common.h"
#include "core/co_callback_event.h"
#include "core/co_callback_request.h"
#include "core/co_server_control.h"


namespace coserver
{

CoDispatcher::CoDispatcher()  
{
}

CoDispatcher::~CoDispatcher() 
{
    for (auto &itr : m_serverControls) {
        SAFE_DELETE(itr);
    }
    m_serverControls.clear();
}

int32_t CoDispatcher::init(CoCycle* cycle)
{
    int32_t ret = init_yieldresume_comm(cycle);
    if (ret != CO_OK) {
        CO_SERVER_LOG_ERROR("start inter resume socket init error, ret:%d", ret);
        return CO_ERROR;
    }

    // listen servers
    for (auto &itr : cycle->m_conf->m_confServers) {
        CoConfServer* confServer = itr;
        CoServerControl* serverControl = new CoServerControl(confServer);
        
        ret = serverControl->init(cycle);
        if (ret != CO_OK) {
            CO_SERVER_LOG_ERROR("start listen ip:%s port:%d failed, ret:%d", confServer->m_listenIP.c_str(), confServer->m_listenPort, ret);
            return CO_ERROR;
        }
        CO_SERVER_LOG_INFO("start listen ip:%s port:%d success", confServer->m_listenIP.c_str(), confServer->m_listenPort);
        m_serverControls.push_back(serverControl);
    }

    return CO_OK;
}

int32_t CoDispatcher::init_yieldresume_comm(CoCycle* cycle)
{
    // yield/resume socket attributes
    int32_t socketFds[2];
    if (socketpair(AF_UNIX, SOCK_STREAM, 0, socketFds) != 0) {
        CO_SERVER_LOG_ERROR("yield/resume UNIX socketpair failed");
        return CO_ERROR;
    }

    // read
    CoConnection* resumeReadConnection = cycle->m_connectionPool->get_connection(socketFds[0]);
    if (resumeReadConnection == NULL) {
        CO_SERVER_LOG_ERROR("yield/resume read comm get connection failed");
        return CO_ERROR;
    }

    // write
    m_writeResumeConnection = cycle->m_connectionPool->get_connection(socketFds[1]);
    if (m_writeResumeConnection == NULL) {
        CO_SERVER_LOG_ERROR("yield/resume write comm get connection failed");
        return CO_ERROR;
    }
    CO_SERVER_LOG_DEBUG("yield/resume communicate read fd:%d write fd:%d", resumeReadConnection->m_coTcp->get_socketfd(), m_writeResumeConnection->m_coTcp->get_socketfd());

    // 监听连接 读事件处理函数
    resumeReadConnection->m_handler = [=](CoConnection* connection) {
        // 将数据全部读出 主要目的是触发epoll 处理m_waitResumes数组数据
        char buffer[8];
        int32_t readSize = connection->m_coTcp->tcp_read(buffer, 8); UNUSED(readSize);
        CO_SERVER_LOG_DEBUG("yield/resume socket read:%d bytes data:%.*s", readSize, readSize, buffer);
    };

    // 监听连接 读事件异常处理函数
    resumeReadConnection->m_handlerException = [=](CoConnection* connection) -> int32_t {
        if (connection->m_flagPendingEof) {
            CO_SERVER_LOG_WARN("yield/resume socket exception, pendingeof:%d", connection->m_flagPendingEof);
        }
        return CO_OK;
    };

    // add to epoll
    return cycle->m_coEpoll->modify_connection(resumeReadConnection, EPOLL_EVENTS_ADD, CO_EVENT_READ);
}

int32_t CoDispatcher::start(CoCycle* cycle)
{
    m_run = true;

    for (;;) {
        if (!m_run) {
            cycle->m_connectionPool->close_all_connection();

            if (cycle->m_timer->empty()) {
                CO_SERVER_LOG_INFO("coserver dispatcher exit");
                break;
            }

            // 定时器需要继续处理
        }

        process_events_and_timers(cycle);
    }

    return CO_OK;
}

int32_t CoDispatcher::stop()
{
    m_run = false;
    return CO_OK;
}

int32_t CoDispatcher::process_events_and_timers(CoCycle* cycle)
{
    CoTimer* timer = cycle->m_timer;
    uint64_t timerTime = timer->find_timer();

    // epoll process
    uint64_t processEventsTime = GET_CURRENTTIME_MS();
    cycle->m_coEpoll->process_events(timerTime);
    processEventsTime = GET_CURRENTTIME_MS() - processEventsTime;

    bool needContinue = false;
    do {
        // 下面三个步骤 可能会出现乱序添加任务 一次性执行完毕后再等待epoll
        // todo 避免循环次数过多, 后续可以添加循环次数限制
        needContinue = false;

        static auto funcResumeProcess = [&](int32_t type, std::pair<CoConnection*, uint32_t> &coroutineData) {
            CoConnection* connection = coroutineData.first;
            uint32_t version = coroutineData.second;
            if (connection->m_version != version) {
                // 连接版本号 防止客户端的连接已经被销毁
                CO_SERVER_LOG_ERROR("(cid:%u) type:%d resume connection, oldversion:%u not equal curversion:%u", connection->m_connId, type, version, connection->m_version);
                return ;
            }

            needContinue = true;
            CoDispatcher::func_dispatcher(connection);
        };

        // wait process events
        uint64_t processDelayEventsTime = GET_CURRENTTIME_MS();
        while (!m_delayConnections.empty()) {
            std::pair<CoConnection*, uint32_t> waitConnection = m_delayConnections.front();
            m_delayConnections.pop();

            funcResumeProcess(0, waitConnection);
        }
        processDelayEventsTime = GET_CURRENTTIME_MS() - processDelayEventsTime;

        // wait process resume  内部socketpair进行通信 防止阻塞在epoll长时间无法处理
        uint64_t processResumeTime = GET_CURRENTTIME_MS();
        while(!m_waitResumes.empty()) {
            m_mtxResume.lock();
            std::pair<CoConnection*, uint32_t> coroutineData = m_waitResumes.front();
            m_waitResumes.pop();
            m_mtxResume.unlock();

            funcResumeProcess(1, coroutineData);
        }
        processResumeTime = GET_CURRENTTIME_MS() - processResumeTime;

        uint64_t processSingleResumeTime = GET_CURRENTTIME_MS();
        while(!m_waitSingles.empty()) {
            m_mtxResume.lock();
            std::pair<CoConnection*, uint32_t> coroutineData = m_waitSingles.front();
            m_waitSingles.pop();
            m_mtxResume.unlock();

            CoConnection* connection = coroutineData.first;
            CoSingle* single = CoSingle::get_instance();
            if (! (single->find_block_mutex(connection)) ) {
                CO_SERVER_LOG_DEBUG("(cid:%u) dispactch single wait connection, not find block mutex, not porcess", connection->m_connId);
                continue;
            }

            funcResumeProcess(2, coroutineData);
        }
        processSingleResumeTime = GET_CURRENTTIME_MS() - processSingleResumeTime;
        
        CO_SERVER_LOG_DEBUG("process events time:%lu delayevents time:%lu, resume time:%lu, single resume time:%lu, next timer:%lu, need continue:%d", processEventsTime, processDelayEventsTime, processResumeTime, processSingleResumeTime, timerTime, needContinue);

        // timer process
        if (timerTime == 0 || processEventsTime > 0 || processDelayEventsTime > 0 || processResumeTime > 0 || processSingleResumeTime > 0) {
            needContinue = true;
            timer->process_timer();

            processEventsTime = 0;
            timerTime = timer->find_timer();
        }

    } while(needContinue);

    return CO_OK;
}

void CoDispatcher::func_dispatcher(CoConnection* connection)
{
    CoThreadLocalInfo* threadInfo = GET_TLS();
    threadInfo->m_curConnection = connection;   // 设置当前线程处理的连接

    // 处理超时/断开连接等异常问题
    if (CO_EXCEPTION == connection->m_handlerException(connection)) {
        CO_SERVER_LOG_WARN("(cid:%u) event handler exception", connection->m_connId);
        return ;
    }

    CoCoroutine* coroutine = connection->m_coroutine;
    CoCoroutineMain* coroutineMain = connection->m_cycle->m_coCoroutineMain;

    if (connection->m_flagBlockConn) {
        // 阻塞连接没有协程数据  对第三方阻塞socket触发的事件 使用原始连接的协程栈数据
        coroutine = connection->m_blockOriginConn->m_coroutine;
        CO_SERVER_LOG_DEBUG("(cid:%u) func dispatcher block connection, use origin connection id:%u", connection->m_connId, connection->m_blockOriginConn->m_connId);
    }

    // 判断连接协程状态
    CoroutineStatus coStatus = coroutine->m_coroutineStatus;
    switch (coStatus) {
        case COROUTINE_READY: {
            // 第一次切入协程
            CO_SERVER_LOG_DEBUG("(cid:%u) coroutine ready, first use coroutine", connection->m_connId);
            coroutine->m_coroutineStatus = CoroutineStatus::COROUTINE_SUSPEND;
            coroutineMain->init_coroutine( [connection]{ func_proc_coroutine(connection); }, coroutine);
            coroutineMain->swap_in(coroutine);
            break;
        }

        case COROUTINE_SUSPEND: {
            // 非第一次切入协程
            CO_SERVER_LOG_DEBUG("(cid:%u) coroutine suspend, now swap in", connection->m_connId);
            coroutineMain->swap_in(coroutine);
            break;
        }

        case COROUTINE_DOWN: {
            CO_SERVER_LOG_FATAL("(cid:%u) coroutine down, shouldn't occur", connection->m_connId);
            break;
        }

        default: {
            CO_SERVER_LOG_FATAL("(cid:%u) coroutine status:%d error", connection->m_connId, coStatus);
            break;
        }
    }

    threadInfo->m_curConnection = NULL;
}

void CoDispatcher::func_proc_coroutine(CoConnection* connection)
{
    // event事件执行完毕 主动切出协程 继续其他协程操作
    CoCoroutine* coroutine = connection->m_coroutine;

    // 执行event handler（内部可随意切出 切入协程）
    connection->m_handler(connection);

    CO_SERVER_LOG_DEBUG("(cid:%u) event handler complete, swap out, coroutine set status ready", connection->m_connId);
    coroutine->m_coroutineStatus = CoroutineStatus::COROUTINE_READY;
    connection->m_cycle->m_coCoroutineMain->swap_out(coroutine);
    return ;
}

// 协程相关
int32_t CoDispatcher::yield(CoConnection* connection) 
{
    // 切出协程
    CO_SERVER_LOG_DEBUG("(cid:%u) dispactch block yield, swap out", connection->m_connId);
    connection->m_cycle->m_coCoroutineMain->swap_out(connection->m_coroutine);
    // wait epoll/timer swap in
    CO_SERVER_LOG_DEBUG("(cid:%u) dispactch block yield, swap in", connection->m_connId);

    if (connection->m_flagDying) {
        // 连接即将销毁 直接返回 不在切出等待切入
        CO_SERVER_LOG_WARN("(cid:%u) dispactch block yield, after swap out connection dying", connection->m_connId);
        return CO_ERROR;
    }
    return CO_OK;
}

int32_t CoDispatcher::yield_thirdsocket(int32_t socketFd, uint32_t eventOP, int32_t timeoutMs, CoConnection* connection, int32_t socketPhase)
{
    /*
        在业务处理函数中 调用了第三方阻塞socket  首先找到当前线程处理的事件event
        客户端事件在处理过程中有其他socket需要处理  可能是第三方socket（第三方socket 不进行主动关闭 只使用其socket fd）

        第三方socket 一定要返回hook函数处 不能因为异常导致修改了第三方socket的属性
    */
    CoCoroutine* coroutine = connection->m_coroutine;
    CoCycle* cycle = connection->m_cycle;
    CoCoroutineMain* coroutineMain = cycle->m_coCoroutineMain;
    CoConnection* blockConnection = connection->m_blockConn;

    // 1.在业务逻辑处理中 原始socket的读写事件不应该还在epoll事件池中
    if (connection->m_epollCurEvents != 0) {
        if (connection->m_epollCurEvents & ~EPOLLRDHUP) {
            CO_SERVER_LOG_FATAL("(cid:%u) yield thirdsocket fd:%d, origin socket:%d read:%d write:%d preevents:%u, shouldn't in epoll", connection->m_connId, socketFd, 
                    connection->m_coTcp->get_socketfd(), connection->m_readEvent->m_flagActive, connection->m_writeEvent->m_flagActive, connection->m_epollCurEvents);
        }
    }

    co_defer(
        // 设置不使用blockconnection的socket
        connection->m_flagUseBlockConn = 0;
        connection->reset_block();
    )

    // 2.设置使用block connection
    connection->m_flagUseBlockConn = 1;
    if (CO_OK != blockConnection->m_coTcp->init_client_socketfd(socketFd, socketPhase == SOCKET_PHASE_CONNECT ? false : true)) {
        CO_SERVER_LOG_ERROR("(cid:%u) dispactch Third SocketIO block yield, init client socketfd:%d failed", connection->m_connId, socketFd);
        return CO_ERROR;
    }

    // 3.添加阻塞socket事件到epoll
    CoEvent* blockEvent = eventOP == (CO_EVENT_READ) ? blockConnection->m_readEvent : blockConnection->m_writeEvent;
    if (cycle->m_coEpoll->modify_connection(blockConnection, EPOLL_EVENTS_ADD, eventOP) != CO_OK) {
        CO_SERVER_LOG_FATAL("(cid:%u) dispactch Third SocketIO block yield, epoll add block event failed", connection->m_connId);
    }

    // 4.添加阻塞事件定时器
    cycle->m_timer->add_timer(blockEvent, timeoutMs);

    // 5.切出协程（使用原始连接的协程 阻塞连接只有socket和事件信息）
    CO_SERVER_LOG_DEBUG("(cid:%u) dispactch Third SocketIO block yield, swap out", connection->m_connId);
    coroutineMain->swap_out(coroutine);

    // wait epoll/timer swap in
    CO_SERVER_LOG_DEBUG("(cid:%u) dispactch Third SocketIO block yield, swap in", connection->m_connId);
    // 第三方阻塞socket切入后 当前线程连接切回原始连接
    CoThreadLocalInfo* threadInfo = GET_TLS();
    threadInfo->m_curConnection = connection;

    // 1.从epoll中删除阻塞socket事件
    if (cycle->m_coEpoll->modify_connection(blockConnection, EPOLL_EVENTS_DEL, eventOP) != CO_OK) {
        CO_SERVER_LOG_FATAL("(cid:%u) dispactch Third SocketIO block yield, epoll del block event failed", connection->m_connId);
    }

    // 2.从定时器中删除阻塞事件
    cycle->m_timer->del_timer(blockEvent);

    // 3.阻塞连接即将销毁
    if (blockConnection->m_flagDying) {
        // 重新切入协程后  异常情况（触发连接超时） 尽快结束请求
        CO_SERVER_LOG_WARN("(cid:%u bcid:%u) dispactch Third SocketIO, block dying", connection->m_connId, blockConnection->m_connId);
        return CO_EXCEPTION;
    }

    // 第三方阻塞socket超时
    if (blockConnection->m_flagTimedOut) {
        CO_SERVER_LOG_WARN("(cid:%u bcid:%u) dispactch Third SocketIO, block socket:%d timeout", connection->m_connId, blockConnection->m_connId, socketFd);
        return CO_TIMEOUT;
    }

    // 第三方阻塞socket出错
    if (blockConnection->m_flagPendingEof) {
        CO_SERVER_LOG_ERROR("(cid:%u bcid:%u) dispactch Third SocketIO, block eof:%d", connection->m_connId, blockConnection->m_connId, blockConnection->m_flagPendingEof);
        return CO_ERROR;
    }
    return CO_OK;
}

int32_t CoDispatcher::yield_timer(CoConnection* connection, uint32_t sleepMs) 
{
    CoCycle* cycle = connection->m_cycle;
    CoEvent* event = connection->m_sleepEvent;

    // 在业务逻辑处理中 原始socket的读写事件不应该还在epoll事件池中
    if (connection->m_epollCurEvents != 0) {
        if (connection->m_epollCurEvents & ~EPOLLRDHUP) {
            CO_SERVER_LOG_FATAL("(cid:%u) yield timer fd:%d, read:%d write:%d preevents:%u, shouldn't in epoll", connection->m_connId, connection->m_coTcp->get_socketfd(), connection->m_readEvent->m_flagActive, connection->m_writeEvent->m_flagActive, connection->m_epollCurEvents);
        }
    }

    connection->m_flagThirdFuncBlocking = 1;
    
    // 添加事件定时器
    cycle->m_timer->add_timer(event, sleepMs);

    // 切出协程
    CO_SERVER_LOG_DEBUG("(cid:%u) dispactch timer yield, swap out", connection->m_connId);
    cycle->m_coCoroutineMain->swap_out(connection->m_coroutine);
    // wait epoll/timer swap in
    CO_SERVER_LOG_DEBUG("(cid:%u) dispactch timer yield, swap in", connection->m_connId);

    connection->m_flagThirdFuncBlocking = 0;

    // 从定时器中删除阻塞事件
    if (event->m_flagTimerSet) {
        cycle->m_timer->del_timer(event);
    }

    if (connection->m_flagDying) {
        // 连接即将销毁 直接返回 不在切出等待切入
        CO_SERVER_LOG_WARN("(cid:%u) dispactch timer yield, after swap out connection dying", connection->m_connId);
        return CO_ERROR;
    }
    return CO_OK;
}

int32_t CoDispatcher::yield(std::pair<void*, uint32_t> &coroutineData)
{
    CoConnection* connection = (CoConnection*)coroutineData.first;
    CoCoroutineMain* coroutineMain = connection->m_cycle->m_coCoroutineMain;

    CO_SERVER_LOG_DEBUG("(cid:%u) dispactch event yield, swap out START", connection->m_connId);
    coroutineMain->swap_out(connection->m_coroutine);
    CO_SERVER_LOG_DEBUG("(cid:%u) dispactch event yield, swap out END", connection->m_connId);

    if (connection->m_flagDying) {
        // 连接即将销毁 可能是连接出错切入  返回错误即可
        CO_SERVER_LOG_ERROR("(cid:%u) dispactch event yield, after swapout connection dying", connection->m_connId);
        return CO_ERROR;
    }
    return CO_OK;
}

int32_t CoDispatcher::resume_async(std::pair<void*, uint32_t> &transforData)
{
    std::pair<CoConnection*, uint32_t> coroutineData = std::make_pair((CoConnection*)(transforData.first), transforData.second);

    // 确定连接后 即可确定对应处理线程
    CoConnection* connection = coroutineData.first;
    CoDispatcher* dispatcher = connection->m_cycle->m_dispatcher;
    bool writeSocket = false;
    
    dispatcher->m_mtxResume.lock();
    if (dispatcher->m_waitResumes.empty()) {
        // write优化 如果queue不为空 表示待处理或者正在处理 不需要在发送socket数据
        writeSocket = true;
    }
    dispatcher->m_waitResumes.push(coroutineData);
    dispatcher->m_mtxResume.unlock();

    if (writeSocket) {
        dispatcher->m_writeResumeConnection->m_coTcp->tcp_write(g_oneByteA.c_str(), 1);
    }
    CO_SERVER_LOG_DEBUG("(cid:%u) prepare resume, push codata and tcpwrite flag(%d) a byte", connection->m_connId, writeSocket);

    return CO_OK;
}

int32_t CoDispatcher::resume_single_async(std::pair<CoConnection*, uint32_t> &coroutineData)
{
    // 确定连接后 即可确定对应处理线程
    CoConnection* connection = coroutineData.first;
    CoDispatcher* dispatcher = connection->m_cycle->m_dispatcher;
    bool writeSocket = false;
    
    dispatcher->m_mtxResume.lock();
    if (dispatcher->m_waitSingles.empty()) {
        // write优化 如果queue不为空 表示待处理或者正在处理 不需要在发送socket数据
        writeSocket = true;
    }
    dispatcher->m_waitSingles.push(coroutineData);
    dispatcher->m_mtxResume.unlock();

    if (writeSocket) {
        dispatcher->m_writeResumeConnection->m_coTcp->tcp_write(g_oneByteA.c_str(), 1);
    }
    CO_SERVER_LOG_DEBUG("(cid:%u) prepare single resume, push codata and tcpwrite flag(%d) a byte", connection->m_connId, writeSocket);

    return CO_OK;
}

}

