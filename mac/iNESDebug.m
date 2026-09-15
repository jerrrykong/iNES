// =====================================================================
// iNES macOS 前端 —— 调试视图公共层实现
//
// 职责:
//   1) 快照: 模拟线程帧末采集 host, 主线程按 win32 的 50ms 节奏取副本;
//   2) 写队列: 主线程提交内存写入, 模拟线程在帧首应用;
//   3) 窗口管理: 6 个独立工具窗口的创建/置前/关闭, 与"调试视图"菜单对应。
//
// 之所以要做快照: host 是 ines_host_t 值成员, 被帧循环高频访问, 必须单线程独占。
// =====================================================================

#import "iNESDebug.h"
#import "iNESDebugView.h"
#import "iNESRegisterView.h"

#include "../comm/log.h"
#include "../comm/thread.h"

#include <string.h>


// ---------------------------------------------------------------------
// 快照层
// ---------------------------------------------------------------------

// win32 的 SetTimer(hWnd, 1, 50, NULL) 节奏
#define IDBG_REFRESH_INTERVAL   0.05

static ines_dbg_snapshot_t  s_snap_back;                 // 模拟线程写入
static ines_dbg_snapshot_t  s_snap_front;                // 主线程读取
static ines_mutex_t         s_mutex_snap;                // 保护 s_snap_front
static volatile int         s_snap_wanted = 0;           // 有调试窗口可见
static volatile int         s_snap_valid  = 0;           // s_snap_front 是否有效
static int                  s_snap_inited = 0;


// ---------------------------------------------------------------------
// 写队列
// ---------------------------------------------------------------------

static ines_dbg_write_t  s_write_queue[IDBG_WRITE_QUEUE_SIZE];
static int               s_write_head = 0;               // 生产者(主线程)
static int               s_write_tail = 0;               // 消费者(模拟线程)
static ines_mutex_t      s_mutex_write;
static int               s_write_inited = 0;


// 互斥量按需初始化(可能在 main() 之外的时机被首次调用)
static void idbg_lazy_init(void)
{
	if (!s_snap_inited)
	{
		ines_mutex_init(&s_mutex_snap);
		s_snap_inited = 1;
	}

	if (!s_write_inited)
	{
		ines_mutex_init(&s_mutex_write);
		s_write_inited = 1;
	}
}


void ines_dbg_set_wanted(int wanted)
{
	s_snap_wanted = wanted ? 1 : 0;
}

int ines_dbg_is_wanted(void)
{
	return s_snap_wanted;
}


// 寄存器采集: 一律直拷 host 字段, 不调用任何端口读函数
//   (ines_ppu_readlow($2002) 会清 VBlank 与 toggle, ines_apu_read($4015) 会清 IRQ)
static void idbg_capture_regs(const ines_host_t* pHost, ines_dbg_regs_t* pRegs)
{
	const ines_cpu_t*  pCpu = &pHost->cpu;
	const ines_ppu_t*  pPpu = &pHost->ppu;
	const ines_apu_t*  pApu = &pHost->apu;
	ines_int_t         i;
	ines_byte_t        st;

	if ((pHost == NULL) || (pRegs == NULL))
		return;

	/* ---- CPU ---- */
	pRegs->a           = pCpu->reg_A;
	pRegs->x           = pCpu->reg_X;
	pRegs->y           = pCpu->reg_Y;
	pRegs->p           = pCpu->reg_P;
	pRegs->sp          = pCpu->reg_SP;
	pRegs->pc          = pCpu->reg_PC;
	pRegs->int_pending = pCpu->INT_pending;
	pRegs->jammed      = (ines_byte_t)(pCpu->jammed ? 1 : 0);
	pRegs->total_cycles = pCpu->total_cycles;

	/* ---- PPU ---- */
	pRegs->ctrl1             = pPpu->reg_ctrl_1;
	pRegs->ctrl2             = pPpu->reg_ctrl_2;
	pRegs->status            = pPpu->reg_status;
	pRegs->oam_addr          = pPpu->reg_spr_addr;
	pRegs->oam_data          = pPpu->sp_RAM[pPpu->reg_spr_addr];
	pRegs->t                 = pPpu->index_t;
	pRegs->v                 = pPpu->index_v;
	pRegs->fine_x            = (ines_byte_t)(pPpu->index_x & 0x07);
	pRegs->toggle            = pPpu->toggle_2005_2006;
	pRegs->read_2007_buffer  = pPpu->read_2007_buffer;
	pRegs->scanline          = pPpu->current_line;
	pRegs->in_vblank         = (ines_byte_t)(pPpu->in_vblank ? 1 : 0);

	/* ---- APU: 各端口上一次写入值 ---- */
	for (i = 0; i < 4; i++)
	{
		pRegs->pulse1[i]   = pApu->channel_pulse1.reg_ctrl[i];
		pRegs->pulse2[i]   = pApu->channel_pulse2.reg_ctrl[i];
		pRegs->triangle[i] = pApu->channel_triangle.reg_ctrl[i];
		pRegs->noise[i]    = pApu->channel_noise.reg_ctrl[i];
		pRegs->dmc[i]      = pApu->channel_dmc.reg_ctrl[i];
	}

	pRegs->ctrl_4015  = pApu->reg_ctrl;
	pRegs->frame_4017 = pApu->reg_frame_mode;

	// $4015 状态位: 与 ines_apu_read() 的派生逻辑一致, 但不触发其副作用(清 IRQ)
	st = 0;
	if (pApu->irq_flag)                              st |= APU_STATUS_FRAME_IRQ;
	if (pApu->channel_dmc.irq_flag)                  st |= APU_STATUS_DMC_IRQ;
	if (pApu->channel_pulse1.length_counter != 0)    st |= APU_STATUS_PULSE1_ENABLED;
	if (pApu->channel_pulse2.length_counter != 0)    st |= APU_STATUS_PULSE2_ENABLED;
	if (pApu->channel_triangle.length_counter != 0)  st |= APU_STATUS_TRIANGLE_ENABLED;
	if (pApu->channel_noise.length_counter != 0)     st |= APU_STATUS_NOISE_ENABLED;
	if (pApu->channel_dmc.length_counter != 0)       st |= APU_STATUS_DMC_ENABLED;
	pRegs->apu_status   = st;
	pRegs->apu_irq_flag = (ines_byte_t)(pApu->irq_flag ? 1 : 0);

	/* ---- I/O ---- */
	pRegs->dma_high   = pHost->DMA_high;
	pRegs->joy_strobe = (ines_byte_t)(pHost->joypad.input_brush ? 1 : 0);
	pRegs->joy2_bits  = (ines_byte_t)(pHost->joypad.joypad_bits[1] & 0xFF);
}


void ines_dbg_capture(const ines_host_t* pHost)
{
	ines_int_t  i;

	if ((pHost == NULL) || !s_snap_wanted)
		return;

	idbg_lazy_init();

	if (pHost->status == NES_STATUS_OFF)
	{
		s_snap_back.rom_off = 1;
		memset(&s_snap_back.regs, 0, sizeof(s_snap_back.regs));
	}
	else
	{
		s_snap_back.rom_off = 0;

		memcpy(s_snap_back.cpu_ram, pHost->cpu.RAM, sizeof(s_snap_back.cpu_ram));

		for (i = 0; i < IDBG_CPU_BANK_COUNT; i++)
		{
			if (pHost->cpu.mem_bank[i] != NULL)
			{
				memcpy(s_snap_back.cpu_bank[i], pHost->cpu.mem_bank[i], IDBG_CPU_BANK_SIZE);
				s_snap_back.cpu_bank_ok[i] = 1;
			}
			else
			{
				s_snap_back.cpu_bank_ok[i] = 0;
			}
		}

		for (i = 0; i < IDBG_PPU_BANK_COUNT; i++)
		{
			if (pHost->ppu.mem_bank[i] != NULL)
			{
				memcpy(s_snap_back.ppu_bank[i], pHost->ppu.mem_bank[i], IDBG_PPU_BANK_SIZE);
				s_snap_back.ppu_bank_ok[i] = 1;
			}
			else
			{
				s_snap_back.ppu_bank_ok[i] = 0;
			}
		}

		memcpy(s_snap_back.bg_pal, pHost->ppu.bg_pal, sizeof(s_snap_back.bg_pal));
		memcpy(s_snap_back.sp_pal, pHost->ppu.sp_pal, sizeof(s_snap_back.sp_pal));
		memcpy(s_snap_back.sp_ram, pHost->ppu.sp_RAM, sizeof(s_snap_back.sp_ram));

		s_snap_back.reg_ctrl_1 = pHost->ppu.reg_ctrl_1;

		idbg_capture_regs(pHost, &s_snap_back.regs);
	}

	ines_mutex_lock(&s_mutex_snap);
	memcpy(&s_snap_front, &s_snap_back, sizeof(s_snap_front));
	s_snap_valid = 1;
	ines_mutex_unlock(&s_mutex_snap);
}


int ines_dbg_acquire(ines_dbg_snapshot_t* pOut)
{
	int  ok;

	if (pOut == NULL)
		return 0;

	idbg_lazy_init();

	ines_mutex_lock(&s_mutex_snap);
	if (s_snap_valid)
	{
		memcpy(pOut, &s_snap_front, sizeof(*pOut));
		ok = 1;
	}
	else
	{
		ok = 0;
	}
	ines_mutex_unlock(&s_mutex_snap);

	return ok;
}


int ines_dbg_read_byte(const ines_dbg_snapshot_t* pSnap, ines_int_t space, ines_int_t addr)
{
	ines_int_t  bank;

	if ((pSnap == NULL) || pSnap->rom_off)
		return -1;

	switch (space)
	{
	case IDBG_SPACE_CPU:
		// 与 win32 的 wMemory_OnDrawEx 一致: 0x0000-0x07FF 为 RAM,
		// 0x0800-0x1FFF 为镜像(不显示), 0x2000-0x3FFF 为寄存器(不显示),
		// 0x4000 以上按 8K 分块映射到 mem_bank
		if ((addr < 0) || (addr >= IDBG_CPU_MEM_BYTES))
			return -1;
		if (addr < IDBG_CPU_RAM_SIZE)
			return pSnap->cpu_ram[addr];

		bank = addr >> 13;
		if ((bank < 3) || (bank >= IDBG_CPU_BANK_COUNT))
			return -1;
		if (!pSnap->cpu_bank_ok[bank])
			return -1;

		return pSnap->cpu_bank[bank][addr & (IDBG_CPU_BANK_SIZE - 1)];

	case IDBG_SPACE_VRAM:
		if ((addr < 0) || (addr >= IDBG_VRAM_MEM_BYTES))
			return -1;
		if (addr < 0x3000)
			return pSnap->ppu_bank[addr >> 10][addr & (IDBG_PPU_BANK_SIZE - 1)];
		if ((addr >= 0x3F00) && (addr < 0x3F10))
			return pSnap->bg_pal[addr - 0x3F00];
		if ((addr >= 0x3F10) && (addr < 0x3F20))
			return pSnap->sp_pal[addr - 0x3F10];
		return -1;

	case IDBG_SPACE_SPRAM:
		if ((addr < 0) || (addr >= IDBG_SPRAM_MEM_BYTES))
			return -1;
		return pSnap->sp_ram[addr];

	default:
		return -1;
	}
}


// ---------------------------------------------------------------------
// 写队列
// ---------------------------------------------------------------------

// 取 host 中某个地址的可写指针(与 win32 的 wMemory_OnChar / wVMemory_OnChar 寻址一致)。
// 返回 NULL 表示该地址不可写
static ines_byte_t* idbg_host_ptr(ines_host_t* pHost, ines_int_t space, ines_int_t addr)
{
	ines_int_t  bank;

	if (pHost->status == NES_STATUS_OFF)
		return NULL;

	switch (space)
	{
	case IDBG_SPACE_CPU:
		if ((addr < 0) || (addr >= IDBG_CPU_MEM_BYTES))
			return NULL;
		if (addr < IDBG_CPU_RAM_SIZE)
			return &pHost->cpu.RAM[addr];

		bank = addr >> 13;
		if ((bank < 3) || (bank >= IDBG_CPU_BANK_COUNT))
			return NULL;
		if (pHost->cpu.mem_bank[bank] == NULL)
			return NULL;

		return &pHost->cpu.mem_bank[bank][addr & (IDBG_CPU_BANK_SIZE - 1)];

	case IDBG_SPACE_VRAM:
		if ((addr < 0) || (addr >= IDBG_VRAM_MEM_BYTES))
			return NULL;
		if (addr < 0x3000)
		{
			if (pHost->ppu.mem_bank[addr >> 10] == NULL)
				return NULL;
			return &pHost->ppu.mem_bank[addr >> 10][addr & (IDBG_PPU_BANK_SIZE - 1)];
		}
		if ((addr >= 0x3F00) && (addr < 0x3F10))
			return &pHost->ppu.bg_pal[addr - 0x3F00];
		if ((addr >= 0x3F10) && (addr < 0x3F20))
			return &pHost->ppu.sp_pal[addr - 0x3F10];
		return NULL;

	case IDBG_SPACE_SPRAM:
		if ((addr < 0) || (addr >= IDBG_SPRAM_MEM_BYTES))
			return NULL;
		return &pHost->ppu.sp_RAM[addr];

	default:
		return NULL;
	}
}


int ines_dbg_post_write(ines_int_t space, ines_int_t addr, ines_byte_t val)
{
	ines_dbg_write_t*  p;
	int                next;

	idbg_lazy_init();

	ines_mutex_lock(&s_mutex_write);

	next = (s_write_head + 1) % IDBG_WRITE_QUEUE_SIZE;
	if (next == s_write_tail)
	{
		ines_mutex_unlock(&s_mutex_write);
		return 0;
	}

	p = &s_write_queue[s_write_head];
	p->kind  = IDBG_WRITE_MEMORY;
	p->space = space;
	p->addr  = addr;
	p->val   = val;
	s_write_head = next;

	ines_mutex_unlock(&s_mutex_write);

	return 1;
}


int ines_dbg_post_reg_write(ines_int_t regId, ines_int_t val)
{
	ines_dbg_write_t*  p;
	int                next;

	if ((regId < 0) || (regId >= IDBG_REG_COUNT))
		return 0;

	idbg_lazy_init();

	ines_mutex_lock(&s_mutex_write);

	next = (s_write_head + 1) % IDBG_WRITE_QUEUE_SIZE;
	if (next == s_write_tail)
	{
		ines_mutex_unlock(&s_mutex_write);
		return 0;
	}

	p = &s_write_queue[s_write_head];
	p->kind  = IDBG_WRITE_REGISTER;
	p->space = 0;
	p->addr  = regId;
	p->val   = val;
	s_write_head = next;

	ines_mutex_unlock(&s_mutex_write);

	return 1;
}


// 寄存器写入(模拟线程, 帧首执行):
//   * CPU 内部寄存器直接改字段;
//   * PPU/APU 端口一律走 ines_ppu_writelow / ines_apu_write, 保留真实写入副作用
//     ($2000 同步 T 的 name table 位、$4003 重载 length counter、$4015 清 DMC IRQ 等);
//   * PPU 内部 T/V/fine_x 直接改字段(不经过 $2005/$2006 双写, 避免连带改动另一个寄存器);
//   * $4014 走 ines_host_write, 会立即触发 256 字节 DMA + 514 周期(视图侧已做二次确认)。
static int idbg_apply_reg_write(ines_host_t* pHost, ines_int_t regId, ines_int_t val)
{
	ines_cpu_t*  pCpu = &pHost->cpu;
	ines_ppu_t*  pPpu = &pHost->ppu;
	ines_apu_t*  pApu = &pHost->apu;

	if (pHost->status == NES_STATUS_OFF)
		return 0;

	switch (regId)
	{
	/* ---- CPU ---- */
	case IDBG_REG_A:        pCpu->reg_A  = (ines_byte_t)(val & 0xFF);            return 1;
	case IDBG_REG_X:        pCpu->reg_X  = (ines_byte_t)(val & 0xFF);            return 1;
	case IDBG_REG_Y:        pCpu->reg_Y  = (ines_byte_t)(val & 0xFF);            return 1;
	case IDBG_REG_P:        pCpu->reg_P  = (ines_byte_t)(val & 0xFF);            return 1;
	case IDBG_REG_SP:       pCpu->reg_SP = (ines_byte_t)(val & 0xFF);            return 1;
	case IDBG_REG_PC:       pCpu->reg_PC = (ines_word_t)(val & 0xFFFF);          return 1;
	case IDBG_REG_IRQ_PEND: pCpu->INT_pending = (ines_byte_t)(val & 0x07);       return 1;

	/* ---- PPU ---- */
	case IDBG_REG_PPUCTRL:  ines_ppu_writelow(pPpu, 0x2000, (ines_byte_t)val);   return 1;
	case IDBG_REG_PPUMASK:  ines_ppu_writelow(pPpu, 0x2001, (ines_byte_t)val);   return 1;
	case IDBG_REG_OAMADDR:  ines_ppu_writelow(pPpu, 0x2003, (ines_byte_t)val);   return 1;
	case IDBG_REG_OAMDATA:  ines_ppu_writelow(pPpu, 0x2004, (ines_byte_t)val);   return 1;
	case IDBG_REG_PPUDATA:  ines_ppu_writelow(pPpu, 0x2007, (ines_byte_t)val);   return 1;
	case IDBG_REG_PPU_T:    pPpu->index_t = (ines_word_t)(val & 0x7FFF);         return 1;
	case IDBG_REG_PPU_V:    pPpu->index_v = (ines_word_t)(val & 0x7FFF);         return 1;

	/* ---- APU(端口写) ---- */
	case IDBG_REG_P1VOL:
	case IDBG_REG_P1SWP:
	case IDBG_REG_P1TLO:
	case IDBG_REG_P1THI:
		ines_apu_write(pApu, (ines_word_t)(0x4000 + (regId - IDBG_REG_P1VOL)), (ines_byte_t)val);
		return 1;
	case IDBG_REG_P2VOL:
	case IDBG_REG_P2SWP:
	case IDBG_REG_P2TLO:
	case IDBG_REG_P2THI:
		ines_apu_write(pApu, (ines_word_t)(0x4004 + (regId - IDBG_REG_P2VOL)), (ines_byte_t)val);
		return 1;
	case IDBG_REG_TRLIN:
	case IDBG_REG_TR_UNUSED:
	case IDBG_REG_TRTLO:
	case IDBG_REG_TRTHI:
		ines_apu_write(pApu, (ines_word_t)(0x4008 + (regId - IDBG_REG_TRLIN)), (ines_byte_t)val);
		return 1;
	case IDBG_REG_NSVOL:
	case IDBG_REG_NS_UNUSED:
	case IDBG_REG_NSFRQ:
	case IDBG_REG_NSLEN:
		ines_apu_write(pApu, (ines_word_t)(0x400C + (regId - IDBG_REG_NSVOL)), (ines_byte_t)val);
		return 1;
	case IDBG_REG_DMFREQ:
	case IDBG_REG_DMDAC:
	case IDBG_REG_DMADDR:
	case IDBG_REG_DMLEN:
		ines_apu_write(pApu, (ines_word_t)(0x4010 + (regId - IDBG_REG_DMFREQ)), (ines_byte_t)val);
		return 1;
	case IDBG_REG_APUCTRL:  ines_apu_write(pApu, 0x4015, (ines_byte_t)val);     return 1;
	case IDBG_REG_FRAMECTR: ines_apu_write(pApu, 0x4017, (ines_byte_t)val);     return 1;

	/* ---- I/O ---- */
	case IDBG_REG_OAMDMA:   ines_host_write(pHost, 0x4014, (ines_byte_t)val);   return 1;
	case IDBG_REG_JOYPAD1:  ines_host_write(pHost, 0x4016, (ines_byte_t)val);   return 1;

	/* ---- 只读项(PPUSTATUS/PPUSCROLL/SCANLINE/VBLANK/TOGGLE/APUSTAT/JOYPAD2/CYCLES) ---- */
	default:
		return 0;
	}
}


int ines_dbg_apply_writes(ines_host_t* pHost)
{
	int  count = 0;

	if (pHost == NULL)
		return 0;

	idbg_lazy_init();

	for (;;)
	{
		ines_dbg_write_t   w;
		ines_byte_t*       p;

		ines_mutex_lock(&s_mutex_write);
		if (s_write_tail == s_write_head)
		{
			ines_mutex_unlock(&s_mutex_write);
			break;
		}
		w = s_write_queue[s_write_tail];
		s_write_tail = (s_write_tail + 1) % IDBG_WRITE_QUEUE_SIZE;
		ines_mutex_unlock(&s_mutex_write);

		if (w.kind == IDBG_WRITE_REGISTER)
		{
			count += idbg_apply_reg_write(pHost, w.addr, w.val);
			continue;
		}

		p = idbg_host_ptr(pHost, w.space, w.addr);
		if (p == NULL)
			continue;

		*p = w.val;
		count++;
	}

	return count;
}


// ---------------------------------------------------------------------
// 窗口管理
// ---------------------------------------------------------------------

ines_cstr_t ines_dbg_view_title(ines_int_t viewId)
{
	switch (viewId)
	{
	case IDBG_VIEW_PATTERN:   return "图形查看器";
	case IDBG_VIEW_NAMETABLE: return "卷轴查看器";
	case IDBG_VIEW_PALETTE:   return "色盘查看器";
	case IDBG_VIEW_MEMORY:    return "内存查看器";
	case IDBG_VIEW_VMEMORY:   return "图形内存查看器";
	case IDBG_VIEW_SPMEMORY:  return "精灵内存查看器";
	case IDBG_VIEW_REGISTER:  return "寄存器查看器";
	default:                  return "调试窗口";
	}
}


NSString* ines_dbg_pattern_title(ines_int_t patIdx)
{
	return [NSString stringWithFormat:@"%s(%s%d)", "图形查看器",
			((patIdx & 0x04) ? "SP" : "BG"), (int)(patIdx & 0x03)];
}


// 各视图的初始客户区尺寸(与 win32 的 OnCreate 一致)
static NSSize idbg_initial_content_size(ines_int_t viewId)
{
	switch (viewId)
	{
	case IDBG_VIEW_NAMETABLE: return NSMakeSize(SCREEN_WIDTH * 2, SCREEN_HEIGHT * 2);   // 512x480
	case IDBG_VIEW_PATTERN:   return NSMakeSize(8 * 16, 8 * 32);                        // 128x256
	case IDBG_VIEW_PALETTE:   return NSMakeSize(8 * 16, 8 * 2);                         // 128x16
	// 寄存器查看器: 名称/地址/值/位格/说明 五列, 由字体度量算出整行宽度
	case IDBG_VIEW_REGISTER:  return [iNESRegisterView suggestedContentSize];
	// 3 个内存查看器: 由字体度量算出"整行 + 一点空隙"的宽度, 默认打开时无需横向滚动
	default:                  return [iNESMemoryView suggestedContentSize];
	}
}


@interface iNESDebugManager () <iNESMemoryViewDelegate, iNESGraphicViewDelegate, iNESRegisterViewDelegate>
{
	// 窗口一经创建即常驻(关闭后再次选择菜单会重新显示, 保留滚动位置等状态),
	// 因此无需额外的视图引用: contentView 由窗口自身持有。
	NSWindow*             _windows[IDBG_VIEW_COUNT];
	ines_dbg_snapshot_t   _snapshot;        // 主线程持有的快照副本
	BOOL                  _haveSnapshot;
	NSTimer*              _timer;
}

- (void)tick;
- (void)refreshView:(ines_int_t)viewId;

@end


@implementation iNESDebugManager

+ (instancetype)sharedManager
{
	static iNESDebugManager*  s_manager = nil;
	static dispatch_once_t    once;

	dispatch_once(&once, ^{ s_manager = [[iNESDebugManager alloc] init]; });

	return s_manager;
}

- (instancetype)init
{
	self = [super init];
	if (self == nil)
		return nil;

	_timer = [NSTimer timerWithTimeInterval:IDBG_REFRESH_INTERVAL
									 target:self
								   selector:@selector(tick)
								   userInfo:nil
									repeats:YES];
	[[NSRunLoop mainRunLoop] addTimer:_timer forMode:NSRunLoopCommonModes];

	return self;
}

- (BOOL)anyWindowVisible
{
	ines_int_t  i;

	for (i = 0; i < IDBG_VIEW_COUNT; i++)
	{
		if ((_windows[i] != nil) && _windows[i].visible)
			return YES;
	}

	return NO;
}

- (void)showView:(ines_int_t)viewId
{
	NSWindow*   window;
	NSView*     content;
	NSString*   title;

	if ((viewId < 0) || (viewId >= IDBG_VIEW_COUNT))
		return;

	window = _windows[viewId];

	if (window == nil)
	{
		NSSize  content_size;

		title        = [NSString stringWithUTF8String:ines_dbg_view_title(viewId)];
		content      = nil;
		content_size = idbg_initial_content_size(viewId);

		// 寄存器查看器(CPU / PPU / APU / IO): 名称 + 地址 + 值 + 位格 + 说明
		if (viewId == IDBG_VIEW_REGISTER)
		{
			iNESRegisterView*  regView = [[iNESRegisterView alloc] init];

			regView.snapshot = &_snapshot;
			regView.delegate = self;

			content = regView;
		}
		// 内存查看器(CPU / VRAM / 精灵内存): 十六进制 + ASCII, 可滚动
		else if ((viewId == IDBG_VIEW_MEMORY) || (viewId == IDBG_VIEW_VMEMORY) || (viewId == IDBG_VIEW_SPMEMORY))
		{
			iNESMemoryView*  memView;
			ines_int_t       space = IDBG_SPACE_CPU;

			if (viewId == IDBG_VIEW_VMEMORY)
				space = IDBG_SPACE_VRAM;
			else if (viewId == IDBG_VIEW_SPMEMORY)
				space = IDBG_SPACE_SPRAM;

			memView = [[iNESMemoryView alloc] initWithSpace:space];
			memView.snapshot = &_snapshot;
			memView.delegate = self;

			content = memView;
		}
		else
		{
			iNESGraphicView*  gfxView;
			ines_int_t        mode = IDBG_GFX_NAMETABLE;

			if (viewId == IDBG_VIEW_PATTERN)
				mode = IDBG_GFX_PATTERN;
			else if (viewId == IDBG_VIEW_PALETTE)
				mode = IDBG_GFX_PALETTE;

			gfxView = [[iNESGraphicView alloc] initWithMode:mode];
			gfxView.snapshot = &_snapshot;
			gfxView.delegate = self;

			if (mode == IDBG_GFX_PATTERN)
				[gfxView setPatIdx:0];

			content = gfxView;
		}

		window = [[NSWindow alloc] initWithContentRect:NSMakeRect(0, 0, content_size.width, content_size.height)
											 styleMask:(NSWindowStyleMaskTitled | NSWindowStyleMaskClosable |
														NSWindowStyleMaskMiniaturizable | NSWindowStyleMaskResizable)
											   backing:NSBackingStoreBuffered
												 defer:NO];
		window.title              = title;
		window.contentView        = content;
		window.releasedWhenClosed = NO;     // ARC 下由本对象强引用持有
		window.contentMinSize     = NSMakeSize(64, 32);

		// 居中后按视图错开摆放, 避免 6 个窗口完全重叠(win32 用 CW_USEDEFAULT 的级联效果)
		[window center];
		{
			NSRect  frame = window.frame;
			CGFloat offset = (CGFloat)(viewId % 3) * 28.0;

			frame.origin.x += offset;
			frame.origin.y -= offset;
			[window setFrame:frame display:NO];
		}

		_windows[viewId] = window;
	}

	[window makeKeyAndOrderFront:nil];
}

- (void)closeAll
{
	ines_int_t  i;

	for (i = 0; i < IDBG_VIEW_COUNT; i++)
	{
		if (_windows[i] != nil)
			[_windows[i] close];
	}
}

// 等价 win32 的 50ms 定时器: 取快照副本并让可见视图重绘
- (void)tick
{
	BOOL  visible = [self anyWindowVisible];

	ines_dbg_set_wanted(visible ? 1 : 0);

	if (!visible)
		return;

	_haveSnapshot = ines_dbg_acquire(&_snapshot) ? YES : NO;

	[self refreshView:IDBG_VIEW_PATTERN];
	[self refreshView:IDBG_VIEW_NAMETABLE];
	[self refreshView:IDBG_VIEW_PALETTE];
	[self refreshView:IDBG_VIEW_MEMORY];
	[self refreshView:IDBG_VIEW_VMEMORY];
	[self refreshView:IDBG_VIEW_SPMEMORY];
	[self refreshView:IDBG_VIEW_REGISTER];
}

- (void)refreshView:(ines_int_t)viewId
{
	NSWindow*  window = _windows[viewId];

	if ((window == nil) || !window.visible)
		return;

	// 寄存器查看器: 行级脏判定, 只重绘值变化的行(暂停时零重绘);
	// 其余视图维持原 50ms 全量重绘
	if (viewId == IDBG_VIEW_REGISTER)
		[(iNESRegisterView*)window.contentView refreshForTick];
	else
		[window.contentView setNeedsDisplay:YES];
}


#pragma mark - iNESMemoryViewDelegate

// 视图已自行完成"高/低半字节"合并, 这里只负责投递到模拟线程
- (void)memoryView:(iNESMemoryView*)view writeByte:(ines_byte_t)val atAddr:(ines_int_t)addr
{
	ines_dbg_post_write(view.space, addr, val);
}


#pragma mark - iNESRegisterViewDelegate

// 视图已按"整值 / 单个位"合并好, 这里只负责投递到模拟线程(帧首应用)
- (void)registerView:(iNESRegisterView*)view writeReg:(ines_int_t)regId value:(ines_int_t)val
{
	ines_dbg_post_reg_write(regId, val);
}

// $4014 OAMDMA 会立即触发 256 字节 DMA + 514 周期: 写前确认一次(按住 Shift 跳过)
- (BOOL)registerView:(iNESRegisterView*)view confirmReg:(ines_int_t)regId value:(ines_int_t)val
{
	NSAlert*  alert;

	if (regId != IDBG_REG_OAMDMA)
		return YES;

	if (([NSEvent modifierFlags] & NSEventModifierFlagShift) != 0)
		return YES;

	alert = [[NSAlert alloc] init];
	alert.alertStyle  = NSAlertStyleWarning;
	alert.messageText = @"写入 $4014 (OAMDMA)";
	alert.informativeText = [NSString stringWithFormat:
							 @"将立即从 $%02X00 传送 256 字节到精灵内存, 并消耗 514 个 CPU 周期。",
							 (int)(val & 0xFF)];
	[alert addButtonWithTitle:@"确定"];
	[alert addButtonWithTitle:@"取消"];

	return ([alert runModal] == NSAlertFirstButtonReturn);
}


#pragma mark - iNESGraphicViewDelegate

- (void)graphicView:(iNESGraphicView*)view didChangePatternIndex:(ines_int_t)patIdx
{
	ines_int_t  i;

	for (i = 0; i < IDBG_VIEW_COUNT; i++)
	{
		if ((_windows[i] != nil) && (_windows[i].contentView == view))
		{
			_windows[i].title = ines_dbg_pattern_title(patIdx);
			break;
		}
	}
}

@end
