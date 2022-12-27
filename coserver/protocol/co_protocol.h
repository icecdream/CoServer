#ifndef _CO_PROTOCOL_H_
#define _CO_PROTOCOL_H_

#include <string>
#include "base/co_buffer.h"


namespace coserver
{

class CoMsg
{
public:
    CoMsg() {}
    virtual ~CoMsg() {}

    virtual int32_t get_msgstatus() = 0;
    virtual const std::string &get_msgbody() = 0;

    virtual void set_msgstatus(int32_t status) = 0;
};


// 多种协议基类(tcp http ...)
class CoProtocol
{
public:
    CoProtocol() {}
    virtual ~CoProtocol() {}

    virtual CoMsg* get_reqmsg() = 0;
    virtual CoMsg* get_respmsg() = 0;

    virtual void reset_reqmsg() = 0;
    virtual void reset_respmsg() = 0;

    virtual int32_t decode(CoBuffer* buffer) = 0;
    virtual int32_t encode(CoBuffer* buffer) = 0;

    const std::string &get_clientip() { return m_clientIP; }
    void set_clientip(const std::string &clientIP) { m_clientIP = clientIP; }

private:
    std::string m_clientIP = "";
};

}

#endif //_CO_PROTOCOL_H_

