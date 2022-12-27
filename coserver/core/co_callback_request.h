#ifndef _CO_CALLBACK_REQUEST_H_
#define _CO_CALLBACK_REQUEST_H_

#include "core/co_request.h"


namespace coserver
{

class CoCallbackRequest {
public:
    static void request_init(CoConnection* connection);

    static void request_read(CoRequest* request);       // 读取客户端请求数据
    static void request_process(CoRequest* request);    // 业务处理
    static void request_write(CoRequest* request, int32_t retCode = 0);
    static void request_finalize(CoRequest* request, int32_t retCode = 0);
    
    static void free_request_connection(CoConnection* connection, int32_t retCode);
};

}

#endif //_CO_CALLBACK_REQUEST_H_

