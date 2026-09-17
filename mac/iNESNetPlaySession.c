// =====================================================================
// iNES macOS 前端 —— 联网对战会话(实现)
//
// 逻辑逐条对标 win32:
//   * 握手: win32/dlgNetPlay.c 的 dlgNetPlay_TimedCheck()
//           (服务端等 START 后回 START_RSP; 客户端连上即发 START 等 START_RSP)
//   * 帧:   win32/iNES.c 的 send_frame() / recv_frame() / cache_add_mine()
//           / cache_add_other() / cache_get()
//
// 关于 START_RSP.fno: 该字段在 win32 里从未使用(预留), 这里把高 32 位用作
// "服务端的缓冲帧数", 客户端据此对齐 —— 两端 net_cache_num 不一致时, 双方的
// 输入延迟不同, 帧号会出现恒定偏移。win32 不读该字段, 故互通无影响。
// =====================================================================

#include "iNESNetPlaySession.h"

#include "../comm/log.h"

#include <string.h>
#include <time.h>


// 与 win32/iNES.c 的 NET_CACHE_MAX_SIZE 一致
#define NP_CACHE_SLOTS     10

// 提示文本的最大长度
#define NP_MSG_MAX         256


static int           s_state      = NP_ST_NONE;
static int           s_is_server  = 0;
static int           s_cache_num  = NP_CACHE_DEFAULT;
static ines_dword_t  s_crc32      = 0;
static time_t        s_state_time = 0;

// 帧缓存: 31~24 控制码, 15~8 副手柄, 7~0 主手柄(与 win32 的 net_cache 同构)
static ines_dword_t  s_cache[NP_CACHE_SLOTS];
static int           s_cache_size = 0;


// ---------------------------------------------------------------------
// 内部工具
// ---------------------------------------------------------------------

// 安全的文本拷贝(始终以 0 结尾)
static void np_copy_text(char* dst, ines_size_t len, const char* src)
{
	if ((dst == NULL) || (len <= 0))
		return;

	if (src == NULL)
		src = "";

	ines_strncpy(dst, src, len - 1);
	dst[len - 1] = 0;
}

// 结束会话并清空帧缓存(不重复关链路以外的副作用)
static void np_reset(int do_close)
{
	if (do_close)
		net_close();

	s_state      = NP_ST_NONE;
	s_state_time = 0;
	s_is_server  = 0;
	s_cache_size = 0;

	memset(s_cache, 0, sizeof(s_cache));
}

// 握手失败: 按 win32 的 dlgNetPlay_ConnectError() 的顺序处理
// (先回错误响应包, 再关链路, 最后把原因交给界面)
static void np_fail(const char* msg, const void* rsp_data, int rsp_len)
{
	if ((rsp_data != NULL) && (rsp_len > 0))
		net_send((void*)rsp_data, rsp_len);

	np_reset(1);

	INES_LOG(LOG_ERR, MOD_NET, ISTR("netplay: %s\n"), (msg != NULL) ? msg : "");
}

// 握手成功: 与 win32 的 IDM_NET_PLAY 处理一致, 预置 net_cache_num 个空帧
static void np_enter_playing(void)
{
	s_state      = NP_ST_PLAYING;
	s_state_time = 0;
	s_cache_size = 0;

	memset(s_cache, 0, sizeof(s_cache));
	s_cache_size = s_cache_num;

	INES_LOG(LOG_NTY, MOD_NET,
			 ISTR("netplay: connected, run as %s, cache_num=%d\n"),
			 s_is_server ? ISTR("server") : ISTR("client"), s_cache_num);
}

// 是否握手超时(与 win32 的 s_status_time + 5 < time(NULL) 一致)
static int np_is_timeout(void)
{
	return (s_state_time != 0)
		&& ((s_state_time + NP_HANDSHAKE_TIMEOUT) < time(NULL));
}


// ---------------------------------------------------------------------
// 帧缓存(与 win32/iNES.c 同名函数等价)
// ---------------------------------------------------------------------

// 本方输入永远写在固定的缓冲帧上
static void np_cache_add_mine(ines_byte_t joypad, ines_byte_t ctrl)
{
	if ((s_cache_num >= 0) && (s_cache_num < NP_CACHE_SLOTS))
	{
		s_cache[s_cache_num] |= (ines_dword_t)joypad;
		s_cache[s_cache_num] |= ((ines_dword_t)ctrl) << 24;
	}
}

// 对方输入追加到队列尾部
static void np_cache_add_other(ines_byte_t joypad, ines_byte_t ctrl)
{
	if (s_cache_size < NP_CACHE_SLOTS)
	{
		s_cache[s_cache_size] |= ((ines_dword_t)joypad) << 8;
		s_cache[s_cache_size] |= ((ines_dword_t)ctrl) << 24;
		s_cache_size++;
	}
}

// 取队首并整体左移
static ines_dword_t np_cache_get(void)
{
	int           i;
	ines_dword_t  ret = 0;

	if (s_cache_size > 0)
	{
		ret = s_cache[0];

		for (i = 1; i < NP_CACHE_SLOTS; i++)
			s_cache[i - 1] = s_cache[i];

		s_cache[NP_CACHE_SLOTS - 1] = 0;
		s_cache_size--;
	}

	return ret;
}


// ---------------------------------------------------------------------
// 对外接口
// ---------------------------------------------------------------------

int np_init(void)
{
	int  rc;

	s_state      = NP_ST_NONE;
	s_is_server  = 0;
	s_cache_num  = NP_CACHE_DEFAULT;
	s_crc32      = 0;
	s_state_time = 0;
	s_cache_size = 0;

	memset(s_cache, 0, sizeof(s_cache));

	rc = net_init();

	INES_LOG(LOG_INF, MOD_NET, ISTR("netplay: net_init() = %d\n"), rc);

	return rc;
}

void np_fini(void)
{
	np_end();
	net_fini();

	INES_LOG(LOG_INF, MOD_NET, ISTR("netplay: net_fini()\n"));
}

void np_end(void)
{
	if (s_state == NP_ST_NONE)
		return;

	np_reset(1);

	INES_LOG(LOG_NTY, MOD_NET, ISTR("netplay: stopped\n"));
}

int np_begin(int is_server, const char* ip, int port, ines_dword_t crc32, int cache_num)
{
	int  rc;

	np_reset(1);

	s_is_server = (is_server != 0) ? 1 : 0;
	s_crc32     = crc32;
	s_cache_num = cache_num;

	if (s_cache_num < NP_CACHE_MIN)
		s_cache_num = NP_CACHE_MIN;

	if (s_cache_num > NP_CACHE_MAX)
		s_cache_num = NP_CACHE_MAX;

	if (s_is_server)
		rc = net_listen((net_saddr_t)ISTR("0.0.0.0"), (net_port_t)port);
	else
		rc = net_connect((net_saddr_t)((ip != NULL) ? ip : "127.0.0.1"), (net_port_t)port);

	if (0 != rc)
	{
		INES_LOG(LOG_ERR, MOD_NET, ISTR("netplay: %s failed : %s\n"),
				 s_is_server ? ISTR("listen") : ISTR("connect"), net_get_last_error());

		np_reset(0);
		return -1;
	}

	s_state      = NP_ST_WAIT_CONN;
	s_state_time = time(NULL);

	INES_LOG(LOG_NTY, MOD_NET, ISTR("netplay: %s at %s:%d, cache_num=%d\n"),
			 s_is_server ? ISTR("listen") : ISTR("connect"),
			 s_is_server ? ISTR("0.0.0.0") : ((ip != NULL) ? ip : ""),
			 port, s_cache_num);

	return 0;
}

int np_state(void)
{
	return s_state;
}

int np_is_server(void)
{
	return (s_state != NP_ST_NONE) ? net_is_server() : 0;
}

int np_cache_num(void)
{
	return s_cache_num;
}

int np_poll(char* msg, ines_size_t len)
{
	int           rc = NP_POLL_PENDING;
	ines_char_t   text[NP_MSG_MAX];

	text[0] = 0;

	if (s_state == NP_ST_NONE)
	{
		np_copy_text(msg, len, text);
		return NP_POLL_PENDING;
	}

	if (s_is_server)
	{
		switch (s_state)
		{
		case NP_ST_WAIT_CONN:
			if (!net_is_connected())
			{
				np_copy_text(text, sizeof(text), ISTR("等待客户端的连接..."));
				break;
			}

			s_state      = NP_ST_WAIT_START;
			s_state_time = time(NULL);

			np_copy_text(text, sizeof(text), ISTR("连接成功，等待验证..."));
			break;

		case NP_ST_WAIT_START:
			{
				struct _net_start  nst;

				if (0 == net_pick_recv_data(&nst, (int)sizeof(nst)))
				{
					struct _net_start_rsp  rsp;

					net_del_recv_data((int)sizeof(nst));

					memset(&rsp, 0, sizeof(rsp));
					rsp.cmd = NET_CMD_START_RSP;

					if (nst.cmd != NET_CMD_START)
					{
						np_copy_text(text, sizeof(text), ISTR("连接错误!"));
						np_fail(text, NULL, 0);
						rc = NP_POLL_FAILED;
					}
					else if (nst.ver != NET_VER)
					{
						rsp.code = 1;
						np_copy_text(text, sizeof(text), ISTR("版本不匹配!"));
						np_fail(text, &rsp, (int)sizeof(rsp));
						rc = NP_POLL_FAILED;
					}
					else if (nst.crc32 != s_crc32)
					{
						rsp.code = 2;
						np_copy_text(text, sizeof(text), ISTR("ROM不匹配!"));
						np_fail(text, &rsp, (int)sizeof(rsp));
						rc = NP_POLL_FAILED;
					}
					else
					{
						rsp.code    = 0;
						rsp.is_ntsc = 1;
						// 高 32 位: 服务端的缓冲帧数(win32 不使用 fno, 互通无影响)
						rsp.fno     = ((ines_int64_t)s_cache_num) << 32;

						net_send(&rsp, (int)sizeof(rsp));
						np_enter_playing();

						np_copy_text(text, sizeof(text), ISTR("连接成功!"));
						rc = NP_POLL_OK;
					}
					break;
				}
			}

			if (np_is_timeout())
			{
				np_copy_text(text, sizeof(text), ISTR("客户端验证超时!"));
				np_fail(text, NULL, 0);
				rc = NP_POLL_FAILED;
			}
			break;

		default:
			break;
		}
	}
	else
	{
		switch (s_state)
		{
		case NP_ST_WAIT_CONN:
			if (net_is_connect_failed())
			{
				np_copy_text(text, sizeof(text), net_get_last_error());
				np_fail(text, NULL, 0);
				rc = NP_POLL_FAILED;
				break;
			}

			if (!net_is_connected())
			{
				np_copy_text(text, sizeof(text), ISTR("正在连接到服务器..."));
				break;
			}

			{
				struct _net_start  nst;

				memset(&nst, 0, sizeof(nst));
				nst.cmd   = NET_CMD_START;
				nst.ver   = NET_VER;
				nst.crc32 = s_crc32;

				net_send(&nst, (int)sizeof(nst));
			}

			s_state      = NP_ST_WAIT_START;
			s_state_time = time(NULL);

			np_copy_text(text, sizeof(text), ISTR("连接成功，等待验证..."));
			break;

		case NP_ST_WAIT_START:
			{
				struct _net_start_rsp  rsp;

				if (0 == net_pick_recv_data(&rsp, (int)sizeof(rsp)))
				{
					net_del_recv_data((int)sizeof(rsp));

					if (rsp.cmd != NET_CMD_START_RSP)
					{
						np_copy_text(text, sizeof(text), ISTR("连接错误!"));
						np_fail(text, NULL, 0);
						rc = NP_POLL_FAILED;
					}
					else if (rsp.code == 0)
					{
						// 服务端下发的缓冲帧数(高 32 位); 0 表示旧版对端, 沿用默认值
						ines_dword_t  peer_cache = (ines_dword_t)(rsp.fno >> 32);

						if ((peer_cache >= NP_CACHE_MIN) && (peer_cache <= NP_CACHE_MAX))
						{
							s_cache_num = (int)peer_cache;
						}
						else if (peer_cache != 0)
						{
							INES_LOG(LOG_WAR, MOD_NET,
									 ISTR("netplay: peer cache_num %u out of range, keep %d\n"),
									 peer_cache, s_cache_num);
						}

						np_enter_playing();

						np_copy_text(text, sizeof(text), ISTR("连接成功!"));
						rc = NP_POLL_OK;
					}
					else if (rsp.code == 1)
					{
						np_copy_text(text, sizeof(text), ISTR("版本不匹配!"));
						np_fail(text, NULL, 0);
						rc = NP_POLL_FAILED;
					}
					else if (rsp.code == 2)
					{
						np_copy_text(text, sizeof(text), ISTR("ROM不匹配!"));
						np_fail(text, NULL, 0);
						rc = NP_POLL_FAILED;
					}
					else
					{
						np_copy_text(text, sizeof(text), ISTR("连接错误!"));
						np_fail(text, NULL, 0);
						rc = NP_POLL_FAILED;
					}
					break;
				}
			}

			if (np_is_timeout())
			{
				np_copy_text(text, sizeof(text), ISTR("服务器验证超时!"));
				np_fail(text, NULL, 0);
				rc = NP_POLL_FAILED;
			}
			break;

		default:
			break;
		}
	}

	np_copy_text(msg, len, text);

	return rc;
}

void np_frame_begin(void)
{
	ines_byte_t  cmd;
	int          flag = 0;

	if (s_state != NP_ST_PLAYING)
		return;

	// 与 win32 的 recv_frame() 一致: 逐包解析, 只把 NET_CMD_FRAME 追加进帧缓存,
	// 其余命令整包丢弃(未知命令按 1 字节丢弃, 与 win32 的 default 分支相同)。
	while ((0 == flag) && (0 == net_pick_recv_data(&cmd, (int)sizeof(cmd))))
	{
		flag = 1;

		switch (cmd)
		{
		case NET_CMD_FRAME:
			{
				struct _net_frame  pkg;

				if (0 == net_pick_recv_data(&pkg, (int)sizeof(pkg)))
				{
					if (s_cache_size < NP_CACHE_SLOTS)
					{
						np_cache_add_other(pkg.joypad, pkg.ctrl);
						net_del_recv_data((int)sizeof(pkg));
						flag = 0;
					}
				}
			}
			break;

		default:
			net_del_recv_data((int)sizeof(cmd));
			flag = 0;
			break;
		}
	}
}

int np_input_ready(void)
{
	return ((s_state == NP_ST_PLAYING) && (s_cache_size > 0)) ? 1 : 0;
}

int np_frame_input(ines_byte_t mine_joypad, ines_byte_t mine_ctrl,
				   ines_int_t* out_main, ines_int_t* out_second, ines_int_t* out_ctrl)
{
	ines_dword_t  value;

	if ((s_state != NP_ST_PLAYING) || (s_cache_size <= 0))
		return -1;

	if ((out_main == NULL) || (out_second == NULL))
		return -1;

	np_cache_add_mine(mine_joypad, mine_ctrl);

	{
		struct _net_frame  pkg;

		memset(&pkg, 0, sizeof(pkg));
		pkg.cmd    = NET_CMD_FRAME;
		pkg.joypad = mine_joypad;
		pkg.ctrl   = mine_ctrl;

		net_send(&pkg, (int)sizeof(pkg));
	}

	value = np_cache_get();

	// 服务端用主手柄, 客户端用副手柄(与 win32 一致)
	if (net_is_server())
	{
		*out_main   = (ines_int_t)(value & 0xff);
		*out_second = (ines_int_t)((value >> 8) & 0xff);
	}
	else
	{
		*out_main   = (ines_int_t)((value >> 8) & 0xff);
		*out_second = (ines_int_t)(value & 0xff);
	}

	if (out_ctrl != NULL)
		*out_ctrl = (ines_int_t)((value >> 24) & 0xff);

	return 0;
}
