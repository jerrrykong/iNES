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
//   IDBG_VIEW_REGISTER  -> iNESRegisterView (寄存器查看器; win32 对应 win32/wRegister.c,
//                                            见 docs/register-view-plan.md)
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
#define IDBG_VIEW_REGISTER     6    // 寄存器查看器(win32: win32/wRegister.c)
#define IDBG_VIEW_COUNT        7


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
// 寄存器查看器: 寄存器标识(ines_dbg_post_reg_write 的 regId)
//
// 取值来源与写入路径见 docs/register-view-plan.md §3; 这里只列标识,
// 展示用的位名/可写掩码放在 iNESRegisterView.m 的静态表里。
// ---------------------------------------------------------------------
#define IDBG_REG_A           0     // CPU 累加器
#define IDBG_REG_X           1     // CPU 变址 X
#define IDBG_REG_Y           2     // CPU 变址 Y
#define IDBG_REG_P           3     // CPU 状态寄存器
#define IDBG_REG_SP          4     // CPU 栈指针
#define IDBG_REG_PC          5     // CPU 程序计数器(16 位)
#define IDBG_REG_IRQ_PEND    6     // CPU 中断挂起(NMI/MMC/APU)
#define IDBG_REG_CYCLES      7     // CPU 累计周期(只读, 64 位)

#define IDBG_REG_PPUCTRL     8     // $2000
#define IDBG_REG_PPUMASK     9     // $2001
#define IDBG_REG_PPUSTATUS   10    // $2002(只读)
#define IDBG_REG_OAMADDR     11    // $2003
#define IDBG_REG_OAMDATA     12    // $2004
#define IDBG_REG_PPUSCROLL   13    // $2005(只读, 由 T/fine_x 反算)
#define IDBG_REG_PPU_T       14    // $2006 内部 T(16 位, 直改字段)
#define IDBG_REG_PPU_V       15    // $2006 内部 V(16 位, 直改字段)
#define IDBG_REG_PPUDATA     16    // $2007(只写, 显示读缓冲)
#define IDBG_REG_SCANLINE    17    // 当前扫描行(只读)
#define IDBG_REG_VBLANK      18    // 是否处于 VBlank(只读)
#define IDBG_REG_TOGGLE      19    // $2005/$2006 写入翻转(只读)

#define IDBG_REG_P1VOL       20    // $4000
#define IDBG_REG_P1SWP       21    // $4001
#define IDBG_REG_P1TLO       22    // $4002
#define IDBG_REG_P1THI       23    // $4003
#define IDBG_REG_P2VOL       24    // $4004
#define IDBG_REG_P2SWP       25    // $4005
#define IDBG_REG_P2TLO       26    // $4006
#define IDBG_REG_P2THI       27    // $4007
#define IDBG_REG_TRLIN       28    // $4008
#define IDBG_REG_TR_UNUSED   29    // $4009(保留)
#define IDBG_REG_TRTLO       30    // $400A
#define IDBG_REG_TRTHI       31    // $400B
#define IDBG_REG_NSVOL       32    // $400C
#define IDBG_REG_NS_UNUSED   33    // $400D(保留)
#define IDBG_REG_NSFRQ       34    // $400E
#define IDBG_REG_NSLEN       35    // $400F
#define IDBG_REG_DMFREQ      36    // $4010
#define IDBG_REG_DMDAC       37    // $4011
#define IDBG_REG_DMADDR      38    // $4012
#define IDBG_REG_DMLEN       39    // $4013
#define IDBG_REG_APUCTRL     40    // $4015(写)
#define IDBG_REG_APUSTAT     41    // $4015(只读, 由内部状态派生)
#define IDBG_REG_FRAMECTR    42    // $4017(写)

#define IDBG_REG_OAMDMA      43    // $4014(只写, 写即触发 DMA)
#define IDBG_REG_JOYPAD1     44    // $4016(写: strobe)
#define IDBG_REG_JOYPAD2     45    // $4017(读: 手柄 2 状态)

#define IDBG_REG_COUNT       46


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


// ---------------------------------------------------------------------
// 寄存器快照(模拟线程直拷字段, 绝不走端口读路径:
//   ines_ppu_readlow($2002) 会清 VBlank, ines_apu_read($4015) 会清 IRQ)
// ---------------------------------------------------------------------
typedef struct _ines_dbg_regs_
{
	/* ---- CPU ---- */
	ines_byte_t   a, x, y, p, sp;
	ines_word_t   pc;
	ines_byte_t   int_pending;      // bit0 NMI / bit1 MMC / bit2 APU
	ines_byte_t   jammed;
	ines_int64_t  total_cycles;

	/* ---- PPU ---- */
	ines_byte_t   ctrl1;            // $2000
	ines_byte_t   ctrl2;            // $2001
	ines_byte_t   status;           // $2002
	ines_byte_t   oam_addr;         // $2003
	ines_byte_t   oam_data;         // $2004 当前值(sp_RAM[oam_addr])
	ines_word_t   t;                // $2006 内部 T
	ines_word_t   v;                // $2006 内部 V
	ines_byte_t   fine_x;           // $2005 第一次写入的低 3 位
	ines_byte_t   toggle;           // $2005/$2006 翻转
	ines_byte_t   read_2007_buffer; // $2007 读缓冲
	ines_int_t    scanline;
	ines_byte_t   in_vblank;

	/* ---- APU(各端口上一次写入值) ---- */
	ines_byte_t   pulse1[4];        // $4000-$4003
	ines_byte_t   pulse2[4];        // $4004-$4007
	ines_byte_t   triangle[4];      // $4008-$400B
	ines_byte_t   noise[4];         // $400C-$400F
	ines_byte_t   dmc[4];           // $4010-$4013
	ines_byte_t   ctrl_4015;        // apu.reg_ctrl(写入值)
	ines_byte_t   frame_4017;       // apu.reg_frame_mode
	ines_byte_t   apu_status;       // 派生状态位
	ines_byte_t   apu_irq_flag;     // frame IRQ 标记

	/* ---- I/O ---- */
	ines_byte_t   dma_high;         // host.DMA_high($4014 上次写入值)
	ines_byte_t   joy_strobe;       // $4016 strobe
	ines_byte_t   joy2_bits;        // $4017 读到的手柄 2 状态
} ines_dbg_regs_t;


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
	ines_dbg_regs_t  regs;                                          // CPU/PPU/APU/IO 寄存器
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

#define IDBG_WRITE_MEMORY      0    // 内存写入(space + addr)
#define IDBG_WRITE_REGISTER    1    // 寄存器写入(addr = IDBG_REG_*, val 为完整值)

typedef struct _ines_dbg_write_
{
	ines_int_t   kind;      // IDBG_WRITE_MEMORY / IDBG_WRITE_REGISTER
	ines_int_t   space;     // 内存写入时有效: IDBG_SPACE_*
	ines_int_t   addr;      // 内存写入: 地址; 寄存器写入: IDBG_REG_*
	ines_int_t   val;       // 内存写入: 一个字节; 寄存器写入: 完整值(8/16 位一次投递)
} ines_dbg_write_t;

// 主线程: 提交一个字节写入。队列满时丢弃并返回 0
int  ines_dbg_post_write(ines_int_t space, ines_int_t addr, ines_byte_t val);

// 主线程: 提交一个寄存器写入(16 位寄存器一次投递, 避免中间态)。队列满时丢弃并返回 0
int  ines_dbg_post_reg_write(ines_int_t regId, ines_int_t val);

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
