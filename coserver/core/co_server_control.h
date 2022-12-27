#ifndef _CO_SERVER_CONTROL_H_
#define _CO_SERVER_CONTROL_H_

#include "base/co_config.h"


namespace coserver
{

struct CoCycle;
struct CoUserFuncs;
struct CoConnection;


class CoServerControl
{
public:
    CoServerControl(CoConfServer* confServer);
    CoServerControl() = delete;
    ~CoServerControl();

    // 开启监听socket
    int32_t init(CoCycle* cycle);

    void    accept(CoConnection* connection, int32_t maxAcceptSize);
    int32_t init_connection(CoCycle* cycle, int32_t socketFd);

    void modify_listening();
    static void func_cleanup(CoConnection* connection);

private:
    int32_t limit();


public:
    CoConfServer*   m_confServer = NULL;
    CoUserFuncs*    m_userFuncs = NULL;

    // listen监听相关
    bool            m_listening = false;
    CoConnection*   m_listenConnection = NULL;

    // todo 限流 ip黑边名单等
    int32_t m_curConnectionSize = 0;
};

}

#endif //_CO_SERVER_CONTROL_H_

