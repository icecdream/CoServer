#include "coserver/core/co_server.h"
#include "coserver/core/co_request.h"

using namespace coserver;

int BusinessProcess(CoUserHandlerData* requestData);
int BusinessDestroy(CoUserHandlerData* requestData);
int BusinessHandlerActiveSubrequest(CoUserHandlerData* requestData);

int main()
{
    CoServer coServer;
    coServer.add_user_handlers("server", BusinessProcess, BusinessDestroy);
    if (CO_OK != coServer.run_server("./coserver.conf")) {
        fprintf(stdout, "coserver init failed\n");
        return -1;
    }

    coServer.shut_down();
    fprintf(stdout, "coserver runforever complete\n");
    return 0;
}

int BusinessProcess(CoUserHandlerData* requestData)
{
    for (int i=0; i<1; ++i) {
        CoUserHandlerData* handlerData = CoUpstreamPool::add_upstream_detach("test_backend", PROTOCOL_HTTP_CLIENT, BusinessHandlerActiveSubrequest);
        if (handlerData) {
            CoUpstreamInfo* upstreamInfo = handlerData->m_upstreamInfos.back();
            CoHTTPRequest* thirdReq = (CoHTTPRequest* )(upstreamInfo->m_protocol->get_reqmsg());
            thirdReq->set_method("GET");
            thirdReq->set_url("/coserver.txt");
            thirdReq->add_header("Content-Type", "application/json");
            thirdReq->add_header("Connection", "Keep-Alive");

        } else {
            fprintf(stderr, "ERROR add detach upstream failed\n");
        }
    }

    return 0;
}

int BusinessHandlerActiveSubrequest(CoUserHandlerData* requestData)
{
    // 异步回调通知
    int i = 0;
    for (auto &upstreamResp : requestData->m_upstreamInfos) {
        CoUpstreamInfo* upstreamInfo = requestData->m_upstreamInfos[i];
        CoHTTPResponse* httpResp = (CoHTTPResponse* )(upstreamInfo->m_protocol->get_respmsg());
        fprintf(stdout, "detach http resp status:%d usetimeus:%d, content:%s\n", upstreamResp->m_status, upstreamResp->m_useTimeUs, httpResp->get_content().c_str());
    }

    return 0;
}

int BusinessDestroy(CoUserHandlerData* requestData)
{
    fprintf(stdout, "business handler destroy\n");
}

// g++ server_third_detach.cpp -g -oserver_third_detach -std=c++11 -lcoserver -lpthread -ldl -L/usr/local/lib64 -I/usr/local/include/coserver
// curl -v -d"body_123" "http://127.0.0.1:15678/ready?a=b&c=d"
// python -m SimpleHTTPServer 10080

