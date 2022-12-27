#include <arpa/inet.h>

#include "protocol/co_protocol_tcp.h"
#include "base/co_log.h"
#include "base/co_config.h"


namespace coserver 
{

CoProtocolTcp::CoProtocolTcp(int32_t type)
: m_type(type)
, m_reqMsg(new CoMsgTcp())
, m_respMsg(new CoMsgTcp()) 
{
}

CoProtocolTcp::~CoProtocolTcp()
{
    SAFE_DELETE(m_reqMsg);
    SAFE_DELETE(m_respMsg);
}

void CoProtocolTcp::reset_reqmsg()
{
    SAFE_DELETE(m_reqMsg);
    m_reqMsg = new CoMsgTcp();
}

void CoProtocolTcp::reset_respmsg()
{
    SAFE_DELETE(m_respMsg);
    m_respMsg = new CoMsgTcp();
}

int32_t CoProtocolTcp::decode(CoBuffer* coBuffer) 
{
    CoMsgTcp* msgTcp = m_type == PROTOCOL_TCP_SERVER ? dynamic_cast<CoMsgTcp *>(m_reqMsg) : dynamic_cast<CoMsgTcp *>(m_respMsg);
    
    int32_t len = coBuffer->get_buffersize();
    // buffer数据长度不足以解析头部数据
    if(len <= (int32_t)sizeof(CoMsgTcpHead)) {
        return CO_AGAIN;
    }

    // check TCP protocol head flag
    CoMsgTcpHead* coMsgTcpHead = (CoMsgTcpHead*)(coBuffer->get_bufferdata());
    if(CO_PROTOCOL_TCP_FLAG != coMsgTcpHead->m_flag) {
        coBuffer->reset();
        return CO_ERROR;
    }
    
    int32_t coMsgTcpHeadLen = sizeof(CoMsgTcpHead);
    int32_t bodyLen = ntohl(coMsgTcpHead->m_length);
    if(bodyLen > (len - coMsgTcpHeadLen)) {
        return CO_AGAIN;
    }

    std::string body = std::string(coMsgTcpHead->m_body, bodyLen);
    msgTcp->set_msgbody(body);
    coBuffer->buffer_erase(bodyLen + coMsgTcpHeadLen);

    return CO_OK;
}

int32_t CoProtocolTcp::encode(CoBuffer* coBuffer) 
{
    CoMsgTcp* msgTcp = m_type == PROTOCOL_TCP_SERVER ? dynamic_cast<CoMsgTcp *>(m_respMsg) : dynamic_cast<CoMsgTcp *>(m_reqMsg);

    CoMsgTcpHead coMsgTcpHead;
    coMsgTcpHead.m_flag = CO_PROTOCOL_TCP_FLAG;
    coMsgTcpHead.m_length = htonl(msgTcp->get_msgbody().length());

    coBuffer->buffer_append((char*)(&coMsgTcpHead), sizeof(CoMsgTcpHead));
    coBuffer->buffer_append(msgTcp->get_msgbody().c_str(), msgTcp->get_msgbody().length());

    return CO_OK;
}

}

