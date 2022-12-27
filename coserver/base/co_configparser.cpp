#include "base/co_configparser.h"
#include "base/co_log.h"
#include "base/co_dns.h"
#include <stdlib.h>
#include <fstream>
#include <sstream>


namespace coserver
{

int32_t g_logLevel = -1;

static bool CheckNumber(const std::string &str);

CoConfigParser::CoConfigParser()
{
}

CoConfigParser::~CoConfigParser()
{
}

int32_t CoConfigParser::parse_configfile(const std::string &configfile)
{
    std::ifstream ifs;
    ifs.open(configfile.c_str());
    if (!ifs.is_open()) {
        CO_SERVER_LOG_ERROR("configfile:%s open failed", configfile.c_str());   
        return CO_ERROR;
    }
    std::string content((std::istreambuf_iterator<char>(ifs)), std::istreambuf_iterator<char>()); 
    ifs.close();

    return parse_content(content);
}

int32_t CoConfigParser::parse_content(const std::string &content)
{
    std::vector<CoBlockArgs> multiBlocks;
    if (read_allblock(content, multiBlocks) != CO_OK) {
        return CO_ERROR;
    }

    for (size_t i = 0; i < multiBlocks.size(); ++i) {
        const CoBlockArgs &blockArgs = multiBlocks[i];

        if (blockArgs.m_blockName == CONF_CONFIG) {
            if (!parse_config_conf(blockArgs)) {
                return CO_ERROR;
            }

        } else if (blockArgs.m_blockName == HOOK_CONFIG) {
            if (!parse_config_hook(blockArgs)) {
                return CO_ERROR;
            }
            
        } else if (blockArgs.m_blockName == SERVER_CONFIG) {
            if (!parse_config_server(blockArgs)) {
                return CO_ERROR;
            }

        } else if (blockArgs.m_blockName == UPSTREAM_CONFIG) {
            if (!parse_config_upstream(blockArgs)) {
                return CO_ERROR;
            }
            
        } else {
            CO_SERVER_LOG_WARN("parameter '%s' unexpected: %d", blockArgs.m_blockName.c_str(), blockArgs.m_lineno);
        }
    }

    return CO_OK;
}

int32_t CoConfigParser::read_allblock(const std::string &content, std::vector<CoBlockArgs> &multiBlocks)
{
    int32_t nest = 0;
    int32_t currLineno = 0;
    char* begin, *end, *fin;

    std::string line;
    CoBlockArgs blockArgs;  // temp block
    CoLineArgs lineArgs;    // temp line m_args

    std::istringstream is(content);
    if (!is.good()) { 
        return CO_ERROR; 
    }

    while(!is.eof()) { // file
        size_t i = 0;
        bool find = false;
        blockArgs.m_blockName.clear();
        blockArgs.m_multiLineArgs.clear();

        while(!is.eof() && getline(is, line)) { // find block 
            ++currLineno;
            lineArgs.m_args.clear();
            lineArgs.m_lineno = currLineno;
            begin = end = fin = NULL;

            for (i = 0; i < line.size(); ++i) { // find line m_args
                char* ch = &line[i];
                switch (*ch) {
                case '#':
                    i = line.size() - 1;
                    break;
                case ' ':
                case '\t':
                    end = ch - 1;
                    break;
                case ';':
                    end = ch - 1;
                    fin = ch;
                    break;
                case '{':
                    ++nest;
                    end = ch - 1;
                    fin = ch;
                    break;
                case '}':
                    --nest;
                    end = ch - 1;
                    fin = ch;
                    break;
                default:
                    if (fin) {
                        CO_SERVER_LOG_ERROR("character '%c' unexpected after '%c': %d:%lu", *ch, *fin, currLineno, i);
                        return CO_ERROR;
                    }
                    if (!begin) {
                        begin = ch; 
                        end = NULL;
                    }
                    break;
                }

                if (nest < 0) {
                    CO_SERVER_LOG_ERROR("character '}' unexpected: %d:%lu", currLineno, i);
                    return CO_ERROR;
                }

                if (begin && end) {
                    find = true;
                    lineArgs.m_args.emplace_back(std::string(begin, end - begin + 1));
                    begin = NULL;
                }
            }

            if (!fin) {
                if(begin) {     // not fin but has character
                    CO_SERVER_LOG_ERROR("unexpected end, maybe miss character ';': %d:%lu", currLineno, i);
                    return CO_ERROR;
                }
                //empty line
            } else if (lineArgs.m_args.empty()) {
                if (*fin != '}') {  // fin but no has character
                    CO_SERVER_LOG_ERROR("character ';' unexpected: %d:%lu", currLineno, i);
                    return CO_ERROR;
                }
            }

            if (!lineArgs.m_args.empty()) {
                blockArgs.m_multiLineArgs.emplace_back(lineArgs);
            }

            if (nest == 0 && find) {
                break;
            }
        }

        if (nest > 0) {     // 0 or >0 only
            CO_SERVER_LOG_ERROR("character '}' unexpected: %d", currLineno);
            return CO_ERROR;
        }

        if (find) {
            blockArgs.m_blockName = blockArgs.m_multiLineArgs[0].m_args[0];
            blockArgs.m_lineno = blockArgs.m_multiLineArgs[0].m_lineno;
            multiBlocks.emplace_back(blockArgs);
        }
    }

    return CO_OK;
}

bool CoConfigParser::parse_config_conf(const CoBlockArgs &blockArgs)
{
    CoConf &conf = m_config.m_conf;

    for (size_t i = 1; i < blockArgs.m_multiLineArgs.size(); ++i) {    // start 1
        const CoLineArgs &lineArgs = blockArgs.m_multiLineArgs[i];
        const std::string &configKey = lineArgs.m_args[0];

        if (configKey == "log_level") {
            if (lineArgs.m_args.size() != 2) {
                CO_SERVER_LOG_ERROR("parameter number error: %d", lineArgs.m_lineno);
                return false;
            }
            conf.m_logLevel = atoi(lineArgs.m_args[1].c_str());

        } else if (configKey == "worker_threads") {
            if (lineArgs.m_args.size() != 2) {
                CO_SERVER_LOG_ERROR("parameter number error: %d", lineArgs.m_lineno);
                return false;
            }
            conf.m_workerThreads = atoi(lineArgs.m_args[1].c_str());

        } else {
            CO_SERVER_LOG_WARN("unknow parameter '%s': %d", configKey.c_str(), lineArgs.m_lineno);         
        }
    }

    g_logLevel = conf.m_logLevel;
    return true;
}

bool CoConfigParser::parse_config_hook(const CoBlockArgs &blockArgs)
{
    CoConfHook &confHook = m_config.m_confHook;

    for (size_t i = 1; i < blockArgs.m_multiLineArgs.size(); ++i) {    // start 1
        const CoLineArgs &lineArgs = blockArgs.m_multiLineArgs[i];
        const std::string &configKey = lineArgs.m_args[0];

        if (configKey == "mutex_rety_time") {
            if (lineArgs.m_args.size() != 2) {
                CO_SERVER_LOG_ERROR("parameter number error: %d", lineArgs.m_lineno);
                return false;
            }
            confHook.m_mutexRetryTime = atoi(lineArgs.m_args[1].c_str());

        } else {
            CO_SERVER_LOG_WARN("unknow parameter '%s': %d", configKey.c_str(), lineArgs.m_lineno);         
        }
    }

    return true;
}

bool CoConfigParser::parse_config_server(const CoBlockArgs &blockArgs)
{
    CoConfServer* configServer = new CoConfServer;

    for (size_t i = 1; i < blockArgs.m_multiLineArgs.size(); ++i) {     // start 1
        const CoLineArgs &lineArgs = blockArgs.m_multiLineArgs[i];
        const std::string &configKey = lineArgs.m_args[0];

        if (configKey == "listen_ip") {
            if (lineArgs.m_args.size() != 2) {
                CO_SERVER_LOG_ERROR("parameter number error: %d", lineArgs.m_lineno);
                return false;
            }
            configServer->m_listenIP = lineArgs.m_args[1];

        } else if (configKey == "listen_port") {
            if (lineArgs.m_args.size() != 2) {
                CO_SERVER_LOG_ERROR("parameter number error: %d", lineArgs.m_lineno);
                return false;
            }
            if (!CheckNumber(lineArgs.m_args[1])) {
                CO_SERVER_LOG_ERROR("'%s' unexpected: %d", lineArgs.m_args[1].c_str(), lineArgs.m_lineno);
                return false;
            }
            configServer->m_listenPort = atoi(lineArgs.m_args[1].c_str());

        } else if (configKey == "max_connections") {
            if (lineArgs.m_args.size() != 2) {
                CO_SERVER_LOG_ERROR("parameter number error: %d", lineArgs.m_lineno);
                return false;
            }
            if (!CheckNumber(lineArgs.m_args[1])) {
                CO_SERVER_LOG_ERROR("'%s' unexpected: %d", lineArgs.m_args[1].c_str(), lineArgs.m_lineno);
                return false;
            }
            configServer->m_maxConnections = atoi(lineArgs.m_args[1].c_str());

        } else if (configKey == "keepalive_timeout") {
            if (lineArgs.m_args.size() != 2) {
                CO_SERVER_LOG_ERROR("parameter number error: %d", lineArgs.m_lineno);
                return false;
            }
            if (!CheckNumber(lineArgs.m_args[1])) {
                CO_SERVER_LOG_ERROR("'%s' unexpected: %d", lineArgs.m_args[1].c_str(), lineArgs.m_lineno);
                return false;
            }
            configServer->m_keepaliveTimeout = atoi(lineArgs.m_args[1].c_str());

        } else if (configKey == "read_timeout") {
            if (lineArgs.m_args.size() != 2) {
                CO_SERVER_LOG_ERROR("parameter number error: %d", lineArgs.m_lineno);
                return false;
            }
            if (!CheckNumber(lineArgs.m_args[1])) {
                CO_SERVER_LOG_ERROR("'%s' unexpected: %d", lineArgs.m_args[1].c_str(), lineArgs.m_lineno);
                return false;
            }
            configServer->m_readTimeout = atoi(lineArgs.m_args[1].c_str());

        } else if (configKey == "write_timeout") {
            if (lineArgs.m_args.size() != 2) {
                CO_SERVER_LOG_ERROR("parameter number error: %d", lineArgs.m_lineno);
                return false;
            }
            if (!CheckNumber(lineArgs.m_args[1])) {
                CO_SERVER_LOG_ERROR("'%s' unexpected: %d", lineArgs.m_args[1].c_str(), lineArgs.m_lineno);
                return false;
            }
            configServer->m_writeTimeout = atoi(lineArgs.m_args[1].c_str());

        } else if (configKey == "server_type") {
            if (lineArgs.m_args.size() != 2) {
                CO_SERVER_LOG_ERROR("parameter number error: %d", lineArgs.m_lineno);
                return false;
            }
            if (!CheckNumber(lineArgs.m_args[1])) {
                CO_SERVER_LOG_ERROR("'%s' unexpected: %d", lineArgs.m_args[1].c_str(), lineArgs.m_lineno);
                return false;
            }

            int32_t serverType = atoi(lineArgs.m_args[1].c_str());
            if (serverType >= PROTOCOL_MAX) {
                CO_SERVER_LOG_ERROR("server_type '%s' too large, unexpected: %d", lineArgs.m_args[1].c_str(), lineArgs.m_lineno);
                return false;
            }
            configServer->m_serverType = serverType;

        } else if (configKey == "handler_name") {
            if (lineArgs.m_args.size() != 2) {
                CO_SERVER_LOG_ERROR("parameter number error: %d", lineArgs.m_lineno);
                return false;
            }
            configServer->m_handlerName = lineArgs.m_args[1];

        } else {
            CO_SERVER_LOG_WARN("unknow parameter '%s': %d", configKey.c_str(), lineArgs.m_lineno);         
        }
    }

    if (configServer->m_listenPort == 0) {
        CO_SERVER_LOG_ERROR("'listenPort' expected at server block: %d", blockArgs.m_lineno);
        return false;
    }

    m_config.m_confServers.emplace_back(configServer);
    return true;
}

bool CoConfigParser::parse_config_upstream(const CoBlockArgs &blockArgs)
{
    CoConfUpstream* confUpstream = new CoConfUpstream;

    for (size_t i = 0; i < blockArgs.m_multiLineArgs.size(); ++i) {     // start 0
        const CoLineArgs &lineArgs = blockArgs.m_multiLineArgs[i];
        const std::string &configKey = lineArgs.m_args[0];

        if (configKey == "upstream") {
            if (lineArgs.m_args.size() != 2) {
                CO_SERVER_LOG_ERROR("parameter number error: %d", lineArgs.m_lineno);
                return false;
            }
            confUpstream->m_name = lineArgs.m_args[1];

        } else if (configKey == "server") {
            if (lineArgs.m_args.size() < 2) {
                CO_SERVER_LOG_ERROR("parameter number error: %d", lineArgs.m_lineno);
                return false;
            }

            int32_t weight = 1;
            std::string host = lineArgs.m_args[1];
            std::string port("0");

            size_t pos = host.find(':');
            if (pos != std::string::npos) {
                port = host.substr(pos+1);
                host.resize(pos);
            }
            if (!CheckNumber(port)) {
                CO_SERVER_LOG_ERROR("'%s' unexpected: %d", port.c_str(), lineArgs.m_lineno);
                return false;
            }
            int32_t portNum = atoi(port.c_str());

            for (size_t j=2; j<lineArgs.m_args.size(); ++j) {
                if (lineArgs.m_args[j].find("weight=") != std::string::npos) {
                    size_t pos = lineArgs.m_args[j].find("weight=");
                    std::string weightvalue = lineArgs.m_args[j].substr(pos+7);
                    if (!CheckNumber(weightvalue)) {
                        CO_SERVER_LOG_ERROR("'%s' unexpected: %d", weightvalue.c_str(), lineArgs.m_lineno);
                        return false;
                    }
                    weight = atoi(weightvalue.c_str());

                } else {
                    CO_SERVER_LOG_ERROR("'%s' unexpected: %d", lineArgs.m_args[2].c_str(), lineArgs.m_lineno);
                    return false;
                }
            }

            if (host.length() > 0 && isalpha(host.at(0))) {
                // dns解析ip地址
                std::vector<std::string> ips;
                if(CO_OK == resolve_dns(host, ips)) {
                    for (size_t i=0; i<ips.size(); ++i) {
                        host = ips[i];
                        confUpstream->m_upstreamServers.emplace_back(CoConfUpstreamServer(host, portNum, weight));
                    }
                }

            } else {
                confUpstream->m_upstreamServers.emplace_back(CoConfUpstreamServer(host, portNum, weight));
            }

        } else if (configKey == "max_connections") {
            if (lineArgs.m_args.size() != 2) {
                CO_SERVER_LOG_ERROR("parameter number error: %d", lineArgs.m_lineno);
                return false;
            }
            if (!CheckNumber(lineArgs.m_args[1])) {
                CO_SERVER_LOG_ERROR("'%s' unexpected: %d", lineArgs.m_args[1].c_str(), lineArgs.m_lineno);
                return false;
            }
            confUpstream->m_maxConnections = atoi(lineArgs.m_args[1].c_str());

        } else if (configKey == "load_balance") {
            if (lineArgs.m_args.size() != 2) {
                CO_SERVER_LOG_ERROR("parameter number error: %d", lineArgs.m_lineno);
                return false;
            }
            confUpstream->m_loadBalance = atoi(lineArgs.m_args[1].c_str());
        
        } else if (configKey == "connect_timeout") {
            if (lineArgs.m_args.size() != 2) {
                CO_SERVER_LOG_ERROR("parameter number error: %d", lineArgs.m_lineno);
                return false;
            }
            confUpstream->m_connTimeout = atoi(lineArgs.m_args[1].c_str());

        } else if (configKey == "read_timeout") {
            if (lineArgs.m_args.size() != 2) {
                CO_SERVER_LOG_ERROR("parameter number error: %d", lineArgs.m_lineno);
                return false;
            }
            confUpstream->m_readTimeout = atoi(lineArgs.m_args[1].c_str());

        } else if (configKey == "write_timeout") {
            if (lineArgs.m_args.size() != 2) {
                CO_SERVER_LOG_ERROR("parameter number error: %d", lineArgs.m_lineno);
                return false;
            }
            confUpstream->m_writeTimeout = atoi(lineArgs.m_args[1].c_str());

        } else if (configKey == "keepalive_timeout") {
            if (lineArgs.m_args.size() != 2) {
                CO_SERVER_LOG_ERROR("parameter number error: %d", lineArgs.m_lineno);
                return false;
            }
            confUpstream->m_keepaliveTimeout = atoi(lineArgs.m_args[1].c_str());

        } else if (configKey == "connection_maxrequest") {
            if (lineArgs.m_args.size() != 2) {
                CO_SERVER_LOG_ERROR("parameter number error: %d", lineArgs.m_lineno);
                return false;
            }
            confUpstream->m_connectionMaxRequest = atoi(lineArgs.m_args[1].c_str());

        } else if (configKey == "connection_maxtime") {
            if (lineArgs.m_args.size() != 2) {
                CO_SERVER_LOG_ERROR("parameter number error: %d", lineArgs.m_lineno);
                return false;
            }
            confUpstream->m_connectionMaxTime = atoi(lineArgs.m_args[1].c_str());

        } else if (configKey == "fail_timeout") {
            if (lineArgs.m_args.size() != 2) {
                CO_SERVER_LOG_ERROR("parameter number error: %d", lineArgs.m_lineno);
                return false;
            }
            confUpstream->m_failTimeout = atoi(lineArgs.m_args[1].c_str());

        } else if (configKey == "fail_maxnum") {
            if (lineArgs.m_args.size() != 2) {
                CO_SERVER_LOG_ERROR("parameter number error: %d", lineArgs.m_lineno);
                return false;
            }
            confUpstream->m_failMaxnum = atoi(lineArgs.m_args[1].c_str());

        } else if (configKey == "retry_maxnum") {
            if (lineArgs.m_args.size() != 2) {
                CO_SERVER_LOG_ERROR("parameter number error: %d", lineArgs.m_lineno);
                return false;
            }
            confUpstream->m_retryMaxnum = atoi(lineArgs.m_args[1].c_str());

        } else {
            CO_SERVER_LOG_ERROR("unknow parameter '%s': %d", configKey.c_str(), lineArgs.m_lineno);         
        }
    }
    
    m_config.m_confUpstreams.emplace_back(confUpstream);
    return true;
}

bool CheckNumber(const std::string &str)
{
    for (size_t i = 0; i < str.size(); ++i) {
        if (str[i] > '9' || str[i] < '0') {
            return false;
        }
    }

    return true;
}

}

