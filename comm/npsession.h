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

/**
 * 主动结束对战时给对端发一次"我退出了"(NET_CMD_QUIT), 尽力而为。
 *
 * 必须与 np_frame_* 同一线程调用(对战期 comm/net.c 由模拟线程独占; win32 的
 * OnIdle 也在主线程, 可直接调), 之后由调用方执行 np_end()。
 * 发送失败(对端已断)不影响本方退出, 故无返回值。
 */
void np_notify_quit(void);

/**
 * 取并清除"对端已主动退出"标志(一次性)。
 *
 * 模拟线程每帧在 np_frame_begin() 之后查询: 返回非 0 表示对端点了"结束/退出",
 * 本方应结束联网并退回单机模式。
 */
int  np_peer_quit(void);

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


// =====================================================================
// 状态同步(联机读档) —— 详见 docs/netplay-state-sync-plan.md
//
// 只有主机(服务端)能发起: 把存档字节流分片发给从机, 双方载入**同一份**字节流,
// 载入成功后一起解冻继续对战。冻结期双方都不推进帧(前端见 NP_SYNC_BUSY 后跳过
// doframe), 但**仍要继续调用** np_frame_begin() 与 np_sync_poll(), 否则收不到包。
//
// 失败处理(需求拍板):
//   * 任一方载入不成功 -> NP_SYNC_RESET: **连接保持**, 主机在解冻后的首帧提交
//     NET_CTRL_CODE_HARDRESET(走现有 ctrl + 延迟线, 双方同帧复位);
//   * 只有链路断开 / 超时才 NP_SYNC_FAILED(此时状态可能已经分叉, 前端结束联网)。
// =====================================================================

// np_sync_state() 的取值
#define NP_SYNC_NONE     0   // 无同步
#define NP_SYNC_BUSY     1   // 同步进行中(双方冻结)
#define NP_SYNC_OK       2   // 成功, 继续对战
#define NP_SYNC_RESET    3   // 载入失败, 需要硬件复位(仅主机执行, 从机忽略)
#define NP_SYNC_FAILED   4   // 链路断开/超时, 前端应结束联网

// 单片载荷上限(接收缓冲 1024B, 留余量)
#define NP_SYNC_CHUNK      512
// 每帧最多发几片: 冻结期单帧不宜发太多, 100KB 存档约 25 帧(0.4s)
#define NP_SYNC_CHUNKS     8
// 存档总长上限(超出则从机 reject)
#define NP_SYNC_MAX_SIZE   (512 * 1024)
// 同步超时(秒)
#define NP_SYNC_TIMEOUT    5
// 存档格式版本(与 core/nes.c 的 INES_STATE_HEADER_VERSION 对齐)
#define NP_STATE_VER       1

/**
 * 从内存载入存档。会话层不认识 core, 由前端提供(通常是"写临时文件 -> ines_load_state")。
 *
 * @param buf  存档字节流
 * @param len  字节数
 * @param user np_sync_set_handler() 传入的上下文
 * @return 0 载入成功; 非 0 失败
 */
typedef int (*np_state_load_fn)(const void* buf, int len, void* user);

/**
 * 算当前状态的摘要(用于两端比对"是否载入了同一份状态")。可为 NULL —— 为 NULL 时
 * 只做传输层 crc32 校验, 不做语义校验。
 *
 * @param out_sign [out] 摘要
 * @param user     上下文
 * @return 0 成功; 非 0 失败
 */
typedef int (*np_state_sign_fn)(ines_dword_t* out_sign, void* user);

/** 注册载入/摘要回调(应在联网开始前后各注册一次均可, 只保存函数指针)。 */
void np_sync_set_handler(np_state_load_fn load, np_state_sign_fn sign, void* user);

/**
 * 主机发起一次状态同步(把整份存档字节流发给从机)。
 *
 * @param buf 存档字节流(内部会拷一份, 调用后可立即释放)
 * @param len 字节数(> 0 且 <= NP_SYNC_MAX_SIZE)
 * @return 0 已发起(进入 NP_SYNC_BUSY); -1 未在对战中 / 非主机 / 参数非法 / 已在同步中
 */
int  np_sync_begin(const void* buf, int len);

/**
 * 推进同步(模拟线程每帧调用一次; 从机无需调用, 它在 np_frame_begin() 里被动应答)。
 * 负责: 分片发送、超时判定、收尾。
 */
void np_sync_poll(void);

/** 当前同步状态, 取值 NP_SYNC_*。 */
int  np_sync_state(void);

/** 取走结果后把状态清回 NP_SYNC_NONE(前端消费一次结果后调用)。 */
void np_sync_clear(void);

/** 取消进行中的同步(退出联网/换 ROM 等), 不发包, 只释放内部缓冲。 */
void np_sync_cancel(void);


#ifdef __cplusplus
};
#endif


#endif
