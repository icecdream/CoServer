#include "coserver/core/co_server.h"
#include "coserver/core/co_request.h"
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <pthread.h>
#include <thread>
#include <fstream>
#include <sstream>
#include <vector>

using namespace coserver;
using std::stringstream;
using std::ifstream;


int BusinessProcess(CoUserHandlerData* requestData);
int BusinessDestroy(CoUserHandlerData* requestData);
int BusinessHandlerClientYieldResume(CoUserHandlerData* requestData);

// 第三方阻塞socket测试
void ThirdBlockSocket(int port, char* recvBuf);
void ThreadFunc();

// 子请求测试
int BusinessHandlerSubrequest(CoUserHandlerData* requestData);

// detach测试
int BusinessHandlerDetach(CoUserHandlerData* requestData);
int BusinessHandlerActiveSubrequest(CoUserHandlerData* requestData);
void DetachTest();

std::vector<std::pair<void*, uint32_t>> g_yieldData;
CoServer g_coServer;
pthread_mutex_t g_mutex = PTHREAD_MUTEX_INITIALIZER;


void test_hook();

int main()
{
    //test_hook();

    std::thread threadTimerFunc(ThreadFunc);
    threadTimerFunc.detach();

    g_coServer.add_user_handlers("server_1", BusinessProcess, BusinessDestroy);
    g_coServer.add_user_handlers("server_2", BusinessHandlerClientYieldResume, BusinessDestroy);
    g_coServer.add_user_handlers("server_3", BusinessHandlerSubrequest, BusinessDestroy);
    g_coServer.add_user_handlers("server_4", BusinessHandlerDetach, BusinessDestroy);
    if (CO_OK != g_coServer.run_server("./coserver.conf", 0)) {
        fprintf(stdout, "coserver init failed\n");
        return -1;
    }

    //while(1) {
    for (int i=0; i<30; ++i) {
	    fprintf(stdout, "sleep %ds\n", i);
        sleep(1);
    }

    g_coServer.shut_down();
    pthread_mutex_destroy(&g_mutex);

    fprintf(stdout, "coserver runforever complete\n");
    return 0;
}

void ThreadFunc()
{
    while(1) {
        std::this_thread::sleep_for(std::chrono::milliseconds(3000));

        for (auto itr = g_yieldData.begin(); itr != g_yieldData.end(); ) {
            fprintf(stdout, "codispatcher resume data\n");
            CoDispatcher::resume_async(*itr);
            itr = g_yieldData.erase(itr);
        }
    }

    return ;
}

int BusinessHandlerDetach(CoUserHandlerData* requestData)
{
    fprintf(stdout, "business handler detach\n");
    //DetachTest();
    return 0;
}

void DetachTest()
{
    for (int i=0; i<1; ++i) {
        CoUserHandlerData* handlerData = CoUpstreamPool::add_upstream_detach("test_backend", PROTOCOL_HTTP_CLIENT, BusinessHandlerActiveSubrequest);
        if (handlerData) {
            CoUpstreamInfo* upstreamInfo = handlerData->m_upstreamInfos.back();
            CoHTTPRequest* httpReq = (CoHTTPRequest* )(upstreamInfo->m_protocol->get_reqmsg());
            
            httpReq->set_method("POST");
            httpReq->set_url("/coserver/test_detach" + std::to_string(i));
            httpReq->add_header("Content-Type", "application/json");
            httpReq->add_header("Connection", "Keep-Alive");
            std::string strcontent = "detach";
            httpReq->append_content(strcontent);

        } else {
            fprintf(stderr, "ERROR add detach upstream failed\n");
        }
    }

    //fprintf(stdout, "codispatcher shut down\n");
    //g_coServer.shut_down();
}

int BusinessHandlerActiveSubrequest(CoUserHandlerData* requestData)
{
    fprintf(stdout, "business handler detach, active subrequest\n");

    int i = 0;
    for (auto &itr : requestData->m_upstreamInfos) {
        CoUpstreamInfo* upstreamInfo = requestData->m_upstreamInfos[i];
        CoUpstreamInfo* upstreamRes = itr;

        CoHTTPResponse* httpResp = (CoHTTPResponse* )(upstreamInfo->m_protocol->get_respmsg());

        fprintf(stdout, "detach http resp index:%d status:%d usetimeus:%d, content:%s\n", i, upstreamRes->m_status, upstreamRes->m_useTimeUs, httpResp->get_content().c_str());
    }

    static int count = 0;
    count++;
    fprintf(stdout, "detach active subrequest count:%d\n", count);

    if (count == 9999) {
        for (int i=0; i<5; ++i) {
            CoUserHandlerData* handlerData = CoUpstreamPool::add_upstream_detach("test_backend", PROTOCOL_HTTP_CLIENT, BusinessHandlerActiveSubrequest);
            if (handlerData) {
                CoUpstreamInfo* upstreamInfo = handlerData->m_upstreamInfos.back();
                CoHTTPRequest* httpReq = (CoHTTPRequest* )(upstreamInfo->m_protocol->get_reqmsg());
                
                httpReq->set_method("POST");
                httpReq->set_url("/coserver/DETACH_" + std::to_string(i));
                httpReq->add_header("Content-Type", "application/json");
                httpReq->add_header("Connection", "Keep-Alive");
                std::string strcontent = "detachtest";
                httpReq->append_content(strcontent);

            } else {
                fprintf(stderr, "ERROR add detach upstream failed\n");
            }
        }
    }

    return 0;
}

int BusinessHandlerSubrequest(CoUserHandlerData* requestData)
{
    //fprintf(stdout, "business BusinessHandlerSubrequest process\n");

    /*
        填充CoMsg  添加到requestData子请求数组中  执行子请求（阻塞）（选择location）
        无关子请求 使用active_request进行执行
    */

    for (int i=0; i<1; ++i) {
        int ret = CoUpstreamPool::add_upstream(requestData, "test_backend", PROTOCOL_HTTP_CLIENT);
        if (ret == 0) {
            CoUpstreamInfo* upstreamInfo = requestData->m_upstreamInfos.back();
            CoHTTPRequest* httpReq = (CoHTTPRequest* )(upstreamInfo->m_protocol->get_reqmsg());
            
            httpReq->set_method("POST");
            httpReq->set_url("/coserver/subrequest_" + std::to_string(i));
            httpReq->add_header("Content-Type", "application/json");
            httpReq->add_header("Connection", "Keep-Alive");
            std::string strcontent = "jjj666_" + std::to_string(i);
            httpReq->append_content(strcontent);

        } else {
            fprintf(stderr, "ERROR add upstream failed\n");
        }
    }

    CoUpstreamPool::run_upstreams(requestData);

    // wait result
    int i = 0;
    for (auto &itr : requestData->m_upstreamInfos) {
        CoUpstreamInfo* upstreamRes = itr;

        CoHTTPResponse* httpResp = (CoHTTPResponse* )(upstreamRes->m_protocol->get_respmsg());

        fprintf(stdout, "http resp index:%d status:%d usetimeus:%d, content:%s\n", i, upstreamRes->m_status, upstreamRes->m_useTimeUs, httpResp->get_content().c_str());

        CoHTTPResponse* clientHttpResp = (CoHTTPResponse*) (requestData->m_protocol->get_respmsg());

        if (httpResp->get_content().length() != 0) {
            std::string resContent = httpResp->get_content() + "\n";
            clientHttpResp->append_content(resContent);
        }

        i++;
    }

    return 0;
}

int BusinessHandlerClientYieldResume(CoUserHandlerData* requestData)
{
    fprintf(stdout, "business handler process client yield resume\n");

    fprintf(stdout, "business handler process yield START1\n");
    g_yieldData.push_back(requestData->m_coroutineData);
    CoDispatcher::yield(requestData->m_coroutineData);
    fprintf(stdout, "business handler process yield END1\n");

    fprintf(stdout, "business handler process yield START2\n");
    g_yieldData.push_back(requestData->m_coroutineData);
    CoDispatcher::yield(requestData->m_coroutineData);
    fprintf(stdout, "business handler process yield END2\n");

    fprintf(stdout, "business handler process yield START3\n");
    g_yieldData.push_back(requestData->m_coroutineData);
    CoDispatcher::yield(requestData->m_coroutineData);
    fprintf(stdout, "business handler process yield END3\n");

    // http protocol
    CoHTTPResponse* msgHttp = (CoHTTPResponse* )(requestData->m_protocol->get_respmsg());
    msgHttp->add_header("dong-test", "dongdongdong");
    msgHttp->append_content("123456789", strlen("123456789"));

    /*
    tcp protocol
    CoMsgTcp *pMsgTcp = (CoMsgTcp *)(requestData->m_protocol->get_respmsg());
    pMsgTcp->set_msgbody("123456789");
    */

    return 0;
}

int BusinessDestroy(CoUserHandlerData* requestData)
{
    fprintf(stdout, "business handler destroy\n");
}

int BusinessProcess(CoUserHandlerData* requestData)
{
    fprintf(stdout, "business handler process start, clientip:%s\n", requestData->m_protocol->get_clientip().c_str());
    pthread_mutex_lock(&g_mutex);
    fprintf(stdout, "business handler process mutex success, sleep 10s\n");
    std::this_thread::sleep_for(std::chrono::milliseconds(8000));
    pthread_mutex_unlock(&g_mutex);
    fprintf(stdout, "business handler process end\n");

    /*
    fprintf(stdout, "business handler process start\n");
    sleep(10);
    fprintf(stdout, "business handler process end\n");
    */
    return CO_OK;

    // 测试请求第三方阻塞服务器
    static int count = 0;
    if (count == 0) {
        count = 1;
        // use third block socket
        char readbuffer[4096] = {0};
        ThirdBlockSocket(25678, readbuffer);

        // 接收tcp协议响应
        CoMsgTcpHead *msgTcpData = (CoMsgTcpHead *)readbuffer;
        fprintf(stdout, "COMM THIRD recv 33333 body:%s\n", msgTcpData->m_body);

        // http protocol
        CoHTTPResponse* msgHttp = (CoHTTPResponse* )(requestData->m_protocol->get_respmsg());
        msgHttp->add_header("dong-test", "dongdongdong");
        msgHttp->append_content(msgTcpData->m_body, strlen(msgTcpData->m_body));

    } else {
        count = 0;
        fprintf(stdout, "NOT COMM recv 33333\n");

        // http protocol
        CoHTTPResponse* msgHttp = (CoHTTPResponse* )(requestData->m_protocol->get_respmsg());
        msgHttp->add_header("dong-test", "dongdongdong");
        msgHttp->append_content("NOT COMM", strlen("NOT COMM"));
    }

    return 0;
}

void ThirdBlockSocket(int port, char* recvBuf)
{
    int socketFd = ::socket(AF_INET, SOCK_STREAM, 0);

    struct sockaddr_in addr;
    addr.sin_family = AF_INET;
    inet_aton("127.0.0.1", &addr.sin_addr);
    addr.sin_port = htons(port);

    fprintf(stdout, "connect 11111 \n");
    int ret = ::connect(socketFd, (struct sockaddr *)&addr, sizeof(addr));
    fprintf(stdout, "connect 22222 \n");
    if (ret < 0) {
        fprintf(stdout, "connect failed\n");
        close(socketFd);
        return ;
    }

    // set socket timeout
    struct timeval temp;
    temp.tv_sec = 10000 / 1000;
    temp.tv_usec = (10000 % 1000) * 1000;
    if (-1 == setsockopt(socketFd, SOL_SOCKET, SO_SNDTIMEO, &temp, sizeof(temp))) {
        fprintf(stdout, "setsndtimeoutms setsockopt SO_SNDTIMEO failed, error:%s", strerror(errno));
        close(socketFd);
        return ;
    }

    if (-1 == setsockopt(socketFd, SOL_SOCKET, SO_RCVTIMEO, &temp, sizeof(temp))) {
        fprintf(stdout, "setrcvtimeoutms setsockopt SO_RCVTIMEO failed, error:%s", strerror(errno));
        close(socketFd);
        return ;
    }

    fprintf(stdout, "send 11111 \n");
    CoMsgTcpHead coMsgTcpHead;
    coMsgTcpHead.m_flag = 0xe8;
    coMsgTcpHead.m_length = htonl(10);

    char buffer[256] = {0};

    memcpy(buffer, (char*)&coMsgTcpHead, sizeof(CoMsgTcpHead));
    memcpy(buffer + sizeof(CoMsgTcpHead), "abcdefghij", 10);

    ret = ::send(socketFd, buffer, sizeof(CoMsgTcpHead) + 10, 0);
    fprintf(stdout, "send 22222 ret:%d\n", ret);

    fprintf(stdout, "recv 11111 \n");
    ret = ::recv(socketFd, recvBuf, 4096, 0);

    fprintf(stdout, "recv 22222 ret:%d\n%s\n", ret, recvBuf);

    close(socketFd);
}

void test_hook()
{
    std::string pathfile = "coserver.conf";
    ifstream fp(pathfile);
    if (!fp.is_open()) {
        fprintf(stderr, "Can't open config file:%s\n", pathfile.c_str());
        return ;
    }

    std::string line, key, value;
    while (getline(fp, line)) {
        fprintf(stderr, "read line:%s\n", line.c_str());
    }

    fp.close();
    return ;
}


// g++ server_tutorial.cpp -g -oserver_tutorial -std=c++11 -lcoserver -lpthread -ldl -L/usr/local/lib64 -I/usr/local/include/coserver
// valgrind --tool=memcheck --leak-check=yes --show-reachable=yes --run-libc-freeres=yes --log-file=./valgrind_report.log ./server_tutorial

