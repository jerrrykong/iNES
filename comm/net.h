
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
#define   NET_CMD_START_RSP   2    // 02  code:1(0:ok,1:version not match, 2:crc32 not match) is_ntsc:1
#define   NET_CMD_FRAME       3    // 05  frame_num:8 joypad_bits:4
#define   NET_CMD_CHAT     0x10    // 10  chat_len:4  chat_str:len

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

#pragma pack(pop)


#define   NET_ST_NONE       0    // 非连网状态
#define   NET_ST_WAIT_CONN  1    // 等待连接(服务器：监听状态，客户端：发起连接状态)
#define   NET_ST_WAIT_START 2    // 等待验证(服务器：连接成功等待对方验证包。客户端已发送验证包等待服务器确认)
#define   NET_ST_PLAYING    3

#define   NET_VER     0x0101

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


int net_close();


void net_fini();


#ifdef __cplusplus
};
#endif


#endif
