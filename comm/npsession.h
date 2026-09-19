#ifndef __INES_NPSESSION_H__
#define __INES_NPSESSION_H__


#include "net.h"


// =====================================================================
// 联网对战会话层(win32 / macOS 共用)
//
// 与 win32 原始实现的对应关系:
//   握手状态机  <- win32/dlgNetPlay.c 的 dlgNetPlay_TimedCheck()
//   帧缓存收发 <- win32/iNES.c 的 send_frame / recv_frame / cache_add_*
//
// 两端都用同一份实现, 是"win ↔ mac 跨端对战"的基础: 握手包、帧包、延迟线
// 语义只有一处定义, 不会出现两端各写一份而悄悄分叉的情况。
//
// 线程约定: 握手期(np_poll)只在主线程调用, 对战期(np_frame_*)只在模拟线程调用;
//           对话框关闭后才会置位联网标志, 两者在时间上不重叠(comm/net.c 不是
//           线程安全的, 因此严禁同时调用)。
// =====================================================================


// 会话状态(与 comm/net.h 的 NET_ST_* 一一对应)
#define NP_ST_NONE        0   // 非联网状态
#define NP_ST_WAIT_CONN   1   // 等待 TCP 连接(服务端: 监听中; 客户端: 连接中)
#define NP_ST_WAIT_START  2   // 等待校验包
#define NP_ST_PLAYING     3   // 对战中

// np_poll() 的返回码
#define NP_POLL_PENDING   0   // 进行中, msg 为当前提示文本
#define NP_POLL_OK        1   // 校验通过, 可以开始对战
#define NP_POLL_FAILED   (-1) // 失败或超时, msg 为原因(链路已关闭)

// 缓冲帧数: 与 win32 下拉框的 1~5 一致, 默认 4(win32 的 net_cache_num 初值)
#define NP_CACHE_MIN      1
#define NP_CACHE_MAX      5
#define NP_CACHE_DEFAULT  4

// 握手超时(秒), 与 win32 dlgNetPlay.c 的 5 秒一致
#define NP_HANDSHAKE_TIMEOUT   5


#ifdef __cplusplus
extern "C"
{
#endif


/** 初始化网络库(等价 win32 的 net_init()), 应在启动时调用一次。 */
int  np_init(void);

/** 关闭网络库, 退出时调用一次。 */
void np_fini(void);

/**
 * 开始建立连接(等价 win32 点"开始"后的 net_listen / net_connect)。
 *
 * @param is_server 1 表示为服务端(监听), 0 表示客户端(连接)
 * @param ip        服务端地址, 客户端使用; 服务端传 NULL 即可。取平台原生编码
 *                  (win32 为 TCHAR/宽字符, macOS 为 UTF-8 char), 与 net_connect() 一致
 * @param port      端口(win32 默认 8891)
 * @param crc32     本方 ROM 的 crc32, 用于与对端校验
 * @param cache_num 缓冲帧数(仅服务端生效, 客户端以服务端下发的为准)
 * @return 0 成功(进入 NP_ST_WAIT_CONN); -1 失败(可用 net_get_last_error() 取原因)
 */
int  np_begin(int is_server, ines_cstr_t ip, int port, ines_dword_t crc32, int cache_num);

/** 结束联网: 关闭链路并清空帧缓存(等价 win32 的 net_close() + is_net_play = 0)。 */
void np_end(void);

/** 当前会话状态, 取值 NP_ST_*。 */
int  np_state(void);

/** 是否服务端(即 win32 的 net_is_server())。 */
int  np_is_server(void);

/** 当前生效的缓冲帧数。 */
int  np_cache_num(void);

/**
 * 推进握手(主线程以 50ms 间隔调用, 等价 win32 的 WM_TIMER 处理)。
 *
 * @param msg 输出当前提示文本(失败时为错误原因), 可为 NULL。**统一为 UTF-8 char**
 *            (win32 侧由 TCHAR 转出, 调用方需按 UTF-8 再转回显示)
 * @param len msg 缓冲区长度
 * @return NP_POLL_PENDING / NP_POLL_OK / NP_POLL_FAILED
 */
int  np_poll(char* msg, ines_size_t len);

/** 帧首收包(模拟线程调用, 等价 win32 的 recv_frame())。 */
void np_frame_begin(void);

/** 本帧是否有可执行的数据(缓存为空表示网络卡, 应跳过本帧等待)。 */
int  np_input_ready(void);

/**
 * 提交本方输入并取回本帧实际使用的输入(模拟线程调用)。
 * 等价 win32 的 cache_add_mine() + send_frame() + cache_get()。
 *
 * @param mine_joypad 本方本帧的按键(8bit)
 * @param mine_ctrl   本方本帧的控制码(0 / NET_CTRL_CODE_SOFTRESET / NET_CTRL_CODE_HARDRESET)
 * @param out_main    [out] 主手柄按键
 * @param out_second  [out] 副手柄按键
 * @param out_ctrl    [out] 本帧要执行的控制码, 可为 NULL
 * @return 0 成功; -1 非对战中或缓存为空
 */
int  np_frame_input(ines_byte_t mine_joypad, ines_byte_t mine_ctrl,
					ines_int_t* out_main, ines_int_t* out_second, ines_int_t* out_ctrl);


#ifdef __cplusplus
};
#endif


#endif
