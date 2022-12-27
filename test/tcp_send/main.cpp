#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <thread>
#include <vector>
#include <signal.h>
#include <sys/time.h>
#include "tcp.h"
using namespace std;

std::string g_request;

void InitSendata();
void SendData();
void InitSendBigData();
void InitSendCounterData();

int g_querySize = 20;

int main(int argc, char **argv)
{
	int threadSize = 20;
	if (argc > 1) {
		threadSize = atoi(argv[1]);
	}
	if (argc > 2) {
		g_querySize = atoi(argv[2]);
	}
	printf("thread size:%d, query size:%d, sleep 3s start\n", threadSize, g_querySize);
	sleep(3);
	

	signal(SIGPIPE, SIG_IGN);

	//InitSendata();
	//InitSendBigData();
	InitSendCounterData();

	struct timeval tval1;
    gettimeofday(&tval1, NULL);
	uint64_t starttime = (uint64_t)(tval1.tv_sec)*1000 + tval1.tv_usec/1000;

	vector<thread> threads;
	for (int i=0; i<threadSize; ++i) {
		thread t(SendData);
		threads.emplace_back(move(t));
	}

	for (auto& itr : threads) {
		itr.join();
	}

	struct timeval tval2;
    gettimeofday(&tval2, NULL);
	uint64_t endtime = (uint64_t)(tval2.tv_sec)*1000 + tval2.tv_usec/1000;

	printf("ALL THREAD use timems:%lu\n", endtime - starttime);

    while (1) {
        sleep(1);
    }
    return 0;
}

void InitSendata()
{
	// send string
    g_request = "POST /test_server HTTP/1.1\r\nContent-Type: application/json\r\nContent-Length: 1224\r\nHost: test_server\r\n\r\n";
    
	for (int i=0; i<1; i++) {
		g_request += "c74ae009EE264e4-f9197E3BAE7-4f7e-a34301AD66003D1Ee-4bbb2bc50991Acfb6c74ae009EE264e4-f9197E3BAE7-4f7e-a34301AD66003D1Ee-4bbb2bc50991Acfb6c74ae009EE264e4-f9197E3BAE7-4f7e-a34301AD66003D1Ee-4bbb2bc50991Acfb6c74ae009EE264e4-f9197E3BAE7-4f7e-a34301AD66003D1Ee-4bbb2bc50991Acfb6c74ae009EE264e4-f9197E3BAE7-4f7e-a34301AD66003D1Ee-4bbb2bc50991Acfb6c74ae009EE264e4-f9197E3BAE7-4f7e-a34301AD66003D1Ee-4bbb2bc50991Acfb6c74ae009EE264e4-f9197E3BAE7-4f7e-a34301AD66003D1Ee-4bbb2bc50991Acfb6c74ae009EE264e4-f9197E3BAE7-4f7e-a34301AD66003D1Ee-4bbb2bc50991Acfb6c74ae009EE264e4-f9197E3BAE7-4f7e-a34301AD66003D1Ee-4bbb2bc50991Acfb6c74ae009EE264e4-f9197E3BAE7-4f7e-a34301AD66003D1Ee-4bbb2bc50991Acfb6c74ae009EE264e4-f9197E3BAE7-4f7e-a34301AD66003D1Ee-4bbb2bc50991Acfb6c74ae009EE264e4-f9197E3BAE7-4f7e-a34301AD66003D1Ee-4bbb2bc50991Acfb6c74ae009EE264e4-f9197E3BAE7-4f7e-a34301AD66003D1Ee-4bbb2bc50991Acfb6c74ae009EE264e4-f9197E3BAE7-4f7e-a34301AD66003D1Ee-4bbb2bc50991Acfb6c74ae009EE264e4-f9197E3BAE7-4f7e-a34301AD66003D1Ee-4bbb2bc50991Acfb6c74ae009EE264e4-f9197E3BAE7-4f7e-a34301AD66003D1Ee-4bbb2bc50991Acfb6c74ae009EE264e4-f9197E3BAE7-4f7e-a34301AD66003D1Ee-4bbb2bc50991Acfb6c74ae009EE264e4-f9197E3BAE7-4f7e-a34301AD66003D1Ee-4bbb2bc50991Acfb6";
	}
}

void InitSendBigData()
{
	// send string
    g_request = "POST /test_server HTTP/1.1\r\nContent-Type: application/json\r\nContent-Length: 1224\r\nHost: test_server\r\n\r\n";
    
	for (int i=0; i<100; i++) {
		g_request += "c74ae009EE264e4-f9197E3BAE7-4f7e-a34301AD66003D1Ee-4bbb2bc50991Acfb6c74ae009EE264e4-f9197E3BAE7-4f7e-a34301AD66003D1Ee-4bbb2bc50991Acfb6c74ae009EE264e4-f9197E3BAE7-4f7e-a34301AD66003D1Ee-4bbb2bc50991Acfb6c74ae009EE264e4-f9197E3BAE7-4f7e-a34301AD66003D1Ee-4bbb2bc50991Acfb6c74ae009EE264e4-f9197E3BAE7-4f7e-a34301AD66003D1Ee-4bbb2bc50991Acfb6c74ae009EE264e4-f9197E3BAE7-4f7e-a34301AD66003D1Ee-4bbb2bc50991Acfb6c74ae009EE264e4-f9197E3BAE7-4f7e-a34301AD66003D1Ee-4bbb2bc50991Acfb6c74ae009EE264e4-f9197E3BAE7-4f7e-a34301AD66003D1Ee-4bbb2bc50991Acfb6c74ae009EE264e4-f9197E3BAE7-4f7e-a34301AD66003D1Ee-4bbb2bc50991Acfb6c74ae009EE264e4-f9197E3BAE7-4f7e-a34301AD66003D1Ee-4bbb2bc50991Acfb6c74ae009EE264e4-f9197E3BAE7-4f7e-a34301AD66003D1Ee-4bbb2bc50991Acfb6c74ae009EE264e4-f9197E3BAE7-4f7e-a34301AD66003D1Ee-4bbb2bc50991Acfb6c74ae009EE264e4-f9197E3BAE7-4f7e-a34301AD66003D1Ee-4bbb2bc50991Acfb6c74ae009EE264e4-f9197E3BAE7-4f7e-a34301AD66003D1Ee-4bbb2bc50991Acfb6c74ae009EE264e4-f9197E3BAE7-4f7e-a34301AD66003D1Ee-4bbb2bc50991Acfb6c74ae009EE264e4-f9197E3BAE7-4f7e-a34301AD66003D1Ee-4bbb2bc50991Acfb6c74ae009EE264e4-f9197E3BAE7-4f7e-a34301AD66003D1Ee-4bbb2bc50991Acfb6c74ae009EE264e4-f9197E3BAE7-4f7e-a34301AD66003D1Ee-4bbb2bc50991Acfb6";
	}
    //printf("send string:\n%s\n", g_request.c_str());
}

void InitSendCounterData()
{
	// send string
    g_request = "GET /counter?opt=101&sell_mode=cpm&key_id=11390&price=1000&uniq_id=f73c9e69-9b04-482e-811c-1494ef629d3e&pvid=f2c207f0-c036-4011-b33e-1e5ad33a3e20&date=20220317&clickid=9424af40-3a86-40ae-970d-2a62d59be6a8&creative_id=156451&creative_ad_id=308609&agent_id=0&unit_id=11390&x=440000_999900_02_2_4_9_2_2_05_2,2_&v=13.10.0&avc=878&psid=238&tmplid=10152&deliver_time=1627474164054 HTTP/1.1\r\nContent-Type: application/json\r\nContent-Length: 0\r\nHost: test_server\r\n\r\n";
    //printf("send string:\n%s\n", g_request.c_str());
}

void SendData()
{
    CTcp *pTcp = new CTcp(15680, "127.0.0.1", 500);
    pTcp->set_nodelay();
    pTcp->client_connect();

    struct timeval tval1;
    gettimeofday(&tval1, NULL);
	uint64_t starttime = (uint64_t)(tval1.tv_sec)*1000 + tval1.tv_usec/1000;

	int i=0;
	while(i < g_querySize) {
		pTcp->tcp_writeall(g_request.c_str(), g_request.size());

		int totalsize = 0;
        //while(1) {
			char buf[8096] = { 0 };
			int ret = pTcp->tcp_read(buf, 8096);
			if (ret <= 0) {
				printf("recv error, close tcp scoket ret:%d\n", ret);
				pTcp->tcp_close();
				break;
			}
			// printf("recv data:\n%s\n", buf);
			//totalsize += ret;
			//printf("recv ret:%d data, total:%d\n", ret, totalsize);
		//}
		
		i++;
		//if (i >= 3000) {
		//	printf("send %d times, close tcp scoket\n", i);
		//	pTcp->tcp_close();
		//	break;
		//}
	}

    struct timeval tval2;
    gettimeofday(&tval2, NULL);
	uint64_t endtime = (uint64_t)(tval2.tv_sec)*1000 + tval2.tv_usec/1000;

	printf("THREAD use timems:%lu\n", endtime - starttime);
	return ;
}

