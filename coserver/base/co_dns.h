#ifndef _CO_DNS_H_
#define _CO_DNS_H_

#include <string>
#include <vector>
#include <netdb.h>
#include <arpa/inet.h>
#include "base/co_log.h"
#include "base/co_common.h"


namespace coserver
{

const int32_t IP_SIZE = 16;

int32_t resolve_dns(const std::string &dns, std::vector<std::string> &ips)
{
    hostent* h = gethostbyname(dns.c_str());
    if (h == NULL || h->h_addr_list[0] == NULL) {
        CO_SERVER_LOG_ERROR("dns:%s not found", dns.c_str());
        switch (h_errno) {
            case HOST_NOT_FOUND:
                CO_SERVER_LOG_ERROR("the host was not found");
                break;
            case NO_ADDRESS:
                CO_SERVER_LOG_ERROR("the name is valid but it has no address");
                break;
            case NO_RECOVERY:
                CO_SERVER_LOG_ERROR("a non-recoverable name server error occurred");
                break;
            case TRY_AGAIN:
                CO_SERVER_LOG_ERROR("the name server is temporarily unavailable");
                break;
        }
        return CO_ERROR;
    }

    for (char** pptr = h->h_addr_list; *pptr != NULL; pptr++) {
        char ip[IP_SIZE] = {'0'};
        if (NULL != inet_ntop(h->h_addrtype, *pptr, ip, IP_SIZE)) {
            ips.emplace_back(std::string(ip));
            CO_SERVER_LOG_INFO("dns:%s resolve ip:%s", dns.c_str(), ip);
        }
    }

    return CO_OK;
}

}

#endif  // _CO_DNS_H_

