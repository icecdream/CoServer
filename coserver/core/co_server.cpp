#include <sys/prctl.h>
#include "core/co_server.h"
#include "base/co_log.h"


namespace coserver
{

extern std::unordered_map<std::string, CoUserFuncs*>  g_userFuncs;

CoServer::CoServer()
{
}

CoServer::~CoServer()
{
    shut_down();

    for (auto &itr : g_userFuncs) {
        SAFE_DELETE(itr.second);
    }
    g_userFuncs.clear();

    SAFE_DELETE(m_configParser);
}

void CoServer::add_user_handlers(const std::string &handlerName, CoFuncUserProcess userProcess, CoFuncUserDestroy userDestroy, void* userData)
{
    CoUserFuncs* userFunc = new CoUserFuncs;
    userFunc->m_userProcess = userProcess;
    userFunc->m_userDestroy = userDestroy;
    userFunc->m_userData = userData;

    g_userFuncs[handlerName] = userFunc;
}

int32_t CoServer::run_server(const std::string &configFilename, int32_t useCurThreadServer)
{
#ifdef SIGPIPE
    signal(SIGPIPE, SIG_IGN);
#endif

    // 解析配置文件 放在最前面 否则read读取配置文件 会使用hook 还没有完全初始化完成
    m_configParser = new CoConfigParser;
    int32_t ret = m_configParser->parse_configfile(configFilename);
    if (ret != CO_OK) {
        CO_SERVER_LOG_ERROR("parse config failed, filename:%s ret:%d", configFilename.c_str(), ret);
        return ret;
    }
    const CoConfig* conf = m_configParser->get_config();

    // start worker threads
    int32_t threadSize = conf->m_conf.m_workerThreads;
    m_workerDispatchers.resize(threadSize);
    m_workerThreads.reserve(threadSize);

    for (int32_t i=0; i<threadSize; ++i) {
        if (i == (threadSize - 1) && 1 == useCurThreadServer) {
            run_server_thread(i);

        } else {
            m_workerThreads.push_back(std::thread([this, i]() {
                std::string threadName = "coserver_" + std::to_string(i);
                prctl(PR_SET_NAME, threadName.c_str(), 0, 0, 0);
                this->run_server_thread(i);
            }));
        }
    }

    return CO_OK;
}

void CoServer::run_server_thread(int32_t index)
{
    // init thread local cycle, hook判断是否有cycle 即可知道是内部线程还是外部线程
    CoThreadLocalInfo* threadInfo = GET_TLS();
    CoCycle* &tlCoCycle = threadInfo->m_coCycle;
    tlCoCycle = new CoCycle;
    tlCoCycle->m_conf = m_configParser->get_config();

    // calc need connection
    int32_t maxConnectionSize = 4;
    for (auto &itr : tlCoCycle->m_conf->m_confServers) {
        CoConfServer* confServer = itr;
        maxConnectionSize += confServer->m_maxConnections;
    }
    for (auto &itr : tlCoCycle->m_conf->m_confUpstreams) {
        CoConfUpstream* confUpstream = itr;
        maxConnectionSize += confUpstream->m_maxConnections;
    }
    int32_t minConnectionSize = maxConnectionSize / 4;

    // init connections
    tlCoCycle->m_connectionPool = new CoConnectionPool(tlCoCycle);
    int32_t ret = tlCoCycle->m_connectionPool->init(minConnectionSize, maxConnectionSize);
    if (ret != CO_OK) {
        CO_SERVER_LOG_ERROR("connectionpool init failed, minsize:%d maxsize:%d ret:%d", minConnectionSize, maxConnectionSize, ret);
        exit(-1);
    }

    // init epoll
    tlCoCycle->m_coEpoll = new CoEpoll;
    ret = tlCoCycle->m_coEpoll->init(maxConnectionSize);
    if (ret != CO_OK) {
        CO_SERVER_LOG_ERROR("epoll init failed, connections max:%d ret:%d", maxConnectionSize, ret);
        exit(-1);
    }

    // init timer
    tlCoCycle->m_timer = new CoTimer;

    // init coroutine
    tlCoCycle->m_coCoroutineMain = new CoCoroutineMain;
    ret = tlCoCycle->m_coCoroutineMain->init(1024 * 1024);
    if (ret != CO_OK) {
        CO_SERVER_LOG_ERROR("coroutine init failed, shared stack size:%d ret:%d", 1024 * 1024, ret);
        exit(-1);
    }

    // init upstreams
    tlCoCycle->m_upstreamPool = new CoUpstreamPool;
    ret = tlCoCycle->m_upstreamPool->init(tlCoCycle);
    if (ret != CO_OK) {
        CO_SERVER_LOG_ERROR("upstreampool init failed, ret:%d", ret);
        exit(-1);
    }
    
    // init procotol
    tlCoCycle->m_protocolFactory = new CoProtocolFactory();

    // init dispatcher
    tlCoCycle->m_dispatcher = new CoDispatcher;
    ret = tlCoCycle->m_dispatcher->init(tlCoCycle);
    if (ret != CO_OK) {
        CO_SERVER_LOG_ERROR("dispacther init failed, ret:%d", ret);
        exit(-1);
    }
    m_workerDispatchers[index] = tlCoCycle->m_dispatcher;

    // run forever
    tlCoCycle->m_dispatcher->start(tlCoCycle);
    return ;
}

int32_t CoServer::shut_down()
{
    for (auto &dispatcher : m_workerDispatchers) {
        // 防止还未添加真正的dispatcher 就shut down
        while (!dispatcher) {
            CO_SERVER_LOG_WARN("coserver shutdown, dispacther not init complete, wait 1 ms");
            usleep(1000);   // sleep 1ms
        }
        dispatcher->stop();
    }
    m_workerDispatchers.clear();

    for (auto &thread : m_workerThreads) {
        thread.join();
    }
    m_workerThreads.clear();

    return CO_OK;
}

}

