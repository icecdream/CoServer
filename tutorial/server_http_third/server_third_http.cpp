#include "coserver/core/co_server.h"
#include "coserver/core/co_request.h"

using namespace coserver;

int BusinessProcess(CoUserHandlerData* requestData);
int BusinessDestroy(CoUserHandlerData* requestData);

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
    CoHTTPResponse* httpResp = (CoHTTPResponse*) (requestData->m_protocol->get_respmsg());

    for (int i=0; i<1; ++i) {
        int ret = CoUpstreamPool::add_upstream(requestData, "test_backend", PROTOCOL_HTTP_CLIENT);
        if (ret == 0) {
            CoUpstreamInfo* upstreamInfo = requestData->m_upstreamInfos.back();
            CoHTTPRequest* thirdReq = (CoHTTPRequest* )(upstreamInfo->m_protocol->get_reqmsg());
            /*
            thirdReq->set_method("POST");
            thirdReq->set_url("/coserver/subrequest");
            thirdReq->add_header("Content-Type", "application/json");
            thirdReq->add_header("Connection", "Keep-Alive");
            thirdReq->append_content("request content");
            */
            thirdReq->set_method("GET");
            thirdReq->set_url("/coserver.txt");
            thirdReq->add_header("Content-Type", "application/json");
            thirdReq->add_header("Connection", "Keep-Alive");

        } else {
            fprintf(stderr, "ERROR add upstream failed\n");
        }
    }

    CoUpstreamPool::run_upstreams(requestData);

    // wait result
    for (auto &upstreamRes : requestData->m_upstreamInfos) {
        CoHTTPResponse* thirdResp = (CoHTTPResponse* )(upstreamRes->m_protocol->get_respmsg());
        
        fprintf(stdout, "http resp status:%d usetimeus:%d, content:%s\n", upstreamRes->m_status, upstreamRes->m_useTimeUs, thirdResp->get_content().c_str());
        httpResp->append_content(thirdResp->get_content());
    }

    return 0;
}

int BusinessDestroy(CoUserHandlerData* requestData)
{
    fprintf(stdout, "business handler destroy\n");
}

// g++ server_third_http.cpp -g -oserver_third_http -std=c++11 -lcoserver -lpthread -ldl -L/usr/local/lib64 -I/usr/local/include/coserver
// curl -v -d"body_123" "http://127.0.0.1:15678/ready?a=b&c=d"
// python -m SimpleHTTPServer 10080

