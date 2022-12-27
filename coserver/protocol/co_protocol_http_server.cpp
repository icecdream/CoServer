#include <stdio.h>
#include "base/co_log.h"
#include "core/co_connection.h"
#include "protocol/co_protocol_http_server.h"


namespace coserver 
{

const int32_t HTTP_MAX_HEADER_LEN = 8192;

static std::unordered_map<int32_t, std::string> HTTP_STATUS_LINES = {
    {100, "Continue"},
    {101, "Switching Protocols"},
    {200, "OK"},
    {201, "Created"},
    {202, "Accepted"},
    {203, "Non-Authoritative Information"},
    {204, "No Content"},
    {205, "Reset Content"},
    {206, "Partial Content"},
    {300, "Multiple Choices"},
    {301, "Moved Permanently"},
    {302, "Found"},
    {303, "See Other"},
    {304, "Not Modified"},
    {305, "Use Proxy"},
    {306, "Unused"},
    {307, "Temporary Redirect"},
    {400, "Bad Request"},
    {401, "Unauthorized"},
    {402, "Payment Required"},
    {403, "Forbidden"},
    {404, "Not Found"},
    {405, "Method Not Allowed"},
    {406, "Not Acceptable"},
    {407, "Proxy Authentication Required"},
    {408, "Request Time-out"},
    {409, "Conflict"},
    {410, "Gone"},
    {411, "Length Required"},
    {412, "Precondition Failed"},
    {413, "Request Entity Too Large"},
    {414, "Request-URI Too Large"},
    {415, "Unsupported Media Type"},
    {416, "Requested range not satisfiable"},
    {417, "Expectation Failed"},
    {500, "Internal Server Error"},
    {501, "Not Implemented"},
    {502, "Bad Gateway"},
    {503, "Service Unavailable"},
    {504, "Gateway Time-out"},
    {505, "HTTP Version not supported"}
};


CoProtocolHttpServer::CoProtocolHttpServer()
: m_reqMsg(new CoHTTPRequest())
, m_respMsg(new CoHTTPResponse()) 
{

}

CoProtocolHttpServer::~CoProtocolHttpServer()
{
    SAFE_DELETE(m_reqMsg);
    SAFE_DELETE(m_respMsg);
}

void CoProtocolHttpServer::reset_reqmsg()
{
    SAFE_DELETE(m_reqMsg);
    m_reqMsg = new CoHTTPRequest();
}
    
void CoProtocolHttpServer::reset_respmsg()
{
    SAFE_DELETE(m_respMsg);
    m_respMsg = new CoHTTPResponse();
}

int32_t CoProtocolHttpServer::decode(CoBuffer* coBuffer) 
{
    CoHTTPRequest* reqMsg = dynamic_cast<CoHTTPRequest* >(m_reqMsg);
    if (eCompleted == reqMsg->m_parseStatus) {
        return CO_OK;
    }

    int32_t ret = CO_AGAIN;
    int32_t parsedLen = 0;
    int32_t len = coBuffer->get_buffersize();
    const void* rawBuffer = coBuffer->get_bufferdata();

    // check
    if (eStartLine == reqMsg->m_parseStatus || eHeader == reqMsg->m_parseStatus) {
        if (len > HTTP_MAX_HEADER_LEN) {
            CO_SERVER_LOG_ERROR("CoProtocolHttpServer  buffer len %d more than MAX header len %d", len, HTTP_MAX_HEADER_LEN);
            coBuffer->reset();
            return eError;
        }
    }

    // parse start line
    if (eStartLine == reqMsg->m_parseStatus) {
        parsedLen = parse_startline(reqMsg, rawBuffer, len);
        CO_SERVER_LOG_DEBUG("CoProtocolHttpServer parsedLen:%d  bufferlen:%d", parsedLen, len);

        if (parsedLen <= 0) {
            if (parsedLen == -1) {
                ret = eError;
                coBuffer->reset();
            }

            return ret;
        }
        reqMsg->m_parseStatus = eHeader;
        CO_SERVER_LOG_DEBUG("CoProtocolHttpServer decode STARTLINE method:%s, url:%s uri:%s, version:%s", reqMsg->get_method().c_str(), reqMsg->get_url().c_str(), reqMsg->get_uri().c_str(), reqMsg->get_version().c_str());
#ifdef CO_LOG_HTTP_DEBUG
            CO_SERVER_LOG_DEBUG("CoProtocolHttpServer decode PARAMS");
            const std::unordered_map<std::string, std::string> &reqParams = reqMsg->get_allparam();
            for (auto itr=reqParams.begin(); itr!=reqParams.end(); ++itr) {
                CO_SERVER_LOG_DEBUG("param key:%s  value:%s", itr->first.c_str(), itr->second.c_str());
            }
#endif
    }

    // parse header
    for(int32_t headerLen = 1; eHeader == reqMsg->m_parseStatus && headerLen > 0 && parsedLen < len; parsedLen += headerLen) {
        headerLen = parse_header(reqMsg, ((char*)rawBuffer) + parsedLen, len - parsedLen);

        char ch = * (((char*)rawBuffer) + parsedLen);
        if('\r' == ch || '\n' == ch) {
            reqMsg->m_parseStatus = eContent;

            const std::string &strContentLen = reqMsg->get_headervalue(CoProtocolHttp::HEADER_CONTENT_LENGTH);
            if (!strContentLen.empty()) {
                reqMsg->m_contentRemain = atoi(strContentLen.c_str());
                reqMsg->reserve_contentlength(reqMsg->m_contentRemain);
            }

#ifdef CO_LOG_HTTP_DEBUG
            CO_SERVER_LOG_DEBUG("CoProtocolHttpServer decode HEADER");
            const std::unordered_map<std::string, std::string> &reqHeaders = reqMsg->get_allheader();
            for (auto itr=reqHeaders.begin(); itr!=reqHeaders.end(); ++itr) {
                CO_SERVER_LOG_DEBUG("header key:%s  value:%s", itr->first.c_str(), itr->second.c_str());
            }
#endif
        }
    }

    // parse content
    if(eContent == reqMsg->m_parseStatus) {
        parsedLen += parse_content(reqMsg, ((char*)rawBuffer) + parsedLen, len - parsedLen);
        CO_SERVER_LOG_DEBUG("CoProtocolHttpServer decode CONTENT %s", reqMsg->get_content().c_str());
    }

    if(eCompleted == reqMsg->m_parseStatus) {
        ret = CO_OK;
    }

    coBuffer->buffer_erase(parsedLen);
    return ret;
}

int32_t CoProtocolHttpServer::encode(CoBuffer* coBuffer) 
{
    CoHTTPResponse* respMsg = dynamic_cast<CoHTTPResponse* >(m_respMsg);

    char buffer[ 512 ] = { 0 };
    // start line
    auto itr = HTTP_STATUS_LINES.find(respMsg->get_statuscode());
    if (itr == HTTP_STATUS_LINES.end()) {
        itr = HTTP_STATUS_LINES.find(500);
    }
    snprintf(buffer, sizeof(buffer), "%s %d %s\r\n", respMsg->get_version().c_str(), respMsg->get_statuscode(), itr->second.c_str());
    coBuffer->buffer_append(buffer, strlen(buffer));

    // header Content-Length
    respMsg->remove_header(CoProtocolHttp::HEADER_CONTENT_LENGTH);
    if(respMsg->get_contentlength() > 0) {
        respMsg->add_header(CoProtocolHttp::HEADER_CONTENT_LENGTH, std::to_string(respMsg->get_contentlength()));

    } else {
        // curl校验content-lenght
        respMsg->add_header(CoProtocolHttp::HEADER_CONTENT_LENGTH, "0");
    }

    // header Server
    respMsg->remove_header(CoProtocolHttp::HEADER_SERVER);
    respMsg->add_header(CoProtocolHttp::HEADER_SERVER, "coserver/http");

    // http response not need Host header

    // add all header
    const std::unordered_map<std::string, std::string> &headers = respMsg->get_allheader();
    for(auto itr=headers.begin(); itr!=headers.end(); ++itr) {
        snprintf(buffer, sizeof(buffer), "%s: %s\r\n", itr->first.c_str(), itr->second.c_str());
        coBuffer->buffer_append(buffer, strlen(buffer));
    }

    coBuffer->buffer_append("\r\n", strlen("\r\n"));

    // body
    if (respMsg->get_contentlength() > 0) {
        const std::string &content = respMsg->get_content();
        coBuffer->buffer_append(content.c_str(), content.length());
    }

    CO_SERVER_LOG_DEBUG("CoProtocolHttpServer encode data:\n%.*s", (int32_t)(coBuffer->get_buffersize()), (const char* )coBuffer->get_bufferdata());
    return CO_OK;
}

}

