#ifndef _CO_TCP_H_
#define _CO_TCP_H_

#include <string>


namespace coserver
{

class CoTCP
{
public:
    CoTCP(const std::string &ip = "", uint16_t port = 0, int32_t sndTimeoutMs = 0, int32_t rcvTimeoutMs = 0, int32_t connTimeoutMs = 0);
    CoTCP(const int32_t socketFd);
    ~CoTCP();

    void reset();

    int32_t init_ipport(const std::string &ip, const uint16_t port);
    int32_t init_client_socketfd(int32_t socketFd, bool getPeerInfo = true);

    // set tcp option funcs
    int32_t set_block();
    int32_t set_nonblock();

    int32_t set_sndtimeout_ms(int32_t timeoutMs);
    int32_t set_rcvtimeout_ms(int32_t timeoutMs);
    int32_t set_conntimeout_ms(int32_t timeoutMs);

    int32_t set_sndbuffer(uint32_t size);
    int32_t set_rcvbuffer(uint32_t size);
    int32_t set_nodelay();
    int32_t set_reused();

    // get param funcs
    int32_t get_socketfd();
    const std::string &get_ip();
    uint16_t get_port();
    const std::string &get_ipport();

    bool get_bconnect();
    int32_t get_sndtimeout();
    int32_t get_rcvtimeout();
    int32_t get_conntimeout();

    // features funcs
    int32_t server_bind();
    int32_t server_listen(const int32_t requestNum = 1);
    int32_t accept(int32_t &clientFd);

    int32_t client_connect();
    int32_t client_reconnect();
    int32_t test_connection(int32_t socketFd = 0);

    // read/write funcs
    int32_t tcp_read(void* const readbuf, const uint32_t bufSize);
    int32_t tcp_readall(void* const readbuf, const uint32_t bufSize);
    int32_t tcp_write(const void* sendBuf, const uint32_t bufSize);
    int32_t tcp_writeall(const void* sendBuf, const uint32_t bufSize);

    int32_t tcp_close();


private:
    std::string m_ip    = "";
    uint16_t    m_port  = 0;
    std::string m_ipport= "";

    int32_t     m_socketfd  = -1;
    int32_t     m_sndTimeoutMs  = 0;
    int32_t     m_rcvTimeoutMs  = 0;
    int32_t     m_connTimeoutMs = 0;

    bool        m_connectState = false;
};

}

#endif //_CO_TCP_H_

