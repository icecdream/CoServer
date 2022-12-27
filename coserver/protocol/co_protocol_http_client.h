#ifndef _CO_PROTOCOL_HTTP_CLIENT_H_
#define _CO_PROTOCOL_HTTP_CLIENT_H_

#include "protocol/co_protocol_http.h"


namespace coserver
{

class CoProtocolHttpClient : public CoProtocol 
{
public:
    CoProtocolHttpClient();
    virtual ~CoProtocolHttpClient();

public:
    virtual int32_t decode(CoBuffer* buffer);
    virtual int32_t encode(CoBuffer* buffer);

    virtual void reset_reqmsg();
    virtual void reset_respmsg();

    virtual CoMsg* get_reqmsg()
    { return m_reqMsg; }

    virtual CoMsg* get_respmsg()
    { return m_respMsg; }

public:
    CoMsg* m_reqMsg;
    CoMsg* m_respMsg;
};

}

#endif //_CO_PROTOCOL_HTTP_CLIENT_H_

