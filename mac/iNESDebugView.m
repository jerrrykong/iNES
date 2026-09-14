// =====================================================================
// iNES macOS 前端 —— 调试视图绘制组件
//
// 与 win32 前端的对应关系:
//   wMemory.c / wVmemory.c / wSPmemory.c  -> iNESMemoryView  (三种 space)
//   wNameTable.c / wPatternTable.c / wPalette.c -> iNESGraphicView (三种 mode)
//
// 绘制布局全部照搬 win32: 等宽字体、以"字符格"为单位的排版与滚动、反色光标。
// 为便于逐行对照, 视图使用 isFlipped = YES(与 win32 的左上原点一致)。
// =====================================================================

#import "iNESDebugView.h"
#import "iNESPalette.h"

#include <math.h>
#include <stdio.h>
#include <string.h>


// ---------------------------------------------------------------------
// 布局常量(与 win32 的 wMemory.c 完全一致)
// ---------------------------------------------------------------------
#define IDBG_LINE_SPLIT_HIGH   3
#define IDBG_BYTES_PER_LINE    16
#define IDBG_HEAD_LINES        1
#define IDBG_ADDR_COLS         6

// 一行正文的字符列数: 16 个 "%02X " (48) + 1 个空格 + 16 个 ASCII 字符
#define IDBG_LINE_BODY_COLS    (IDBG_BYTES_PER_LINE * 3 + 1 + IDBG_BYTES_PER_LINE)   // 65

// 整行字符列数(地址列 + 正文列)与内存窗口的初始客户区高度(高度与 win32 一致)
#define IDBG_LINE_COLS         (IDBG_ADDR_COLS + IDBG_LINE_BODY_COLS)                // 71
#define IDBG_WIN_HEIGHT        512

// ---------------------------------------------------------------------
// 图形视图的固定逻辑尺寸(与 win32 的宏一致)
// ---------------------------------------------------------------------
#define IDBG_NT_WIDTH          (SCREEN_WIDTH * 2)    // 512
#define IDBG_NT_HEIGHT         (SCREEN_HEIGHT * 2)   // 480
#define IDBG_PT_WIDTH          (8 * 16)              // 128
#define IDBG_PT_HEIGHT         (8 * 32)              // 256
#define IDBG_PAL_WIDTH         (8 * 16)              // 128
#define IDBG_PAL_HEIGHT        (8 * 2)               // 16

// 未载入 ROM 时的填充灰, 与 win32 的 RGB(128,128,128) 一致
#define IDBG_OFF_GRAY          (128.0 / 255.0)


#pragma mark - 通用工具

// 由半字节取十六进制字符
static inline char idbg_hex_char(ines_int_t v)
{
	return (char)((v < 10) ? ('0' + v) : ('A' + (v - 10)));
}

// 未载入 ROM 时填充灰色底
static void idbg_fill_off_color(void)
{
	[[NSColor colorWithSRGBRed:IDBG_OFF_GRAY green:IDBG_OFF_GRAY blue:IDBG_OFF_GRAY alpha:1.0] setFill];
	NSRectFill(NSMakeRect(0, 0, 1e6, 1e6));
}

// 内存视图的字体: 与 win32 的 LOGFONT(Courier New, 12px, 细体) 对齐;
// 取不到 Courier New 时退化为系统等宽字体, 排版不受影响。
// 视图实例与"初始窗口尺寸"都必须走这里, 否则算出来的客户区宽度会和实际排版对不上。
static NSFont* idbg_memory_font(void)
{
	NSFont*  font = [NSFont fontWithName:@"Courier New" size:12.0];

	if (font == nil)
		font = [NSFont userFixedPitchFontOfSize:12.0];

	return font;
}


#pragma mark - iNESMemoryView

@interface iNESMemoryView ()
{
	ines_int_t     _startLine;      // 垂直滚动起点(行)
	ines_int_t     _startCol;       // 水平滚动起点(字符列)
	ines_int_t     _curAddr;        // 光标地址
	ines_int_t     _halfChar;       // 0 = 待输入高半字节, 1 = 待输入低半字节
	CGFloat        _charW;          // 字符宽度(像素, 必须保留小数, 见 rebuildMetrics)
	ines_int_t     _charH;          // 字符高度(像素)
	ines_int_t     _rows;           // 当前可见行数
	ines_int_t     _cols;           // 当前可见字符列数
	ines_int_t     _totalRows;      // 总行数
	ines_int_t     _totalCols;      // 总字符列数
	CGFloat        _wheelX;         // 滚轮横向累积量(像素, 仅触控板使用)
	CGFloat        _wheelY;         // 滚轮纵向累积量(像素, 仅触控板使用)
	NSFont*        _font;
	NSDictionary*  _attrs;
	NSScroller*    _hScroller;
	NSScroller*    _vScroller;
}

- (ines_int_t)memBytes;
- (void)rebuildMetrics;
- (void)layoutScrollers;
- (void)updateScrollers;
- (void)setCursor:(ines_int_t)addr;
- (void)scrollBy:(ines_int_t)bar code:(ines_int_t)code;
- (void)ensureCursorVisible;

@end


@implementation iNESMemoryView

- (instancetype)initWithSpace:(ines_int_t)space
{
	NSSize  content = [iNESMemoryView suggestedContentSize];

	// 初始 frame 与窗口管理器给出的客户区尺寸保持一致(见 iNESDebug.m: idbg_initial_content_size)
	self = [super initWithFrame:NSMakeRect(0, 0, content.width, content.height)];
	if (self == nil)
		return nil;

	_space     = space;
	_startLine = 0;
	_startCol  = 0;
	_curAddr   = 0;
	_halfChar  = 0;

	_font  = idbg_memory_font();

	_attrs = @{ NSFontAttributeName: _font,
				NSForegroundColorAttributeName: [NSColor textColor] };

	[self rebuildMetrics];

	_hScroller = [[NSScroller alloc] initWithFrame:NSMakeRect(0, 0, 100, 15)];
	_vScroller = [[NSScroller alloc] initWithFrame:NSMakeRect(0, 0, 15, 100)];
	[_hScroller setScrollerStyle:NSScrollerStyleLegacy];
	[_vScroller setScrollerStyle:NSScrollerStyleLegacy];
	[_hScroller setControlSize:NSControlSizeSmall];
	[_vScroller setControlSize:NSControlSizeSmall];
	[_hScroller setTarget:self];
	[_vScroller setTarget:self];
	[_hScroller setAction:@selector(scrollerAction:)];
	[_vScroller setAction:@selector(scrollerAction:)];
	[self addSubview:_hScroller];
	[self addSubview:_vScroller];

	// 先按初始尺寸摆一次(之后由 viewDidMoveToWindow / 尺寸变化再校正)
	[self layoutScrollers];
	[self updateScrollers];

	return self;
}

// 推荐的初始客户区大小: 宽度上让整行(地址列 + 十六进制 + 空格 + ASCII 区)完整可见,
// 右侧再留一个字符的空隙(行尾与反色光标不会紧贴垂直滚动条, 看起来也更舒展),
// 最后加上垂直滚动条自身占用的宽度。高度沿用 win32 的 512。
// 度量方式与 rebuildMetrics 完全一致, 所以默认打开时整行可见、不会出现横向滚动。
+ (NSSize)suggestedContentSize
{
	NSSize   sz;
	CGFloat  charW;
	CGFloat  textW;
	CGFloat  scrollerW;

	sz        = [@"X" sizeWithAttributes:@{ NSFontAttributeName: idbg_memory_font() }];
	charW     = (sz.width < 1.0) ? 1.0 : sz.width;
	textW     = (CGFloat)IDBG_LINE_COLS * charW;
	scrollerW = [NSScroller scrollerWidthForControlSize:NSControlSizeSmall
										  scrollerStyle:NSScrollerStyleLegacy];

	return NSMakeSize(ceil(textW + charW + scrollerW), IDBG_WIN_HEIGHT);
}

// 与 win32 的左上原点一致(所有 y 坐标可直接照搬)
- (BOOL)isFlipped
{
	return YES;
}

- (BOOL)acceptsFirstResponder
{
	return YES;
}

- (BOOL)hasContent
{
	return ((_snapshot != NULL) && !_snapshot->rom_off);
}

- (ines_int_t)memBytes
{
	switch (_space)
	{
	case IDBG_SPACE_VRAM:   return IDBG_VRAM_MEM_BYTES;
	case IDBG_SPACE_SPRAM:  return IDBG_SPRAM_MEM_BYTES;
	default:                return IDBG_CPU_MEM_BYTES;
	}
}

// 实测字符宽高 + 计算总行/列数(等价 win32 在 OnCreate 里用 DrawText(DT_CALCRECT) 量的字符格)
- (void)rebuildMetrics
{
	NSSize    sz;

	sz = [@"X" sizeWithAttributes:@{ NSFontAttributeName: _font }];

	// 宽度必须保留小数而不能向上取整: 正文是整行交给 CoreText 排版的, 它按字体的实际
	// advance 逐字符推进(Courier New 12pt = 7.2px), 而光标与鼠标命中都按 _charW 累加。
	// 若在此 ceil 成 8, 每列就多算 0.8px, 越靠右偏差越大(ASCII 区累积到约 40px),
	// 表现为反色光标与实际的字节/字符错位。
	_charW = sz.width;
	if (_charW < 1.0) _charW = 1.0;

	// 高度保持取整: 正文行间距由本视图自己按 _charH 累加, 取整后与光标的 y 公式
	// ((行 + 头行) * _charH + 分隔高)整数等价, 纵向不会错位。
	_charH = (ines_int_t)ceil(sz.height);
	if (_charH < 1) _charH = 1;

	_totalCols = IDBG_LINE_BODY_COLS;
	_totalRows = [self memBytes] / IDBG_BYTES_PER_LINE;
}

// 内容区(扣除滚动条)与 win32 的客户区等价
- (NSSize)contentSize
{
	NSSize   sz = self.bounds.size;
	CGFloat  w  = [NSScroller scrollerWidthForControlSize:NSControlSizeSmall
											scrollerStyle:NSScrollerStyleLegacy];

	return NSMakeSize(MAX(10.0, sz.width - w), MAX(10.0, sz.height - w));
}

// 滚动条的摆放(纵向在右、横向在底; 视图已翻转, 底部即 y 最大处)
- (void)layoutScrollers
{
	NSSize   sz = self.bounds.size;
	CGFloat  w  = [NSScroller scrollerWidthForControlSize:NSControlSizeSmall
											scrollerStyle:NSScrollerStyleLegacy];

	_vScroller.frame = NSMakeRect(sz.width - w, 0, w, MAX(0.0, sz.height - w));
	_hScroller.frame = NSMakeRect(0, sz.height - w, MAX(0.0, sz.width - w), w);
}

- (void)resizeSubviewsWithOldSize:(NSSize)oldSize
{
	[super resizeSubviewsWithOldSize:oldSize];

	[self layoutScrollers];
	[self updateScrollers];
}

// 窗口把 contentView 的尺寸设为内容区大小; 若该尺寸恰好等于视图初始 frame(内存查看器
// 两者都是 512x512), 就不会触发 resizeSubviewsWithOldSize:, 滚动条会停留在 init 里
// 临时 frame(0,0,100,15)/(0,0,15,100) 上 —— 表现为"刚打开时滚动条位置不对, 调整一次
// 窗口大小后才正常"。这里补一次校正。
- (void)viewDidMoveToWindow
{
	[super viewDidMoveToWindow];

	[self layoutScrollers];
	[self updateScrollers];
}

// 等价 win32 的 wMemory_UpdateScrollBar + wMemory_SetCursorVisible
- (void)updateScrollers
{
	NSSize       content = [self contentSize];
	ines_int_t   pageCol  = (ines_int_t)(content.width / _charW);
	ines_int_t   pageRow  = (ines_int_t)((content.height - _charH - IDBG_LINE_SPLIT_HIGH) / _charH);
	ines_int_t   maxCol;
	ines_int_t   maxRow;

	if (pageCol < 1) pageCol = 1;
	if (pageRow < 1) pageRow = 1;

	_cols = pageCol;
	_rows = pageRow;

	// 横向(不需要滚动时保持占位并禁用, 与 win32 的 EnableScrollBar 语义一致)
	if (_totalCols <= pageCol)
	{
		_startCol = 0;
		_hScroller.enabled = NO;
		_hScroller.knobProportion = 1.0;
		_hScroller.doubleValue = 0.0;
	}
	else
	{
		maxCol = _totalCols - pageCol;
		if (_startCol > maxCol) _startCol = maxCol;
		if (_startCol < 0)      _startCol = 0;
		_hScroller.hidden = NO;
		_hScroller.enabled = YES;
		_hScroller.knobProportion = (CGFloat)pageCol / (CGFloat)_totalCols;
		_hScroller.doubleValue = (maxCol > 0) ? ((CGFloat)_startCol / (CGFloat)maxCol) : 0.0;
	}

	// 纵向(同上: 不需要滚动时保持占位并禁用)
	if (_totalRows <= pageRow)
	{
		_startLine = 0;
		_vScroller.enabled = NO;
		_vScroller.knobProportion = 1.0;
		_vScroller.doubleValue = 0.0;
	}
	else
	{
		maxRow = _totalRows - pageRow;
		if (_startLine > maxRow) _startLine = maxRow;
		if (_startLine < 0)      _startLine = 0;
		_vScroller.enabled = YES;
		_vScroller.knobProportion = (CGFloat)pageRow / (CGFloat)_totalRows;
		_vScroller.doubleValue = (maxRow > 0) ? ((CGFloat)_startLine / (CGFloat)maxRow) : 0.0;
	}
}

// 等价 win32 的 wMemory_OnScroll: bar 取 'h' / 'v', code 取 NSScrollerPart
- (void)scrollBy:(ines_int_t)bar code:(ines_int_t)code
{
	ines_int_t   pos;
	ines_int_t   page;
	ines_int_t   maxPos;

	if (bar == 0)
	{
		pos    = _startCol;
		page   = _cols;
		maxPos = MAX(0, _totalCols - page);

		switch (code)
		{
		case NSScrollerDecrementLine: pos -= 1;    break;
		case NSScrollerIncrementLine: pos += 1;    break;
		case NSScrollerDecrementPage: pos -= page; break;
		case NSScrollerIncrementPage: pos += page; break;
		case NSScrollerKnob:
		case NSScrollerKnobSlot:
			pos = (maxPos > 0) ? (ines_int_t)llround(_hScroller.doubleValue * (double)maxPos) : 0;
			break;
		default:
			return;
		}

		if (pos < 0)      pos = 0;
		if (pos > maxPos) pos = maxPos;
		_startCol = pos;
	}
	else
	{
		pos    = _startLine;
		page   = _rows;
		maxPos = MAX(0, _totalRows - page);

		switch (code)
		{
		case NSScrollerDecrementLine: pos -= 1;    break;
		case NSScrollerIncrementLine: pos += 1;    break;
		case NSScrollerDecrementPage: pos -= page; break;
		case NSScrollerIncrementPage: pos += page; break;
		case NSScrollerKnob:
		case NSScrollerKnobSlot:
			pos = (maxPos > 0) ? (ines_int_t)llround(_vScroller.doubleValue * (double)maxPos) : 0;
			break;
		default:
			return;
		}

		if (pos < 0)      pos = 0;
		if (pos > maxPos) pos = maxPos;
		_startLine = pos;
	}

	[self updateScrollers];
	[self setNeedsDisplay:YES];
}

- (void)scrollerAction:(NSScroller*)sender
{
	NSScrollerPart  part = sender.hitPart;
	ines_int_t      bar  = (sender == _hScroller) ? 0 : 1;

	// 系统启用 overlay 样式滚动条时 hitPart 恒为 NSScrollerNoPart,
	// 此时退化为按滑块位置更新(拖拽仍然可用)
	if (part == NSScrollerNoPart)
	{
		[self scrollBy:bar code:NSScrollerKnob];
		return;
	}

	[self scrollBy:bar code:part];
}

- (void)setCursor:(ines_int_t)addr
{
	if ((addr < 0) || (addr >= [self memBytes]))
		return;

	_curAddr  = addr;
	_halfChar = 0;
	[self ensureCursorVisible];
	[self setNeedsDisplay:YES];

	if ([_delegate respondsToSelector:@selector(memoryViewDidChangeCursor:)])
		[_delegate memoryViewDidChangeCursor:self];
}

// 等价 win32 的 wMemory_SetCursorVisible: 让光标始终落在可见区域内
- (void)ensureCursorVisible
{
	ines_int_t   curRow = _curAddr / IDBG_BYTES_PER_LINE;
	ines_int_t   curCol = (_curAddr % IDBG_BYTES_PER_LINE) * 3 + _halfChar;

	if (curRow > _startLine + _rows - 1)
		_startLine = curRow - _rows + 1;
	else if (curRow < _startLine)
		_startLine = curRow;

	if (curCol > _startCol + _cols - 1)
		_startCol = curCol - _cols + 1;
	else if (curCol < _startCol)
		_startCol = curCol;

	[self updateScrollers];
}

#pragma mark 交互

// 滚轮滚动(win32 通过 WM_MOUSEWHEEL / 滚动条实现, 这里直接改 _startLine / _startCol)。
// 方向遵循 NSScrollView 的约定: 增量为正表示查看更上方 / 更左侧的内容。
- (void)scrollWheel:(NSEvent*)event
{
	CGFloat     dy    = event.scrollingDeltaY;
	CGFloat     dx    = event.scrollingDeltaX;
	ines_int_t  dLine = 0;
	ines_int_t  dCol  = 0;

	if (event.hasPreciseScrollingDeltas)
	{
		// 触控板: 增量是像素, 先累积, 攒够一格才滚一行 / 一列
		_wheelY += dy;
		_wheelX += dx;

		while (_wheelY >= _charH)  { _wheelY -= _charH; dLine--; }
		while (_wheelY <= -_charH) { _wheelY += _charH; dLine++; }
		while (_wheelX >= _charW)  { _wheelX -= _charW; dCol--; }
		while (_wheelX <= -_charW) { _wheelX += _charW; dCol++; }
	}
	else
	{
		// 传统滚轮: 增量本身就是行数
		dLine = -(ines_int_t)dy;
		dCol  = -(ines_int_t)dx;
	}

	if ((dLine == 0) && (dCol == 0))
		return;

	if (dLine != 0)
	{
		ines_int_t  maxRow = MAX(0, _totalRows - _rows);

		_startLine += dLine;
		if (_startLine < 0)      _startLine = 0;
		if (_startLine > maxRow) _startLine = maxRow;
	}

	if (dCol != 0)
	{
		ines_int_t  maxCol = MAX(0, _totalCols - _cols);

		_startCol += dCol;
		if (_startCol < 0)      _startCol = 0;
		if (_startCol > maxCol) _startCol = maxCol;
	}

	[self updateScrollers];
	[self setNeedsDisplay:YES];
}

- (void)mouseDown:(NSEvent*)event
{
	NSPoint      pt = [self convertPoint:event.locationInWindow fromView:nil];
	ines_int_t   row, col;

	// 等价 win32 的 wMemory_OnLButtonDown
	pt.x -= IDBG_ADDR_COLS * _charW;
	pt.y -= IDBG_HEAD_LINES * _charH + IDBG_LINE_SPLIT_HIGH;

	if ((pt.x <= 0) || (pt.y <= 0))
		return;

	row = (ines_int_t)(pt.y / _charH) + _startLine;
	col = (ines_int_t)(pt.x / _charW) + _startCol;

	if (col / 3 < IDBG_BYTES_PER_LINE)
	{
		[self setCursor:row * IDBG_BYTES_PER_LINE + col / 3];
	}
	else
	{
		col -= IDBG_BYTES_PER_LINE * 3 + 1;
		if ((col >= 0) && (col < IDBG_BYTES_PER_LINE))
			[self setCursor:row * IDBG_BYTES_PER_LINE + col];
	}
}

- (void)keyDown:(NSEvent*)event
{
	unsigned short  code = event.keyCode;
	NSString*       chars = [event charactersIgnoringModifiers];

	// 等价 win32 的 wMemory_OnKeyDown(方向键与翻页键)
	switch (code)
	{
	case 123:   // 左
		[self setCursor:_curAddr - 1];
		return;
	case 124:   // 右
		[self setCursor:_curAddr + 1];
		return;
	case 126:   // 上
		[self setCursor:_curAddr - IDBG_BYTES_PER_LINE];
		return;
	case 125:   // 下
		[self setCursor:_curAddr + IDBG_BYTES_PER_LINE];
		return;
	case 116:   // PageUp
		[self scrollBy:1 code:NSScrollerDecrementPage];
		return;
	case 121:   // PageDown
		[self scrollBy:1 code:NSScrollerIncrementPage];
		return;
	case 115:   // Home
		_startLine = 0;
		[self updateScrollers];
		[self setNeedsDisplay:YES];
		return;
	case 119:   // End
		_startLine = MAX(0, _totalRows - _rows);
		[self updateScrollers];
		[self setNeedsDisplay:YES];
		return;
	default:
		break;
	}

	// 等价 win32 的 wMemory_OnChar: 十六进制半字节输入
	if ((chars != nil) && (chars.length > 0))
	{
		unichar  c = [chars characterAtIndex:0];
		ines_int_t  val = -1;

		if ((c >= '0') && (c <= '9'))      val = (ines_int_t)(c - '0');
		else if ((c >= 'a') && (c <= 'f')) val = (ines_int_t)(c - 'a') + 10;
		else if ((c >= 'A') && (c <= 'F')) val = (ines_int_t)(c - 'A') + 10;

		if (val >= 0)
		{
			ines_int_t  cur = ines_dbg_read_byte(_snapshot, _space, _curAddr);

			// 该地址不可寻址(等价 win32 的 p_mem == NULL): 丢弃本次输入
			if (cur < 0)
				return;

			// 与 win32 相同: 先填高半字节, 再填低半字节并自动跳到下一字节
			if (_halfChar == 0)
			{
				_halfChar = 1;
				[_delegate memoryView:self writeByte:(ines_byte_t)((cur & 0x0F) | (val << 4))
							   atAddr:_curAddr];
				[self setNeedsDisplay:YES];
			}
			else
			{
				_halfChar = 0;
				[_delegate memoryView:self writeByte:(ines_byte_t)((cur & 0xF0) | val)
							   atAddr:_curAddr];
				[self setCursor:_curAddr + 1];
			}
			return;
		}
	}

	[super keyDown:event];
}

#pragma mark 绘制

// 生成一行的"完整文本"(未做水平滚动), 格式与 win32 完全一致:
//   "%04X  " + 16 * "%02X " + ' ' + 16 个 ASCII(不可打印为 '.')
static void idbg_mem_format_line(const ines_dbg_snapshot_t* pSnap, ines_int_t space,
								 ines_int_t addr, char* pOut, size_t outLen)
{
	size_t       n = 0;
	ines_int_t   i;
	int          v;

	n += (size_t)snprintf(pOut + n, outLen - n, "%04X  ", (unsigned int)(addr & 0xFFFF));

	for (i = 0; i < IDBG_BYTES_PER_LINE; i++)
	{
		v = ines_dbg_read_byte(pSnap, space, addr + i);
		if (v >= 0)
			n += (size_t)snprintf(pOut + n, outLen - n, "%02X ", (unsigned int)v);
		else
			n += (size_t)snprintf(pOut + n, outLen - n, "?? ");
	}

	if (n < outLen)
		pOut[n++] = ' ';

	for (i = 0; i < IDBG_BYTES_PER_LINE; i++)
	{
		v = ines_dbg_read_byte(pSnap, space, addr + i);
		if (n + 2 >= outLen)
			break;
		pOut[n++] = ((v >= 0x20) && (v < 0x7F)) ? (char)v : '.';
	}

	pOut[n] = 0;
}

// 按 win32 的做法做水平滚动: 地址列固定, 其后 65 列整体左移 startCol 列
static NSString* idbg_apply_start_col(NSString* line, ines_int_t startCol)
{
	if (startCol <= 0)
		return line;
	if ((NSUInteger)startCol >= line.length)
		return @"";

	return [line substringFromIndex:(NSUInteger)startCol];
}

- (void)drawRect:(NSRect)dirtyRect
{
	const ines_dbg_snapshot_t*  pSnap = _snapshot;
	NSSize          content;
	ines_int_t      y;
	ines_int_t      iLine;
	ines_int_t      totalLines;
	ines_int_t      addr;
	ines_int_t      iCol;
	ines_int_t      curRow;
	ines_int_t      curCol;
	char            buf[256];

	// 未载入 ROM: 纯灰底(与 win32 一致)
	if ((pSnap == NULL) || pSnap->rom_off)
	{
		idbg_fill_off_color();
		return;
	}

	[[NSColor textBackgroundColor] setFill];
	NSRectFill(self.bounds);

	content = [self contentSize];
	y       = 0;

	// ---- 头部行(地址列固定, 列标随水平滚动左移, 与 win32 的指针偏移等价) ----
	{
		NSString*  head = @"ADDR  ";
		NSString*  body = @"+0 +1 +2 +3 +4 +5 +6 +7 +8 +9 +A +B +C +D +E +F  0123456789ABCDEF";

		head = [head stringByAppendingString:idbg_apply_start_col(body, _startCol)];
		[head drawAtPoint:NSMakePoint(0, y) withAttributes:_attrs];
	}

	y += _charH;

	// ---- 头行与正文之间的分隔线 ----
	{
		NSBezierPath*  path = [NSBezierPath bezierPath];

		[[NSColor textColor] setStroke];
		[path setLineWidth:1.0];
		[path moveToPoint:NSMakePoint(0, (CGFloat)y + IDBG_LINE_SPLIT_HIGH / 2.0 + 0.5)];
		[path lineToPoint:NSMakePoint(content.width, (CGFloat)y + IDBG_LINE_SPLIT_HIGH / 2.0 + 0.5)];
		[path stroke];
	}

	y += IDBG_LINE_SPLIT_HIGH;

	// ---- 正文 ----
	totalLines = (ines_int_t)((content.height - _charH * IDBG_HEAD_LINES - IDBG_LINE_SPLIT_HIGH) / _charH) + 1;
	if (totalLines + _startLine > _totalRows)
		totalLines = _totalRows - _startLine;

	for (iLine = 0; iLine < totalLines; iLine++)
	{
		NSString*  text;
		NSString*  prefix;
		NSString*  body;

		addr = (_startLine + iLine) * IDBG_BYTES_PER_LINE;

		idbg_mem_format_line(pSnap, _space, addr, buf, sizeof(buf));

		text   = [NSString stringWithUTF8String:buf];
		prefix = [text substringToIndex:IDBG_ADDR_COLS];
		body   = [text substringFromIndex:IDBG_ADDR_COLS];

		text = [prefix stringByAppendingString:idbg_apply_start_col(body, _startCol)];
		[text drawAtPoint:NSMakePoint(0, y) withAttributes:_attrs];

		y += _charH;
	}

	// ---- 光标(与 win32 一致: 十六进制区与 ASCII 区各一块反色) ----
	curRow = _curAddr / IDBG_BYTES_PER_LINE;
	curCol = _curAddr % IDBG_BYTES_PER_LINE;
	iLine  = curRow - _startLine;

	if ((iLine >= 0) && (iLine < totalLines))
	{
		for (iCol = 0; iCol < 2; iCol++)
		{
			ines_int_t  cellCol;

			// iCol == 0: 十六进制区(带半字节偏移); iCol == 1: ASCII 区
			cellCol = (iCol == 0)
					? (curCol * 3 + _halfChar)
					: (curCol + IDBG_BYTES_PER_LINE * 3 + 1);

			if (cellCol < _startCol)
				continue;
			if (cellCol - _startCol >= _cols)
				continue;

			{
				NSRect  rc;

				rc.origin.x    = (CGFloat)((cellCol - _startCol + IDBG_ADDR_COLS) * _charW);
				rc.origin.y    = (CGFloat)((iLine + IDBG_HEAD_LINES) * _charH + IDBG_LINE_SPLIT_HIGH);
				rc.size.width  = (CGFloat)_charW;
				rc.size.height = (CGFloat)_charH;

				// 反色(等价 win32 的 InvertRect)
				[[NSColor whiteColor] setFill];
				NSRectFillUsingOperation(NSIntersectionRect(rc, self.bounds),
										 NSCompositingOperationDifference);
			}
		}
	}
}

@end


#pragma mark - iNESGraphicView

// 位平面解码(与 win32 的 wNameTable.c / wPatternTable.c 完全一致):
//   pat = p_pat[t] | (p_pat[t+8] << 8)
//   重排为 h7 l7 h5 l5 h3 l3 h1 l1 h6 l6 h4 l4 h2 l2 h0 l0
//   再按 >>14, >>6, >>12, >>4, >>10, >>2, >>8, 0 取出 8 个 2bit 像素
static inline void idbg_decode_tile_row(ines_int_t pat, ines_int_t attr, ines_byte_t* p_line)
{
	pat = (pat & 0xAA55) | ((pat & 0x00AA) << 7) | ((pat & 0x5500) >> 7);

	p_line[0] = (ines_byte_t)(((pat >> 14) & 0x3) | attr);
	p_line[1] = (ines_byte_t)(((pat >> 6)  & 0x3) | attr);
	p_line[2] = (ines_byte_t)(((pat >> 12) & 0x3) | attr);
	p_line[3] = (ines_byte_t)(((pat >> 4)  & 0x3) | attr);
	p_line[4] = (ines_byte_t)(((pat >> 10) & 0x3) | attr);
	p_line[5] = (ines_byte_t)(((pat >> 2)  & 0x3) | attr);
	p_line[6] = (ines_byte_t)(((pat >> 8)  & 0x3) | attr);
	p_line[7] = (ines_byte_t)((pat & 0x3) | attr);
}

// 8bit 索引色 -> 32bit BGRA。输入为自底向上(与 win32 的 DIB 一致), 输出为自顶向下
static void idbg_index_to_bgra(const ines_byte_t* pSrc, ines_byte_t* pDst,
							   ines_int_t w, ines_int_t h, const ines_dword_t* pTable)
{
	ines_int_t  x, y;

	for (y = 0; y < h; y++)
	{
		const ines_byte_t*  src = pSrc + (size_t)(h - 1 - y) * (size_t)w;
		ines_dword_t*       dst = (ines_dword_t*)(void*)(pDst + (size_t)y * (size_t)w * 4);

		for (x = 0; x < w; x++)
			dst[x] = pTable[src[x] & 0x3F];
	}
}


@interface iNESGraphicView ()
{
	ines_byte_t*  _idxBuf;      // 索引色缓冲(自底向上)
	ines_byte_t*  _bgraBuf;     // 转换后的 BGRA(自顶向下)
	ines_int_t    _imgW;        // 当前索引图宽
	ines_int_t    _imgH;        // 当前索引图高
}

- (BOOL)prepareBuffers;
- (void)buildNameTableIndexed:(const ines_dbg_snapshot_t*)pSnap;
- (void)buildPatternIndexed:(const ines_dbg_snapshot_t*)pSnap;
- (void)buildPaletteIndexed:(const ines_dbg_snapshot_t*)pSnap;
- (void)buildColorTable:(const ines_dbg_snapshot_t*)pSnap table:(ines_dword_t*)pTable;

@end


@implementation iNESGraphicView

- (instancetype)initWithMode:(ines_int_t)mode
{
	self = [super initWithFrame:NSMakeRect(0, 0, IDBG_NT_WIDTH, IDBG_NT_HEIGHT)];
	if (self == nil)
		return nil;

	_mode   = mode;
	_patIdx = 0;

	// 按模式预分配缓冲(失败时 drawRect 会退化为空白, 不影响运行)
	[self prepareBuffers];

	return self;
}

- (void)dealloc
{
	if (_idxBuf != NULL)
		free(_idxBuf);
	if (_bgraBuf != NULL)
		free(_bgraBuf);
}

// 与 win32 的左上原点一致
- (BOOL)isFlipped
{
	return YES;
}

- (BOOL)acceptsFirstResponder
{
	return NO;
}

- (BOOL)hasContent
{
	return ((_snapshot != NULL) && !_snapshot->rom_off);
}

- (BOOL)prepareBuffers
{
	ines_int_t  w, h;

	switch (_mode)
	{
	case IDBG_GFX_PATTERN:  w = IDBG_PT_WIDTH;  h = IDBG_PT_HEIGHT; break;
	case IDBG_GFX_PALETTE:  w = IDBG_PAL_WIDTH; h = IDBG_PAL_HEIGHT; break;
	default:                w = IDBG_NT_WIDTH;  h = IDBG_NT_HEIGHT; break;
	}

	if ((_idxBuf != NULL) && (_imgW == w) && (_imgH == h))
		return YES;

	if (_idxBuf != NULL)  { free(_idxBuf);  _idxBuf  = NULL; }
	if (_bgraBuf != NULL) { free(_bgraBuf); _bgraBuf = NULL; }

	_imgW    = 0;
	_imgH    = 0;
	_idxBuf  = (ines_byte_t*)malloc((size_t)w * (size_t)h);
	_bgraBuf = (ines_byte_t*)malloc((size_t)w * (size_t)h * 4);

	if ((_idxBuf == NULL) || (_bgraBuf == NULL))
	{
		if (_idxBuf != NULL)  { free(_idxBuf);  _idxBuf  = NULL; }
		if (_bgraBuf != NULL) { free(_bgraBuf); _bgraBuf = NULL; }
		return NO;
	}

	_imgW = w;
	_imgH = h;

	return YES;
}

#pragma mark 索引图生成

// 卷轴查看器: 4 个 name table 按 win32 的排布拼接(n=0 左下, 1 右下, 2 左上, 3 右上)
- (void)buildNameTableIndexed:(const ines_dbg_snapshot_t*)pSnap
{
	ines_int_t          n, x, y, t;
	ines_int_t          pa, pa_base, attr, pat;
	ines_byte_t*        p_block;
	ines_byte_t*        p_line;
	const ines_byte_t*  p_pat;

	pa_base = (pSnap->reg_ctrl_1 & PPU_BG_MEM_MASK) ? 0x1000 : 0;

	for (n = 0; n < 4; n++)
	{
		const ines_byte_t*  nt = pSnap->ppu_bank[n + 8];

		p_block = _idxBuf + (size_t)((1 - (n >> 1)) * SCREEN_HEIGHT) * IDBG_NT_WIDTH
						 + (n & 1) * SCREEN_WIDTH;

		for (y = 0; y < 30; y++)
		{
			for (x = 0; x < 32; x++)
			{
				pa   = pa_base + nt[y * 32 + x] * 16;
				attr = ((nt[0x3C0 + y / 4 * 8 + x / 4] >> ((x & 2) + (y & 2) * 2)) & 3) << 2;

				p_line = p_block + (size_t)(SCREEN_HEIGHT - (y * 8) - 1) * IDBG_NT_WIDTH + x * 8;
				p_pat  = pSnap->ppu_bank[pa >> 10] + (pa & (IDBG_PPU_BANK_SIZE - 1));

				for (t = 0; t < 8; t++)
				{
					pat = p_pat[t] | ((ines_int_t)p_pat[t + 8] << 8);
					idbg_decode_tile_row(pat, attr, p_line);
					p_line -= IDBG_NT_WIDTH;
				}
			}
		}
	}
}

// 图形查看器: 256 个 tile 按 16 列 32 行铺开
- (void)buildPatternIndexed:(const ines_dbg_snapshot_t*)pSnap
{
	ines_int_t          x, y, t;
	ines_int_t          pa, pat;
	ines_byte_t*        p_line;
	const ines_byte_t*  p_pat;

	for (y = 0; y < 32; y++)
	{
		for (x = 0; x < 16; x++)
		{
			pa     = (y * 16 + x) * 16;
			p_line = _idxBuf + (size_t)(IDBG_PT_HEIGHT - (y * 8) - 1) * IDBG_PT_WIDTH + x * 8;
			p_pat  = pSnap->ppu_bank[pa >> 10] + (pa & (IDBG_PPU_BANK_SIZE - 1));

			for (t = 0; t < 8; t++)
			{
				pat = p_pat[t] | ((ines_int_t)p_pat[t + 8] << 8);
				idbg_decode_tile_row(pat, 0, p_line);
				p_line -= IDBG_PT_WIDTH;
			}
		}
	}
}

// 色盘查看器: 上行 16 个背景色块, 下行 16 个精灵色块
- (void)buildPaletteIndexed:(const ines_dbg_snapshot_t*)pSnap
{
	ines_int_t          x, y, t;
	ines_byte_t*        p_line;
	const ines_byte_t*  p_pal;

	for (y = 0; y < 2; y++)
	{
		p_pal = (y == 0) ? pSnap->bg_pal : pSnap->sp_pal;

		for (x = 0; x < 16; x++)
		{
			p_line = _idxBuf + (size_t)(IDBG_PAL_HEIGHT - (y * 8) - 1) * IDBG_PAL_WIDTH + x * 8;

			for (t = 0; t < 8; t++)
			{
				memset(p_line, p_pal[x], 8);
				p_line -= IDBG_PAL_WIDTH;
			}
		}
	}
}

// 调色板查表(与 win32 的 bmiColors 填充一致)
- (void)buildColorTable:(const ines_dbg_snapshot_t*)pSnap table:(ines_dword_t*)pTable
{
	ines_int_t          i;
	const ines_byte_t*  pPal;

	if (_mode == IDBG_GFX_PALETTE)
	{
		for (i = 0; i < MAX_COLORS; i++)
			pTable[i] = iNES_palette_bgra[i];
		return;
	}

	if (_mode == IDBG_GFX_PATTERN)
	{
		pPal = (_patIdx & 0x04) ? pSnap->sp_pal : pSnap->bg_pal;

		for (i = 0; i < 4; i++)
			pTable[i] = iNES_palette_bgra[pPal[(_patIdx & 0x03) * 4 + i] & 0x3F];
		return;
	}

	// 卷轴查看器使用背景色板的前 16 项
	for (i = 0; i < 16; i++)
		pTable[i] = iNES_palette_bgra[pSnap->bg_pal[i] & 0x3F];
}

#pragma mark 绘制

- (void)drawRect:(NSRect)dirtyRect
{
	const ines_dbg_snapshot_t*  pSnap = _snapshot;
	ines_dword_t     color_table[64];
	CGColorSpaceRef  cs;
	CGContextRef     ctx;
	CGImageRef       img;
	NSImage*         image;

	// 未载入 ROM: 纯灰底(与 win32 一致)
	if ((pSnap == NULL) || pSnap->rom_off)
	{
		idbg_fill_off_color();
		return;
	}

	if (![self prepareBuffers])
		return;

	// 1) 生成索引图(自底向上, 与 win32 的 DIB 布局一致)
	switch (_mode)
	{
	case IDBG_GFX_PATTERN:  [self buildPatternIndexed:pSnap];  break;
	case IDBG_GFX_PALETTE:  [self buildPaletteIndexed:pSnap];  break;
	default:                [self buildNameTableIndexed:pSnap]; break;
	}

	// 2) 构建调色板
	memset(color_table, 0, sizeof(color_table));
	[self buildColorTable:pSnap table:color_table];

	// 3) 索引色 -> BGRA(顺带翻转为自顶向下)
	idbg_index_to_bgra(_idxBuf, _bgraBuf, _imgW, _imgH, color_table);

	// 4) 最近邻放大铺满客户区(等价 win32 的 StretchDIBits + COLORONCOLOR)
	cs = CGColorSpaceCreateDeviceRGB();
	if (cs == NULL)
		return;

	ctx = CGBitmapContextCreate(_bgraBuf, (size_t)_imgW, (size_t)_imgH, 8,
								(size_t)_imgW * 4, cs,
								kCGBitmapByteOrder32Little | kCGImageAlphaNoneSkipFirst);
	CGColorSpaceRelease(cs);

	if (ctx == NULL)
		return;

	img = CGBitmapContextCreateImage(ctx);
	CGContextRelease(ctx);

	if (img == NULL)
		return;

	image = [[NSImage alloc] initWithCGImage:img size:NSMakeSize((CGFloat)_imgW, (CGFloat)_imgH)];
	CGImageRelease(img);

	[image drawInRect:self.bounds
			 fromRect:NSMakeRect(0, 0, (CGFloat)_imgW, (CGFloat)_imgH)
			operation:NSCompositingOperationCopy
			 fraction:1.0
	   respectFlipped:YES
				hints:@{ NSImageHintInterpolation: @(NSImageInterpolationNone) }];
}

#pragma mark 交互

// 等价 win32 的 wPT_OnLButtonDown: 点击循环切换图案索引
- (void)mouseDown:(NSEvent*)event
{
	if (_mode != IDBG_GFX_PATTERN)
		return;

	_patIdx = (_patIdx + 1) & 0x07;

	[_delegate graphicView:self didChangePatternIndex:_patIdx];

	[self setNeedsDisplay:YES];
}

@end
