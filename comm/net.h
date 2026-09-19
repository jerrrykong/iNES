
#ifndef __NET_H__
#define __NET_H__

#include "idef.h"


#ifdef __cplusplus
extern "C"
{
#endif

typedef  ines_str_t    net_saddr_t;
typedef  ines_word_t   net_port_t;



#define   NET_CMD_NOP         0    // 00 
#define   NET_CMD_START       1    // 01  ver:4  crc32:4
#define   NET_CMD_START_RSP   2    // 02  code:1(0:ok, 1:版本不一致, 2:ROM 不一致, 3:参数异常) is_ntsc:1
                                   //     成功时 fno 高 32 位 = 服务端缓冲帧数(客户端必须采用);
                                   //     失败时 fno = 服务端 NET_VER(让客户端能提示"本机 x / 对端 y")
#define   NET_CMD_FRAME       3    // 05  frame_num:8 joypad_bits:4
#define   NET_CMD_CHAT     0x10    // 10  chat_len:4  chat_str:len
#define   NET_CMD_QUIT     0x11    // 11  本方主动结束对战(仅 cmd, 无载荷); 对端收到后退回单机

// 状态同步(联机读档): 主机把存档字节流分片发给从机, 双方载入同一份后再继续对战。
// 属不兼容修改 -> 随它一起提升 NET_VER(见 docs/netplay-protocol-version-plan.md)
#define   NET_CMD_STATE_REQ   0x20 // 20  slot:1 total_len:4 crc32:4 state_ver:2   主机->从机
#define   NET_CMD_STATE_RSP   0x21 // 21  code:1 (0:ready 1:reject)                从机->主机
#define   NET_CMD_STATE_DATA  0x22 // 22  len:2 data:len(<= NP_SYNC_CHUNK)         主机->从机, N 片
#define   NET_CMD_STATE_DONE  0x23 // 23  code:1 (0:ok 1:fail) sign:4              从机->主机
#define   NET_CMD_STATE_GO    0x24 // 24  code:1 (0:go 1:reset -> 主机随后发硬复位) 主机->从机

#define   NET_CTRL_CODE_HARDRESET  1
#define   NET_CTRL_CODE_SOFTRESET  2



#pragma pack(push, 1)

struct _net_start {
	ines_byte_t  cmd;
	ines_dword_t ver;
	ines_dword_t crc32;
	ines_int64_t  fno;
};

struct _net_start_rsp {
	ines_byte_t  cmd;
	ines_byte_t  code;
	ines_byte_t  is_ntsc;
	ines_int64_t  fno;
};

struct _net_frame {
	ines_byte_t   cmd;
	ines_byte_t   joypad;
	ines_byte_t   ctrl;
};

struct _net_chat {
	ines_byte_t   cmd;
	ines_dword_t  len;
	ines_char_t   text[1];
};

// 状态同步(全部 pack(1); DATA 是变长包: 实际长度 = 3 + len)
struct _net_state_req {
	ines_byte_t   cmd;
	ines_byte_t   slot;
	ines_dword_t  total_len;
	ines_dword_t  crc32;
	ines_word_t   state_ver;
};

struct _net_state_rsp {
	ines_byte_t   cmd;
	ines_byte_t   code;
};

struct _net_state_data {
	ines_byte_t   cmd;
	ines_word_t   len;
	ines_byte_t   data[1];
};

struct _net_state_done {
	ines_byte_t   cmd;
	ines_byte_t   code;
	ines_dword_t  sign;
};

struct _net_state_go {
	ines_byte_t   cmd;
	ines_byte_t   code;
};

#pragma pack(pop)


#define   NET_ST_NONE       0    // 非连网状态
#define   NET_ST_WAIT_CONN  1    // 等待连接(服务器：监听状态，客户端：发起连接状态)
#define   NET_ST_WAIT_START 2    // 等待验证(服务器：连接成功等待对方验证包。客户端已发送验证包等待服务器确认)
#define   NET_ST_PLAYING    3

// 联网协议版本: **单一整数**, 两端必须完全相等才允许连接(不区分主次版本)。
// 规则: 不兼容的协议修改 -> +1; 兼容修改(旧端按原逻辑仍能正常对战) -> 不变。
// 详见 docs/netplay-protocol-version-plan.md 的"版本变更表":
//   1 = 基线(START / START_RSP / FRAME / CHAT / QUIT; START_RSP.fno 高 32 位复用为缓冲帧数)
//   2 = 状态载入(STATE_*)、握手失败回传服务端版本、缓冲帧数强制由主机下发、发现层带版本号
#define   NET_VER     2

int net_init();

ines_cstr_t net_get_last_error();

int net_listen(net_saddr_t  addr, net_port_t  port);

int net_connect(net_saddr_t  addr, net_port_t  port);

int net_is_server();

int net_is_connected();
int net_is_connect_failed();

int net_check_recv();
int net_pick_recv_data(void* pv, int len);
int net_del_recv_data(int len);

int net_recv(void* date, int len);
int net_send(void* date, int len);

/**
 * 完整发送一块数据(循环 send + 等可写), 用于超过发送缓冲的大块数据。
 *
 * 背景: socket 是非阻塞的, net_send() 只 send 一次且不检查返回值 —— 小包(帧包
 * 3 字节)没问题, 但几十 KB 的存档字节流必丢数据, 所以大块一律走本函数。
 *
 * @param data 数据起始地址
 * @param len  字节数
 * @return 实际发出的字节数(== len); -1 参数非法 / 未连接 / 超时(约 5 秒)
 */
int net_send_all(const void* data, int len);

/**
 * 取监听 socket 的实际端口。
 * net_listen() 传 0 时由系统分配端口, 必须靠本函数取回后才能告诉对端。
 *
 * @return 实际端口(主机序); 未监听时为 0
 */
net_port_t net_get_local_port();

int net_close();


void net_fini();


#ifdef __cplusplus
};
#endif


#endif
