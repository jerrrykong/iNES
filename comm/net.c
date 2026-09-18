#include "net.h"
#include "log.h"
#ifdef WIN32
#include <WinSock2.h>
//#define  EINPROGRESS    WSAEINPROGRESS
typedef SOCKET  socket_t;
#elif defined(INES_POSIX)
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <memory.h>
#include <signal.h>
#include <errno.h>

#define  closesocket   close
#define INVALID_SOCKET  -1
typedef  int  socket_t;

#endif



static  socket_t  s_sock_listen = INVALID_SOCKET;
static  socket_t  s_sock_conn = INVALID_SOCKET;

// 监听 socket 的实际端口(net_listen 传 0 时由系统分配, 需靠它取回后告知对端)
static  net_port_t  s_listen_port = 0;

static char  s_recv_buffer[1024];
static int   s_recv_len = 0; 
//static char  s_send_buffer[1024];
//static int   s_send_len = 0; 

#define NET_MAX_ERROR    1024
static ines_char_t  s_net_last_err[NET_MAX_ERROR];

static  char* __t2a(ines_str_t ts)
{
#ifdef WIN32
#ifdef UNICODE
	static char out[1024];
	wcstombs(out, ts,  sizeof(out));
	return out;
#else
	return (char*)ts;
#endif
#elif defined(INES_POSIX)
	return (char*)ts;
#endif
}

ines_cstr_t   net_get_last_error()
{
	return s_net_last_err;
}

int net_init()
{
#ifdef WIN32
	WSADATA  wsadata;
	memset(&wsadata, 0, sizeof(wsadata));
	if(0 != WSAStartup(MAKEWORD(2,2), &wsadata))
		return -1;
#elif defined(INES_POSIX)
	signal(SIGPIPE, SIG_IGN);
#endif
	s_sock_listen = INVALID_SOCKET;
	s_sock_conn = INVALID_SOCKET;
	s_listen_port = 0;
	s_recv_len = 0;
	s_net_last_err[0] = 0;
	return 0;
}

static int net_get_error_code()
{
#ifdef WIN32
	return WSAGetLastError();
#elif defined(INES_POSIX)
	return errno;
#endif
}



static ines_cstr_t   net_strerror(int errCode)
{
#ifdef WIN32
	static ines_char_t  desc[512];
	if(!FormatMessage(FORMAT_MESSAGE_FROM_SYSTEM,  0, errCode, 0, desc, 511, NULL))
		return ISTR("");
	return desc;
#elif defined(INES_POSIX)
	return strerror(errCode);
#endif
}

static ines_cstr_t   net_error()
{
	int nerr = net_get_error_code();
	ines_snprintf(s_net_last_err, NET_MAX_ERROR, ISTR("(%d) %s"), nerr, net_strerror(nerr));
	return s_net_last_err;
}


static int net_socket_error(socket_t sock)
{
	int errorCode = 0;
#ifdef WIN32
	int len = sizeof(errorCode);
#elif defined(INES_POSIX)
	socklen_t len = sizeof(errorCode);
#endif
	int ret = getsockopt(sock, SOL_SOCKET, SO_ERROR, (char*)&errorCode, &len);
	if(ret != 0)
		return ret;
	return errorCode;
}





static int net_set_async(socket_t   sock)
{
#ifdef WIN32
	u_long    on = 1;
	if(ioctlsocket(sock, FIONBIO, &on) < 0)
		return -1;
#elif defined(INES_POSIX)
	const int on = 1;
	if(ioctl(sock, FIONBIO, (void*)&on) < 0)
		return -1;
#endif
	return 0;
}

static int net_check_read(socket_t  sock)
{
	struct timeval  tv;
	int n;
	fd_set  rfds;

	FD_ZERO(&rfds);
	FD_SET(sock, &rfds);


	tv.tv_sec = 0;
	tv.tv_usec = 1;

	// POSIX 的 nfds 必须是"最大描述符 + 1", 传 sock 会让 select 监听 0..sock-1 而
	// 恰好漏掉本 socket, 结果恒为 0(永不可读); Windows 下 nfds 被忽略, 行为不变。
	n = select(sock + 1, &rfds, NULL, NULL, &tv);

	if(n > 0)
		return 1;


	return 0;

}


static int net_check_write(socket_t  sock)
{
	struct timeval  tv;
	int n;
	fd_set  wfds;

	FD_ZERO(&wfds);
	FD_SET(sock, &wfds);


	tv.tv_sec = 0;
	tv.tv_usec = 1;

	// 同上: nfds 必须是 sock + 1
	n = select(sock + 1, NULL, &wfds, NULL, &tv);

	if(n > 0)
		return 1;


	return 0;

}
#if 0
static int net_check_error(socket_t  sock)
{
	struct timeval  tv;
	int n;
	fd_set  efds;

	FD_ZERO(&efds);
	FD_SET(sock, &efds);


	tv.tv_sec = 0;
	tv.tv_usec = 1;

	n = select(sock, NULL, NULL, &efds, &tv);

	if(n > 0)
		return 1;


	return 0;

}
#endif


int net_listen(net_saddr_t  saddr, net_port_t  port)
{
	/// create socket
	socket_t   sock;
	struct sockaddr_in   addr;
	
	net_close();

	sock = socket(AF_INET, SOCK_STREAM, 0);

	if(sock == INVALID_SOCKET)
	{
		net_error();
		INES_LOG(LOG_ERR, MOD_NET, ISTR("create server socket error: %s\n"), net_get_last_error());
		return -1;
	}

	//  set async i/o socket
	net_set_async(sock);

	/// bind addr
	memset(&addr, 0, sizeof(addr));
	addr.sin_family = AF_INET;
	addr.sin_port   = htons(port);
	addr.sin_addr.s_addr = inet_addr(__t2a(saddr));	


	if(0 != bind(sock, (struct sockaddr*)&addr, sizeof(addr)))
	{
		net_error();
		INES_LOG(LOG_ERR, MOD_NET, ISTR("bind socket error: %s\n"), net_get_last_error());

		closesocket(sock);
		return -1;
	}

	/// start listen
	if(0 != listen(sock, 10))
	{
		net_error();
		INES_LOG(LOG_ERR, MOD_NET, ISTR("socket listen error: %s\n"), net_get_last_error());
		closesocket(sock);
		return -1;
	}

	s_sock_listen = sock;

	// 取实际端口: 调用方可能传 0 让系统分配(避免多实例端口冲突)
	{
		struct sockaddr_in  local;
#ifdef WIN32
		int                 local_len;
#elif defined(INES_POSIX)
		socklen_t           local_len;
#endif

		local_len = sizeof(local);
		memset(&local, 0, sizeof(local));

		if(0 == getsockname(sock, (struct sockaddr*)&local, &local_len))
			s_listen_port = (net_port_t)ntohs(local.sin_port);
		else
			s_listen_port = (net_port_t)port;
	}

	INES_LOG(LOG_ERR, MOD_NET, ISTR("socket start listen at %s:%d\n"), saddr, s_listen_port);
	s_net_last_err[0] = 0;

	return 0;
}

int net_connect(net_saddr_t  saddr, net_port_t  port)
{
	/// create socket
	socket_t   sock;
	struct sockaddr_in   addr;
	int ret;

	net_close();
	sock = socket(AF_INET, SOCK_STREAM, 0);

	if(sock == INVALID_SOCKET)
	{
		net_error();
		INES_LOG(LOG_ERR, MOD_NET, ISTR("create server socket error: %s\n"), net_get_last_error());
		return -1;
	}


	//  set async i/o socket
	net_set_async(sock);

	// connect to

	memset(&addr, 0, sizeof(addr));
	addr.sin_family = AF_INET;
	addr.sin_port   = htons(port);
	addr.sin_addr.s_addr = inet_addr(__t2a(saddr));	

	ret = connect(sock, (struct sockaddr*)&addr, sizeof(addr));

	if(ret != 0 )
	{

#ifdef WIN32
		if(WSAGetLastError() != WSAEWOULDBLOCK )
#elif defined(INES_POSIX)
		if(errno != EINPROGRESS)
#endif
		{
			net_error();
			INES_LOG(LOG_ERR, MOD_NET, ISTR("socket connect error: %s\n"), net_get_last_error());
			return -1;
		}
	}

	s_sock_conn = sock;

	s_recv_len = 0;

	INES_LOG(LOG_ERR, MOD_NET, ISTR("socket connect to %s:%d\n"), saddr, port);
	s_net_last_err[0] = 0;

	return 0;
}


int net_is_server()
{
	return s_sock_listen != INVALID_SOCKET;
}

int net_is_connected()
{
	if(s_sock_listen != INVALID_SOCKET)
	{
		struct sockaddr_in   addr;
		// accept() 的长度参数: Winsock 为 int*, POSIX 为 socklen_t*(同 net_socket_error 的写法)
#ifdef WIN32
		int                  addr_len;
#elif defined(INES_POSIX)
		socklen_t            addr_len;
#endif
		socket_t  sock_c;

		// run as server

		if(s_sock_conn != INVALID_SOCKET)
			return 1;

		if(0 == net_check_read(s_sock_listen))
			return 0;

		// check listen 

		addr_len = sizeof(addr);
		sock_c = accept(s_sock_listen, (struct sockaddr*)&addr, &addr_len);
		

		if(sock_c == INVALID_SOCKET)
			return 0;


		s_sock_conn = sock_c;

		s_recv_len = 0;

		return 1;
	}
	else if(s_sock_conn != INVALID_SOCKET)
	{
		// check client connected? 
		if(0 == net_check_write(s_sock_conn))
			return 0;

		if(0 != net_socket_error(s_sock_conn))
			return 0;

		return 1;
	}


	return 0;
}

int net_is_connect_failed()
{
	if(s_sock_conn != INVALID_SOCKET)
	{
		int nerr = net_socket_error(s_sock_conn);
		if(0 !=  nerr)
		{
			ines_snprintf(s_net_last_err, NET_MAX_ERROR, ISTR("(%d) %s"), nerr, net_strerror(nerr));
			return 1;
		}
	}
	return 0;
}


int net_check_recv()
{
	if(net_check_read(s_sock_conn))
	{
		int len = recv(s_sock_conn, s_recv_buffer + s_recv_len, sizeof(s_recv_buffer) - s_recv_len, 0);
		if(len <= 0)
		{
			// len == 0 ==> close by peer
			// len < 0  ==> socker recv error
			
		}
		else
		{
			s_recv_len += len;
		}
	}
	return s_recv_len;
}


int net_pick_recv_data(void* pv, int len)
{
	int n;

	n = net_check_recv();

	if(len <= 0 || len > n)
		return -1;

	memcpy(pv, s_recv_buffer, len);
	return 0;
}

int net_del_recv_data(int len)
{
	int n;

	n = net_check_recv();

	if(len <= 0 || len > n)
		return -1;

	
	s_recv_len -= len;
	memmove(s_recv_buffer, s_recv_buffer + len, s_recv_len);

	return 0;
}



int net_send(void* data, int len)
{
	int n = 0;
	if(s_sock_conn != INVALID_SOCKET)
	{
		n = send(s_sock_conn, (const char*)data, len, 0);
	}
	return n;
}

net_port_t net_get_local_port()
{
	return s_listen_port;
}

int net_close()
{
	if(s_sock_conn != INVALID_SOCKET)
	{
		closesocket(s_sock_conn);
		s_sock_conn = INVALID_SOCKET;
	}

	if(s_sock_listen != INVALID_SOCKET)
	{
		closesocket(s_sock_listen);
		s_sock_listen = INVALID_SOCKET;
	}

	s_listen_port = 0;
	s_recv_len = 0;

	return 0;
}


void net_fini()
{	
#ifdef WIN32
	WSACleanup();
#endif
}
