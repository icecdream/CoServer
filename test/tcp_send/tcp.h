#ifndef _TCP_H_
#define _TCP_H_

#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <errno.h>
#include <string>
#include <strings.h>
#include <fcntl.h>
#include <sys/types.h>
#include <string.h>
#include <stdio.h>
#include <netinet/tcp.h>

#include "global.h"

#define TCP_DISCONNECT	0


class CTcp
{
public:
	CTcp(const USHORT port = 0, const std::string& ip = "", const int timeoutms = 0);
	virtual ~CTcp() throw();


	void set_ip(const std::string& ip);
	void set_port(const USHORT port);
	bool set_block();
	bool set_nonblock();
	void set_bconnect(bool bconnect);
	bool set_timeoutms(int timeoutms);
	static int set_limit(int limit);
	int set_send_buffer(UINT size);
	int set_recv_buffer(UINT size);
	int set_nodelay();
	int set_reused();

	void get_ip(std::string& strIp);
	USHORT get_port();
	int get_fd();
	bool get_bconnect();
	int get_timeout();
	uint64_t get_connect_time();

	bool server_bind();
	bool server_listen(const int request_num = 1/*SOMAXCONN*/);
	CTcp* server_accept();

	int client_connect();
	int client_reconnect();

	int tcp_read(void* const readbuf, const UINT bufsize);
	int tcp_readall(void* const readbuf, const UINT readsize);
	int tcp_write(const void* sendbuf, const UINT bufsize);
	int tcp_writeall(const void* sendbuf, const UINT bufsize);

	bool tcp_close();
	bool tcp_reset();

protected:
	CTcp(const int fd);

private:
	int m_fd = -1;

	std::string m_ip;
	USHORT m_port;

	int m_timeoutms = 0;
	uint64_t m_connTimems = 0;

	bool m_bconnect;
};

#endif /* _TCP_H_ */
