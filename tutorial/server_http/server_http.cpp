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
    CoProtocol* protocol = requestData->m_protocol;

    // http request info
    CoHTTPRequest* httpReq = (CoHTTPRequest* )(protocol->get_reqmsg());

    fprintf(stderr, "http request method:%s url:%s clientip:%s\n", httpReq->get_method().c_str(), httpReq->get_url().c_str(), protocol->get_clientip().c_str());

    fprintf(stderr, "http request all params\n");
    const std::unordered_map<std::string, std::string> &requestAllParam = httpReq->get_allparam();
    for (auto &param : requestAllParam) {
        fprintf(stderr, "%s: %s\n", param.first.c_str(), param.second.c_str());
    }
    
    fprintf(stderr, "http request all headers\n");
    const std::unordered_map<std::string, std::string> &requestAllHeader = httpReq->get_allheader();
    for (auto &param : requestAllHeader) {
        fprintf(stderr, "%s: %s\n", param.first.c_str(), param.second.c_str());
    }

    fprintf(stderr, "http request body:%s\n", httpReq->get_msgbody().c_str());


    // http response
    CoHTTPResponse* httpResp = (CoHTTPResponse* )(protocol->get_respmsg());
    // httpResp->set_statuscode(200);  // 默认200
    httpResp->add_header("coserver-test", "test,testdata");
    std::string content = "coserver test content data";
    httpResp->append_content(content);

    return 0;
}

int BusinessDestroy(CoUserHandlerData* requestData)
{
    fprintf(stdout, "business handler destroy\n");
}

// g++ server_http.cpp -g -oserver_http -std=c++11 -lcoserver -lpthread -ldl -L/usr/local/lib64 -I/usr/local/include/coserver
// curl -v -d"body_123" "http://127.0.0.1:15678/ready?a=b&c=d"

