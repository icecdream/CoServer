#include "coserver/core/co_server.h"
#include "coserver/core/co_request.h"

using namespace coserver;


int BusinessProcess(CoUserHandlerData* requestData);
int BusinessDestroy(CoUserHandlerData* requestData);

void ThreadFunc();
std::vector<std::pair<void*, uint32_t>> g_yieldData;

int main()
{
    std::thread threadTimer(ThreadFunc);
    threadTimer.detach();

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

void ThreadFunc()
{
    while(1) {
        std::this_thread::sleep_for(std::chrono::milliseconds(5000));

        for (auto itr = g_yieldData.begin(); itr != g_yieldData.end(); ) {
            fprintf(stdout, "time consuming task complete\n");
            CoDispatcher::resume_async(*itr);
            itr = g_yieldData.erase(itr);
        }
    }

    return ;
}

int BusinessProcess(CoUserHandlerData* requestData)
{
    // 通知其他线程 执行耗时任务
    g_yieldData.push_back(requestData->m_coroutineData);
    CoDispatcher::yield(requestData->m_coroutineData);

    CoHTTPResponse* httpResp = (CoHTTPResponse* )(requestData->m_protocol->get_respmsg());
    httpResp->append_content("12345678");

    return 0;
}

int BusinessDestroy(CoUserHandlerData* requestData)
{
    fprintf(stdout, "business handler destroy\n");
}

// g++ server_third_async.cpp -g -oserver_third_async -std=c++11 -lcoserver -lpthread -ldl -L/usr/local/lib64 -I/usr/local/include/coserver
// curl -v -d"body_123" "http://127.0.0.1:15678/ready?a=b&c=d"

