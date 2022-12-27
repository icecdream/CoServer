#include "coserver/core/co_server.h"
#include "coserver/core/co_request.h"
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <thread>
#include <vector>
#include <atomic>

using namespace coserver;

std::atomic<uint64_t> g_requestId(0);

int BusinessProcess(CoUserHandlerData* requestData);
int BusinessProcess2(CoUserHandlerData* requestData);
int BusinessDestroy(CoUserHandlerData* requestData);

void ThreadFunc();

std::vector<std::pair<void*, uint32_t>> g_yieldData;


int main()
{
    CoServer coServer;
    coServer.add_user_handlers("server_1", BusinessProcess, BusinessDestroy);
    coServer.add_user_handlers("server_2", BusinessProcess2, BusinessDestroy);
    if (CO_OK != coServer.run_server("./coserver.conf")) {
        printf("coserver init failed\n");
        return -1;
    }

	while(1) {
		sleep(1);	
	}

    fprintf(stdout, "coserver runforever complete\n");
    return 0;
}

int BusinessDestroy(CoUserHandlerData* requestData)
{
    fprintf(stdout, "business handler destroy\n");
}

int BusinessProcess(CoUserHandlerData* requestData)
{
    fprintf(stdout, "business handler process\n");

	g_requestId++;

    // http protocol
    CoHTTPResponse* msgHttp = (CoHTTPResponse* )requestData->m_protocol->get_respmsg();
    msgHttp->add_header("dong-test", "dongdongdong");
	std::string content = "third server_" + std::to_string(g_requestId);
    msgHttp->append_content(content);

//	if ((g_requestId % 10000) == 0) {
    	//sleep(100);
    	usleep(5000);
 //   }

    return 0;
}

int BusinessProcess2(CoUserHandlerData* requestData)
{
    fprintf(stdout, "business handler process2, sleep 3s\n");

    //tcp protocol
    CoMsgTcp *pMsgTcp = (CoMsgTcp *)requestData->m_protocol->get_respmsg();
    pMsgTcp->set_msgbody("J6ZV37");

    usleep(6000000);

    return 0;
}

// g++ net_test.cpp -onet_test -g -std=c++11 -lcoserver -lpthread -ldl -L/usr/local/lib64 -I/usr/local/include/coserver

