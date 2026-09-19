// =====================================================================
// 局域网快速配对 —— 发现层(实现)
//
// 用法(主线程, 与对战链路互斥):
//   net_init(); lan_gen_peer_id(id); lan_open(id);
//   每 1 秒  : lan_advertise(crc32, tcp_port, region, nick, rom);
//   每 500ms : lan_poll(rooms, LAN_ROOM_MAX);
//   选定角色 : lan_find(peer_id, &room) -> lan_close() -> net_listen/net_connect
//
// 实现要点:
//   1) 广播目标是 255.255.255.255 的有限广播, 不出本网段(需求: 仅局域网)。
//   2) 同机多实例共存: POSIX 用 SO_REUSEPORT(广播报文会复制到每个绑定者),
//      Windows 下等价语义是 SO_REUSEADDR; 两者不可互换。
//   3) socket 全程非阻塞, 因此 lan_poll() 不会卡住 UI。
// =====================================================================

#include "lan.h"
#include "log.h"

#ifdef WIN32
#include <WinSock2.h>
#include <windows.h>     // WideCharToMultiByte / MultiByteToWideChar(UTF-8 转换)
#include <stdlib.h>
typedef SOCKET  socket_t;
#define  LAN_INVALID_SOCKET   INVALID_SOCKET
#elif defined(INES_POSIX)
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <errno.h>
#include <stdlib.h>
typedef  int  socket_t;
#define  LAN_INVALID_SOCKET   (-1)
#define  closesocket          close
#endif

#include <string.h>
#include <time.h>


// ---------------------------------------------------------------------
// 内部状态
// ---------------------------------------------------------------------

static socket_t     s_sock       = LAN_INVALID_SOCKET;
static lan_room_t   s_rooms[LAN_ROOM_MAX];
static int          s_room_count = 0;
static ines_byte_t  s_self_id[16];
static int          s_have_self  = 0;

// 自带 LCG: 只为生成 peer_id, 不用全局 rand() 以免污染进程随机序列
static ines_dword_t s_rand_state = 0;


// ---------------------------------------------------------------------
// 内部工具
// ---------------------------------------------------------------------

// 线性同余伪随机(取高 8 位)
static ines_byte_t lan_rand_byte(void)
{
	s_rand_state = s_rand_state * 1103515245u + 12345u;
	return (ines_byte_t)((s_rand_state >> 16) & 0xFF);
}

// 本地字符集(win32 Unicode 下为宽字符) -> UTF-8 字节流。
// 返回的缓冲区由本函数静态持有, 仅在下次调用前有效(调用方须立即消费)。
//
// win32 分支**必须**显式按 CP_UTF8 转换: wcstombs() 走的是当前 locale, 而
// Windows 的默认 locale 不是 UTF-8, 中文昵称/ROM 名会被转成 '?' 或直接失败;
// macOS(非 UNICODE)本就是 UTF-8 直通, 两端只有都按 UTF-8 才能正确显示中文。
static const char* lan_t2a(ines_cstr_t ts)
{
#ifdef UNICODE
	static char  out[512];
	int          n;

	if (ts == NULL)
		return "";

	n = WideCharToMultiByte(CP_UTF8, 0, ts, -1, out, (int)sizeof(out) - 1, NULL, NULL);

	if (n <= 0)
		out[0] = 0;
	else
		out[n] = 0;

	return out;
#else
	return (ts != NULL) ? (const char*)ts : "";
#endif
}

// UTF-8 字节流 -> 本地字符集
static void lan_a2t(ines_str_t dst, int dst_chars, const char* src, int src_bytes)
{
	if ((dst == NULL) || (dst_chars <= 0))
		return;

	if ((src == NULL) || (src_bytes <= 0))
	{
		dst[0] = 0;
		return;
	}

	if (src_bytes >= dst_chars)
		src_bytes = dst_chars - 1;

#ifdef UNICODE
	{
		char  tmp[512];
		int   n;

		if (src_bytes > (int)(sizeof(tmp) - 1))
			src_bytes = (int)(sizeof(tmp) - 1);

		memcpy(tmp, src, src_bytes);
		tmp[src_bytes] = 0;

		// 与 lan_t2a() 对称: 显式按 UTF-8 解释对端发来的字节, 不依赖 locale
		n = MultiByteToWideChar(CP_UTF8, 0, tmp, -1, dst, dst_chars - 1);

		if (n <= 0)
			dst[0] = 0;
		else
			dst[n] = 0;
	}
#else
	memcpy(dst, src, src_bytes);
	dst[src_bytes] = 0;
#endif
}

// 按 peer_id 查找房间
static lan_room_t* lan_room_find(const ines_byte_t* peer_id)
{
	int i;

	for (i = 0; i < s_room_count; i++)
	{
		if (memcmp(s_rooms[i].peer_id, peer_id, 16) == 0)
			return &s_rooms[i];
	}

	return NULL;
}

// 取一个空位(表满时覆盖最旧的一项)
static lan_room_t* lan_room_alloc(const ines_byte_t* peer_id)
{
	lan_room_t* r;
	int         i;

	if (s_room_count < LAN_ROOM_MAX)
	{
		r = &s_rooms[s_room_count++];
		memset(r, 0, sizeof(*r));
		memcpy(r->peer_id, peer_id, 16);
		return r;
	}

	{
		int oldest = 0;

		for (i = 1; i < s_room_count; i++)
		{
			if (s_rooms[i].last_seen < s_rooms[oldest].last_seen)
				oldest = i;
		}

		r = &s_rooms[oldest];
		memset(r, 0, sizeof(*r));
		memcpy(r->peer_id, peer_id, 16);
		return r;
	}
}

// 用一条 beacon 更新房间表(不存在则新增)
static void lan_room_update(const lan_beacon_t* b, ines_dword_t addr)
{
	lan_room_t* r;
	int         nick_len;
	int         rom_len;

	r = lan_room_find(b->peer_id);

	if (r == NULL)
	{
		r = lan_room_alloc(b->peer_id);
		INES_LOG(LOG_NTY, MOD_NET, ISTR("lan: room found, port=%d\n"),
				 (int)ntohs(b->tcp_port));
	}

	r->crc32     = ntohl(b->crc32);
	r->tcp_port  = ntohs(b->tcp_port);
	r->region    = b->region;
	r->addr      = addr;
	r->last_seen = (ines_dword_t)time(NULL);

	nick_len = (int)b->nick_len;
	if (nick_len < 0)
		nick_len = 0;
	if (nick_len > LAN_NICK_MAX)
		nick_len = LAN_NICK_MAX;

	rom_len = (int)b->rom_len;
	if (rom_len < 0)
		rom_len = 0;
	if (rom_len > LAN_ROM_MAX)
		rom_len = LAN_ROM_MAX;

	lan_a2t(r->nick, LAN_NICK_MAX + 1, (const char*)b->nick, nick_len);
	lan_a2t(r->rom,  LAN_ROM_MAX + 1,  (const char*)b->rom,  rom_len);
}

// 把一段文本拷进报文(超长截断, 余量补 0)
static void lan_pack_text(ines_byte_t* dst, int dst_max, ines_byte_t* out_len, ines_cstr_t src)
{
	const char* text;
	size_t      len;

	text = lan_t2a(src);
	len  = strlen(text);

	if (len > (size_t)dst_max)
		len = (size_t)dst_max;

	memset(dst, 0, (size_t)dst_max);

	if (len > 0)
		memcpy(dst, text, len);

	*out_len = (ines_byte_t)len;
}


// ---------------------------------------------------------------------
// 对外接口
// ---------------------------------------------------------------------

void lan_gen_peer_id(ines_byte_t* out_id)
{
	int i;

	if (out_id == NULL)
		return;

	// 首次调用时用"时间 + 时钟 + 栈地址"做种子, 避免同机同时启动的两个实例撞 id
	if (s_rand_state == 0)
	{
		s_rand_state = (ines_dword_t)time(NULL)
					 ^ ((ines_dword_t)clock() << 8)
					 ^ ((ines_dword_t)(ines_size_t)&i);
	}

	for (i = 0; i < 16; i++)
		out_id[i] = lan_rand_byte();
}

int lan_open(const ines_byte_t* peer_id)
{
	socket_t            sock;
	struct sockaddr_in  addr;
	int                 on;
	int                 ret;

	lan_close();

	if (peer_id == NULL)
	{
		INES_LOG(LOG_ERR, MOD_NET, ISTR("lan: open failed, peer_id is null\n"));
		return -1;
	}

	memcpy(s_self_id, peer_id, 16);
	s_have_self = 1;

	sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);

	if (sock == LAN_INVALID_SOCKET)
	{
		INES_LOG(LOG_ERR, MOD_NET, ISTR("lan: create udp socket failed\n"));
		return -1;
	}

	// 允许发送广播
	on = 1;
	ret = setsockopt(sock, SOL_SOCKET, SO_BROADCAST, (const char*)&on, sizeof(on));

	if (ret != 0)
	{
		INES_LOG(LOG_ERR, MOD_NET, ISTR("lan: setsockopt(SO_BROADCAST) failed\n"));
		closesocket(sock);
		return -1;
	}

	// 允许同机多个实例同时绑 8892:
	//   POSIX 用 SO_REUSEPORT; Windows 下等价语义是 SO_REUSEADDR(不可互换)。
	on = 1;
#ifdef WIN32
	ret = setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, (const char*)&on, sizeof(on));
#else
	ret = setsockopt(sock, SOL_SOCKET, SO_REUSEPORT, (const char*)&on, sizeof(on));
#endif

	if (ret != 0)
	{
		INES_LOG(LOG_ERR, MOD_NET, ISTR("lan: setsockopt(REUSE) failed\n"));
		closesocket(sock);
		return -1;
	}

	memset(&addr, 0, sizeof(addr));
	addr.sin_family      = AF_INET;
	addr.sin_port        = htons(LAN_PORT);
	addr.sin_addr.s_addr = htonl(INADDR_ANY);

	if (0 != bind(sock, (struct sockaddr*)&addr, sizeof(addr)))
	{
		INES_LOG(LOG_ERR, MOD_NET, ISTR("lan: bind udp %d failed\n"), LAN_PORT);
		closesocket(sock);
		return -1;
	}

	// 非阻塞: lan_poll() 不会卡住调用方
#ifdef WIN32
	{
		u_long nb = 1;
		ioctlsocket(sock, FIONBIO, &nb);
	}
#else
	{
		int nb = 1;
		ioctl(sock, FIONBIO, (void*)&nb);
	}
#endif

	s_sock = sock;

	INES_LOG(LOG_NTY, MOD_NET, ISTR("lan: discovery opened at udp %d\n"), LAN_PORT);

	return 0;
}

void lan_close(void)
{
	if (s_sock != LAN_INVALID_SOCKET)
	{
		closesocket(s_sock);
		s_sock = LAN_INVALID_SOCKET;

		INES_LOG(LOG_NTY, MOD_NET, ISTR("lan: discovery closed\n"));
	}

	s_room_count = 0;
	memset(s_rooms, 0, sizeof(s_rooms));
}

int lan_is_open(void)
{
	return (s_sock != LAN_INVALID_SOCKET) ? 1 : 0;
}

int lan_advertise(ines_dword_t crc32, ines_word_t tcp_port, ines_byte_t region,
				  ines_cstr_t nick, ines_cstr_t rom)
{
	lan_beacon_t        b;
	struct sockaddr_in  dst;
	int                 ret;

	if (s_sock == LAN_INVALID_SOCKET)
		return -1;

	memset(&b, 0, sizeof(b));
	memcpy(b.magic, LAN_MAGIC, LAN_MAGIC_LEN);
	b.ver      = htonl(LAN_VER);
	b.crc32    = htonl(crc32);
	b.tcp_port = htons(tcp_port);
	b.region   = region;
	memcpy(b.peer_id, s_self_id, 16);

	lan_pack_text(b.nick, LAN_NICK_MAX, &b.nick_len, nick);
	lan_pack_text(b.rom,  LAN_ROM_MAX,  &b.rom_len,  rom);

	memset(&dst, 0, sizeof(dst));
	dst.sin_family      = AF_INET;
	dst.sin_port        = htons(LAN_PORT);
	dst.sin_addr.s_addr = htonl(INADDR_BROADCAST);

	ret = (int)sendto(s_sock, (const char*)&b, sizeof(b), 0,
					  (struct sockaddr*)&dst, sizeof(dst));

	if (ret != (int)sizeof(b))
		return -1;

	return 0;
}

int lan_poll(lan_room_t* out_rooms, int max_rooms)
{
	time_t  now;
	int     i;
	int     w;

	if (s_sock == LAN_INVALID_SOCKET)
		return -1;

	// 1) 收包(非阻塞, 无数据即返回)
	for (i = 0; i < LAN_POLL_MAX_PACK; i++)
	{
		struct sockaddr_in  from;
		lan_beacon_t        b;
		int                 len;
#ifdef WIN32
		int                 from_len;
#else
		socklen_t           from_len;
#endif

		from_len = sizeof(from);
		len = (int)recvfrom(s_sock, (char*)&b, sizeof(b), 0,
							(struct sockaddr*)&from, &from_len);

		if (len <= 0)
			break;   // 非阻塞: 无数据(WSAEWOULDBLOCK / EAGAIN)或出错

		if (len != (int)sizeof(b))
			continue;   // 定长协议, 长度不符一律丢弃

		if (memcmp(b.magic, LAN_MAGIC, LAN_MAGIC_LEN) != 0)
			continue;

		if ((ines_dword_t)ntohl(b.ver) != (ines_dword_t)LAN_VER)
			continue;

		// 过滤自身广播
		if (s_have_self && (memcmp(b.peer_id, s_self_id, 16) == 0))
			continue;

		lan_room_update(&b, from.sin_addr.s_addr);
	}

	// 2) 剔除超时项(开始对战的一方会停止广播, 房间在 TTL 后自然消失)
	now = time(NULL);
	w   = 0;

	for (i = 0; i < s_room_count; i++)
	{
		if ((time_t)s_rooms[i].last_seen + LAN_TTL_SEC < now)
			continue;

		if (w != i)
			s_rooms[w] = s_rooms[i];

		w++;
	}

	s_room_count = w;

	// 3) 输出快照
	if ((out_rooms == NULL) || (max_rooms <= 0))
		return s_room_count;

	{
		int n = (s_room_count < max_rooms) ? s_room_count : max_rooms;

		if (n > 0)
			memcpy(out_rooms, s_rooms, (size_t)n * sizeof(lan_room_t));

		return n;
	}
}

int lan_find(const ines_byte_t* peer_id, lan_room_t* out_room)
{
	lan_room_t* r;

	if (peer_id == NULL)
		return -1;

	r = lan_room_find(peer_id);

	if (r == NULL)
		return -1;

	if (out_room != NULL)
		memcpy(out_room, r, sizeof(*r));

	return 0;
}

int lan_addr_str(ines_dword_t addr, ines_str_t buf, int len)
{
	ines_byte_t  b[4];

	if ((buf == NULL) || (len <= 0))
		return -1;

	// addr 为网络序(大端), 按内存顺序取即点分十进制的高位到低位
	memcpy(b, &addr, 4);
	ines_snprintf(buf, len, ISTR("%d.%d.%d.%d"),
				  (int)b[0], (int)b[1], (int)b[2], (int)b[3]);

	return 0;
}
