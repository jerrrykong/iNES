// =====================================================================
// 联网对战会话层(实现) —— win32 / macOS 共用
//
// 逻辑逐条对标 win32 原始实现:
//   * 握手: win32/dlgNetPlay.c 的 dlgNetPlay_TimedCheck()
//           (服务端等 START 后回 START_RSP; 客户端连上即发 START 等 START_RSP)
//   * 帧:   win32/iNES.c 的 send_frame() / recv_frame() / cache_add_mine()
//           / cache_add_other() / cache_get()
//
// 关于 START_RSP.fno: 该字段在 win32 里从未使用(预留), 这里把高 32 位用作
// "服务端的缓冲帧数", 客户端据此对齐 —— 两端 net_cache_num 不一致时, 双方的
// 输入延迟不同, 帧号会出现恒定偏移。**客户端必须采用服务端下发的值**: 读到
// 0(旧版未携带)或越界时, 一律视为版本过旧, 拒绝连接(而不是沿用本地默认值)。
// code != 0 时该字段改为回传服务端 NET_VER, 供客户端提示"本机 x / 对端 y"。
// =====================================================================

#include "npsession.h"

#include "log.h"

#ifdef WIN32
#include <windows.h>
#endif

#include <stdlib.h>
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
static int           s_peer_quit  = 0;    // 收到过对端的 NET_CMD_QUIT(由 np_peer_quit() 取走)

// 状态同步(联机读档): 见下方"状态同步"一节
static int           s_sync_step  = 0;    // SYNC_* 子状态
static int           s_sync_host  = 0;    // 本方是否发起方(主机)
static int           s_sync_ret   = NP_SYNC_NONE;
static ines_byte_t*  s_sync_buf   = NULL;
static ines_dword_t  s_sync_len   = 0;
static ines_dword_t  s_sync_got   = 0;
static ines_dword_t  s_sync_crc   = 0;
static time_t        s_sync_time  = 0;

static np_state_load_fn  s_load_fn  = NULL;
static np_state_sign_fn  s_sign_fn  = NULL;
static void*             s_load_ctx = NULL;

// 帧缓存: 31~24 控制码, 15~8 副手柄, 7~0 主手柄(与 win32 的 net_cache 同构)
static ines_dword_t  s_cache[NP_CACHE_SLOTS];
static int           s_cache_size = 0;


// ---------------------------------------------------------------------
// 内部工具
// ---------------------------------------------------------------------

/** 平台原生字符串之间的拷贝(内部用), 始终以 0 结尾。 */
static void np_copy_tstr(ines_str_t dst, ines_size_t len, ines_cstr_t src)
{
	if ((dst == NULL) || (len <= 0))
		return;

	if (src == NULL)
		src = ISTR("");

	ines_strncpy(dst, src, len - 1);
	dst[len - 1] = 0;
}

/**
 * 把平台原生字符串(win32: 宽字符 / macOS: UTF-8)拷成 **UTF-8 char**, 始终以 0 结尾。
 *
 * 提示文本对外统一 UTF-8, 界面层再各自转成可显示的字符串 —— 这样同一份会话层
 * 在 Unicode(win32 GUI)与非 Unicode(macOS / inescore)目标下都能给出正确的中文提示。
 */
static void np_copy_utf8(char* dst, ines_size_t len, ines_cstr_t src)
{
	if ((dst == NULL) || (len <= 0))
		return;

	if (src == NULL)
		src = ISTR("");

#ifdef UNICODE
	if (0 == WideCharToMultiByte(CP_UTF8, 0, src, -1, dst, (int)(len - 1), NULL, NULL))
		dst[0] = 0;

	dst[len - 1] = 0;
#else
	{
		ines_size_t  i;

		for (i = 0; (i + 1) < len; i++)
		{
			dst[i] = (char)src[i];

			if (src[i] == 0)
				break;
		}

		dst[len - 1] = 0;
	}
#endif
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
	s_peer_quit  = 0;

	// 同步中的存档缓冲也要一起释放(退出联网/换 ROM 时会在任意阶段被打断)
	if (s_sync_buf != NULL)
	{
		free(s_sync_buf);
		s_sync_buf = NULL;
	}

	s_sync_step = 0;
	s_sync_host = 0;
	s_sync_ret  = NP_SYNC_NONE;
	s_sync_len  = 0;
	s_sync_got  = 0;
	s_sync_crc  = 0;
	s_sync_time = 0;

	memset(s_cache, 0, sizeof(s_cache));
}

// 握手失败: 按 win32 的 dlgNetPlay_ConnectError() 的顺序处理
// (先回错误响应包, 再关链路, 最后把原因交给界面)
static void np_fail(ines_cstr_t msg, const void* rsp_data, int rsp_len)
{
	if ((rsp_data != NULL) && (rsp_len > 0))
		net_send((void*)rsp_data, rsp_len);

	np_reset(1);

	INES_LOG(LOG_ERR, MOD_NET, ISTR("netplay: %s\n"), (msg != NULL) ? msg : ISTR(""));
}

// 握手成功: 与 win32 的 IDM_NET_PLAY 处理一致, 预置 cache_num 个空帧
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
// 状态同步(联机读档)
//
// 主机: np_sync_begin() -> 发 REQ -> (RSP ready) -> 分片发 DATA -> 收 DONE
//       -> 自己载入同一份字节流并比对摘要 -> 发 GO(go/reset)
// 从机: 收 REQ -> 回 RSP -> 收 DATA -> 载入 -> 回 DONE(ok, sign) -> 收 GO
//
// 任何一步"载入不成功" -> 主机解冻后首帧提交 NET_CTRL_CODE_HARDRESET, **连接保持**。
// ---------------------------------------------------------------------

// 内部子状态
#define SYNC_IDLE       0
#define SYNC_WAIT_RSP   1   // 主机: 已发 REQ
#define SYNC_SEND       2   // 主机: 分片发送中
#define SYNC_WAIT_DONE  3   // 主机: 已发完
#define SYNC_RECV_DATA  4   // 从机: 收 DATA
#define SYNC_WAIT_GO    5   // 从机: 已回 DONE

/** CRC32(IEEE 802.3), 用于校验传输的存档字节流。 */
static ines_dword_t np_crc32(const void* data, int len)
{
	static ines_dword_t   table[256];
	static int            inited = 0;
	const ines_byte_t*    p   = (const ines_byte_t*)data;
	ines_dword_t          crc = 0xffffffffu;
	int                   i;
	int                   j;

	// 表只在首次调用时生成: 联网期间只会由模拟线程/主线程各调一次,
	// 即便竞态也只是重复写入相同的常量值
	if (!inited)
	{
		for (i = 0; i < 256; i++)
		{
			ines_dword_t  c = (ines_dword_t)i;

			for (j = 0; j < 8; j++)
				c = (c & 1) ? (0xedb88320u ^ (c >> 1)) : (c >> 1);

			table[i] = c;
		}

		inited = 1;
	}

	if ((p == NULL) || (len <= 0))
		return 0;

	for (i = 0; i < len; i++)
		crc = table[(crc ^ p[i]) & 0xff] ^ (crc >> 8);

	return crc ^ 0xffffffffu;
}

/** 释放内部缓冲并把子状态机复位(不改变对外的 s_sync_ret)。 */
static void np_sync_free(void)
{
	if (s_sync_buf != NULL)
	{
		free(s_sync_buf);
		s_sync_buf = NULL;
	}

	s_sync_step = SYNC_IDLE;
	s_sync_host = 0;
	s_sync_len  = 0;
	s_sync_got  = 0;
	s_sync_crc  = 0;
	s_sync_time = 0;
}

/** 结束一次同步(结果由 np_sync_state() 交给前端)。 */
static void np_sync_finish(int result)
{
	s_sync_ret = result;

	np_sync_free();

	INES_LOG(LOG_NTY, MOD_NET, ISTR("netplay: state sync %s\n"),
			 (result == NP_SYNC_OK)    ? ISTR("ok")
			 : ((result == NP_SYNC_RESET) ? ISTR("failed -> hard reset")
										  : ISTR("failed -> link broken")));
}

/** 发一个同步包(失败只记日志: 链路问题会由超时/连接检查兜住)。 */
static void np_sync_send(const void* pkg, int len)
{
	if (net_send_all(pkg, len) != len)
		INES_LOG(LOG_ERR, MOD_NET, ISTR("netplay: state sync send failed : %s\n"), net_get_last_error());
}

/** 从机: 回 DONE。 */
static void np_sync_send_done(int code, ines_dword_t sign)
{
	struct _net_state_done  done;

	memset(&done, 0, sizeof(done));
	done.cmd  = NET_CMD_STATE_DONE;
	done.code = (ines_byte_t)code;
	done.sign = sign;

	np_sync_send(&done, (int)sizeof(done));
}

/** 主机: 处理 RSP / DONE。返回 1 表示已消费一个包。 */
static int np_sync_host_recv(void)
{
	ines_byte_t  cmd;

	if (s_sync_step == SYNC_IDLE)
		return 0;

	if (0 != net_pick_recv_data(&cmd, (int)sizeof(cmd)))
		return 0;

	if (cmd == NET_CMD_STATE_RSP)
	{
		struct _net_state_rsp  rsp;

		if (0 != net_pick_recv_data(&rsp, (int)sizeof(rsp)))
			return 0;

		net_del_recv_data((int)sizeof(rsp));

		if (s_sync_step != SYNC_WAIT_RSP)
			return 1;

		if (rsp.code != 0)
		{
			INES_LOG(LOG_WAR, MOD_NET, ISTR("netplay: peer rejected state sync, code=%d\n"),
					 (int)rsp.code);
			np_sync_finish(NP_SYNC_RESET);
			return 1;
		}

		s_sync_step = SYNC_SEND;
		s_sync_time = time(NULL);
		return 1;
	}

	if (cmd == NET_CMD_STATE_DONE)
	{
		struct _net_state_done  done;

		if (0 != net_pick_recv_data(&done, (int)sizeof(done)))
			return 0;

		net_del_recv_data((int)sizeof(done));

		if (s_sync_step != SYNC_WAIT_DONE)
			return 1;

		{
			struct _net_state_go  go;
			ines_dword_t          sign = 0;

			memset(&go, 0, sizeof(go));
			go.cmd = NET_CMD_STATE_GO;

			// 主机自己载入同一份字节流, 再与从机的状态摘要比对;
			// 任一步失败(或摘要不一致) -> GO(reset), 双方同帧硬复位, 连接保持
			if ((done.code == 0)
			 && (s_load_fn != NULL)
			 && (0 == s_load_fn(s_sync_buf, (int)s_sync_len, s_load_ctx))
			 && ((s_sign_fn == NULL) || (0 == s_sign_fn(&sign, s_load_ctx)))
			 && (sign == done.sign))
			{
				go.code = 0;
				np_sync_send(&go, (int)sizeof(go));
				np_sync_finish(NP_SYNC_OK);
			}
			else
			{
				INES_LOG(LOG_WAR, MOD_NET,
						 ISTR("netplay: state sync mismatch (peer=%u, mine=%u)\n"),
						 (unsigned)done.sign, (unsigned)sign);

				go.code = 1;
				np_sync_send(&go, (int)sizeof(go));
				np_sync_finish(NP_SYNC_RESET);
			}
		}
		return 1;
	}

	// 其余 STATE_*(不该由主机收到的): 整包丢弃, 避免卡住接收缓冲
	if (cmd >= NET_CMD_STATE_REQ)
	{
		net_del_recv_data((int)sizeof(cmd));
		return 1;
	}

	return 0;
}

/** 从机: 处理 REQ / DATA / GO。返回 1 表示已消费一个包。 */
static int np_sync_client_recv(void)
{
	ines_byte_t  cmd;

	if (0 != net_pick_recv_data(&cmd, (int)sizeof(cmd)))
		return 0;

	if (cmd == NET_CMD_STATE_REQ)
	{
		struct _net_state_req  req;
		struct _net_state_rsp  rsp;
		ines_byte_t            code = 0;

		if (0 != net_pick_recv_data(&req, (int)sizeof(req)))
			return 0;

		net_del_recv_data((int)sizeof(req));

		if (s_sync_step != SYNC_IDLE)
			return 1;   // 已在同步中: 忽略重复的 REQ

		if ((req.total_len == 0) || (req.total_len > NP_SYNC_MAX_SIZE))
			code = 1;
		else if (req.state_ver != NP_STATE_VER)
			code = 1;
		else
		{
			s_sync_buf = (ines_byte_t*)malloc((size_t)req.total_len);

			if (s_sync_buf == NULL)
				code = 1;
		}

		memset(&rsp, 0, sizeof(rsp));
		rsp.cmd  = NET_CMD_STATE_RSP;
		rsp.code = code;

		np_sync_send(&rsp, (int)sizeof(rsp));

		if (code != 0)
		{
			// 本方状态没变; 主机收到 reject 后同样走复位, 两端保持一致
			np_sync_finish(NP_SYNC_RESET);
			return 1;
		}

		s_sync_len  = req.total_len;
		s_sync_got  = 0;
		s_sync_crc  = req.crc32;
		s_sync_step = SYNC_RECV_DATA;
		s_sync_ret  = NP_SYNC_BUSY;      // 冻结: 前端跳过 doframe
		s_sync_time = time(NULL);

		INES_LOG(LOG_NTY, MOD_NET, ISTR("netplay: state sync recv %u bytes\n"),
				 (unsigned)s_sync_len);
		return 1;
	}

	if (cmd == NET_CMD_STATE_DATA)
	{
		ines_byte_t  head[3];
		int          len;

		if (0 != net_pick_recv_data(head, (int)sizeof(head)))
			return 0;

		len = (int)head[1] | ((int)head[2] << 8);

		if ((len <= 0) || (len > NP_SYNC_CHUNK))
		{
			// 长度非法: 无法定位下一个包, 只能放弃(前端按异常结束联网)
			net_del_recv_data((int)sizeof(head));
			np_sync_send_done(1, 0);
			np_sync_finish(NP_SYNC_FAILED);
			return 1;
		}

		{
			ines_byte_t  tmp[NP_SYNC_CHUNK + 8];

			if (0 != net_pick_recv_data(tmp, 3 + len))
				return 0;             // 还没收全, 留到下一帧

			net_del_recv_data(3 + len);

			if ((s_sync_step != SYNC_RECV_DATA) || (s_sync_buf == NULL))
				return 1;

			if ((s_sync_got + (ines_dword_t)len) > s_sync_len)
			{
				np_sync_send_done(1, 0);
				np_sync_finish(NP_SYNC_RESET);
				return 1;
			}

			memcpy(s_sync_buf + s_sync_got, tmp + 3, (size_t)len);
			s_sync_got += (ines_dword_t)len;
			s_sync_time = time(NULL);

			if (s_sync_got >= s_sync_len)
			{
				ines_dword_t  sign = 0;

				// 收齐: 传输层 crc32 -> 载入 -> 算状态摘要
				if ((np_crc32(s_sync_buf, (int)s_sync_len) != s_sync_crc)
				 || (s_load_fn == NULL)
				 || (0 != s_load_fn(s_sync_buf, (int)s_sync_len, s_load_ctx))
				 || ((s_sign_fn != NULL) && (0 != s_sign_fn(&sign, s_load_ctx))))
				{
					np_sync_send_done(1, 0);
					np_sync_finish(NP_SYNC_RESET);
				}
				else
				{
					np_sync_send_done(0, sign);
					s_sync_step = SYNC_WAIT_GO;
					s_sync_time = time(NULL);
				}
			}
		}
		return 1;
	}

	if (cmd == NET_CMD_STATE_GO)
	{
		struct _net_state_go  go;

		if (0 != net_pick_recv_data(&go, (int)sizeof(go)))
			return 0;

		net_del_recv_data((int)sizeof(go));

		if (s_sync_step != SYNC_WAIT_GO)
			return 1;

		// code=1: 主机随后会提交硬复位控制码(延迟线保证双方同帧执行),
		//         **本方不能自行复位**, 否则帧号会错位
		np_sync_finish((go.code == 0) ? NP_SYNC_OK : NP_SYNC_RESET);
		return 1;
	}

	// 其余 STATE_*(不该由从机收到的): 整包丢弃
	if (cmd >= NET_CMD_STATE_REQ)
	{
		net_del_recv_data((int)sizeof(cmd));
		return 1;
	}

	return 0;
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

void np_notify_quit(void)
{
	ines_byte_t  cmd = NET_CMD_QUIT;

	// 只有"已在对战中"才谈得上通知对端; 握手期对方还在等包, 发了也没人处理
	if (s_state != NP_ST_PLAYING)
		return;

	// 尽力而为: 对端可能已经断开, 发送结果不影响本方退出
	net_send(&cmd, (int)sizeof(cmd));

	INES_LOG(LOG_NTY, MOD_NET, ISTR("netplay: notify peer quit\n"));
}

int np_peer_quit(void)
{
	int  ret = s_peer_quit;

	s_peer_quit = 0;

	return ret;
}

int np_begin(int is_server, ines_cstr_t ip, int port, ines_dword_t crc32, int cache_num)
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
		rc = net_connect((net_saddr_t)((ip != NULL) ? ip : ISTR("127.0.0.1")), (net_port_t)port);

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
			 s_is_server ? ISTR("0.0.0.0") : ((ip != NULL) ? ip : ISTR("")),
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
		np_copy_utf8(msg, len, text);
		return NP_POLL_PENDING;
	}

	if (s_is_server)
	{
		switch (s_state)
		{
		case NP_ST_WAIT_CONN:
			if (!net_is_connected())
			{
				np_copy_tstr(text, count_of(text), ISTR("等待客户端的连接..."));
				break;
			}

			s_state      = NP_ST_WAIT_START;
			s_state_time = time(NULL);

			np_copy_tstr(text, count_of(text), ISTR("连接成功，等待验证..."));
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
						np_copy_tstr(text, count_of(text), ISTR("连接错误!"));
						np_fail(text, NULL, 0);
						rc = NP_POLL_FAILED;
					}
					else if (nst.ver != NET_VER)
					{
						// 失败路径 fno 空闲: 回传本机(服务端)版本, 让对端能显示双方版本号
						rsp.code = 1;
						rsp.fno  = (ines_int64_t)NET_VER;

						ines_snprintf(text, count_of(text),
									  ISTR("版本不一致(本机 %u / 对端 %u), 请升级到相同版本!"),
									  (unsigned)NET_VER, (unsigned)nst.ver);
						np_fail(text, &rsp, (int)sizeof(rsp));
						rc = NP_POLL_FAILED;
					}
					else if (nst.crc32 != s_crc32)
					{
						rsp.code = 2;
						np_copy_tstr(text, count_of(text), ISTR("ROM不匹配!"));
						np_fail(text, &rsp, (int)sizeof(rsp));
						rc = NP_POLL_FAILED;
					}
					else
					{
						rsp.code    = 0;
						rsp.is_ntsc = 1;
						// 高 32 位: 服务端的缓冲帧数(旧版对端不读该字段, 互通无影响)
						rsp.fno     = ((ines_int64_t)s_cache_num) << 32;

						net_send(&rsp, (int)sizeof(rsp));
						np_enter_playing();

						np_copy_tstr(text, count_of(text), ISTR("连接成功!"));
						rc = NP_POLL_OK;
					}
					break;
				}
			}

			if (np_is_timeout())
			{
				np_copy_tstr(text, count_of(text), ISTR("客户端验证超时!"));
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
				np_copy_tstr(text, count_of(text), net_get_last_error());
				np_fail(text, NULL, 0);
				rc = NP_POLL_FAILED;
				break;
			}

			if (!net_is_connected())
			{
				np_copy_tstr(text, count_of(text), ISTR("正在连接到服务器..."));
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

			np_copy_tstr(text, count_of(text), ISTR("连接成功，等待验证..."));
			break;

		case NP_ST_WAIT_START:
			{
				struct _net_start_rsp  rsp;

				if (0 == net_pick_recv_data(&rsp, (int)sizeof(rsp)))
				{
					net_del_recv_data((int)sizeof(rsp));

					if (rsp.cmd != NET_CMD_START_RSP)
					{
						np_copy_tstr(text, count_of(text), ISTR("连接错误!"));
						np_fail(text, NULL, 0);
						rc = NP_POLL_FAILED;
					}
					else if (rsp.code == 0)
					{
						// 服务端下发的缓冲帧数(高 32 位): **必须采用**, 不可用本地默认值 ——
						// 两端帧数不同 -> 输入延迟不同 -> 帧号恒定偏移。
						// 读到 0(旧版未携带)或越界 -> 一律拒绝(等价"版本过旧")。
						ines_dword_t  peer_cache = (ines_dword_t)(rsp.fno >> 32);

						if ((peer_cache < NP_CACHE_MIN) || (peer_cache > NP_CACHE_MAX))
						{
							np_copy_tstr(text, count_of(text),
										 ISTR("对端版本过旧(未下发缓冲帧数), 请升级到相同版本!"));
							np_fail(text, NULL, 0);
							rc = NP_POLL_FAILED;
							break;
						}

						s_cache_num = (int)peer_cache;

						np_enter_playing();

						np_copy_tstr(text, count_of(text), ISTR("连接成功!"));
						rc = NP_POLL_OK;
					}
					else if (rsp.code == 1)
					{
						ines_dword_t  peer_ver = (ines_dword_t)(rsp.fno & 0xffffffffu);

						ines_snprintf(text, count_of(text),
									  ISTR("版本不一致(本机 %u / 对端 %u), 请升级到相同版本!"),
									  (unsigned)NET_VER, (unsigned)peer_ver);
						np_fail(text, NULL, 0);
						rc = NP_POLL_FAILED;
					}
					else if (rsp.code == 2)
					{
						np_copy_tstr(text, count_of(text), ISTR("ROM不匹配!"));
						np_fail(text, NULL, 0);
						rc = NP_POLL_FAILED;
					}
					else
					{
						np_copy_tstr(text, count_of(text), ISTR("连接错误!"));
						np_fail(text, NULL, 0);
						rc = NP_POLL_FAILED;
					}
					break;
				}
			}

			if (np_is_timeout())
			{
				np_copy_tstr(text, count_of(text), ISTR("服务器验证超时!"));
				np_fail(text, NULL, 0);
				rc = NP_POLL_FAILED;
			}
			break;

		default:
			break;
		}
	}

	np_copy_utf8(msg, len, text);

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

		// 状态同步包: 只由同步状态机处理(其余命令字落回下面的 switch)
		if ((s_sync_step != SYNC_IDLE) || (cmd >= NET_CMD_STATE_REQ))
		{
			int  used = (s_sync_host != 0) ? np_sync_host_recv() : np_sync_client_recv();

			if (used != 0)
			{
				flag = 0;      // 已消费一个包, 继续解析下一个
				continue;
			}

			// STATE_* 但数据没收全(变长的 DATA 最常见): 留在缓冲里等下一帧,
			// 绝不能落进 default 分支(那样会按 1 字节丢弃, 把包拆坏)
			if (cmd >= NET_CMD_STATE_REQ)
				break;
		}

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

		case NET_CMD_QUIT:
			// 对端主动结束: 置标志交给前端(它在同一帧结束后退回单机), 整包丢弃
			s_peer_quit = 1;
			net_del_recv_data((int)sizeof(cmd));
			flag = 0;

			INES_LOG(LOG_NTY, MOD_NET, ISTR("netplay: peer quitted\n"));
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
	//
	// 注意: 这里依赖 net_is_server()(即"监听 socket 是否存在"), 因此开打后
	// **不能**关闭监听 socket, 否则服务端会按客户机取值(手柄反转)。
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


// ---------------------------------------------------------------------
// 状态同步(联机读档) —— 对外接口
// ---------------------------------------------------------------------

void np_sync_set_handler(np_state_load_fn load, np_state_sign_fn sign, void* user)
{
	s_load_fn  = load;
	s_sign_fn  = sign;
	s_load_ctx = user;
}

int np_sync_begin(const void* buf, int len)
{
	struct _net_state_req  req;

	if ((buf == NULL) || (len <= 0) || (len > NP_SYNC_MAX_SIZE))
		return -1;

	// 只有主机(监听方)能发起, 且必须已在对战中
	if ((s_state != NP_ST_PLAYING) || (net_is_server() == 0))
		return -1;

	if (s_sync_step != SYNC_IDLE)
		return -1;

	s_sync_buf = (ines_byte_t*)malloc((size_t)len);
	if (s_sync_buf == NULL)
		return -1;

	memcpy(s_sync_buf, buf, (size_t)len);

	s_sync_host = 1;
	s_sync_len  = (ines_dword_t)len;
	s_sync_got  = 0;
	s_sync_crc  = np_crc32(buf, len);
	s_sync_step = SYNC_WAIT_RSP;
	s_sync_ret  = NP_SYNC_BUSY;       // 冻结: 前端跳过 doframe
	s_sync_time = time(NULL);

	memset(&req, 0, sizeof(req));
	req.cmd       = NET_CMD_STATE_REQ;
	req.slot      = 0;
	req.total_len = s_sync_len;
	req.crc32     = s_sync_crc;
	req.state_ver = NP_STATE_VER;

	if (net_send_all(&req, (int)sizeof(req)) != (int)sizeof(req))
	{
		np_sync_finish(NP_SYNC_FAILED);
		return -1;
	}

	INES_LOG(LOG_NTY, MOD_NET, ISTR("netplay: state sync begin, %d bytes\n"), len);

	return 0;
}

void np_sync_poll(void)
{
	if (s_sync_step == SYNC_IDLE)
		return;

	if (s_state != NP_ST_PLAYING)
	{
		np_sync_cancel();
		return;
	}

	if (0 == net_is_connected())
	{
		np_sync_finish(NP_SYNC_FAILED);
		return;
	}

	if (s_sync_host != 0)
	{
		if (s_sync_step == SYNC_SEND)
		{
			int  i;

			// 每帧最多 NP_SYNC_CHUNKS 片: 冻结期单帧不宜发太久(100KB 约 25 帧)
			for (i = 0; (i < NP_SYNC_CHUNKS) && (s_sync_got < s_sync_len); i++)
			{
				ines_byte_t  tmp[NP_SYNC_CHUNK + 8];
				int          n = (int)(s_sync_len - s_sync_got);

				if (n > NP_SYNC_CHUNK)
					n = NP_SYNC_CHUNK;

				tmp[0] = NET_CMD_STATE_DATA;
				tmp[1] = (ines_byte_t)(n & 0xff);
				tmp[2] = (ines_byte_t)((n >> 8) & 0xff);

				memcpy(tmp + 3, s_sync_buf + s_sync_got, (size_t)n);

				if (net_send_all(tmp, 3 + n) != (3 + n))
				{
					np_sync_finish(NP_SYNC_FAILED);
					return;
				}

				s_sync_got += (ines_dword_t)n;
			}

			if (s_sync_got >= s_sync_len)
			{
				s_sync_step = SYNC_WAIT_DONE;
				s_sync_time = time(NULL);
			}

			// 发送阶段不判超时: net_send_all() 自带 5 秒超时并返回失败
			return;
		}

		// 等 RSP / DONE: 超时即"两端状态无法保证一致" -> 硬件复位(连接保持)
		if ((s_sync_time != 0) && ((s_sync_time + NP_SYNC_TIMEOUT) < time(NULL)))
		{
			INES_LOG(LOG_ERR, MOD_NET, ISTR("netplay: state sync timeout (step=%d)\n"), s_sync_step);
			np_sync_finish(NP_SYNC_RESET);
		}

		return;
	}

	// 从机: 卡在"收数据 / 等 GO"说明主机侧异常, 状态可能已分叉 -> 交前端结束联网
	if ((s_sync_time != 0) && ((s_sync_time + NP_SYNC_TIMEOUT) < time(NULL)))
	{
		INES_LOG(LOG_ERR, MOD_NET, ISTR("netplay: state sync timeout (step=%d)\n"), s_sync_step);
		np_sync_finish(NP_SYNC_FAILED);
	}
}

int np_sync_state(void)
{
	return s_sync_ret;
}

void np_sync_clear(void)
{
	s_sync_ret = NP_SYNC_NONE;
}

void np_sync_cancel(void)
{
	np_sync_free();

	s_sync_ret = NP_SYNC_NONE;
}
