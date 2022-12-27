#include <stdio.h>
#include <string.h>
#include "protocol/co_protocol_http_client.h"
#include "base/co_log.h"
#include "core/co_connection.h"


namespace coserver 
{

const int32_t HTTP_MAX_HEADER_LEN = 8192;


CoProtocolHttpClient::CoProtocolHttpClient()
: m_reqMsg(new CoHTTPRequest())
, m_respMsg(new CoHTTPResponse()) 
{
}

CoProtocolHttpClient::~CoProtocolHttpClient()
{
    SAFE_DELETE(m_reqMsg);
    SAFE_DELETE(m_respMsg);
}

void CoProtocolHttpClient::reset_reqmsg()
{
    SAFE_DELETE(m_reqMsg);
    m_reqMsg = new CoHTTPRequest();
}

void CoProtocolHttpClient::reset_respmsg()
{
    SAFE_DELETE(m_respMsg);
    m_respMsg = new CoHTTPResponse();
}

int32_t CoProtocolHttpClient::encode(CoBuffer* coBuffer) 
{
    CoHTTPRequest* reqMsg = dynamic_cast<CoHTTPRequest* >(m_reqMsg);

    char buffer[ 512 ] = { 0 };
    // start line
    snprintf(buffer, sizeof(buffer), "%s %s %s\r\n", reqMsg->get_method().c_str(), reqMsg->get_url().c_str(), reqMsg->get_version().c_str());
    coBuffer->buffer_append(buffer, strlen(buffer));

    // header Content-Length
    reqMsg->remove_header(CoProtocolHttp::HEADER_CONTENT_LENGTH);
    if(reqMsg->get_contentlength() > 0) {
        reqMsg->add_header(CoProtocolHttp::HEADER_CONTENT_LENGTH, std::to_string(reqMsg->get_contentlength()));
    }

    // header Server
    reqMsg->remove_header(CoProtocolHttp::HEADER_SERVER);
    reqMsg->add_header(CoProtocolHttp::HEADER_SERVER, "coserver/http");

    // header Host
    if (reqMsg->get_headervalue(CoProtocolHttp::HEADER_HOST).empty()) {
        CoConnection* connection = (CoConnection*)(coBuffer->get_userdata());
        reqMsg->add_header(CoProtocolHttp::HEADER_HOST, connection->m_coTcp->get_ipport());
    }

    // add all header
    const std::unordered_map<std::string, std::string> &headers = reqMsg->get_allheader();
    for(auto itr=headers.begin(); itr!=headers.end(); ++itr) {
        snprintf(buffer, sizeof(buffer), "%s: %s\r\n", itr->first.c_str(), itr->second.c_str());
        coBuffer->buffer_append(buffer, strlen(buffer));
    }

    coBuffer->buffer_append("\r\n", strlen("\r\n"));

    // body
    if (reqMsg->get_contentlength() > 0) {
        const std::string &content = reqMsg->get_content();
        coBuffer->buffer_append(content.c_str(), content.length());
    }

    CO_SERVER_LOG_DEBUG("CoProtocolHttpClient encode data:\n%.*s", (int32_t)(coBuffer->get_buffersize()), (const char* )(coBuffer->get_bufferdata()));
    return CO_OK;
}

int32_t CoProtocolHttpClient::decode(CoBuffer* coBuffer) 
{
    CoHTTPResponse* respMsg = dynamic_cast<CoHTTPResponse* >(m_respMsg);
    if (eCompleted == respMsg->m_parseStatus) {
        return CO_OK;
    }

    int32_t ret = CO_AGAIN;
    int32_t parsedLen = 0;
    int32_t len = coBuffer->get_buffersize();
    const void* rawBuffer = coBuffer->get_bufferdata();

    // check
    if (eStartLine == respMsg->m_parseStatus || eHeader == respMsg->m_parseStatus) {
        if (len > HTTP_MAX_HEADER_LEN) {
            CO_SERVER_LOG_ERROR("CoProtocolHttpClient buffer len %d more than MAX header len %d", len, HTTP_MAX_HEADER_LEN);
            coBuffer->reset();
            return eError;
        }
    }

    // parse start line
    if (eStartLine == respMsg->m_parseStatus) {
        parsedLen = parse_startline(respMsg, rawBuffer, len);
        CO_SERVER_LOG_DEBUG("CoProtocolHttpClient parsedLen:%d  bufferlen:%d", parsedLen, len);

        if (parsedLen <= 0) {
            if (parsedLen == -1) {
                ret = eError;
                coBuffer->reset();
            }

            return ret;
        }
        respMsg->m_parseStatus = eHeader;
        CO_SERVER_LOG_DEBUG("CoProtocolHttpClient decode STARTLINE version:%s, statuscode:%d, reasonphrase:%s", respMsg->get_version().c_str(), respMsg->get_statuscode(), respMsg->get_reasonphrase().c_str());
    }

    // parse header
    for(int32_t headerLen = 1; eHeader == respMsg->m_parseStatus && headerLen > 0 && parsedLen < len; parsedLen += headerLen) {
        headerLen = parse_header(respMsg, ((char*)rawBuffer) + parsedLen, len - parsedLen);

        char ch = * (((char*)rawBuffer) + parsedLen);
        if('\r' == ch || '\n' == ch) {
            respMsg->m_parseStatus = eContent;

            const std::string &contentLen = respMsg->get_headervalue(CoProtocolHttp::HEADER_CONTENT_LENGTH);
            if (!contentLen.empty()) {
                // content-length
                respMsg->m_contentRemain = atoi(contentLen.c_str());
                respMsg->reserve_contentlength(respMsg->m_contentRemain);
            } else {
                // chunked
                const std::string &chunked = respMsg->get_headervalue(CoProtocolHttp::HEADER_TRANSFER_ENCODING);
                if (!chunked.empty() && chunked == "chunked") {
                    respMsg->m_contentChunked = 1;
                }
            }

#ifdef CO_LOG_HTTP_DEBUG
            CO_SERVER_LOG_DEBUG("CoProtocolHttpClient decode HEADER Content-Lenght:%d chunked:%d", respMsg->m_contentRemain, respMsg->m_contentChunked);
            const std::unordered_map<std::string, std::string> &resp_headers = respMsg->get_allheader();
            for (auto itr=resp_headers.begin(); itr!=resp_headers.end(); ++itr) {
                CO_SERVER_LOG_DEBUG("headerkey:%s  value:%s", itr->first.c_str(), itr->second.c_str());
            }
#endif
        }
    }

    // parse content
    if(eContent == respMsg->m_parseStatus) {
        if (!respMsg->m_contentChunked) {
            // content-lenght
            parsedLen += parse_content(respMsg, ((char*)rawBuffer) + parsedLen, len - parsedLen);

        } else {
            // http chunked
            int32_t parseRet = parse_content_chunked(respMsg, ((char*)rawBuffer) + parsedLen, len - parsedLen);
            if (parseRet == -1) {
                CO_SERVER_LOG_ERROR("CoProtocolHttpClient chunked content parse failed, %.*s", len - parsedLen, ((char*)rawBuffer) + parsedLen);
                return parseRet;
            }
            parsedLen += parseRet;
        }
    }

    if(eCompleted == respMsg->m_parseStatus) {
        ret = CO_OK;
        CO_SERVER_LOG_DEBUG("CoProtocolHttpClient decode CONTENT %s", respMsg->get_content().c_str());
    }

    coBuffer->buffer_erase(parsedLen);
    return ret;
}

}

