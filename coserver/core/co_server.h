#ifndef _CO_SERVER_H_
#define _CO_SERVER_H_

#include "core/co_cycle.h"
#include "core/co_request.h"
#include "base/co_configparser.h"


namespace coserver
{

class CoServer
{
public:
    CoServer();
    ~CoServer();

    /*
        函数功能: 添加server对应的处理函数

        参数: 
            handlerName: 配置文件中server块下的handler_name名称
            userProcess: server新请求时的回调 处理函数
            userDestroy: server请求结束时的回调 处理函数
            userData: 用户信息 回调函数时的参数
    */
    void    add_user_handlers(const std::string &handlerName, CoFuncUserProcess userProcess, CoFuncUserDestroy userDestroy, void* userData = NULL);
   
    /*
        函数功能: 运行服务

        参数: 
            configFilename: 配置文件绝对路径
            useCurThreadServer: 是否使用当前线程作为server线程中的一个, 配置多线程时 
                参数为0 会新建N个线程
                参数为1 会新建N-1个线程+当前线程

        返回值: CO_OK成功 其他错误
    */
    int32_t run_server(const std::string &configFilename, int32_t useCurThreadServer = 1);

    /*
        函数功能: 停止服务

        返回值: CO_OK成功 其他错误
    */
    int32_t shut_down();

    /*
        函数功能: 运行一个server线程

        参数: 
            index: 第index个线程
    */
    void run_server_thread(int32_t index);


private:
    CoConfigParser* m_configParser = NULL;

    std::vector<std::thread> m_workerThreads;
    std::vector<CoDispatcher*> m_workerDispatchers;
};

}

#endif //_CO_SERVER_H_

