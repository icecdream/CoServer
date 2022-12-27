#ifndef _CO_PROTOCOL_HTTP_H_
#define _CO_PROTOCOL_HTTP_H_

#include <unordered_map>
#include "protocol/co_protocol.h"


namespace coserver
{

enum { eStartLine, eHeader, eContent, eCompleted, eError };


class CoProtocolHttp : public CoMsg {
public:
    static const std::string HEADER_CONTENT_LENGTH;
    static const std::string HEADER_CONTENT_TYPE;
    static const std::string HEADER_CONNECTION;
    static const std::string HEADER_PROXY_CONNECTION;
    static const std::string HEADER_TRANSFER_ENCODING;
    static const std::string HEADER_DATE;
    static const std::string HEADER_SERVER;
    static const std::string HEADER_HOST;

public:
    CoProtocolHttp() {}
    virtual ~CoProtocolHttp() {}

    virtual void set_msgstatus(int32_t status);
    virtual int32_t get_msgstatus();

    void add_header(const char* name, const char* value);
    void add_header(const std::string &name, const std::string &value);
    int32_t remove_header(const std::string &name);
    const std::string &get_headervalue(const std::string &name) const;
    const std::unordered_map<std::string, std::string> &get_allheader() const;

    void append_content(const void* content, int32_t length);
    void append_content(const std::string &content);
    const std::string &get_content() const;
    virtual void set_msgbody(const std::string &body);
    virtual const std::string &get_msgbody();

    void set_version(const char* version);
    const std::string &get_version() const;

    void reserve_contentlength(int32_t contentLength);
    int32_t get_contentlength() const;


public:
    int32_t m_status            = 0;
    int32_t m_parseStatus       = eStartLine;
    int32_t m_contentRemain     = 0;
    int32_t m_contentChunked    = 0;    // 是否chunked模式

protected:
    std::string m_version       = "HTTP/1.1";
    int32_t m_contentLength     = 0;    // content length

    std::string m_content       = "";   // content
    std::unordered_map<std::string, std::string> m_headers; // headers
};


class CoHTTPRequest : public CoProtocolHttp
{
public:
    CoHTTPRequest() {}
    virtual ~CoHTTPRequest() {}

public:
    void set_method(const std::string &method);
    const std::string &get_method() const;

    void set_uri(const std::string &uri);
    const std::string &get_uri() const;

    void set_url(const std::string &url);
    const std::string &get_url() const;

    void add_param(const char* name, const char* value);
    void add_param(const std::string &name, const std::string &value);
    int32_t remove_param(const std::string &name);
    const std::string &get_paramvalue(const std::string &name) const;

    const std::unordered_map<std::string, std::string> &get_allparam() const;

private:
    std::string m_uri        = "";
    std::string m_url        = "";
    std::string m_method     = "";
    std::unordered_map<std::string, std::string> m_params;
};


class CoHTTPResponse : public CoProtocolHttp
{
public:
    CoHTTPResponse() {}
    virtual ~CoHTTPResponse() {}

public:
    void set_statuscode(int32_t statusCode);
    int32_t get_statuscode() const;

    void set_reasonphrase(const char* reasonPhrase);
    const std::string &get_reasonphrase() const;

private:
    int32_t m_statuscode        = 200;
    std::string m_reasonPhrase  = "";
};

// parse HTTP func
int32_t parse_startline(CoHTTPResponse* message, const void* buffer, int32_t len);
int32_t parse_startline(CoHTTPRequest* message, const void* buffer, int32_t len);
int32_t parse_header(CoProtocolHttp* message, const void* buffer, int32_t len);
int32_t parse_content(CoProtocolHttp* message, const void* buffer, int32_t len);
int32_t parse_content_chunked(CoProtocolHttp* message, const void* buffer, int32_t len);

}

#endif //_CO_PROTOCOL_HTTP_H_

