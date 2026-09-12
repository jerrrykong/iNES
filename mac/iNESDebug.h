#ifndef __INES_DEBUG_H__
#define __INES_DEBUG_H__


#import <Cocoa/Cocoa.h>

#include "../core/nes.h"


// =====================================================================
// iNES macOS 前端 —— 调试视图公共层
//
// 对应 win32 前端的 6 个独立工具窗口:
//   IDBG_VIEW_PATTERN   -> wPatternTable.c  (图形查看器)
//   IDBG_VIEW_NAMETABLE -> wNameTable.c     (卷轴查看器)
//   IDBG_VIEW_PALETTE   -> wPalette.c       (色盘查看器)
//   IDBG_VIEW_MEMORY    -> wMemory.c        (内存查看器)
//   IDBG_VIEW_VMEMORY   -> wVmemory.c       (图形内存查看器)
//   IDBG_VIEW_SPMEMORY  -> wSPmemory.c      (精灵内存查看器)
//   (win32 的"寄存器"菜单项为空实现, 这里同样不提供)
//
// 线程模型:
//   host 由模拟线程独占。调试视图不能直接读取 host, 因此:
//     * 模拟线程在每个帧末调用 ines_dbg_capture() 采集一份快照(仅当有窗口可见);
//     * 主线程按 win32 的 50ms 节奏调用 ines_dbg_acquire() 取快照副本并重绘;
//     * 内存视图的写入请求由主线程入队(ines_dbg_post_write),
//       模拟线程在帧首调用 ines_dbg_apply_writes() 应用到 host。
// =====================================================================


// ---------------------------------------------------------------------
// 调试视图标识(与 win32 的"调试视图"菜单项顺序一致)
// ---------------------------------------------------------------------
#define IDBG_VIEW_PATTERN      0    // 图形查看器
#define IDBG_VIEW_NAMETABLE    1    // 卷轴查看器
#define IDBG_VIEW_PALETTE      2    // 色盘查看器
#define IDBG_VIEW_MEMORY       3    // 内存查看器
#define IDBG_VIEW_VMEMORY      4    // 图形内存查看器
#define IDBG_VIEW_SPMEMORY     5    // 精灵内存查看器
#define IDBG_VIEW_COUNT        6


// ---------------------------------------------------------------------
// 内存视图的空间标识(对应 win32 的三个内存查看器)
// ---------------------------------------------------------------------
#define IDBG_SPACE_CPU         0    // 程序内存查看器(host.cpu: 0x0000-0xFFFF)
#define IDBG_SPACE_VRAM        1    // 图案内存查看器(host.ppu: 0x0000-0x3FFF)
#define IDBG_SPACE_SPRAM       2    // 精灵内存查看器(host.ppu.sp_RAM: 0x00-0xFF)

// 各空间的地址范围(与 win32 的 MEMORY_BYTES/VMEMORY_BYTES/SPMEMORY_BYTES 一致)
#define IDBG_CPU_MEM_BYTES     0x10000
#define IDBG_VRAM_MEM_BYTES    0x4000
#define IDBG_SPRAM_MEM_BYTES   0x0100


// ---------------------------------------------------------------------
// 图形视图的模式标识(对应 win32 的三个图形查看器)
// ---------------------------------------------------------------------
#define IDBG_GFX_NAMETABLE     0    // 卷轴查看器(4 个 name table 拼成 512x480)
#define IDBG_GFX_PATTERN       1    // 图形查看器(256 个 tile 拼成 128x256)
#define IDBG_GFX_PALETTE       2    // 色盘查看器(2x16 个色块拼成 128x16)


// ---------------------------------------------------------------------
// 快照缓冲尺寸(与 core 的结构体布局一一对应)
// ---------------------------------------------------------------------
#define IDBG_CPU_RAM_SIZE      0x800                        // host.cpu.RAM
#define IDBG_CPU_BANK_COUNT    8                            // host.cpu.mem_bank[8]
#define IDBG_CPU_BANK_SIZE     0x2000                       // 每个 CPU bank 8K
#define IDBG_PPU_BANK_COUNT    12                           // host.ppu.mem_bank[NES_MAX_VMEM_BANKS]
#define IDBG_PPU_BANK_SIZE     0x400                        // 每个 PPU bank 1K
#define IDBG_PAL_SIZE          0x10                         // bg_pal / sp_pal
#define IDBG_SPRAM_SIZE        0x100                        // sp_RAM


// 调试数据快照: 模拟线程写入(加锁), 主线程读取
typedef struct _ines_dbg_snapshot_
{
	ines_int_t   rom_off;                                          // 1 = 未载入 ROM
	ines_byte_t  cpu_ram[IDBG_CPU_RAM_SIZE];                       // 0x0000-0x07FF
	ines_byte_t  cpu_bank[IDBG_CPU_BANK_COUNT][IDBG_CPU_BANK_SIZE];// mem_bank[0..7]
	ines_byte_t  cpu_bank_ok[IDBG_CPU_BANK_COUNT];                 // 1 = 该 bank 指针有效
	ines_byte_t  ppu_bank[IDBG_PPU_BANK_COUNT][IDBG_PPU_BANK_SIZE];// mem_bank[0..11]
	ines_byte_t  ppu_bank_ok[IDBG_PPU_BANK_COUNT];
	ines_byte_t  bg_pal[IDBG_PAL_SIZE];
	ines_byte_t  sp_pal[IDBG_PAL_SIZE];
	ines_byte_t  sp_ram[IDBG_SPRAM_SIZE];
	ines_byte_t  reg_ctrl_1;                                       // PPU 控制寄存器 1
} ines_dbg_snapshot_t;


// ---------------------------------------------------------------------
// 模拟线程 -> 主线程: 快照
// ---------------------------------------------------------------------

// 模拟线程: 声明当前是否有调试窗口可见(为 0 时 ines_dbg_capture 直接返回, 不做拷贝)
void ines_dbg_set_wanted(int wanted);
int  ines_dbg_is_wanted(void);

// 模拟线程: 采集一份快照(内部加锁)。仅在 ines_dbg_is_wanted() 为真时才有实际开销
void ines_dbg_capture(const ines_host_t* pHost);

// 主线程: 取一份快照副本(内部加锁)。返回 0 表示尚无可用快照
int  ines_dbg_acquire(ines_dbg_snapshot_t* pOut);

// 主线程: 从快照里读取指定空间的一个字节。返回 -1 表示该地址不可寻址
int  ines_dbg_read_byte(const ines_dbg_snapshot_t* pSnap, ines_int_t space, ines_int_t addr);


// ---------------------------------------------------------------------
// 主线程 -> 模拟线程: 内存写入请求
// ---------------------------------------------------------------------
#define IDBG_WRITE_QUEUE_SIZE  256

typedef struct _ines_dbg_write_
{
	ines_int_t   space;     // IDBG_SPACE_*
	ines_int_t   addr;
	ines_byte_t  val;
} ines_dbg_write_t;

// 主线程: 提交一个字节写入。队列满时丢弃并返回 0
int  ines_dbg_post_write(ines_int_t space, ines_int_t addr, ines_byte_t val);

// 模拟线程: 应用所有挂起的写入, 返回实际写入的条数
int  ines_dbg_apply_writes(ines_host_t* pHost);


// ---------------------------------------------------------------------
// 窗口管理
// ---------------------------------------------------------------------

// 调试窗口的工具条标题(与 win32 的 IDS_WND_*_TITLE 一致)
ines_cstr_t ines_dbg_view_title(ines_int_t viewId);

// 图形查看器的窗口标题: "<图形查看器>(BG0)" 形式, 与 win32 的 wPT_UpdateTitle 一致
NSString*   ines_dbg_pattern_title(ines_int_t patIdx);


@interface iNESDebugManager : NSObject

// 主窗口(调试窗口的 owner, 用于窗口层级与关闭联动)
@property (nonatomic, assign) NSWindow*  parentWindow;

+ (instancetype)sharedManager;

// 打开指定调试视图(已打开则置前)
- (void)showView:(ines_int_t)viewId;

// 关闭全部调试窗口
- (void)closeAll;

// 当前是否有任一调试窗口可见
- (BOOL)anyWindowVisible;

@end


#endif
