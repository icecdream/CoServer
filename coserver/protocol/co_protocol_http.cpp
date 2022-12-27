#include <stdio.h>
#include <string.h>
#include "protocol/co_protocol_http.h"
#include "base/co_log.h"


namespace coserver 
{

const std::string CoProtocolHttp::HEADER_CONTENT_LENGTH = "Content-Length";
const std::string CoProtocolHttp::HEADER_CONTENT_TYPE = "Content-Type";
const std::string CoProtocolHttp::HEADER_CONNECTION = "Connection";
const std::string CoProtocolHttp::HEADER_PROXY_CONNECTION = "Proxy-Connection";
const std::string CoProtocolHttp::HEADER_TRANSFER_ENCODING = "Transfer-Encoding";
const std::string CoProtocolHttp::HEADER_DATE = "Date";
const std::string CoProtocolHttp::HEADER_SERVER = "Server";
const std::string CoProtocolHttp::HEADER_HOST = "Host";

const std::string STRING_EMPTY("");

static char* hasStrsep(char* *s, const char* del)
{
    char* d, *tok;

    if (!s || !*s) return NULL;
    tok = *s;
    d = strstr(tok, del);

    if (d) {
        *s = d + strlen(del);
        *d = '\0';
    } else {
        *s = NULL;
    }

    return tok;
}

int32_t parse_startline(CoHTTPResponse* message, const void* buffer, int32_t len)
{
    int32_t lineLen = 0;

    char* pos = (char*)memchr(buffer, '\n', len);
    if(NULL != pos) {
        lineLen = pos - (char*)buffer + 1;

        char* line = (char*)malloc(lineLen + 1);
        memcpy(line, buffer, lineLen);
        line[lineLen] = '\0';

        pos = line;

        char* first, * second;
        first = hasStrsep(&pos, " ");
        second = hasStrsep(&pos, " ");

        if(0 != strncasecmp(line, "HTTP", 4)) {
            lineLen = -1;

        } else {
            if(NULL != first) {
                message->set_version(first);
            }
            if(NULL != second) {
                message->set_statuscode(atoi(second));
            }
            if(NULL != pos) {
                message->set_reasonphrase(strtok(pos, "\r\n"));
            }
        }

        free(line);
    }

    return lineLen;
}

int32_t parse_startline(CoHTTPRequest* message, const void* buffer, int32_t len)
{
    int32_t lineLen = 0;

    char* pos = (char*)memchr(buffer, '\n', len);
    if(NULL != pos) {
        lineLen = pos - (char*)buffer + 1;

        char* line = (char*)malloc(lineLen + 1);
        memcpy(line, buffer, lineLen);
        line[lineLen] = '\0';

        pos = line;

        char* first, * second;
        first = hasStrsep(&pos, " ");
        second = hasStrsep(&pos, " ");

        if(NULL != first) {
            message->set_method(first);
        }
        if(NULL != second) {
            message->set_url(second);
        }
        if(NULL != second) {
            message->set_uri(hasStrsep(&second, "?"));
        }
        if(NULL != pos) {
            message->set_version(strtok(pos, "\r\n"));
        }

        char* params = second;
        for(; NULL != params && '\0' != *params;) {
            char* value = hasStrsep(&params, "&");
            char* name = hasStrsep(&value, "=");
            message->add_param(name, NULL == value ? "" : value);
        }

        free(line);
    }

    return lineLen;
}

int32_t parse_header(CoProtocolHttp* message, const void* buffer, int32_t len)
{
    int32_t lineLen = 0;

    char* pos = (char*)memchr(buffer, '\n', len);
    if(NULL != pos) {
        lineLen = pos - (char*)buffer + 1;

        char* line = (char*)malloc(lineLen + 1);
        memcpy(line, buffer, lineLen);
        line[ lineLen ] = '\0';

        pos = line;

        char* name = hasStrsep(&pos, ":");

        if(NULL != pos) {
            pos = strtok(pos, "\r\n");
            if (NULL != pos) {
                pos += strspn(pos, " ");
                message->add_header(name, pos);
            }
        }

        free(line);
    }

    return lineLen;
}

int32_t parse_content(CoProtocolHttp* message, const void* buffer, int32_t len)
{
    int32_t parsedLen = 0;

    if(message->m_contentRemain > 0) {
        parsedLen = len > message->m_contentRemain ? message->m_contentRemain : len;
        message->append_content(((char*)buffer), parsedLen);
        message->m_contentRemain -= parsedLen;
    }
    
    if (message->m_contentRemain <= 0) {
        message->m_parseStatus = eCompleted;
    }

    return parsedLen;
}

int32_t parse_content_chunked(CoProtocolHttp* message, const void* oribuffer, int32_t orilen)
{
    int32_t parsedLen = 0;
    char* buffer = (char*)oribuffer;
    int32_t len = orilen;

    while (1) {
        char* pos = (char*)memchr(buffer, '\n', len);
        if (NULL == pos) {
            return parsedLen;  // 未找到\n 数据不足
        }

        if (pos == buffer || *(--pos) != '\r') {
            return -1;
        }

        // calc chunked size
        int32_t chunkedSize = 0;
        for (char* s = buffer; s < pos; ++s) {
            char ch = *s;

            if (ch >= '0' && ch <= '9') {
                chunkedSize = chunkedSize * 16 + (ch - '0');
                continue ;
            }

            char c = ch | 0x20;
            if (c >= 'a' && c <= 'f') {
                chunkedSize = chunkedSize * 16 + (c - 'a' + 10);
                continue ;
            }

            return -1;
        }

        pos += 2;   // skip header \r\n
        chunkedSize += 2;  // chunk body \r\n
        int32_t chunkedHeaderLen = pos - buffer;

        if (len < chunkedHeaderLen + chunkedSize) {
#ifdef CO_LOG_HTTP_DEBUG
            CO_SERVER_LOG_DEBUG("http parse chunked not enough data, orilen:%d len:%d headerlen:%d chunkedsize:%d", orilen, len, chunkedHeaderLen, chunkedSize);
#endif
            return parsedLen;
        }
        parsedLen += chunkedHeaderLen + chunkedSize;

        // chunked结束标识  2 -> \r\n
        if (2 == chunkedSize) {
            // chunked数据读取完毕
            message->m_parseStatus = eCompleted;
            return parsedLen;
        }

        // 读取到有效数据
        message->append_content(((char*)pos), chunkedSize - 2); // no need end \r\n

        len = orilen - parsedLen;
        buffer = (char *)oribuffer + parsedLen;
    }

    return parsedLen;
}

void CoProtocolHttp::set_msgstatus(int32_t status)
{
    m_status = status;
}

int32_t CoProtocolHttp::get_msgstatus() 
{
    return m_status;
}

void CoProtocolHttp::add_header(const char* name, const char* value)
{
    if (name && value) {
        add_header(std::string(name), std::string(value));
    }
}

void CoProtocolHttp::add_header(const std::string &name, const std::string &value)
{
    m_headers[name] = value;
}

int32_t CoProtocolHttp::remove_header(const std::string &name) 
{
    auto itr = m_headers.find(name);
    if (itr == m_headers.end()) {
        return 0;
    }

    m_headers.erase(itr);
    return 1;
}

const std::string &CoProtocolHttp::get_headervalue(const std::string &name) const
{
    auto itr = m_headers.find(name);
    if (itr == m_headers.end()) {
        return STRING_EMPTY;
    }

    return itr->second;
}

const std::unordered_map<std::string, std::string> &CoProtocolHttp::get_allheader() const
{
    return m_headers;
}

void CoProtocolHttp::set_msgbody(const std::string &body)
{
    m_content.assign(body);
    m_contentLength = body.length();
}

const std::string&CoProtocolHttp::get_msgbody()
{
    return get_content();
}

void CoProtocolHttp::append_content(const void* content, int32_t length)
{
    m_content.append((const char* )content, length);
    m_contentLength = m_contentLength + length;
}

void CoProtocolHttp::append_content(const std::string &content)
{
    m_content.append(content.c_str(), content.length());
    m_contentLength = m_contentLength + content.length();
}

const std::string &CoProtocolHttp::get_content() const
{
    return m_content;
}

void CoProtocolHttp::set_version(const char* version)
{
    m_version.assign(version);
}

const std::string &CoProtocolHttp::get_version() const
{
    return m_version;
}

void CoProtocolHttp::reserve_contentlength(int32_t contentLength)
{
    m_content.reserve(contentLength);
}

int32_t CoProtocolHttp::get_contentlength() const
{
    return m_contentLength;
}


void CoHTTPRequest::set_method(const std::string &method)
{
    m_method.assign(method);
}

const std::string &CoHTTPRequest::get_method() const
{
    return m_method;
}

void CoHTTPRequest::set_uri(const std::string &uri)
{
    m_uri = uri;
}

const std::string &CoHTTPRequest::get_uri() const
{
    return m_uri;
}

void CoHTTPRequest::set_url(const std::string &url)
{
    m_url = url;
}

const std::string &CoHTTPRequest::get_url() const
{
    return m_url;
}

void CoHTTPRequest::add_param(const char* name, const char* value)
{
    if (name && value) {
        add_param(std::string(name), std::string(value));
    }
}

void CoHTTPRequest::add_param(const std::string &name, const std::string &value)
{
    m_params[name] = value;
}

int32_t CoHTTPRequest::remove_param(const std::string &name)
{
    auto itr = m_params.find(name);
    if (itr == m_params.end()) {
        return 0;
    }

    m_params.erase(itr);
    return 1;
}

const std::string &CoHTTPRequest::get_paramvalue(const std::string &name) const
{
    auto itr = m_params.find(name);
    if (itr == m_params.end()) {
        return STRING_EMPTY;
    }

    return itr->second;
}

const std::unordered_map<std::string, std::string> &CoHTTPRequest::get_allparam() const
{
    return m_params;
}


void CoHTTPResponse::set_statuscode(int32_t statusCode) 
{
    m_statuscode = statusCode;
}

int32_t CoHTTPResponse::get_statuscode() const {
    return m_statuscode;
}

void CoHTTPResponse::set_reasonphrase(const char* reasonPhrase)
{
    m_reasonPhrase = reasonPhrase;
}

const std::string &CoHTTPResponse::get_reasonphrase() const
{
    return m_reasonPhrase;
}

}

