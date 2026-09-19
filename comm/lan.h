
#ifndef __LAN_H__
#define __LAN_H__

#include "idef.h"


#ifdef __cplusplus
extern "C"
{
#endif


// =====================================================================
// 局域网快速配对 —— 发现层(comm/lan)
//
// 职责: 用 UDP 广播在局域网内"发布/发现"房间, 只负责找到对端, 不参与对战。
//       与对战链路(comm/net.c 的 TCP)完全独立, 且两者在时间上互斥:
//         发现阶段(面板打开) -> 选定角色 -> 关闭发现 -> 交棒会话层(net_listen/net_connect)
//
// 线程约定: 全部 API 只在主线程调用; socket 为非阻塞, 因此无需加锁。
// 前置条件: 调用 lan_open() 前必须已调用 net_init()(Win32 需要 WSAStartup)。
// 字符集:   报文内文本一律 UTF-8 字节流; 进出本模块的 ines_char_t 由内部转换。
// =====================================================================


#define LAN_PORT            8892    // 发现端口(UDP 广播)
#define LAN_MAGIC           "INESLAN1"
#define LAN_MAGIC_LEN       8
#define LAN_VER             1       // 发现协议版本(与 NET_VER 相互独立)
#define LAN_NICK_MAX        24      // 昵称最大字节数(UTF-8, 不含结尾 0)
#define LAN_ROM_MAX         56      // ROM 名最大字节数(UTF-8, 不含结尾 0)
#define LAN_ROOM_MAX        32      // 房间表上限
#define LAN_TTL_SEC         3       // 超过该秒数没收到 beacon 即剔除
#define LAN_SEND_INTERVAL   1       // 广播间隔(秒), 由调用方的定时器控制
#define LAN_POLL_MAX_PACK   32      // 单次 lan_poll() 最多处理的包数(防 flood)


// 定长广播报文: 全部网络字节序, 共 128 字节
#pragma pack(push, 1)

struct _lan_beacon {
	ines_byte_t  magic[LAN_MAGIC_LEN];  // "INESLAN1"
	ines_dword_t ver;                   // 发现协议版本
	ines_dword_t crc32;                 // ROM 校验码(与握手包里的同一个值)
	ines_word_t  tcp_port;              // 实际对战端口(可能不等于 8891)
	ines_byte_t  region;                // 0 NTSC / 1 PAL(仅提示用)
	ines_byte_t  nick_len;              // ≤ LAN_NICK_MAX
	ines_byte_t  rom_len;               // ≤ LAN_ROM_MAX
	ines_byte_t  cache_num;             // 缓冲帧数(房主设定, 加入方以它为准; 0 = 未携带)
	ines_byte_t  peer_id[16];           // 进程级随机 id: 房间主键 + 过滤自身
	ines_byte_t  nick[LAN_NICK_MAX];    // 昵称(UTF-8, 不补 0)
	ines_byte_t  rom[LAN_ROM_MAX];      // ROM 文件名(UTF-8, 不补 0)
	ines_byte_t  pad[10];               // 补齐到 128 字节
};

typedef struct _lan_beacon  lan_beacon_t;

#pragma pack(pop)


// 房间(发现到的对端)。文本字段已是本地字符集(win32 Unicode 下为宽字符)
struct _lan_room {
	ines_byte_t  peer_id[16];
	ines_dword_t crc32;
	ines_word_t  tcp_port;
	ines_byte_t  region;
	ines_byte_t  cache_num;                 // 房主的缓冲帧数(0 = 对端未携带)
	ines_dword_t addr;                      // 对端 IPv4(网络序)
	ines_dword_t last_seen;                 // 上次收到 beacon 的时间(time(NULL))
	ines_char_t  nick[LAN_NICK_MAX + 1];
	ines_char_t  rom[LAN_ROM_MAX + 1];
};

typedef struct _lan_room  lan_room_t;


/**
 * 生成进程级随机 peer_id(不触碰全局 rand(), 用自带 LCG)。
 *
 * @param out_id 输出 16 字节
 */
void lan_gen_peer_id(ines_byte_t* out_id);

/**
 * 打开发现通道: 创建 UDP socket、允许广播、允许同机多实例同绑、设为非阻塞。
 *
 * @param peer_id 本进程 id(16 字节), 用于过滤自身广播
 * @return 0 成功; -1 失败(原因见 iNES.log)
 */
int  lan_open(const ines_byte_t* peer_id);

/** 关闭发现通道并清空房间表(可重复调用)。 */
void lan_close(void);

/** 发现通道是否已打开。 */
int  lan_is_open(void);

/**
 * 广播一次本机的房间信息(调用方按 LAN_SEND_INTERVAL 秒的节奏调用)。
 *
 * @param crc32    本机 ROM 校验码
 * @param tcp_port 本机实际监听的 TCP 端口(net_get_local_port() 的返回值)
 * @param region   0 NTSC / 1 PAL
 * @param nick     昵称
 * @param rom      ROM 文件名
 * @param cache_num 缓冲帧数(房主设定, 发布后不可改; 加入方直接采用)
 * @return 0 成功; -1 未打开或发送失败
 */
int  lan_advertise(ines_dword_t crc32, ines_word_t tcp_port, ines_byte_t region,
				   ines_cstr_t nick, ines_cstr_t rom, ines_byte_t cache_num);

/**
 * 收一次包并输出房间表快照(调用方按 500ms 节奏调用)。
 * 内部会: 收包(最多 LAN_POLL_MAX_PACK 个) -> 按 peer_id 去重更新 -> 剔除超时项。
 *
 * @param out_rooms 输出缓冲区, 可为 NULL(只做维护、不取快照)
 * @param max_rooms 缓冲区容量(最多拷贝这么多条)
 * @return ≥0 房间数(可能被 max_rooms 截断); -1 未打开
 */
int  lan_poll(lan_room_t* out_rooms, int max_rooms);

/**
 * 按 peer_id 取一个房间(用于点"加入"时取最新的 IP 与端口)。
 *
 * @param peer_id 16 字节
 * @param out_room 输出, 可为 NULL
 * @return 0 找到; -1 未找到(可能刚好超时消失)
 */
int  lan_find(const ines_byte_t* peer_id, lan_room_t* out_room);

/**
 * 网络序 IPv4 转点分十进制文本(给 net_connect() 用)。
 *
 * @param addr 网络序 IPv4
 * @param buf  输出缓冲区
 * @param len  缓冲区字符数
 * @return 0 成功; -1 参数非法
 */
int  lan_addr_str(ines_dword_t addr, ines_str_t buf, int len);


#ifdef __cplusplus
};
#endif


#endif
