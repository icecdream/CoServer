#ifndef _CO_CALLBACK_UPSTREAM_H_
#define _CO_CALLBACK_UPSTREAM_H_

#include <cstdint>


namespace coserver
{

struct CoRequest;
struct CoConnection;

class CoCallbackUpstream {
public:
    static void upstream_init(CoConnection* connection);

    static void upstream_write(CoConnection* connection);
    static void upstream_read(CoConnection* connection);
    static void upstream_process(CoConnection* connection);

    // 释放upstream request（没有连接相关资源）
    static void upstream_finalize_request(CoRequest* request, int32_t retCode);
    static void upstream_finalize(CoRequest* request, int32_t retCode);
};

}

#endif //_CO_CALLBACK_UPSTREAM_H_

