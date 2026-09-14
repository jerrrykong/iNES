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


void ines_dbg_capture(const ines_host_t* pHost)
{
	ines_int_t  i;

	if ((pHost == NULL) || !s_snap_wanted)
		return;

	idbg_lazy_init();

	if (pHost->status == NES_STATUS_OFF)
	{
		s_snap_back.rom_off = 1;
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
	p->space = space;
	p->addr  = addr;
	p->val   = val;
	s_write_head = next;

	ines_mutex_unlock(&s_mutex_write);

	return 1;
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
	// 3 个内存查看器: 由字体度量算出"整行 + 一点空隙"的宽度, 默认打开时无需横向滚动
	default:                  return [iNESMemoryView suggestedContentSize];
	}
}


@interface iNESDebugManager () <iNESMemoryViewDelegate, iNESGraphicViewDelegate>
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

		// 内存查看器(CPU / VRAM / 精灵内存): 十六进制 + ASCII, 可滚动
		if ((viewId == IDBG_VIEW_MEMORY) || (viewId == IDBG_VIEW_VMEMORY) || (viewId == IDBG_VIEW_SPMEMORY))
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
}

- (void)refreshView:(ines_int_t)viewId
{
	NSWindow*  window = _windows[viewId];

	if ((window == nil) || !window.visible)
		return;

	[window.contentView setNeedsDisplay:YES];
}


#pragma mark - iNESMemoryViewDelegate

// 视图已自行完成"高/低半字节"合并, 这里只负责投递到模拟线程
- (void)memoryView:(iNESMemoryView*)view writeByte:(ines_byte_t)val atAddr:(ines_int_t)addr
{
	ines_dbg_post_write(view.space, addr, val);
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
