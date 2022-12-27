#include <dlfcn.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <pthread.h>
#include <string>
#include "base/co_hook.h"
#include "base/co_common.h"
#include "base/co_log.h"
#include "core/co_event.h"
#include "core/co_cycle.h"

namespace coserver
{

void get_socket_ipport(int32_t socketFd, std::string &localIP, int32_t &localPort, std::string &remoteIP, int32_t &remotePort);

// socket移除非阻塞属性
void socket_remove_nonblock(int32_t socketFd, int32_t flags)
{
    int32_t tmpErrno = errno;
    fcntl(socketFd, F_SETFL, flags & ~O_NONBLOCK);
    errno = tmpErrno;
    CO_SERVER_LOG_DEBUG("hook socketfd:%d remove nonblock", socketFd);
    return ;
}

// hook socket读写函数
template <typename OriginFn, typename ... Args>
static ssize_t read_write_mode(int32_t socketFd, OriginFn originFn, const char* hookFnName, uint32_t eventOP, int32_t timeoutSO, Args && ... args)
{
    // 判断线程私有变量是否有cycle 确定是否需要hook
    CoThreadLocalInfo* threadInfo = GET_TLS();
    if (threadInfo->m_coCycle == NULL) {
        return originFn(socketFd, std::forward<Args>(args)...);
    }

    CoConnection* connection = threadInfo->m_curConnection;
    if (connection->m_flagDying) {
        // 连接正准备销毁 不在执行相关的网络操作
        CO_SERVER_LOG_WARN("(cid:%u) hook readwrite connection dying, socketfd:%d", connection->m_connId, socketFd);
        return CO_ERROR;
    }

    int32_t flags = 0;
    bool innerSocket = threadInfo->m_coCycle->m_connectionPool->is_inner_socketfd(socketFd);
    if (!innerSocket) {
        // 第三方socket
        struct stat fd_stat;
        if (-1 == fstat(socketFd, &fd_stat)) {
            return originFn(socketFd, std::forward<Args>(args)...);
        }
        
        // 非socket 不进行hook
        if (!S_ISSOCK(fd_stat.st_mode)) {
            return originFn(socketFd, std::forward<Args>(args)...);
        }

        flags = fcntl(socketFd, F_GETFL, 0);
        if (-1 == flags || (flags & O_NONBLOCK)) {
            // 已经是非阻塞调用 直接执行
            return originFn(socketFd, std::forward<Args>(args)...);
        }

        // 接下来说明：是网络socket调用 并且 没有设置非阻塞, 说明极大概率会阻塞

        // 设置socket非阻塞
        if (-1 == fcntl(socketFd, F_SETFL, flags | O_NONBLOCK)) {
            return originFn(socketFd, std::forward<Args>(args)...);
        }
    }
    CO_SERVER_LOG_DEBUG("(cid:%u) read write mode hookname:%s, socketfd(inner:%d):%d", connection->m_connId, hookFnName, innerSocket, socketFd);

    // 第三方socket 恢复属性
    co_defer(
        if (!innerSocket) {
            socket_remove_nonblock(socketFd, flags);
        }
    )

    ssize_t ret = originFn(socketFd, std::forward<Args>(args)...);
    if (ret == -1 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
        // socket无法立刻得到响应 加入epoll异步等待响应
        if (innerSocket) {
            CO_SERVER_LOG_DEBUG("(cid:%u) hook readwrite, inner socket IO yield, socketfd:%d", connection->m_connId, socketFd)
            ret = CoDispatcher::yield(connection);

        } else {
            int32_t timeoutMs = -1;
            struct timeval timeout;
            socklen_t timeoutLen = sizeof(timeout);
            if (0 == getsockopt(socketFd, SOL_SOCKET, timeoutSO, &timeout, &timeoutLen)) {
                if (timeout.tv_sec > 0 || timeout.tv_usec > 0) {
                    timeoutMs = timeout.tv_sec * 1000 + timeout.tv_usec / 1000;
                }
            }

            CO_SERVER_LOG_DEBUG("(cid:%u) hook readwrite, third socket IO yield, socketfd:%d timeout:%dms", connection->m_connId, socketFd, timeoutMs);
            ret = CoDispatcher::yield_thirdsocket(socketFd, eventOP, timeoutMs, connection);
        }

        if (ret != CO_OK) {
            std::string localIP, remoteIP;
            int32_t localPort = 0, remotePort = 0;
            get_socket_ipport(socketFd, localIP, localPort, remoteIP, remotePort);

            CO_SERVER_LOG_WARN("(cid:%u) hook readwrite yield/resume failed, ret:%ld socketfd:%d(local ip:%s port:%d, remote ip:%s port:%d)", connection->m_connId, ret, socketFd, localIP.c_str(), localPort, remoteIP.c_str(), remotePort);

            if (ret == CO_TIMEOUT) {
                errno = EAGAIN;
            }
            return CO_ERROR;
        }

        // epoll异步通知 继续调用原始函数
        CO_SERVER_LOG_DEBUG("(cid:%u) hook readwrite, epoll async resume, continue origin func, socketfd:%d", connection->m_connId, socketFd);
        ret = originFn(socketFd, std::forward<Args>(args)...);
    }

    return ret;
}

extern "C" {

typedef int32_t(*connect_t)(int32_t, const struct sockaddr* , socklen_t);
static connect_t fnConnect = NULL;

typedef ssize_t(*read_t)(int32_t, void* , size_t);
static read_t fnRead = NULL;

typedef ssize_t(*readv_t)(int32_t, const struct iovec* , int32_t);
static readv_t fnReadv = NULL;

typedef ssize_t(*recv_t)(int32_t socketFd, void* buffer, size_t len, int32_t flags);
static recv_t fnRecv = NULL;

typedef ssize_t(*recvfrom_t)(int32_t socketFd, void* buffer, size_t len, int32_t flags, struct sockaddr* srcAddr, socklen_t* addrlen);
static recvfrom_t fnRecvfrom = NULL;

typedef ssize_t(*recvmsg_t)(int32_t socketFd, struct msghdr* msg, int32_t flags);
static recvmsg_t fnRecvmsg = NULL;

typedef ssize_t(*write_t)(int32_t, const void* , size_t);
static write_t fnWrite = NULL;

typedef ssize_t(*writev_t)(int32_t, const struct iovec* , int32_t);
static writev_t fnWritev = NULL;

typedef ssize_t(*send_t)(int32_t socketFd, const void* buffer, size_t len, int32_t flags);
static send_t fnSend = NULL;

typedef ssize_t(*sendto_t)(int32_t socketFd, const void* buffer, size_t len, int32_t flags, const struct sockaddr* destAddr, socklen_t addrlen);
static sendto_t fnSendto = NULL;

typedef ssize_t(*sendmsg_t)(int32_t socketFd, const struct msghdr* msg, int32_t flags);
static sendmsg_t fnSendmsg = NULL;

typedef int32_t(*accept_t)(int32_t socketFd, struct sockaddr* addr, socklen_t* addrlen);
static accept_t fnAccept = NULL;

typedef uint32_t(*sleep_t)(uint32_t uSeconds);
static sleep_t fnSleep = NULL;

typedef int32_t(*usleep_t)(useconds_t usec);
static usleep_t fnUsleep = NULL;

#if (CO_HOOK_SELECT)
typedef int32_t(*select_t)(int32_t nfds, fd_set *readfds, fd_set *writefds, fd_set *exceptfds, struct timeval *timeout);
static select_t fnSelect = NULL;
#endif

#if (CO_HOOK_MUTEX)
typedef int32_t(*pthread_mutex_lock_t)(pthread_mutex_t* mutex);
static pthread_mutex_lock_t fnPthreadMutexLock = NULL;

typedef int32_t(*pthread_mutex_unlock_t)(pthread_mutex_t* mutex);
static pthread_mutex_unlock_t fnPthreadMutexUnlock = NULL;
#endif


int32_t connect(int32_t socketFd, const struct sockaddr* addr, socklen_t addrlen)
{
    if (!fnConnect) init_coroutine_hook();
    
    // 判断线程私有变量是否有cycle 确定是否需要hook 
    CoThreadLocalInfo* threadInfo = GET_TLS();
    if (threadInfo->m_coCycle == NULL) {
        return fnConnect(socketFd, addr, addrlen);
    }

    CoConnection* connection = threadInfo->m_curConnection;
    if (connection->m_flagDying) {
        // 连接正准备销毁 不在执行相关的网络操作
        CO_SERVER_LOG_WARN("(cid:%u) hook connect connection dying, socketfd:%d", connection->m_connId, socketFd);
        return CO_ERROR;
    }

    int32_t flags = 0;
    bool innerSocket = threadInfo->m_coCycle->m_connectionPool->is_inner_socketfd(socketFd);
    if (!innerSocket) {
        // 第三方socket
        flags = fcntl(socketFd, F_GETFL, 0);
        if (-1 == flags || (flags & O_NONBLOCK)) {
            // 已经是非阻塞调用 直接执行
            return fnConnect(socketFd, addr, addrlen);
        }

        // 接下来说明：socket连接 并且 没有设置非阻塞，说明极大概率会阻塞

        // 设置socket非阻塞
        if (-1 == fcntl(socketFd, F_SETFL, flags | O_NONBLOCK)) {
            return fnConnect(socketFd, addr, addrlen);
        }
    }
    CO_SERVER_LOG_DEBUG("(cid:%u) connect hook, socketfd(inner:%d):%d", connection->m_connId, innerSocket, socketFd);

    // 第三方socket 恢复属性
    co_defer(
        if (!innerSocket) {
            socket_remove_nonblock(socketFd, flags);
        }
    )

    int32_t ret = fnConnect(socketFd, addr, addrlen);
    if (ret == 0) {
        CO_SERVER_LOG_DEBUG("(cid:%u) hook connect syscall immediately completed, socketfd:%d", connection->m_connId, socketFd);
        return 0;

    } else {
        // ret == -1
        if (errno == EINPROGRESS) {
            if (innerSocket) {
                // 内部socket
                CO_SERVER_LOG_DEBUG("(cid:%u) hook connect, inner socket IO yield, socketfd:%d", connection->m_connId, socketFd);
                ret = CoDispatcher::yield(connection);

            } else {
                // 第三方socket的连接超时时间优先使用SO_SNDTIMEO发送超时时间
                int32_t timeoutMs = -1;
                struct timeval timeout;
                socklen_t timeoutLen = sizeof(timeout);
                if (0 == getsockopt(socketFd, SOL_SOCKET, SO_SNDTIMEO, &timeout, &timeoutLen)) {
                    if (timeout.tv_sec > 0 || timeout.tv_usec > 0) {
                        timeoutMs = timeout.tv_sec * 1000 + timeout.tv_usec / 1000;
                    }
                }

                CO_SERVER_LOG_DEBUG("(cid:%u) hook connect, third socket IO yield, socketfd:%d timeout:%dms", connection->m_connId, socketFd, timeoutMs);
                ret = CoDispatcher::yield_thirdsocket(socketFd, CO_EVENT_WRITE, timeoutMs, connection, SOCKET_PHASE_CONNECT);
            }

            if (ret != CO_OK) {
                CO_SERVER_LOG_WARN("(cid:%u) hook connect yield/resume failed, ret:%d socketfd:%d", connection->m_connId, ret, socketFd);
                if (ret == CO_TIMEOUT) {
                    errno = EAGAIN;
                }
                return CO_ERROR;
            }

        } else {
            // error
            CO_SERVER_LOG_WARN("(cid:%u) hook connect origin func failed, ret:%d socketfd:%d", connection->m_connId, ret, socketFd);
            return ret;
        }
    }

    int32_t socketOptError;
    socklen_t len = sizeof(int32_t);
    if (0 == getsockopt(socketFd, SOL_SOCKET, SO_ERROR, &socketOptError, &len)) {
        if (0 == socketOptError) {
            return CO_OK;
        }
        errno = socketOptError;
        CO_SERVER_LOG_WARN("(cid:%u) hook connect epoll async resume, connect exec failed, errno:%d socketfd:%d", connection->m_connId, socketOptError, socketFd);

    } else {
        CO_SERVER_LOG_ERROR("(cid:%u) hook connect epoll async resume, func getsockopt failed, errno:%d socketfd:%d", connection->m_connId, socketOptError, socketFd);
    }

    return CO_ERROR;
}

int32_t accept(int32_t socketFd, struct sockaddr* addr, socklen_t* addrlen)
{
    if (!fnAccept) init_coroutine_hook();
    return read_write_mode(socketFd, fnAccept, "accept", CO_EVENT_READ, SO_RCVTIMEO, addr, addrlen);
}

ssize_t read(int32_t socketFd, void* buffer, size_t count)
{
    if (!fnRead) init_coroutine_hook();
    return read_write_mode(socketFd, fnRead, "read", CO_EVENT_READ, SO_RCVTIMEO, buffer, count);
}

ssize_t readv(int32_t socketFd, const struct iovec* iovec, int32_t iovcnt)
{
    if (!fnReadv) init_coroutine_hook();
    return read_write_mode(socketFd, fnReadv, "readv", CO_EVENT_READ, SO_RCVTIMEO, iovec, iovcnt);
}

ssize_t recv(int32_t socketFd, void* buffer, size_t len, int32_t flags)
{
    if (!fnRecv) init_coroutine_hook();
    return read_write_mode(socketFd, fnRecv, "recv", CO_EVENT_READ, SO_RCVTIMEO, buffer, len, flags);
}

ssize_t recvfrom(int32_t socketFd, void* buffer, size_t len, int32_t flags, struct sockaddr* srcAddr, socklen_t* addrlen)
{
    if (!fnRecvfrom) init_coroutine_hook();
    return read_write_mode(socketFd, fnRecvfrom, "recvfrom", CO_EVENT_READ, SO_RCVTIMEO, buffer, len, flags, srcAddr, addrlen);
}

ssize_t recvmsg(int32_t socketFd, struct msghdr* msg, int32_t flags)
{
    if (!fnRecvmsg) init_coroutine_hook();
    return read_write_mode(socketFd, fnRecvmsg, "recvmsg", CO_EVENT_READ, SO_RCVTIMEO, msg, flags);
}

ssize_t write(int32_t socketFd, const void* buffer, size_t count)
{
    if (!fnWrite) init_coroutine_hook();
    return read_write_mode(socketFd, fnWrite, "write", CO_EVENT_WRITE, SO_SNDTIMEO, buffer, count);
}

ssize_t writev(int32_t socketFd, const struct iovec* iovec, int32_t iovcnt)
{
    if (!fnWritev) init_coroutine_hook();
    return read_write_mode(socketFd, fnWritev, "writev", CO_EVENT_WRITE, SO_SNDTIMEO, iovec, iovcnt);
}

ssize_t send(int32_t socketFd, const void* buffer, size_t len, int32_t flags)
{
    if (!fnSend) init_coroutine_hook();
    return read_write_mode(socketFd, fnSend, "send", CO_EVENT_WRITE, SO_SNDTIMEO, buffer, len, flags);
}

ssize_t sendto(int32_t socketFd, const void* buffer, size_t len, int32_t flags, const struct sockaddr* destAddr, socklen_t addrlen)
{
    if (!fnSendto) init_coroutine_hook();
    return read_write_mode(socketFd, fnSendto, "sendto", CO_EVENT_WRITE, SO_SNDTIMEO, buffer, len, flags, destAddr, addrlen);
}

ssize_t sendmsg(int32_t socketFd, const struct msghdr* msg, int32_t flags)
{
    if (!fnSendmsg) init_coroutine_hook();
    return read_write_mode(socketFd, fnSendmsg, "sendmsg", CO_EVENT_WRITE, SO_SNDTIMEO, msg, flags);
}

/*
    关于sleep/mutex
    逻辑是优先执行完当前流程  超时/出错是次优先级
*/
uint32_t sleep(uint32_t seconds)
{
    if (!fnSleep) init_coroutine_hook();

    // 判断线程私有变量是否有cycle 确定是否需要hook
    CoThreadLocalInfo* threadInfo = GET_TLS();
    if (threadInfo->m_coCycle == NULL) {
        return fnSleep(seconds);
    }
    CoConnection* connection = threadInfo->m_curConnection;
    CO_SERVER_LOG_DEBUG("(cid:%u) hook sleep, sec:%u", connection->m_connId, seconds);

    time_t startTime = time(0);
    int32_t ret = CoDispatcher::yield_timer(connection, seconds * 1000);
    if (CO_OK != ret) {
        // sleep没有成功时 返回剩余时间
        time_t endTime = time(0);
        uint32_t remainTime = seconds - (endTime - startTime);
        CO_SERVER_LOG_ERROR("(cid:%u) hook sleep failed, sec:%u ret:%d, remain time:%u", connection->m_connId, seconds, ret, remainTime);
        return remainTime;
    }
    return CO_OK;
}

int32_t usleep(useconds_t usec)
{
    if (!fnUsleep) init_coroutine_hook();

    // 判断线程私有变量是否有cycle 确定是否需要hook
    CoThreadLocalInfo* threadInfo = GET_TLS();
    if (threadInfo->m_coCycle == NULL) {
        return fnUsleep(usec);
    }
    CoConnection* connection = threadInfo->m_curConnection;
    CO_SERVER_LOG_DEBUG("(cid:%u) hook usleep, usec:%u", connection->m_connId, usec);

    // 内部定时器在ms级别 大于1ms时间在进行hook
    if (usec > 1000) {
        uint32_t sleepMs = usec / 1000;
        int32_t ret = CoDispatcher::yield_timer(connection, sleepMs);
        if (CO_OK != ret) {
            CO_SERVER_LOG_ERROR("(cid:%u) hook usleep failed, usec:%u ms:%u ret:%d", connection->m_connId, usec, sleepMs, ret);
            return CO_ERROR;
        }
        
    } else {
        return fnUsleep(usec);
    }

    return CO_OK;
}

#if (CO_HOOK_SELECT)
// 针对第三方select, 如果只有一个socket在里面 也进行hook操作, 可能是connect超时用法 进行非阻塞处理
int32_t select(int32_t nfds, fd_set *readfds, fd_set *writefds, fd_set *exceptfds, struct timeval *timeout)
{
    if (!fnSelect) init_coroutine_hook();

    // 判断线程私有变量是否有cycle 确定是否需要hook
    CoThreadLocalInfo* threadInfo = GET_TLS();
    if (threadInfo->m_coCycle == NULL) {
        return fnSelect(nfds, readfds, writefds, exceptfds, timeout);
    }

    CoConnection* connection = threadInfo->m_curConnection;
    if (connection->m_flagDying) {
        // 连接正准备销毁 不在执行相关的网络操作
        CO_SERVER_LOG_WARN("(cid:%u) hook select connection dying", connection->m_connId);
        return CO_ERROR;
    }

    int32_t timeoutMs = -1;
    if (timeout) {
        timeoutMs = timeout->tv_sec * 1000 + timeout->tv_usec / 1000;
    }

    if (timeoutMs == 0) {
        return fnSelect(nfds, readfds, writefds, exceptfds, timeout);
    }

    if (!nfds && !readfds && !writefds && !exceptfds && timeout) {
        // 没有socket, 只需要设置超时时间
        int32_t ret = CoDispatcher::yield_timer(connection, timeoutMs);
        if (CO_OK != ret) {
            CO_SERVER_LOG_ERROR("(cid:%u) hook select sleep failed, ms:%u ret:%d", connection->m_connId, timeoutMs, ret);
            return CO_ERROR;
        }
        return CO_OK;
    }

    int32_t selectSocket = -1;
    int32_t socketEvents = -1;
    std::pair<fd_set*, uint32_t> fdSets[3] = {
        {readfds, CO_EVENT_READ},
        {writefds, CO_EVENT_WRITE},
        {exceptfds, CO_EVENT_HUP}
    };

    nfds = std::min<int32_t>(nfds, FD_SETSIZE);
    for (int32_t i=0; i<nfds; ++i) {
        // 3 => read write except
        for (int32_t si=0; si<3; ++si) {
            if (!fdSets[si].first)
                continue;

            if (!FD_ISSET(i, fdSets[si].first))
                continue;

            // i socket已经设置
            if (selectSocket == -1 && socketEvents == -1) {
                selectSocket = i;
                socketEvents = fdSets[si].second;

            } else {
                // select设置了多个socket, 实现较复杂, 不hook 继续调用放处理
                return fnSelect(nfds, readfds, writefds, exceptfds, timeout);
            }
        }
    }

    // 因为不对socket做真正读写处理, 所以这里不检查和设置socket的 非阻塞

    CO_SERVER_LOG_DEBUG("(cid:%u) hook select, third socket IO yield, socketfd:%d event:%d timeout:%dms", connection->m_connId, selectSocket, socketEvents, timeoutMs);
    int32_t ret = CoDispatcher::yield_thirdsocket(selectSocket, socketEvents, timeoutMs, connection, SOCKET_PHASE_CONNECT);
    if (ret != CO_OK) {
        CO_SERVER_LOG_WARN("(cid:%u) hook select yield/resume failed, ret:%d socketfd:%d)", connection->m_connId, ret, selectSocket);
        if (ret == CO_TIMEOUT) {
            if (readfds) FD_ZERO(readfds);
            if (writefds) FD_ZERO(writefds);
            if (exceptfds) FD_ZERO(exceptfds);
            return 0;
        }
        return CO_ERROR;
    }

    // 成功, 继续非阻塞调用select, 进行相关填充
    CO_SERVER_LOG_DEBUG("(cid:%u) hook select, epoll async resume, continue origin select, socketfd:%d, ret:%d", connection->m_connId, selectSocket, ret);

    timeval immedaitely = {0, 0};
    return fnSelect(nfds, readfds, writefds, exceptfds, &immedaitely);
}
#endif

#if (CO_HOOK_MUTEX)
// 第三方阻塞的mutex 防止死锁出现
int32_t pthread_mutex_lock(pthread_mutex_t* mutex)
{
    if (!fnPthreadMutexLock) init_coroutine_hook();

    // CO_SERVER_LOG_DEBUG("hook pthread_mutex_lock mutex:%p  ----------- ", mutex);

    // 判断线程私有变量是否有cycle 确定是否需要hook
    CoThreadLocalInfo* threadInfo = GET_TLS();
    if (threadInfo->m_coCycle == NULL) {
        return fnPthreadMutexLock(mutex);
    }
    CoConnection* connection = threadInfo->m_curConnection;

    while (1) {
        // pthread_mutex_trylock出错不设置errno 在返回值中标识错误
        int32_t ret = pthread_mutex_trylock(mutex);
        if (CO_OK == ret) {
            CO_SERVER_LOG_DEBUG("(cid:%u) hook pthread_mutex_lock, try lock mutex:%p success", connection->m_connId, mutex);
            return CO_OK;
        }

        // ret != CO_OK
        if (EBUSY == ret || EAGAIN == ret) {
#if (CO_MUTEX_SLEEP)
            CoDispatcher::yield_timer(connection, connection->m_cycle->m_conf->m_confHook.m_mutexRetryTime);
#else 
            // 锁被占用 切出等待切入
            CO_SERVER_LOG_DEBUG("(cid:%u) hook pthread_mutex_lock busy, add block mutex:%p, ret:%d", connection->m_connId, mutex, ret);
            CoSingle* single = CoSingle::get_instance();
            single->add_block_mutex(mutex, connection);
            CO_SERVER_LOG_DEBUG("(cid:%u) hook pthread_mutex_lock busy, add block mutex:%p resume", connection->m_connId, mutex); 
#endif

        } else {
            // 其他错误
            CO_SERVER_LOG_FATAL("(cid:%u) hook pthread_mutex_lock try lock mutex:%p failed, ret:%d", connection->m_connId, mutex, ret);
            return fnPthreadMutexLock(mutex);
        }
    }

    return CO_OK;
}

int32_t pthread_mutex_unlock(pthread_mutex_t* mutex)
{
    if (!fnPthreadMutexUnlock) init_coroutine_hook();

    // CO_SERVER_LOG_DEBUG("hook pthread_mutex_unlock mutex:%p  ----------- ", mutex);

#if (CO_MUTEX_SLEEP)
    return fnPthreadMutexUnlock(mutex);
#endif

    // 不hook外部线程释放锁 依靠timer去尝试加锁
    CoThreadLocalInfo* threadInfo = GET_TLS();
    if (threadInfo->m_coCycle == NULL) {
        return fnPthreadMutexUnlock(mutex);
    }
    // 全局通知内部线程尝试加锁
    CO_SERVER_LOG_DEBUG("hook pthread_mutex_unlock, free block mutex:%p", mutex);

    int32_t ret = fnPthreadMutexUnlock(mutex);
    int32_t errnoBak = errno;

    // 全局释放锁
    CoSingle* single = CoSingle::get_instance();
    single->free_block_mutex(mutex);

    errno = errnoBak;
    return ret;
}
#endif

}

int32_t init_hook()
{
    fnConnect = (connect_t)dlsym(RTLD_NEXT, "connect");
    fnRead = (read_t)dlsym(RTLD_NEXT, "read");
    fnReadv = (readv_t)dlsym(RTLD_NEXT, "readv");
    fnRecv = (recv_t)dlsym(RTLD_NEXT, "recv");
    fnRecvfrom = (recvfrom_t)dlsym(RTLD_NEXT, "recvfrom");
    fnRecvmsg = (recvmsg_t)dlsym(RTLD_NEXT, "recvmsg");
    fnWrite = (write_t)dlsym(RTLD_NEXT, "write");
    fnWritev = (writev_t)dlsym(RTLD_NEXT, "writev");
    fnSend = (send_t)dlsym(RTLD_NEXT, "send");
    fnSendto = (sendto_t)dlsym(RTLD_NEXT, "sendto");
    fnSendmsg = (sendmsg_t)dlsym(RTLD_NEXT, "sendmsg");
    fnAccept = (accept_t)dlsym(RTLD_NEXT, "accept");
    fnSleep = (sleep_t)dlsym(RTLD_NEXT, "sleep");
    fnUsleep = (usleep_t)dlsym(RTLD_NEXT, "usleep");
    if (!fnConnect || !fnRead || !fnWrite || !fnReadv || !fnWritev || !fnSend || !fnSendto || !fnSendmsg || !fnAccept || !fnSleep || !fnUsleep) {
        CO_SERVER_LOG_FATAL("coroutine hook syscall failed");
        exit(1);
    }

#if (CO_HOOK_SELECT)
    fnSelect = (select_t)dlsym(RTLD_NEXT, "select");
    if (!fnSelect) {
        CO_SERVER_LOG_FATAL("coroutine hook syscall select failed");
        exit(1);
    }
#endif

#if (CO_HOOK_MUTEX)
    fnPthreadMutexLock = (pthread_mutex_lock_t)dlsym(RTLD_NEXT, "pthread_mutex_lock");
    fnPthreadMutexUnlock = (pthread_mutex_unlock_t)dlsym(RTLD_NEXT, "pthread_mutex_unlock");
    if (!fnPthreadMutexLock || !fnPthreadMutexUnlock) {
        CO_SERVER_LOG_FATAL("coroutine hook syscall mutex failed");
        exit(1);
    }
#endif

    CO_SERVER_LOG_INFO("coroutine hook syscall success");
    return CO_OK;
}

void init_coroutine_hook()
{
    static int32_t isInit = init_hook();
    UNUSED(isInit);
}

void get_socket_ipport(int32_t socketFd, std::string &localIP, int32_t &localPort, std::string &remoteIP, int32_t &remotePort) 
{
    struct sockaddr_in addr;
    socklen_t addrLen = sizeof(addr);
    char ipAddr[INET_ADDRSTRLEN];

    // local ip and port
    memset(&addr, 0, sizeof(addr));
    if (0 == getsockname(socketFd, (struct sockaddr*)&addr, &addrLen)) {
        if (addr.sin_family == AF_INET) {
            localIP = inet_ntop(AF_INET, &addr.sin_addr, ipAddr, sizeof(ipAddr));
            localPort = ntohs(addr.sin_port);
        }
    }

    // remote ip and port
    memset(&addr, 0, sizeof(addr));
    if (0 == getpeername(socketFd, (struct sockaddr*)&addr, &addrLen)) {
        if (addr.sin_family == AF_INET) {
            remoteIP = inet_ntop(AF_INET, &addr.sin_addr, ipAddr, sizeof(ipAddr));
            remotePort = ntohs(addr.sin_port);
        }
    }

    return ;
}

}

