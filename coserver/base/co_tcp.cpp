#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/ioctl.h>
#include <netinet/tcp.h>
#include "base/co_tcp.h"
#include "base/co_common.h"
#include "base/co_log.h"


namespace coserver
{

CoTCP::CoTCP(const std::string &ip, uint16_t port, int32_t sndTimeoutMs, int32_t rcvTimeoutMs, int32_t connTimeoutMs)
: m_ip(ip)
, m_port(port)
, m_sndTimeoutMs(sndTimeoutMs)
, m_rcvTimeoutMs(rcvTimeoutMs)
, m_connTimeoutMs(connTimeoutMs)
{
    m_ipport = m_ip + ":" + std::to_string(m_port);
}

CoTCP::CoTCP(const int32_t socketFd)
: m_socketfd(socketFd)
{
    if (m_socketfd > 0) {
        m_connectState = true;
    }
}

CoTCP::~CoTCP()
{
    tcp_close();
}

void CoTCP::reset()
{
    m_ip.clear();
    m_port = 0;
    m_ipport.clear();

    m_socketfd = -1;
    m_sndTimeoutMs = 0;
    m_rcvTimeoutMs = 0;
    m_connTimeoutMs = 0;

    m_connectState = false;
}

int32_t CoTCP::init_ipport(const std::string &ip, const uint16_t port)
{
    m_ip = ip;
    m_port = port;
    m_ipport = m_ip + ":" + std::to_string(m_port);

    if (m_socketfd > 0) {
        CO_SERVER_LOG_ERROR("socket init again %d", m_socketfd);
        return CO_OK;
    }

    m_socketfd = ::socket(AF_INET, SOCK_STREAM, 0);
    if (m_socketfd == -1) {
        CO_SERVER_LOG_ERROR("socket failed, error:%s", strerror(errno));
        return CO_ERROR;
    }

    if (m_sndTimeoutMs > 0 && !set_sndtimeout_ms(m_sndTimeoutMs)) {
        CO_SERVER_LOG_ERROR("set socket send time out error");
        return CO_ERROR;
    }

    if (m_rcvTimeoutMs > 0 && !set_rcvtimeout_ms(m_rcvTimeoutMs)) {
        CO_SERVER_LOG_ERROR("set socket recv time out error");
        return CO_ERROR;
    }
    
    return CO_OK;
}

int32_t CoTCP::init_client_socketfd(int32_t socketFd, bool getPeerInfo)
{
    if (m_socketfd != -1) {
        CO_SERVER_LOG_FATAL("socket unexpected reuse, origin socket:%d", m_socketfd);
        tcp_close();
    }

    m_port = 0;
    m_ip.clear();
    m_ipport.clear();

    m_socketfd = socketFd;
    m_connectState = true;

    if (getPeerInfo) {
        struct sockaddr_in addr;
        memset(&addr, 0, sizeof(addr));
        socklen_t len = sizeof(addr);
        if(CO_OK == getpeername(m_socketfd, (struct sockaddr *)&addr, &len)) {
            m_ip = inet_ntoa(addr.sin_addr);
            m_port = ntohs(addr.sin_port);
            m_ipport = m_ip + ":" + std::to_string(m_port);

        } else {
            // 有可能是还未连接好 获取不到对端状态
            CO_SERVER_LOG_ERROR("socket:%d getpeername failed, errno info:%s", m_socketfd, strerror(errno));
        }
    }

    return CO_OK;
}

int32_t CoTCP::set_block()
{
    uint64_t flag = 0;
    return !::ioctl(m_socketfd, FIONBIO, &flag);
}

int32_t CoTCP::set_nonblock()
{
    uint64_t flag = 1;
    return ::ioctl(m_socketfd, FIONBIO, &flag);
}

int32_t CoTCP::set_sndtimeout_ms(int32_t timeoutMs) 
{
    struct timeval temp;
    temp.tv_sec = timeoutMs / 1000;
    temp.tv_usec = (timeoutMs % 1000) * 1000;
    m_sndTimeoutMs = timeoutMs;

    if (CO_ERROR == setsockopt(m_socketfd, SOL_SOCKET, SO_SNDTIMEO, &temp, sizeof(temp))) {
        CO_SERVER_LOG_ERROR("setsndtimeoutms setsockopt SO_SNDTIMEO failed, error:%s", strerror(errno));
        return CO_ERROR;
    }

    return CO_OK;
}

int32_t CoTCP::set_rcvtimeout_ms(int32_t timeoutMs)
{
    struct timeval temp;
    temp.tv_sec = timeoutMs / 1000;
    temp.tv_usec = (timeoutMs % 1000) * 1000;
    m_rcvTimeoutMs = timeoutMs;

    if (CO_ERROR == setsockopt(m_socketfd, SOL_SOCKET, SO_RCVTIMEO, &temp, sizeof(temp))) {
        CO_SERVER_LOG_ERROR("setrcvtimeoutms setsockopt SO_RCVTIMEO failed, error:%s", strerror(errno));
        return CO_ERROR;
    }

    return CO_OK;
}

int32_t CoTCP::set_conntimeout_ms(int32_t timeoutMs)
{
    m_connTimeoutMs = timeoutMs;
    return CO_OK;
}

int32_t CoTCP::set_sndbuffer(uint32_t size)
{
    if (CO_ERROR == setsockopt(m_socketfd, SOL_SOCKET, SO_SNDBUF, &size, sizeof(size))) {
        CO_SERVER_LOG_ERROR("setsndbuffer setsockopt failed, error:%s", strerror(errno));
        return CO_ERROR;
    }

    return CO_OK;
}

int32_t CoTCP::set_rcvbuffer(uint32_t size)
{
    if (CO_ERROR == setsockopt(m_socketfd, SOL_SOCKET, SO_RCVBUF, &size, sizeof(size))) {
        CO_SERVER_LOG_ERROR("setrcvbuffer setsockopt failed, error:%s", strerror(errno));
        return CO_ERROR;
    }

    return CO_OK;
}

int32_t CoTCP::set_nodelay()
{
    int32_t on = 1;
    if (CO_ERROR == setsockopt(m_socketfd, IPPROTO_TCP, TCP_NODELAY, &on, sizeof(on))) {
        CO_SERVER_LOG_ERROR("setnodelay setsockopt failed, error:%s", strerror(errno));
        return CO_ERROR;
    }

    return CO_OK;
}

int32_t CoTCP::set_reused()
{
    int32_t on = 1;
    if (CO_ERROR == setsockopt(m_socketfd, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on))) {
        CO_SERVER_LOG_ERROR("setreused setsockopt reuseaddr failed, error:%s", strerror(errno));
        return CO_ERROR;
    }

    if (CO_ERROR == setsockopt(m_socketfd, SOL_SOCKET, SO_REUSEPORT, &on, sizeof(on))) {
        CO_SERVER_LOG_ERROR("setreused setsockopt reuseport failed, error:%s", strerror(errno));
        return CO_ERROR;
    }

    return CO_OK;
}


const std::string &CoTCP::get_ip()
{
    return m_ip;
}

uint16_t CoTCP::get_port()
{
    return m_port;
}

const std::string &CoTCP::get_ipport() 
{
    return m_ipport;
}

int32_t CoTCP::get_socketfd()
{
    return m_socketfd;
}

bool CoTCP::get_bconnect()
{
    return m_connectState;
}

int32_t CoTCP::get_sndtimeout()
{
    return m_sndTimeoutMs;
}

int32_t CoTCP::get_rcvtimeout()
{
    return m_rcvTimeoutMs;
}

int32_t CoTCP::get_conntimeout()
{
    return m_connTimeoutMs;
}

int32_t CoTCP::server_bind()
{
    if(m_port <= 0) {
        return CO_ERROR;
    }
    
    struct sockaddr_in addr;
    addr.sin_family = AF_INET;
    addr.sin_port = htons(m_port);

    if(m_ip.empty()) {
        // 绑定通配地址
        addr.sin_addr.s_addr = INADDR_ANY;          
    } else {
        // 绑定指定IP
        inet_aton(m_ip.c_str(), &addr.sin_addr);
    }

    if(::bind(m_socketfd, (struct sockaddr *)&addr, sizeof(struct sockaddr)) == -1) {
        CO_SERVER_LOG_ERROR("bind failed, error:%s", strerror(errno));
        return CO_ERROR;
    }

    return CO_OK;
}

int32_t CoTCP::server_listen(const int32_t requestNum)
{
    if(::listen(m_socketfd, requestNum) < 0) {
        CO_SERVER_LOG_ERROR("listen failed, error:%s", strerror(errno));
        return CO_ERROR;
    }

    return CO_OK;
}

int32_t CoTCP::accept(int32_t &clientFd)
{
    clientFd = -1;
    if(m_socketfd <= 0) {
        return CO_ERROR;
    }

    struct sockaddr_in addr;
    uint32_t size = sizeof(struct sockaddr_in);
    if((clientFd = ::accept(m_socketfd, (struct sockaddr *)&addr, &size)) < 0) {
        //accept失败
        if (errno != EAGAIN) {
            CO_SERVER_LOG_ERROR("accept failed, error:%s", strerror(errno));
        }
        return CO_ERROR;
    }

    return CO_OK;
}

int32_t CoTCP::client_connect()
{
    if(m_socketfd < 0 || m_ip.empty() || m_port == 0) {
        return CO_ERROR;
    }
    
    struct sockaddr_in addr;
    addr.sin_family = AF_INET;
    addr.sin_port = htons(m_port);
    inet_aton(m_ip.c_str(), &addr.sin_addr);

    int32_t ret = ::connect(m_socketfd, (struct sockaddr *)&addr, sizeof(addr));
    if (ret < 0) {
        if (errno == EAGAIN) {
            errno = 0;
            ret = CO_TIMEOUT;
        } else if (errno == EINPROGRESS) {
            ret = CO_AGAIN;
        } else {
            CO_SERVER_LOG_ERROR("connect failed, error:%s", strerror(errno));
            ret = CO_ERROR;
        }

    } else if (ret == 0) {
        m_connectState = true;
        ret = CO_OK;

    } else {
        ret = CO_ERROR;
    }

    return ret;
}

int32_t CoTCP::client_reconnect()
{
    tcp_close();

    m_socketfd = ::socket(AF_INET, SOCK_STREAM, 0);
    if(m_socketfd == -1) {
        CO_SERVER_LOG_ERROR("socket failed, error:%s", strerror(errno));
        return CO_ERROR;
    }

    if(m_sndTimeoutMs > 0) {
        set_sndtimeout_ms(m_sndTimeoutMs);
    }

    if (m_rcvTimeoutMs > 0) {
        set_rcvtimeout_ms(m_rcvTimeoutMs);
    }

    return client_connect();
}

int32_t CoTCP::test_connection(int32_t socketFd)
{
    if (socketFd == 0) {
        socketFd = m_socketfd;
    }

    int32_t ret = 0;
    socklen_t len = sizeof(int32_t);
    if (getsockopt(socketFd, SOL_SOCKET, SO_ERROR, (void* )&ret, &len) == -1) {
        ret = errno;
    }

    if (ret) {
        return ret;
    }

    return CO_OK;
}

int32_t CoTCP::tcp_read(void* const readbuf, const uint32_t bufSize)
{
    if(readbuf == NULL || bufSize <= 0 || m_socketfd <= 0) {
        CO_SERVER_LOG_WARN("parameters error");
        return CO_ERROR;
    }

    int32_t ret = ::recv(m_socketfd, readbuf, bufSize, 0);
    switch(ret) {
        case -1:
            if(errno == EAGAIN) {   //超时
                errno = 0;
                ret = CO_TIMEOUT;
                break;
            } else {
                CO_SERVER_LOG_WARN("recv failed, error:%s", strerror(errno));
                ret = CO_ERROR;
            }
            break;

        case 0:
            ret = CO_CONNECTION_CLOSE;
            break;

        default:
            break;
    }

    return ret;
}

int32_t CoTCP::tcp_readall(void* const readbuf, const uint32_t bufSize)
{
    if(readbuf == NULL || bufSize <= 0) {
        CO_SERVER_LOG_WARN("parameters error");
        return CO_ERROR;
    }

    int32_t ret = 0;
    int32_t length = 0;
    while((ret = tcp_read(((char* )readbuf + length), bufSize - length)) > 0) {
        length += ret;    // 已读取的长度

        if((uint32_t)length >= bufSize) {
            // 数据全部完毕
            break;
        }
    }

    if(ret <= 0) {
        return ret;
    }

    return length;
}

int32_t CoTCP::tcp_write(const void* sendBuf, const uint32_t bufSize)
{
    int32_t ret = CO_ERROR;
    if(sendBuf == NULL || bufSize <= 0 || m_socketfd <= 0) {//参数有误
        CO_SERVER_LOG_WARN("parameters error");
        return ret;
    }

    ret = ::send(m_socketfd, sendBuf, bufSize, 0);
    if (ret < 0) {
        if (errno == EAGAIN) {
            errno = 0;
            ret = CO_TIMEOUT;
        } else {
            CO_SERVER_LOG_WARN("send failed, socket:%d size:%u error:%s", m_socketfd, bufSize, strerror(errno));
            ret = CO_ERROR;
        }
    }

    return ret;
}

int32_t CoTCP::tcp_writeall(const void* sendBuf, const uint32_t bufSize)
{
    if(sendBuf == NULL || bufSize <= 0) {//参数有误
        CO_SERVER_LOG_WARN("parameters error");
        return CO_ERROR;
    }

    int32_t ret = 0;
    int32_t length = 0;
    while((ret = tcp_write(((char* )sendBuf + length), bufSize - length)) > 0) {
        length += ret;

        if((uint32_t)length >= bufSize) {
            break;
        }
    }

    if(ret <= 0) {
        return ret;
    }

    return length;
}

int32_t CoTCP::tcp_close()
{
    SAFE_CLOSE(m_socketfd);
    m_connectState = false;
    return CO_OK;
}

}

