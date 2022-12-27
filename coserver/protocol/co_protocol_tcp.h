#ifndef _CO_PROTOCOL_TCP_H_
#define _CO_PROTOCOL_TCP_H_

#include "protocol/co_protocol.h"


namespace coserver
{

struct CoMsgTcpHead 
{
    int32_t m_flag  = 0;
    int32_t m_length= 0;
    char m_body[];
};

const int32_t CO_PROTOCOL_TCP_FLAG = 0xe8;


class CoMsgTcp : public CoMsg
{
public:
    CoMsgTcp() {}
    virtual ~CoMsgTcp() {}

public:
    void set_msgbody(const std::string &body)
    { m_body.assign(body); }

    virtual const std::string &get_msgbody()
    { return m_body; }
    
    virtual void set_msgstatus(int32_t status)
    { m_status = status; }

    virtual int32_t get_msgstatus()
    { return m_status; }

private:
    int32_t     m_status    = 0;
    std::string m_body      = "";
};


// tcp协议解析类
class CoProtocolTcp : public CoProtocol 
{
public:
    CoProtocolTcp(int32_t type);
    CoProtocolTcp() = delete;
    virtual ~CoProtocolTcp();

public:
    virtual CoMsg* get_reqmsg()
    { return m_reqMsg; }

    virtual CoMsg* get_respmsg()
    { return m_respMsg; }

    virtual void reset_reqmsg();
    virtual void reset_respmsg();

    virtual int32_t decode(CoBuffer* buffer);
    virtual int32_t encode(CoBuffer* buffer);

public:
    int32_t m_type; // server or client

    CoMsg* m_reqMsg;
    CoMsg* m_respMsg;
};

}

#endif //_CO_PROTOCOL_TCP_H_

