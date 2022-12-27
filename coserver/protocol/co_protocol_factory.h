#ifndef _CO_PROTOCOL_FACTORY_H_
#define _CO_PROTOCOL_FACTORY_H_

#include "protocol/co_protocol.h"
#include "protocol/co_protocol_tcp.h"
#include "protocol/co_protocol_http_client.h"
#include "protocol/co_protocol_http_server.h"


namespace coserver
{

// 协议工厂类  根据协议类型生成不同协议解析类
class CoProtocolFactory
{
public:
    CoProtocolFactory() {}
    ~CoProtocolFactory() {}

    CoProtocol* create_protocol(int32_t protocolType) 
    {
        CoProtocol* protocol = NULL;

        switch(protocolType) {
            case PROTOCOL_TCP_SERVER:
            {
                protocol = new CoProtocolTcp(PROTOCOL_TCP_SERVER);
                break;
            }
            case PROTOCOL_TCP_CLIENT:
            {
                protocol = new CoProtocolTcp(PROTOCOL_TCP_CLIENT);
                break;
            }
            case PROTOCOL_HTTP_SERVER:
            {
                protocol = new CoProtocolHttpServer();
                break;
            }
            case PROTOCOL_HTTP_CLIENT:
            {
                protocol = new CoProtocolHttpClient();
                break;
            }
            default:
                break;
        }

        return protocol;
    }

};

}

#endif //_CO_PROTOCOL_FACTORY_H_

