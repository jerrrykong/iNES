// =====================================================================
// iNES Win32 前端 —— 寄存器查看器
//
// 与 mac/iNESRegisterView.m 同构(布局/交互见 docs/register-view-plan.md):
//   * 一行一个寄存器: 名称 / 地址 / 值 / 位格 / 说明; 四周留空半个字符;
//   * 位格每行只画 8 位: 16 位寄存器拆"低字节行 L / 高字节行 H"两行, 值列仍是 16 位整值;
//   * 选中与可写是两个正交维度: 可写选中态 = 反白, 只读 = 灰底(选中只加焦点框);
//   * 位格保留位名字母, 字色/字重表达位值: 1 = 常色粗体, 0 = 浅灰;
//   * 值列与位格都可选中, 单击只选中, 输入才改写(位格只接受 0/1);
//   * 值变化时红色高亮 0.5s。
//
// 与 mac 的差异(线程模型):
//   win32 的 OnIdle() 直接在主线程调用 ines_host_doframe(), host 即由本窗口所在线程独占 ——
//   取值直读 host 字段、改值直接应用, 不需要 mac 的快照与写队列。但仍遵守两条硬约定:
//     1) 取值一律直拷字段: ines_ppu_readlow($2002) 会清 VBlank、ines_apu_read($4015) 会清 IRQ;
//     2) 写 PPU/APU 端口一律走 ines_ppu_writelow / ines_apu_write, 保留真实写入副作用
//        ($2000 同步 T 的 NT 位、$4003 重载 length counter、$4015 清 DMC IRQ 等)。
//   寄存器编号与 mac/iNESDebug.h 的 IDBG_REG_* 一一对应, 改动时两边要同步。
//
// 窗口类/字体度量/滚动条/双缓冲骨架与 wMemory.c 保持一致。
// =====================================================================

#include "stdafx.h"
#include "../comm/log.h"
#include "../core/nes.h"
#include "wRegister.h"
#include "Resource.h"


extern ines_host_t   host;


// ---------------------------------------------------------------------
// 布局常量(单位: 字符列)
//
// 位格只画 8 列: 16 位寄存器拆成两个显示行(低字节 / 高字节), 避免横向过宽。
// ---------------------------------------------------------------------
#define WREG_NAME_COLS     10
#define WREG_ADDR_COLS      7
#define WREG_VALUE_COLS     6
#define WREG_TAG_COLS       1                                  // "L" / "H" 高低字节标记
#define WREG_BIT_CELL       3                                  // "[x]" 占 3 列
#define WREG_BIT_COLS      (8 * WREG_BIT_CELL)                 // 24(每行 8 位)
#define WREG_NOTE_COLS     26

#define WREG_LINE_COLS     (WREG_NAME_COLS + WREG_ADDR_COLS + WREG_VALUE_COLS + \
							WREG_TAG_COLS + WREG_BIT_COLS + WREG_NOTE_COLS)

#define WREG_COL_ADDR      (WREG_NAME_COLS)                                 // 10
#define WREG_COL_VALUE     (WREG_COL_ADDR   + WREG_ADDR_COLS)               // 17
#define WREG_COL_TAG       (WREG_COL_VALUE  + WREG_VALUE_COLS)              // 23
#define WREG_COL_BITS      (WREG_COL_TAG    + WREG_TAG_COLS)                // 24
#define WREG_COL_NOTE      (WREG_COL_BITS   + WREG_BIT_COLS)                // 48

#define WREG_WIN_WIDTH     560
#define WREG_WIN_HEIGHT    580
#define WREG_HEAD_LINES    1

// 显示行上限: 4 个组标题 + 每个寄存器最多 2 行(16 位 = 低字节行 + 高字节行)
#define WREG_MAX_LINES     128

// 值列显示格式
#define WREG_FMT_HEX       0
#define WREG_FMT_DEC       1
#define WREG_FMT_TEXT      2

// 变化高亮保持时长(毫秒)
#define WREG_HILITE_TIME   500

// Tips 显示时长(毫秒)
#define WREG_TIPS_TIME     800

// 只读单元格的灰底(与"未载入 ROM"的中灰区分)
#define WREG_GRAY_FILL     RGB(240, 240, 240)
#define WREG_OFF_GRAY      RGB(128, 128, 128)

// 变化高亮字色
#define WREG_RED_TEXT      RGB(200, 0, 0)

// 光标列
#define WREG_CUR_VALUE     0
#define WREG_CUR_BIT       1


// ---------------------------------------------------------------------
// 寄存器标识(与 mac/iNESDebug.h 的 IDBG_REG_* 一一对应)
// ---------------------------------------------------------------------
#define WREG_REG_A           0     // CPU 累加器
#define WREG_REG_X           1     // CPU 变址 X
#define WREG_REG_Y           2     // CPU 变址 Y
#define WREG_REG_P           3     // CPU 状态寄存器
#define WREG_REG_SP          4     // CPU 栈指针
#define WREG_REG_PC          5     // CPU 程序计数器(16 位)
#define WREG_REG_IRQ_PEND    6     // CPU 中断挂起(NMI/MMC/APU)
#define WREG_REG_CYCLES      7     // CPU 累计周期(只读, 64 位)

#define WREG_REG_PPUCTRL     8     // $2000
#define WREG_REG_PPUMASK     9     // $2001
#define WREG_REG_PPUSTATUS   10    // $2002(只读)
#define WREG_REG_OAMADDR     11    // $2003
#define WREG_REG_OAMDATA     12    // $2004
#define WREG_REG_PPUSCROLL   13    // $2005(只读, 由 T/fine_x 反算)
#define WREG_REG_PPU_T       14    // $2006 内部 T(16 位, 直改字段)
#define WREG_REG_PPU_V       15    // $2006 内部 V(16 位, 直改字段)
#define WREG_REG_PPUDATA     16    // $2007(只写, 显示读缓冲)
#define WREG_REG_SCANLINE    17    // 当前扫描行(只读)
#define WREG_REG_VBLANK      18    // 是否处于 VBlank(只读)
#define WREG_REG_TOGGLE      19    // $2005/$2006 写入翻转(只读)

#define WREG_REG_P1VOL       20    // $4000
#define WREG_REG_P1SWP       21    // $4001
#define WREG_REG_P1TLO       22    // $4002
#define WREG_REG_P1THI       23    // $4003
#define WREG_REG_P2VOL       24    // $4004
#define WREG_REG_P2SWP       25    // $4005
#define WREG_REG_P2TLO       26    // $4006
#define WREG_REG_P2THI       27    // $4007
#define WREG_REG_TRLIN       28    // $4008
#define WREG_REG_TR_UNUSED   29    // $4009(保留)
#define WREG_REG_TRTLO       30    // $400A
#define WREG_REG_TRTHI       31    // $400B
#define WREG_REG_NSVOL       32    // $400C
#define WREG_REG_NS_UNUSED   33    // $400D(保留)
#define WREG_REG_NSFRQ       34    // $400E
#define WREG_REG_NSLEN       35    // $400F
#define WREG_REG_DMFREQ      36    // $4010
#define WREG_REG_DMDAC       37    // $4011
#define WREG_REG_DMADDR      38    // $4012
#define WREG_REG_DMLEN       39    // $4013
#define WREG_REG_APUCTRL     40    // $4015(写)
#define WREG_REG_APUSTAT     41    // $4015(只读, 由内部状态派生)
#define WREG_REG_FRAMECTR    42    // $4017(写)

#define WREG_REG_OAMDMA      43    // $4014(只写, 写即触发 DMA)
#define WREG_REG_JOYPAD1     44    // $4016(写: strobe)
#define WREG_REG_JOYPAD2     45    // $4017(读: 手柄 2 状态)

#define WREG_REG_COUNT       46


// ---------------------------------------------------------------------
// 寄存器定义表(下标即 WREG_REG_* 的顺序)
// ---------------------------------------------------------------------
typedef struct _wreg_def_
{
	ines_int_t     reg_id;
	const char*    name;
	const char*    addr;      // "$2000" / "-"
	ines_int_t     fmt;       // WREG_FMT_*
	ines_int_t     width;     // 8 / 16 / 64 / 0(纯文本行)
	unsigned int   wmask;     // 可写位掩码(0 = 整行只读)
	const char*    bits;      // 位缩写, MSB -> LSB
	const char*    note;      // 说明
} wreg_def_t;


static const wreg_def_t  s_defs[WREG_REG_COUNT] =
{
	/* ---------------- CPU ---------------- */
	{ WREG_REG_A,          "A",         "-",      WREG_FMT_HEX,  8, 0x00FF, "76543210",         "累加器" },
	{ WREG_REG_X,          "X",         "-",      WREG_FMT_HEX,  8, 0x00FF, "76543210",         "变址寄存器 X" },
	{ WREG_REG_Y,          "Y",         "-",      WREG_FMT_HEX,  8, 0x00FF, "76543210",         "变址寄存器 Y" },
	{ WREG_REG_P,          "P",         "-",      WREG_FMT_HEX,  8, 0x00FF, "NVRBDIZC",         "状态 N V R B D I Z C" },
	{ WREG_REG_SP,         "SP",        "-",      WREG_FMT_HEX,  8, 0x00FF, "76543210",         "栈指针(页 1)" },
	{ WREG_REG_PC,         "PC",        "-",      WREG_FMT_HEX, 16, 0xFFFF, "FEDCBA9876543210", "程序计数器" },
	{ WREG_REG_IRQ_PEND,   "IRQ.PEND",  "-",      WREG_FMT_HEX,  8, 0x0007, "     AMN",         "NMI/MMC/APU 挂起" },
	{ WREG_REG_CYCLES,     "CYCLES",    "-",      WREG_FMT_DEC, 64, 0x0000, "",                 "累计周期(只读)" },

	/* ---------------- PPU ---------------- */
	{ WREG_REG_PPUCTRL,    "PPUCTRL",   "$2000",  WREG_FMT_HEX,  8, 0x00FF, "NMSBsInn",         "NMI/图样/尺寸/增量/NT" },
	{ WREG_REG_PPUMASK,    "PPUMASK",   "$2001",  WREG_FMT_HEX,  8, 0x00FF, "BGRsbmMg",         "色彩/BG/SPR 显示控制" },
	{ WREG_REG_PPUSTATUS,  "PPUSTATUS", "$2002",  WREG_FMT_HEX,  8, 0x0000, "VSO-----",         "VBlank/Spr0/溢出(只读)" },
	{ WREG_REG_OAMADDR,    "OAMADDR",   "$2003",  WREG_FMT_HEX,  8, 0x00FF, "76543210",         "OAM 地址" },
	{ WREG_REG_OAMDATA,    "OAMDATA",   "$2004",  WREG_FMT_HEX,  8, 0x00FF, "76543210",         "OAM 数据(写后地址+1)" },
	{ WREG_REG_PPUSCROLL,  "PPUSCROLL", "$2005",  WREG_FMT_TEXT, 0, 0x0000, "",                 "X=--- FX=- Y=--- FY=-" },
	{ WREG_REG_PPU_T,      "PPUADDR.T", "$2006T", WREG_FMT_HEX, 16, 0x7FFF, "0YYYNNYYYYYXXXXX", "内部 T(直改字段)" },
	{ WREG_REG_PPU_V,      "PPUADDR.V", "$2006V", WREG_FMT_HEX, 16, 0x7FFF, "0YYYNNYYYYYXXXXX", "当前 VRAM 地址 V" },
	{ WREG_REG_PPUDATA,    "PPUDATA",   "$2007",  WREG_FMT_HEX,  8, 0x00FF, "76543210",         "写: VRAM; 读: 缓冲" },
	{ WREG_REG_SCANLINE,   "SCANLINE",  "-",      WREG_FMT_DEC, 16, 0x0000, "FEDCBA9876543210", "当前扫描行(只读)" },
	{ WREG_REG_VBLANK,     "VBLANK",    "-",      WREG_FMT_HEX,  8, 0x0000, "-------V",         "VBlank 标志(只读)" },
	{ WREG_REG_TOGGLE,     "TOGGLE",    "-",      WREG_FMT_HEX,  8, 0x0000, "-------T",         "0=首字节 1=次字节" },

	/* ---------------- APU ---------------- */
	{ WREG_REG_P1VOL,      "P1VOL",     "$4000",  WREG_FMT_HEX,  8, 0x00FF, "ddLCvvvv",         "音量/包络/占空比" },
	{ WREG_REG_P1SWP,      "P1SWP",     "$4001",  WREG_FMT_HEX,  8, 0x00FF, "EpppNsss",         "扫频" },
	{ WREG_REG_P1TLO,      "P1TLO",     "$4002",  WREG_FMT_HEX,  8, 0x00FF, "tttttttt",         "定时器低 8 位" },
	{ WREG_REG_P1THI,      "P1THI",     "$4003",  WREG_FMT_HEX,  8, 0x00FF, "lllllttt",         "长度/定时器高 3 位" },
	{ WREG_REG_P2VOL,      "P2VOL",     "$4004",  WREG_FMT_HEX,  8, 0x00FF, "ddLCvvvv",         "音量/包络/占空比" },
	{ WREG_REG_P2SWP,      "P2SWP",     "$4005",  WREG_FMT_HEX,  8, 0x00FF, "EpppNsss",         "扫频" },
	{ WREG_REG_P2TLO,      "P2TLO",     "$4006",  WREG_FMT_HEX,  8, 0x00FF, "tttttttt",         "定时器低 8 位" },
	{ WREG_REG_P2THI,      "P2THI",     "$4007",  WREG_FMT_HEX,  8, 0x00FF, "lllllttt",         "长度/定时器高 3 位" },
	{ WREG_REG_TRLIN,      "TRLIN",     "$4008",  WREG_FMT_HEX,  8, 0x00FF, "Crrrrrrr",         "线性计数器" },
	{ WREG_REG_TR_UNUSED,  "TR.UNUSED", "$4009",  WREG_FMT_HEX,  8, 0x0000, "--------",         "保留(未使用)" },
	{ WREG_REG_TRTLO,      "TRTLO",     "$400A",  WREG_FMT_HEX,  8, 0x00FF, "tttttttt",         "定时器低 8 位" },
	{ WREG_REG_TRTHI,      "TRTHI",     "$400B",  WREG_FMT_HEX,  8, 0x00FF, "lllllttt",         "长度/定时器高 3 位" },
	{ WREG_REG_NSVOL,      "NSVOL",     "$400C",  WREG_FMT_HEX,  8, 0x003F, "--LCvvvv",         "音量/包络(位 7-6 未用)" },
	{ WREG_REG_NS_UNUSED,  "NS.UNUSED", "$400D",  WREG_FMT_HEX,  8, 0x0000, "--------",         "保留(未使用)" },
	{ WREG_REG_NSFRQ,      "NSFRQ",     "$400E",  WREG_FMT_HEX,  8, 0x00FF, "Mppppppp",         "模式/周期" },
	{ WREG_REG_NSLEN,      "NSLEN",     "$400F",  WREG_FMT_HEX,  8, 0x00F8, "lllll---",         "长度(位 2-0 未用)" },
	{ WREG_REG_DMFREQ,     "DMFREQ",    "$4010",  WREG_FMT_HEX,  8, 0x00FF, "ILrrrrrr",         "IRQ/循环/速率" },
	{ WREG_REG_DMDAC,      "DMDAC",     "$4011",  WREG_FMT_HEX,  8, 0x007F, "-ddddddd",         "DAC 直写" },
	{ WREG_REG_DMADDR,     "DMADDR",    "$4012",  WREG_FMT_HEX,  8, 0x00FF, "aaaaaaaa",         "采样起始地址" },
	{ WREG_REG_DMLEN,      "DMLEN",     "$4013",  WREG_FMT_HEX,  8, 0x00FF, "llllllll",         "采样长度" },
	{ WREG_REG_APUCTRL,    "APUCTRL",   "$4015",  WREG_FMT_HEX,  8, 0x001F, "---DNT21",         "声道使能(写)" },
	{ WREG_REG_APUSTAT,    "APUSTAT",   "$4015R", WREG_FMT_HEX,  8, 0x0000, "FD-dNT21",         "状态(只读, 不清 IRQ)" },
	{ WREG_REG_FRAMECTR,   "FRAMECTR",  "$4017",  WREG_FMT_HEX,  8, 0x00C0, "MI------",         "帧计数器模式" },

	/* ---------------- I/O ---------------- */
	{ WREG_REG_OAMDMA,     "OAMDMA",    "$4014",  WREG_FMT_HEX,  8, 0x00FF, "hhhhhhhh",         "写即触发 256B DMA" },
	{ WREG_REG_JOYPAD1,    "JOYPAD1",   "$4016",  WREG_FMT_HEX,  8, 0x0001, "-------S",         "手柄 strobe(写)" },
	{ WREG_REG_JOYPAD2,    "JOYPAD2",   "$4017R", WREG_FMT_HEX,  8, 0x0000, "RLDUSsBA",         "手柄 2 按键(读)" },
};


// 分组(下标与 s_defs 的区间一一对应)
#define WREG_GROUP_COUNT   4

static const struct _wreg_group_
{
	const char*   title;
	ines_int_t    start;
	ines_int_t    count;
} s_groups[WREG_GROUP_COUNT] =
{
	{ "CPU",  0,  8 },
	{ "PPU",  8, 12 },
	{ "APU", 20, 23 },
	{ "I/O", 43,  3 },
};


// ---------------------------------------------------------------------
// 显示行: 8 位寄存器占 1 行, 16 位寄存器占 2 行(低字节 + 高字节)
// ---------------------------------------------------------------------
typedef struct _wreg_row_
{
	ines_int_t    group;    // 所属组
	ines_int_t    reg;      // s_defs 下标, -1 = 组标题行
	ines_int_t    sub;      // 0 = 低字节行(首行), 1 = 高字节行(仅 16 位)
} wreg_row_t;


static HWND wReg_hWnd = NULL;
static BOOL wReg_bShow = FALSE;
static const TCHAR wReg_szClassName[128] = _T("iNES_RegisterWnd");
static TCHAR wReg_szTitle[MAX_LOADSTRING];

// 绘制资源
static HFONT    wReg_hFont = NULL;
static HFONT    wReg_hFontBold = NULL;
static int      wReg_iCharWidth = 0;
static int      wReg_iCharHeight = 0;

// 布局(每次绘制/命中测试前由 wReg_Layout 重算)
static int      wReg_iPadX = 0;
static int      wReg_iPadY = 0;
static int      wReg_iRows = 1;

// 行模型与光标
static wreg_row_t  s_lineMap[WREG_MAX_LINES];
static ines_int_t  s_lineCount = 0;
static ines_int_t  s_startLine = 0;
static ines_int_t  s_curLine = 1;                 // 默认选中 CPU 组的第一个寄存器(A)
static ines_int_t  s_cursorCol = WREG_CUR_VALUE;
static ines_int_t  s_bitIndex = 0;

// 值列编辑
static BOOL         s_editActive = FALSE;
static ines_int64_t s_editValue = 0;
static ines_int_t   s_editNibble = 0;

static BOOL         s_folded[WREG_GROUP_COUNT];

// 变化跟踪(红色高亮)
static ines_int64_t s_lastValue[WREG_REG_COUNT];
static ines_int64_t s_prevValue[WREG_REG_COUNT];
static DWORD        s_lastChange[WREG_REG_COUNT];
static BOOL         s_lastValid[WREG_REG_COUNT];

// 重绘请求: 结构性变化(滚动/折叠/选中/编辑)由各处理函数直接 InvalidateRect
static int   wReg_iUpdate = 0;

// Tips
static ines_char_t  s_tips[128];
static DWORD        s_tipsExpire = 0;

// 静态文本的 TCHAR 副本(定义表是 UTF-8 常量, 建窗口时转换一次)
static ines_char_t  s_nameText[WREG_REG_COUNT][16];
static ines_char_t  s_addrText[WREG_REG_COUNT][12];
static ines_char_t  s_noteText[WREG_REG_COUNT][64];
static ines_char_t  s_groupText[WREG_GROUP_COUNT][16];
static ines_char_t  s_headNote[16];                 // 表头"说明"列标题


static LRESULT CALLBACK	wReg_WindowProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam);


// ---------------------------------------------------------------------
// 文本与度量
// ---------------------------------------------------------------------

// UTF-8 常量 -> TCHAR(iNES 目标按 UNICODE 编译, 需转成 UTF-16)
static VOID wReg_Utf8ToTChar(ines_str_t pOut, ines_size_t nLen, const char* pUtf8)
{
	if ((pOut == NULL) || (nLen == 0))
		return;

	pOut[0] = 0;

	if (pUtf8 == NULL)
		return;

#ifdef UNICODE
	MultiByteToWideChar(CP_UTF8, 0, pUtf8, -1, pOut, (int)nLen);
	pOut[nLen - 1] = 0;
#else
	strncpy(pOut, pUtf8, nLen - 1);
	pOut[nLen - 1] = 0;
#endif
}

// 逐字符输出(单字符等宽, x 为单元格左边界, 单元格宽 nCols 列)
static VOID wReg_DrawChar(HDC hDC, TCHAR ch, int x, int y, int nCols, COLORREF clr, BOOL bBold)
{
	int  cx;

	cx = x + (nCols * wReg_iCharWidth - wReg_iCharWidth) / 2;
	SetTextColor(hDC, clr);
	SelectObject(hDC, (HGDIOBJ)(bBold ? wReg_hFontBold : wReg_hFont));
	TextOut(hDC, cx, y, &ch, 1);
	SelectObject(hDC, (HGDIOBJ)wReg_hFont);
}

static COLORREF wReg_Mix(COLORREF c1, COLORREF c2, int nPercent)
{
	int  r;
	int  g;
	int  b;

	// nPercent: c1 占比(0~100)
	r = (GetRValue(c1) * nPercent + GetRValue(c2) * (100 - nPercent)) / 100;
	g = (GetGValue(c1) * nPercent + GetGValue(c2) * (100 - nPercent)) / 100;
	b = (GetBValue(c1) * nPercent + GetBValue(c2) * (100 - nPercent)) / 100;

	return RGB(r, g, b);
}


// ---------------------------------------------------------------------
// 取值(直拷 host 字段, 绝不走端口读路径)
// ---------------------------------------------------------------------
static ines_int64_t wReg_Value(ines_int_t regIdx)
{
	const ines_cpu_t*  pCpu;
	const ines_ppu_t*  pPpu;
	const ines_apu_t*  pApu;
	ines_int_t         reg_id;

	if ((regIdx < 0) || (regIdx >= WREG_REG_COUNT))
		return 0;

	if (host.status == NES_STATUS_OFF)
		return 0;

	pCpu = &host.cpu;
	pPpu = &host.ppu;
	pApu = &host.apu;
	reg_id = s_defs[regIdx].reg_id;

	switch (reg_id)
	{
	/* ---- CPU ---- */
	case WREG_REG_A:          return pCpu->reg_A;
	case WREG_REG_X:          return pCpu->reg_X;
	case WREG_REG_Y:          return pCpu->reg_Y;
	case WREG_REG_P:          return pCpu->reg_P;
	case WREG_REG_SP:         return pCpu->reg_SP;
	case WREG_REG_PC:         return pCpu->reg_PC;
	case WREG_REG_IRQ_PEND:   return pCpu->INT_pending;
	case WREG_REG_CYCLES:     return pCpu->total_cycles;

	/* ---- PPU ---- */
	case WREG_REG_PPUCTRL:    return pPpu->reg_ctrl_1;
	case WREG_REG_PPUMASK:    return pPpu->reg_ctrl_2;
	case WREG_REG_PPUSTATUS:  return pPpu->reg_status;
	case WREG_REG_OAMADDR:    return pPpu->reg_spr_addr;
	case WREG_REG_OAMDATA:    return pPpu->sp_RAM[pPpu->reg_spr_addr];
	case WREG_REG_PPUSCROLL:  return 0;                          // 文本行, 值在说明列
	case WREG_REG_PPU_T:      return pPpu->index_t;
	case WREG_REG_PPU_V:      return pPpu->index_v;
	case WREG_REG_PPUDATA:    return pPpu->read_2007_buffer;
	case WREG_REG_SCANLINE:   return pPpu->current_line;
	case WREG_REG_VBLANK:     return pPpu->in_vblank ? 1 : 0;
	case WREG_REG_TOGGLE:     return pPpu->toggle_2005_2006;

	/* ---- APU ---- */
	case WREG_REG_P1VOL:
	case WREG_REG_P1SWP:
	case WREG_REG_P1TLO:
	case WREG_REG_P1THI:      return pApu->channel_pulse1.reg_ctrl[reg_id - WREG_REG_P1VOL];
	case WREG_REG_P2VOL:
	case WREG_REG_P2SWP:
	case WREG_REG_P2TLO:
	case WREG_REG_P2THI:      return pApu->channel_pulse2.reg_ctrl[reg_id - WREG_REG_P2VOL];
	case WREG_REG_TRLIN:
	case WREG_REG_TR_UNUSED:
	case WREG_REG_TRTLO:
	case WREG_REG_TRTHI:      return pApu->channel_triangle.reg_ctrl[reg_id - WREG_REG_TRLIN];
	case WREG_REG_NSVOL:
	case WREG_REG_NS_UNUSED:
	case WREG_REG_NSFRQ:
	case WREG_REG_NSLEN:      return pApu->channel_noise.reg_ctrl[reg_id - WREG_REG_NSVOL];
	case WREG_REG_DMFREQ:
	case WREG_REG_DMDAC:
	case WREG_REG_DMADDR:
	case WREG_REG_DMLEN:      return pApu->channel_dmc.reg_ctrl[reg_id - WREG_REG_DMFREQ];
	case WREG_REG_APUCTRL:    return pApu->reg_ctrl;
	case WREG_REG_FRAMECTR:   return pApu->reg_frame_mode;
	case WREG_REG_APUSTAT:
	{
		ines_byte_t  st = 0;

		// 与 ines_apu_read() 的派生逻辑一致, 但不触发其副作用(清 IRQ)
		if (pApu->irq_flag)                              st |= APU_STATUS_FRAME_IRQ;
		if (pApu->channel_dmc.irq_flag)                  st |= APU_STATUS_DMC_IRQ;
		if (pApu->channel_pulse1.length_counter != 0)    st |= APU_STATUS_PULSE1_ENABLED;
		if (pApu->channel_pulse2.length_counter != 0)    st |= APU_STATUS_PULSE2_ENABLED;
		if (pApu->channel_triangle.length_counter != 0)  st |= APU_STATUS_TRIANGLE_ENABLED;
		if (pApu->channel_noise.length_counter != 0)     st |= APU_STATUS_NOISE_ENABLED;
		if (pApu->channel_dmc.length_counter != 0)       st |= APU_STATUS_DMC_ENABLED;

		return st;
	}

	/* ---- I/O ---- */
	case WREG_REG_OAMDMA:     return host.DMA_high;
	case WREG_REG_JOYPAD1:    return host.joypad.input_brush ? 1 : 0;
	case WREG_REG_JOYPAD2:    return host.joypad.joypad_bits[1] & 0xFF;

	default:                  return 0;
	}
}


// $2005 的派生显示: 由 T 与 fine_x 反算卷轴值
static VOID wReg_ScrollText(ines_str_t pOut, ines_size_t nLen)
{
	ines_int_t  x;
	ines_int_t  y;

	x = (ines_int_t)(((host.ppu.index_t & 0x001F) << 3) | (host.ppu.index_x & 0x07));
	y = (ines_int_t)((((host.ppu.index_t >> 5) & 0x001F) << 3) | ((host.ppu.index_t >> 12) & 0x07));

	_sntprintf(pOut, nLen, ISTR("X=%03d FX=%d Y=%03d FY=%d"),
			   (int)x, (int)(host.ppu.index_x & 0x07), (int)y, (int)((host.ppu.index_t >> 12) & 0x07));
	pOut[nLen - 1] = 0;
}


// 值列文本(编辑中显示编辑缓冲)
static VOID wReg_ValueText(ines_str_t pOut, ines_size_t nLen, const wreg_def_t* pDef, ines_int64_t val)
{
	if (pDef->fmt == WREG_FMT_TEXT)
	{
		_sntprintf(pOut, nLen, ISTR("  -"));
	}
	else if (pDef->fmt == WREG_FMT_DEC)
	{
		_sntprintf(pOut, nLen, ISTR("%") ISTR(PRI64) ISTR("d"), val);
	}
	else if (pDef->width == 16)
	{
		_sntprintf(pOut, nLen, ISTR("%04X"), (ines_dword_t)(val & 0xFFFF));
	}
	else
	{
		_sntprintf(pOut, nLen, ISTR("%02X"), (ines_dword_t)(val & 0xFF));
	}

	pOut[nLen - 1] = 0;
}


// 位缩写字母: bit 0 = 最低位
static char wReg_BitLetter(const wreg_def_t* pDef, ines_int_t bit)
{
	ines_size_t  len;

	if ((pDef->width <= 0) || (pDef->width > 16))
		return ' ';

	len = (pDef->bits != NULL) ? strlen(pDef->bits) : 0;
	if ((ines_size_t)(pDef->width - 1 - bit) >= len)
		return '?';

	return pDef->bits[pDef->width - 1 - bit];
}


// 位格在"字节行"内的位置(右对齐: 最低位恒在最右), 每行只画 8 位
static ines_int_t wReg_BitCell(ines_int_t bit)
{
	return 7 - (bit & 7);
}


// 该寄存器占几个显示行(16 位 = 低字节行 + 高字节行)
static ines_int_t wReg_RowSpan(ines_int_t width)
{
	return (width == 16) ? 2 : 1;
}


// ---------------------------------------------------------------------
// 布局 / 行模型
// ---------------------------------------------------------------------
static BOOL wReg_HasContent(VOID)
{
	return (host.status != NES_STATUS_OFF);
}

// 内容区: 四周留空半个字符(横向按字符宽, 纵向按行高)
static VOID wReg_Layout(HWND hWnd)
{
	RECT  rcClient;
	int   width;
	int   height;

	GetClientRect(hWnd, &rcClient);

	wReg_iPadX = wReg_iCharWidth / 2;
	wReg_iPadY = wReg_iCharHeight / 2;

	width  = (rcClient.right - rcClient.left) - wReg_iPadX * 2;
	height = (rcClient.bottom - rcClient.top) - wReg_iPadY * 2;

	if (width < 10)  width = 10;
	if (height < 10) height = 10;

	wReg_iRows = height / wReg_iCharHeight - WREG_HEAD_LINES;
	if (wReg_iRows < 1)
		wReg_iRows = 1;
}

// 行 iLine 的顶端 y(相对客户区)
static int wReg_LineY(ines_int_t iLine)
{
	return wReg_iPadY + (int)((iLine - s_startLine) + WREG_HEAD_LINES) * wReg_iCharHeight;
}

static int wReg_ColX(ines_int_t nCol)
{
	return wReg_iPadX + (int)nCol * wReg_iCharWidth;
}


// 按分组/折叠状态重建显示行表: 组标题 1 行; 8 位寄存器 1 行; 16 位寄存器 2 行(低/高字节)
static VOID wReg_RebuildLineMap(VOID)
{
	ines_int_t  n = 0;
	ines_int_t  g;
	ines_int_t  i;

	for (g = 0; g < WREG_GROUP_COUNT; g++)
	{
		if (n >= WREG_MAX_LINES)
			break;

		s_lineMap[n].group = g;
		s_lineMap[n].reg   = -1;
		s_lineMap[n].sub   = 0;
		n++;

		if (s_folded[g])
			continue;

		for (i = 0; i < s_groups[g].count; i++)
		{
			ines_int_t  reg = s_groups[g].start + i;

			if (n >= WREG_MAX_LINES)
				break;

			s_lineMap[n].group = g;
			s_lineMap[n].reg   = reg;
			s_lineMap[n].sub   = 0;
			n++;

			if ((n < WREG_MAX_LINES) && (wReg_RowSpan(s_defs[reg].width) == 2))
			{
				s_lineMap[n].group = g;
				s_lineMap[n].reg   = reg;
				s_lineMap[n].sub   = 1;
				n++;
			}
		}
	}

	s_lineCount = n;
}


// 当前选中的寄存器(s_defs 下标), 无效时返回 -1
static ines_int_t wReg_CurReg(VOID)
{
	if ((s_curLine < 0) || (s_curLine >= s_lineCount))
		return -1;

	return s_lineMap[s_curLine].reg;
}


// 返回 TRUE 表示该行是组标题(*pGroup = 组号); FALSE 表示寄存器行(*pReg / *pSub)
static BOOL wReg_RowAt(ines_int_t line, ines_int_t* pGroup, ines_int_t* pReg, ines_int_t* pSub)
{
	if (pGroup != NULL) *pGroup = -1;
	if (pReg   != NULL) *pReg   = -1;
	if (pSub   != NULL) *pSub   = 0;

	if ((line < 0) || (line >= s_lineCount))
		return FALSE;

	if (pGroup != NULL) *pGroup = s_lineMap[line].group;
	if (pReg   != NULL) *pReg   = s_lineMap[line].reg;
	if (pSub   != NULL) *pSub   = s_lineMap[line].sub;

	return (s_lineMap[line].reg < 0);
}


static VOID wReg_UpdateScrollBar(HWND hWnd)
{
	SCROLLINFO  sinfo;
	int         maxPos;

	wReg_Layout(hWnd);

	maxPos = (int)s_lineCount - wReg_iRows;

	memset(&sinfo, 0, sizeof(sinfo));
	sinfo.cbSize = sizeof(sinfo);
	sinfo.fMask  = SIF_RANGE | SIF_POS | SIF_PAGE | SIF_DISABLENOSCROLL;
	sinfo.nMin   = 0;
	sinfo.nMax   = (int)s_lineCount;
	sinfo.nPage  = (UINT)wReg_iRows;

	if (maxPos < 0)
		maxPos = 0;
	if (s_startLine > maxPos)
		s_startLine = (ines_int_t)maxPos;
	if (s_startLine < 0)
		s_startLine = 0;

	sinfo.nPos = (int)s_startLine;

	SetScrollInfo(hWnd, SB_VERT, &sinfo, TRUE);
}


static VOID wReg_OnScroll(HWND hWnd, int nCode)
{
	SCROLLINFO  sinfo;
	int         pos;

	memset(&sinfo, 0, sizeof(sinfo));
	sinfo.cbSize = sizeof(sinfo);
	sinfo.fMask  = SIF_RANGE | SIF_PAGE | SIF_POS | SIF_TRACKPOS;

	GetScrollInfo(hWnd, SB_VERT, &sinfo);

	pos = s_startLine;

	switch (nCode)
	{
	case SB_TOP:
		pos = 0;
		break;
	case SB_BOTTOM:
		pos = sinfo.nMax - (int)sinfo.nPage;
		break;
	case SB_LINEUP:
		pos--;
		break;
	case SB_LINEDOWN:
		pos++;
		break;
	case SB_PAGEUP:
		pos -= (int)sinfo.nPage;
		break;
	case SB_PAGEDOWN:
		pos += (int)sinfo.nPage;
		break;
	case SB_THUMBPOSITION:
	case SB_THUMBTRACK:
		pos = sinfo.nTrackPos;
		break;
	default:
		return;
	}

	if (pos < 0)
		pos = 0;
	if (pos > sinfo.nMax - (int)sinfo.nPage)
		pos = sinfo.nMax - (int)sinfo.nPage;

	s_startLine = (ines_int_t)pos;

	wReg_UpdateScrollBar(hWnd);
	InvalidateRect(hWnd, NULL, FALSE);
}


// ---------------------------------------------------------------------
// 选中 / 编辑 / 写入
// ---------------------------------------------------------------------
static VOID wReg_ShowTips(const char* pUtf8)
{
	wReg_Utf8ToTChar(s_tips, count_of(s_tips), pUtf8);
	s_tipsExpire = GetTickCount() + WREG_TIPS_TIME;
}

static BOOL wReg_TipsActive(VOID)
{
	return ((s_tips[0] != 0) && (GetTickCount() < s_tipsExpire));
}


static VOID wReg_CancelEdit(VOID)
{
	s_editActive = FALSE;
	s_editValue  = 0;
	s_editNibble = 0;
}


static VOID wReg_EnsureCursorVisible(HWND hWnd)
{
	ines_int_t  reg;
	ines_int_t  span;

	reg = wReg_CurReg();
	span = (reg >= 0) ? wReg_RowSpan(s_defs[reg].width) : 1;

	if (s_curLine < s_startLine)
		s_startLine = s_curLine;
	else if ((s_curLine + span - 1) > (s_startLine + wReg_iRows - 1))
		s_startLine = s_curLine + span - 1 - wReg_iRows + 1;

	wReg_UpdateScrollBar(hWnd);
}


// line 可以是寄存器的首行或(16 位)高字节行, 内部统一归一到首行
static VOID wReg_SelectLine(HWND hWnd, ines_int_t line, ines_int_t col, ines_int_t bit)
{
	ines_int_t  group = -1;
	ines_int_t  reg   = -1;
	ines_int_t  sub   = 0;

	if ((line < 0) || (line >= s_lineCount))
		return;
	if (wReg_RowAt(line, &group, &reg, &sub))
		return;                                        // 组标题行不接受选中

	// 16 位寄存器的高字节行: 归一到首行
	while ((sub > 0) && (line > 0))
	{
		line--;
		sub = s_lineMap[line].sub;
	}

	if (s_editActive && (line != s_curLine))
		wReg_CancelEdit();

	s_curLine   = line;
	s_cursorCol = col;

	if (col == WREG_CUR_BIT)
	{
		ines_int_t  width = s_defs[reg].width;

		if (width > 16) width = 16;
		if (width <= 0)
		{
			s_cursorCol = WREG_CUR_VALUE;              // 无位格的行退回值列
		}
		else
		{
			if (bit < 0)      bit = 0;
			if (bit >= width) bit = width - 1;
			s_bitIndex = bit;
		}
	}

	wReg_EnsureCursorVisible(hWnd);
	InvalidateRect(hWnd, NULL, FALSE);
}


// 在"寄存器"之间移动(跳过组标题, 16 位寄存器的高字节行不停留)
static VOID wReg_MoveRow(HWND hWnd, ines_int_t delta)
{
	ines_int_t  line = s_curLine;

	for (;;)
	{
		line += delta;
		if ((line < 0) || (line >= s_lineCount))
			return;
		if (s_lineMap[line].reg < 0)
			continue;                                  // 跳过组标题
		if (s_lineMap[line].sub != 0)
			continue;                                  // 停在首行

		wReg_SelectLine(hWnd, line, s_cursorCol, s_bitIndex);
		return;
	}
}


static VOID wReg_MoveToWritableRow(HWND hWnd, ines_int_t delta)
{
	ines_int_t  line = s_curLine;

	for (;;)
	{
		line += delta;
		if ((line < 0) || (line >= s_lineCount))
			return;
		if (s_lineMap[line].reg < 0)
			continue;
		if (s_lineMap[line].sub != 0)
			continue;
		if (s_defs[s_lineMap[line].reg].wmask == 0)
			continue;                                  // 只读行: Tab 跳过

		wReg_SelectLine(hWnd, line, s_cursorCol, s_bitIndex);
		return;
	}
}


// $4014 OAMDMA 会立即触发 256 字节 DMA + 514 周期: 写前确认一次(按住 Shift 跳过)
static BOOL wReg_ConfirmWrite(HWND hWnd, ines_int_t regId, ines_int_t val)
{
	ines_char_t  szMsg[256];
	int          ret;

	if (regId != WREG_REG_OAMDMA)
		return TRUE;

	if (GetKeyState(VK_SHIFT) < 0)
		return TRUE;

	_sntprintf(szMsg, count_of(szMsg),
			   ISTR("将立即从 $%02X00 传送 256 字节到精灵内存, 并消耗 514 个 CPU 周期。"),
			   (int)(val & 0xFF));
	szMsg[count_of(szMsg) - 1] = 0;

	ret = MessageBox(hWnd, szMsg, wReg_szTitle, MB_OKCANCEL | MB_ICONWARNING | MB_DEFBUTTON2);

	return (ret == IDOK);
}


// 应用一次写请求(与 mac 的 idbg_apply_reg_write 同语义):
//   * CPU 内部寄存器直接改字段;
//   * PPU/APU 端口走 ines_ppu_writelow / ines_apu_write, 保留真实写入副作用;
//   * PPU 内部 T/V 直接改字段(不经过 $2005/$2006 双写, 避免连带改动另一个寄存器);
//   * $4014 / $4016 走 ines_host_write。
// win32 的主线程即模拟线程(OnIdle 里调 ines_host_doframe), 因此这里直接应用, 无需写队列。
static BOOL wReg_ApplyWrite(ines_int_t regId, ines_int_t val)
{
	ines_cpu_t*  pCpu = &host.cpu;
	ines_ppu_t*  pPpu = &host.ppu;
	ines_apu_t*  pApu = &host.apu;

	if (host.status == NES_STATUS_OFF)
		return FALSE;

	switch (regId)
	{
	/* ---- CPU ---- */
	case WREG_REG_A:        pCpu->reg_A  = (ines_byte_t)(val & 0xFF);            return TRUE;
	case WREG_REG_X:        pCpu->reg_X  = (ines_byte_t)(val & 0xFF);            return TRUE;
	case WREG_REG_Y:        pCpu->reg_Y  = (ines_byte_t)(val & 0xFF);            return TRUE;
	case WREG_REG_P:        pCpu->reg_P  = (ines_byte_t)(val & 0xFF);            return TRUE;
	case WREG_REG_SP:       pCpu->reg_SP = (ines_byte_t)(val & 0xFF);            return TRUE;
	case WREG_REG_PC:       pCpu->reg_PC = (ines_word_t)(val & 0xFFFF);          return TRUE;
	case WREG_REG_IRQ_PEND: pCpu->INT_pending = (ines_byte_t)(val & 0x07);       return TRUE;

	/* ---- PPU ---- */
	case WREG_REG_PPUCTRL:  ines_ppu_writelow(pPpu, 0x2000, (ines_byte_t)val);   return TRUE;
	case WREG_REG_PPUMASK:  ines_ppu_writelow(pPpu, 0x2001, (ines_byte_t)val);   return TRUE;
	case WREG_REG_OAMADDR:  ines_ppu_writelow(pPpu, 0x2003, (ines_byte_t)val);   return TRUE;
	case WREG_REG_OAMDATA:  ines_ppu_writelow(pPpu, 0x2004, (ines_byte_t)val);   return TRUE;
	case WREG_REG_PPUDATA:  ines_ppu_writelow(pPpu, 0x2007, (ines_byte_t)val);   return TRUE;
	case WREG_REG_PPU_T:    pPpu->index_t = (ines_word_t)(val & 0x7FFF);         return TRUE;
	case WREG_REG_PPU_V:    pPpu->index_v = (ines_word_t)(val & 0x7FFF);         return TRUE;

	/* ---- APU(端口写) ---- */
	case WREG_REG_P1VOL:
	case WREG_REG_P1SWP:
	case WREG_REG_P1TLO:
	case WREG_REG_P1THI:
		ines_apu_write(pApu, (ines_word_t)(0x4000 + (regId - WREG_REG_P1VOL)), (ines_byte_t)val);
		return TRUE;
	case WREG_REG_P2VOL:
	case WREG_REG_P2SWP:
	case WREG_REG_P2TLO:
	case WREG_REG_P2THI:
		ines_apu_write(pApu, (ines_word_t)(0x4004 + (regId - WREG_REG_P2VOL)), (ines_byte_t)val);
		return TRUE;
	case WREG_REG_TRLIN:
	case WREG_REG_TR_UNUSED:
	case WREG_REG_TRTLO:
	case WREG_REG_TRTHI:
		ines_apu_write(pApu, (ines_word_t)(0x4008 + (regId - WREG_REG_TRLIN)), (ines_byte_t)val);
		return TRUE;
	case WREG_REG_NSVOL:
	case WREG_REG_NS_UNUSED:
	case WREG_REG_NSFRQ:
	case WREG_REG_NSLEN:
		ines_apu_write(pApu, (ines_word_t)(0x400C + (regId - WREG_REG_NSVOL)), (ines_byte_t)val);
		return TRUE;
	case WREG_REG_DMFREQ:
	case WREG_REG_DMDAC:
	case WREG_REG_DMADDR:
	case WREG_REG_DMLEN:
		ines_apu_write(pApu, (ines_word_t)(0x4010 + (regId - WREG_REG_DMFREQ)), (ines_byte_t)val);
		return TRUE;
	case WREG_REG_APUCTRL:  ines_apu_write(pApu, 0x4015, (ines_byte_t)val);     return TRUE;
	case WREG_REG_FRAMECTR: ines_apu_write(pApu, 0x4017, (ines_byte_t)val);     return TRUE;

	/* ---- I/O ---- */
	case WREG_REG_OAMDMA:   ines_host_write(&host, 0x4014, (ines_byte_t)val);   return TRUE;
	case WREG_REG_JOYPAD1:  ines_host_write(&host, 0x4016, (ines_byte_t)val);   return TRUE;

	/* ---- 只读项(PPUSTATUS/PPUSCROLL/SCANLINE/VBLANK/TOGGLE/APUSTAT/JOYPAD2/CYCLES) ---- */
	default:
		return FALSE;
	}
}


static VOID wReg_CommitValue(HWND hWnd, ines_int_t regIdx, ines_int64_t val)
{
	const wreg_def_t*  pDef;
	ines_int64_t       mask;

	if ((regIdx < 0) || (regIdx >= WREG_REG_COUNT))
		return;

	pDef = &s_defs[regIdx];

	if (pDef->wmask == 0)
	{
		wReg_ShowTips("该寄存器只读");
		InvalidateRect(hWnd, NULL, FALSE);
		return;
	}

	mask = (pDef->width >= 64) ? (ines_int64_t)~0ULL : ((1ULL << pDef->width) - 1ULL);
	val  = (val & mask) & (ines_int64_t)pDef->wmask;

	if (!wReg_ConfirmWrite(hWnd, pDef->reg_id, (ines_int_t)val))
	{
		InvalidateRect(hWnd, NULL, FALSE);
		return;
	}

	if (wReg_ApplyWrite(pDef->reg_id, (ines_int_t)val))
		INES_LOG(LOG_DBG, MOD_SYS, ISTR("%s: reg=%d val=0x%X\n"), __TFUNCTION__, (int)pDef->reg_id, (unsigned int)val);

	wReg_iUpdate = 1;
	InvalidateRect(hWnd, NULL, FALSE);
}


static VOID wReg_CommitEdit(HWND hWnd)
{
	ines_int_t  reg = wReg_CurReg();

	if (!s_editActive || (reg < 0))
		return;

	wReg_CancelEdit();
	wReg_CommitValue(hWnd, reg, s_editValue);
}


// 位格: 单比特改写并立即提交
static VOID wReg_ToggleBit(HWND hWnd, ines_int_t bit, ines_int_t bitVal)
{
	ines_int_t            reg = wReg_CurReg();
	const wreg_def_t*     pDef;
	ines_int64_t          cur;

	if (reg < 0)
		return;

	pDef = &s_defs[reg];

	if (((pDef->wmask >> bit) & 1) == 0)
	{
		wReg_ShowTips("该位只读");
		InvalidateRect(hWnd, NULL, FALSE);
		return;
	}

	cur = wReg_Value(reg);
	cur = (cur & ~(1ULL << bit)) | ((ines_int64_t)(bitVal ? 1 : 0) << bit);

	wReg_CommitValue(hWnd, reg, cur);
}


// ---------------------------------------------------------------------
// 剪贴板(右键菜单: 复制值 / 复制全部)
// ---------------------------------------------------------------------
static VOID wReg_CopyText(HWND hWnd, ines_cstr_t pText)
{
	HGLOBAL      hMem;
	LPTSTR       pBuf;
	ines_size_t  nBytes;

	if (pText == NULL)
		return;

	if (!OpenClipboard(hWnd))
		return;

	EmptyClipboard();

	nBytes = (_tcslen(pText) + 1) * sizeof(ines_char_t);
	hMem = GlobalAlloc(GMEM_MOVEABLE, nBytes);

	if (hMem != NULL)
	{
		pBuf = (LPTSTR)GlobalLock(hMem);
		if (pBuf != NULL)
		{
			memcpy(pBuf, pText, nBytes);
			GlobalUnlock(hMem);
#ifdef UNICODE
			SetClipboardData(CF_UNICODETEXT, hMem);
#else
			SetClipboardData(CF_TEXT, hMem);
#endif
		}
		else
		{
			GlobalFree(hMem);
		}
	}

	CloseClipboard();
}


static VOID wReg_CopyValue(HWND hWnd)
{
	ines_int_t      reg = wReg_CurReg();
	ines_char_t     szValue[32];

	if (!wReg_HasContent() || (reg < 0))
		return;

	wReg_ValueText(szValue, count_of(szValue), &s_defs[reg], wReg_Value(reg));
	wReg_CopyText(hWnd, szValue);
}


static VOID wReg_CopyAll(HWND hWnd)
{
	ines_char_t   szAll[WREG_REG_COUNT * 80];
	ines_char_t   szValue[32];
	ines_size_t   nUsed = 0;
	ines_int_t    i;

	if (!wReg_HasContent())
		return;

	szAll[0] = 0;

	for (i = 0; i < WREG_REG_COUNT; i++)
	{
		int  n;

		wReg_ValueText(szValue, count_of(szValue), &s_defs[i], wReg_Value(i));

		n = _sntprintf(szAll + nUsed, count_of(szAll) - nUsed,
					   ISTR("%-10s %-7s %s\r\n"), s_nameText[i], s_addrText[i], szValue);
		if (n <= 0)
			break;

		nUsed += (ines_size_t)n;
		if (nUsed >= count_of(szAll) - 1)
			break;
	}

	szAll[count_of(szAll) - 1] = 0;
	wReg_CopyText(hWnd, szAll);
}


static VOID wReg_OnContextMenu(HWND hWnd, int x, int y)
{
	HMENU    hMenu;
	POINT    pt;
	UINT     cmd;

	hMenu = CreatePopupMenu();
	if (hMenu == NULL)
		return;

	AppendMenu(hMenu, MF_STRING, 1, ISTR("复制值"));
	AppendMenu(hMenu, MF_STRING, 2, ISTR("复制全部"));

	pt.x = x;
	pt.y = y;
	ClientToScreen(hWnd, &pt);

	cmd = (UINT)TrackPopupMenu(hMenu, TPM_LEFTALIGN | TPM_TOPALIGN | TPM_RETURNCMD | TPM_RIGHTBUTTON,
							   pt.x, pt.y, 0, hWnd, NULL);

	DestroyMenu(hMenu);

	switch (cmd)
	{
	case 1:
		wReg_CopyValue(hWnd);
		break;
	case 2:
		wReg_CopyAll(hWnd);
		break;
	default:
		break;
	}
}


// ---------------------------------------------------------------------
// 交互
// ---------------------------------------------------------------------
static VOID wReg_OnLButtonDown(HWND hWnd, int x, int y)
{
	ines_int_t  line;
	ines_int_t  col;
	ines_int_t  group = -1;
	ines_int_t  reg   = -1;
	ines_int_t  sub   = 0;

	if (!wReg_HasContent())
		return;

	wReg_Layout(hWnd);

	x -= wReg_iPadX;
	y -= wReg_iPadY;

	line = (ines_int_t)(y / wReg_iCharHeight) - WREG_HEAD_LINES + s_startLine;
	if (line < 0)
		return;

	if (wReg_RowAt(line, &group, &reg, &sub))
		return;                                          // 组标题: 由双击处理折叠

	if (reg < 0)
		return;

	col = (ines_int_t)(x / wReg_iCharWidth);

	if (col < WREG_COL_VALUE)
		return;                                          // 名称/地址列: 不选中
	else if (col < WREG_COL_BITS)
		wReg_SelectLine(hWnd, line, WREG_CUR_VALUE, 0);
	else if (col < WREG_COL_NOTE)
	{
		ines_int_t  cell  = (col - WREG_COL_BITS) / WREG_BIT_CELL;
		ines_int_t  bit   = (sub * 8) + (7 - cell);      // 子行决定是低字节还是高字节
		ines_int_t  width = s_defs[reg].width;

		if (width > 16) width = 16;
		if ((width > 0) && (bit >= 0) && (bit < width))
			wReg_SelectLine(hWnd, line, WREG_CUR_BIT, bit);
	}
}


static VOID wReg_OnLButtonDblClk(HWND hWnd, int x, int y)
{
	ines_int_t  line;
	ines_int_t  group = -1;
	ines_int_t  reg   = -1;
	ines_int_t  sub   = 0;

	if (!wReg_HasContent())
		return;

	wReg_Layout(hWnd);

	y -= wReg_iPadY;
	line = (ines_int_t)(y / wReg_iCharHeight) - WREG_HEAD_LINES + s_startLine;
	if (line < 0)
		return;

	// 组标题: 双击折叠/展开
	if (wReg_RowAt(line, &group, &reg, &sub) && (group >= 0))
	{
		s_folded[group] = !s_folded[group];
		wReg_RebuildLineMap();
		wReg_UpdateScrollBar(hWnd);
		InvalidateRect(hWnd, NULL, FALSE);
	}
}


static VOID wReg_OnMouseWheel(HWND hWnd, int nDelta)
{
	ines_int_t  dLine;

	dLine = -(nDelta / WHEEL_DELTA) * 3;

	if (dLine == 0)
		return;

	s_startLine += dLine;
	if (s_startLine < 0)
		s_startLine = 0;
	if (s_startLine > (s_lineCount - wReg_iRows))
		s_startLine = s_lineCount - wReg_iRows;
	if (s_startLine < 0)
		s_startLine = 0;

	wReg_UpdateScrollBar(hWnd);
	InvalidateRect(hWnd, NULL, FALSE);
}


static VOID wReg_OnKeyDown(HWND hWnd, UINT nKeyCode)
{
	ines_int_t  reg;
	BOOL        bShift;

	if (!wReg_HasContent())
		return;

	reg    = wReg_CurReg();
	bShift = (GetKeyState(VK_SHIFT) < 0);

	switch (nKeyCode)
	{
	case VK_UP:
		wReg_CancelEdit();
		wReg_MoveRow(hWnd, -1);
		return;
	case VK_DOWN:
		wReg_CancelEdit();
		wReg_MoveRow(hWnd, 1);
		return;
	case VK_LEFT:
		if (s_cursorCol == WREG_CUR_BIT)
		{
			if ((reg >= 0) && (s_bitIndex < s_defs[reg].width - 1))
				s_bitIndex++;
			InvalidateRect(hWnd, NULL, FALSE);
		}
		else if (s_editActive)
		{
			if (s_editNibble > 0)
				s_editNibble--;
			InvalidateRect(hWnd, NULL, FALSE);
		}
		return;
	case VK_RIGHT:
		if (s_cursorCol == WREG_CUR_BIT)
		{
			if (s_bitIndex > 0)
				s_bitIndex--;
			InvalidateRect(hWnd, NULL, FALSE);
		}
		else if (s_editActive)
		{
			if ((reg >= 0) && (s_editNibble < (s_defs[reg].width / 4) - 1))
				s_editNibble++;
			InvalidateRect(hWnd, NULL, FALSE);
		}
		return;
	case VK_PRIOR:
		wReg_OnScroll(hWnd, SB_PAGEUP);
		return;
	case VK_NEXT:
		wReg_OnScroll(hWnd, SB_PAGEDOWN);
		return;
	case VK_HOME:
		if (s_cursorCol == WREG_CUR_BIT)
		{
			if (reg >= 0)
			{
				ines_int_t  w = s_defs[reg].width;

				if (w > 16) w = 16;
				if (w > 0) s_bitIndex = w - 1;
			}
			InvalidateRect(hWnd, NULL, FALSE);
		}
		else
		{
			s_startLine = 0;
			wReg_UpdateScrollBar(hWnd);
			InvalidateRect(hWnd, NULL, FALSE);
		}
		return;
	case VK_END:
		if (s_cursorCol == WREG_CUR_BIT)
		{
			s_bitIndex = 0;
			InvalidateRect(hWnd, NULL, FALSE);
		}
		else
		{
			s_startLine = s_lineCount - wReg_iRows;
			if (s_startLine < 0) s_startLine = 0;
			wReg_UpdateScrollBar(hWnd);
			InvalidateRect(hWnd, NULL, FALSE);
		}
		return;
	case VK_RETURN:
		wReg_CommitEdit(hWnd);
		return;
	case VK_ESCAPE:
		wReg_CancelEdit();
		InvalidateRect(hWnd, NULL, FALSE);
		return;
	case VK_TAB:
		wReg_CancelEdit();
		wReg_MoveToWritableRow(hWnd, bShift ? -1 : 1);
		return;
	case VK_SPACE:
		if (s_cursorCol == WREG_CUR_VALUE)
		{
			// 值列 -> 位格(光标落在最高可写位)
			if ((reg >= 0) && (s_defs[reg].width > 0) && (s_defs[reg].width <= 16))
			{
				ines_int_t  b;

				for (b = s_defs[reg].width - 1; b >= 0; b--)
				{
					if (((s_defs[reg].wmask >> b) & 1) != 0)
						break;
				}
				if (b < 0)
					b = s_defs[reg].width - 1;

				s_cursorCol = WREG_CUR_BIT;
				s_bitIndex  = b;
				InvalidateRect(hWnd, NULL, FALSE);
			}
		}
		else if (reg >= 0)
		{
			ines_int64_t  cur = wReg_Value(reg);

			wReg_ToggleBit(hWnd, s_bitIndex, ((cur >> s_bitIndex) & 1) ? 0 : 1);
		}
		return;
	default:
		break;
	}
}


static VOID wReg_OnChar(HWND hWnd, TCHAR nChar)
{
	ines_int_t  reg;
	int         val = -1;

	if (!wReg_HasContent())
		return;

	reg = wReg_CurReg();
	if (reg < 0)
		return;

	// ---- 位格: 只接受 0 / 1 ----
	if (s_cursorCol == WREG_CUR_BIT)
	{
		if ((nChar == _T('0')) || (nChar == _T('1')))
		{
			wReg_ToggleBit(hWnd, s_bitIndex, (nChar == _T('1')) ? 1 : 0);
			if (s_bitIndex > 0)
				s_bitIndex--;                             // 便于从高位往低位连续输入
			InvalidateRect(hWnd, NULL, FALSE);
		}
		else
		{
			wReg_ShowTips("只能输入 0 / 1");
			InvalidateRect(hWnd, NULL, FALSE);
		}
		return;
	}

	// ---- 值列: 十六进制 nibble ----
	if ((nChar >= _T('0')) && (nChar <= _T('9')))
		val = (int)(nChar - _T('0'));
	else if ((nChar >= _T('a')) && (nChar <= _T('f')))
		val = (int)(nChar - _T('a')) + 10;
	else if ((nChar >= _T('A')) && (nChar <= _T('F')))
		val = (int)(nChar - _T('A')) + 10;

	if (val < 0)
		return;

	{
		ines_int_t  width  = s_defs[reg].width;
		ines_int_t  digits = width / 4;

		if ((width != 8) && (width != 16))
		{
			wReg_ShowTips("该寄存器只读");
			InvalidateRect(hWnd, NULL, FALSE);
			return;
		}
		if (s_defs[reg].wmask == 0)
		{
			wReg_ShowTips("该寄存器只读");
			InvalidateRect(hWnd, NULL, FALSE);
			return;
		}

		if (!s_editActive)
		{
			s_editActive = TRUE;
			s_editValue  = wReg_Value(reg);
			s_editNibble = 0;
		}

		{
			ines_int_t  shift = (digits - 1 - s_editNibble) * 4;

			s_editValue = (s_editValue & ~((ines_int64_t)0x0F << shift)) | ((ines_int64_t)val << shift);
		}

		s_editNibble++;
		if (s_editNibble >= digits)
			wReg_CommitEdit(hWnd);
		else
			InvalidateRect(hWnd, NULL, FALSE);
	}
}


// ---------------------------------------------------------------------
// 变化跟踪(红色高亮 0.5s)
// ---------------------------------------------------------------------
static VOID wReg_TrackChanges(VOID)
{
	DWORD        now = GetTickCount();
	ines_int_t   i;

	if (!wReg_HasContent())
	{
		for (i = 0; i < WREG_REG_COUNT; i++)
			s_lastValid[i] = FALSE;
		return;
	}

	for (i = 0; i < WREG_REG_COUNT; i++)
	{
		ines_int64_t  v = wReg_Value(i);

		if (!s_lastValid[i])
		{
			s_lastValid[i]  = TRUE;
			s_lastValue[i]  = v;
			s_lastChange[i] = 0;
			continue;
		}

		if (v != s_lastValue[i])
		{
			s_prevValue[i]  = s_lastValue[i];
			s_lastValue[i]  = v;
			s_lastChange[i] = now;
		}
	}
}


// 高亮有效期内或刚过期都要重绘(余量 70ms > 刷新间隔 50ms)
static BOOL wReg_HighlightActive(VOID)
{
	DWORD       now = GetTickCount();
	ines_int_t  i;

	for (i = 0; i < WREG_REG_COUNT; i++)
	{
		if (s_lastValid[i] && (s_lastChange[i] != 0) &&
			((now - s_lastChange[i]) < (DWORD)(WREG_HILITE_TIME + 70)))
			return TRUE;
	}

	return FALSE;
}


// 50ms 定时器: 变化记账 + 重绘判定(与 wMemory 的 WM_TIMER 节奏一致)
static VOID wReg_OnTimer(HWND hWnd)
{
	wReg_TrackChanges();

	if (wReg_iUpdate)
	{
		wReg_iUpdate = 0;
		InvalidateRect(hWnd, NULL, FALSE);
		return;
	}

	// 暂停时没有帧推进, 但高亮回落 / Tips 淡出仍需补一拍重绘
	if (wReg_TipsActive() || wReg_HighlightActive())
	{
		InvalidateRect(hWnd, NULL, FALSE);
		return;
	}
}


// ---------------------------------------------------------------------
// 绘制
// ---------------------------------------------------------------------
static BOOL wReg_IsChanged(ines_int_t reg, DWORD now)
{
	return (s_lastValid[reg] && (s_lastChange[reg] != 0) &&
			((now - s_lastChange[reg]) < (DWORD)WREG_HILITE_TIME));
}


static VOID wReg_FillCell(HDC hDC, const RECT* pRc, BOOL bSelected, BOOL bWritable,
						  HBRUSH hBrushGray, HBRUSH hBrushSel)
{
	if (!bWritable)
	{
		FillRect(hDC, pRc, hBrushGray);
		return;
	}

	if (bSelected)
		FillRect(hDC, pRc, hBrushSel);
}


static VOID wReg_StrokeFocus(HDC hDC, const RECT* pRc, HBRUSH hBrush)
{
	RECT  rc = *pRc;

	rc.right  -= 1;
	rc.bottom -= 1;
	FrameRect(hDC, &rc, hBrush);
}


static VOID wReg_OnDrawEx(HWND hWnd, HDC hDC, LPRECT lpClipRect)
{
	RECT       rcClient;
	HBRUSH     hBrushBG;
	HBRUSH     hBrushGray;
	HBRUSH     hBrushSel;
	HBRUSH     hBrushRowSel;
	HBRUSH     hBrushGroup;
	HBRUSH     hBrushOff;
	HBRUSH     hBrushLine;
	HPEN       hPenLine;
	HPEN       hPenBit;
	HPEN       hOldPen;
	HFONT      hOldFont;
	COLORREF   clrText;
	COLORREF   clrGray;
	COLORREF   clrNote;
	COLORREF   clrDim;
	COLORREF   clrSelText;
	DWORD      now;
	ines_int_t curReg;
	ines_int_t iLine;
	ines_int_t total;
	ines_int_t bitIdx;
	BOOL       bContent;
	int        y;

	GetClientRect(hWnd, &rcClient);
	wReg_Layout(hWnd);

	bContent = wReg_HasContent();

	hBrushOff    = CreateSolidBrush(WREG_OFF_GRAY);
	hBrushBG     = CreateSolidBrush(GetSysColor(COLOR_WINDOW));
	hBrushGray   = CreateSolidBrush(WREG_GRAY_FILL);
	hBrushSel    = CreateSolidBrush(GetSysColor(COLOR_HIGHLIGHT));
	hBrushRowSel = CreateSolidBrush(wReg_Mix(GetSysColor(COLOR_HIGHLIGHT), GetSysColor(COLOR_WINDOW), 10));
	hBrushGroup  = CreateSolidBrush(GetSysColor(COLOR_BTNFACE));
	hBrushLine   = CreateSolidBrush(GetSysColor(COLOR_3DSHADOW));
	hPenLine     = CreatePen(PS_SOLID, 1, GetSysColor(COLOR_3DSHADOW));
	hPenBit      = CreatePen(PS_SOLID, 1, wReg_Mix(GetSysColor(COLOR_WINDOWTEXT), GetSysColor(COLOR_WINDOW), 25));

	clrText    = GetSysColor(COLOR_WINDOWTEXT);
	clrGray    = GetSysColor(COLOR_GRAYTEXT);
	clrNote    = GetSysColor(COLOR_GRAYTEXT);
	clrDim     = wReg_Mix(clrText, GetSysColor(COLOR_WINDOW), 45);
	clrSelText = GetSysColor(COLOR_HIGHLIGHTTEXT);

	// 未载入 ROM: 整块灰底(与 3 个内存视图一致)
	if (!bContent)
	{
		FillRect(hDC, lpClipRect, hBrushOff);
		goto wReg_draw_cleanup;
	}

	FillRect(hDC, lpClipRect, hBrushBG);

	hOldPen  = (HPEN)SelectObject(hDC, (HGDIOBJ)hPenLine);
	hOldFont = (HFONT)SelectObject(hDC, (HGDIOBJ)wReg_hFont);

	SetBkMode(hDC, TRANSPARENT);

	now    = GetTickCount();
	curReg = wReg_CurReg();

	// ---- 头部 ----
	y = wReg_iPadY;

	SetTextColor(hDC, clrText);
	TextOut(hDC, wReg_ColX(0), y, ISTR("NAME"), 4);
	TextOut(hDC, wReg_ColX(WREG_COL_ADDR), y, ISTR("ADDR"), 4);
	TextOut(hDC, wReg_ColX(WREG_COL_VALUE), y, ISTR("VALUE"), 5);
	SetTextColor(hDC, clrNote);
	TextOut(hDC, wReg_ColX(WREG_COL_NOTE), y, s_headNote, (int)_tcslen(s_headNote));

	// 位号: 每行只 8 位, 头部标 7..0(高低字节行共用同一组位号)
	SetTextColor(hDC, clrText);
	for (bitIdx = 0; bitIdx < 8; bitIdx++)
	{
		TCHAR  ch = (TCHAR)(_T('0') + (7 - bitIdx));

		wReg_DrawChar(hDC, ch, wReg_ColX(WREG_COL_BITS + bitIdx * WREG_BIT_CELL), y, WREG_BIT_CELL, clrText, FALSE);
	}

	// 头部分隔线
	MoveToEx(hDC, wReg_iPadX, y + wReg_iCharHeight, NULL);
	LineTo(hDC, rcClient.right - wReg_iPadX, y + wReg_iCharHeight);

	// ---- 正文 ----
	total = s_lineCount;
	if ((total - s_startLine) > wReg_iRows)
		total = s_startLine + wReg_iRows;

	for (iLine = s_startLine; iLine < total; iLine++)
	{
		ines_int_t  group = s_lineMap[iLine].group;
		ines_int_t  reg   = s_lineMap[iLine].reg;
		ines_int_t  sub   = s_lineMap[iLine].sub;
		BOOL        sel;
		RECT        rcRow;

		y = wReg_LineY(iLine);
		sel = ((reg >= 0) && (reg == curReg));

		rcRow.left   = wReg_iPadX;
		rcRow.top    = y;
		rcRow.right  = rcClient.right - wReg_iPadX;
		rcRow.bottom = y + wReg_iCharHeight;

		if (reg < 0)
		{
			ines_char_t  szTitle[32];

			FillRect(hDC, &rcRow, hBrushGroup);
			SetTextColor(hDC, clrText);

			_sntprintf(szTitle, count_of(szTitle), ISTR("%s %s"),
					   (s_folded[group] ? ISTR("+") : ISTR("-")), s_groupText[group]);
			szTitle[count_of(szTitle) - 1] = 0;
			TextOut(hDC, wReg_ColX(0), y, szTitle, (int)_tcslen(szTitle));
			continue;
		}

		{
			const wreg_def_t*  pDef     = &s_defs[reg];
			ines_int64_t       val      = wReg_Value(reg);
			ines_int_t         width    = pDef->width;
			BOOL               writable = (pDef->wmask != 0);
			BOOL               changed  = wReg_IsChanged(reg, now);
			ines_int64_t       diff;

			// 选中行的淡色行底(与"当前单元格"的反白区分); 16 位寄存器的两个子行都铺
			if (sel)
				FillRect(hDC, &rcRow, hBrushRowSel);

			// ---- 名称 / 地址 / 值 / 说明: 只在首行画 ----
			if (sub == 0)
			{
				RECT  rcValue;

				SetTextColor(hDC, changed ? WREG_RED_TEXT : clrText);
				TextOut(hDC, wReg_ColX(0), y, s_nameText[reg], (int)_tcslen(s_nameText[reg]));

				SetTextColor(hDC, clrGray);
				TextOut(hDC, wReg_ColX(WREG_COL_ADDR), y, s_addrText[reg], (int)_tcslen(s_addrText[reg]));

				// ---- 值列 ----
				{
					ines_char_t  szValue[32];
					BOOL         cellSel = (sel && (s_cursorCol == WREG_CUR_VALUE));
					int          nLen;

					if (s_editActive && sel)
						wReg_ValueText(szValue, count_of(szValue), pDef, s_editValue);
					else
						wReg_ValueText(szValue, count_of(szValue), pDef, val);

					nLen = (int)_tcslen(szValue);

					rcValue.left   = wReg_ColX(WREG_COL_VALUE);
					rcValue.top    = y;
					rcValue.right  = rcValue.left + WREG_VALUE_COLS * wReg_iCharWidth;
					rcValue.bottom = y + wReg_iCharHeight;

					wReg_FillCell(hDC, &rcValue, cellSel, writable, hBrushGray, hBrushSel);

					// 只读值: 灰底表示不可改, 但字色与可写值一致(黑/深色, 变化仍红)
					if (cellSel)
						SetTextColor(hDC, clrSelText);
					else
						SetTextColor(hDC, changed ? WREG_RED_TEXT : clrText);

					// 等宽字体 + 纯 ASCII: 宽度 = 字符数 * 字符宽, 免测量; 值列右对齐
					TextOut(hDC, rcValue.right - nLen * wReg_iCharWidth - wReg_iCharWidth / 2,
							y, szValue, nLen);

					// 编辑标记
					if (s_editActive && sel)
						TextOut(hDC, rcValue.left, y, ISTR("*"), 1);

					if (cellSel)
						wReg_StrokeFocus(hDC, &rcValue, hBrushSel);
				}

				// ---- 说明(只画在首行) ----
				SetTextColor(hDC, clrNote);
				if (pDef->reg_id == WREG_REG_PPUSCROLL)
				{
					ines_char_t  szScroll[48];

					wReg_ScrollText(szScroll, count_of(szScroll));
					TextOut(hDC, wReg_ColX(WREG_COL_NOTE), y, szScroll, (int)_tcslen(szScroll));
				}
				else
				{
					TextOut(hDC, wReg_ColX(WREG_COL_NOTE), y, s_noteText[reg], (int)_tcslen(s_noteText[reg]));
				}
			}

			// ---- 位格: 每个子行画 8 位(sub 0 = 低字节, sub 1 = 高字节) ----
			if ((width > 0) && (width <= 16) && ((sub * 8) < width))
			{
				ines_int_t  base = sub * 8;
				ines_int_t  bMax = (width < (base + 8)) ? width : (base + 8);
				ines_int_t  b;

				diff = changed ? (val ^ s_prevValue[reg]) : 0;

				// 高低字节标记(仅 16 位寄存器)
				if (width == 16)
				{
					TCHAR  ch = (TCHAR)(sub ? _T('H') : _T('L'));

					SetTextColor(hDC, clrNote);
					TextOut(hDC, wReg_ColX(WREG_COL_TAG), y, &ch, 1);
				}

				for (b = base; b < bMax; b++)
				{
					ines_int_t  cell = wReg_BitCell(b);
					RECT        brc;
					BOOL        wbit = (((pDef->wmask >> b) & 1) != 0);
					BOOL        bsel = (sel && (s_cursorCol == WREG_CUR_BIT) && (s_bitIndex == b));
					BOOL        one  = (((val >> b) & 1) != 0);
					TCHAR       ch;
					COLORREF    clr;
					BOOL        bBold;

					brc.left   = wReg_ColX(WREG_COL_BITS + cell * WREG_BIT_CELL);
					brc.top    = y;
					brc.right  = brc.left + WREG_BIT_CELL * wReg_iCharWidth;
					brc.bottom = y + wReg_iCharHeight;

					wReg_FillCell(hDC, &brc, bsel, wbit, hBrushGray, hBrushSel);

					// 字色/字重表达 0 与 1: 1 = 常色粗体, 0 = 浅灰;
					// 只读位(灰底)同样区分明暗, 但不加粗
					if (bsel)
					{
						clr   = clrSelText;
						bBold = TRUE;
					}
					else if (!wbit)
					{
						clr   = one ? clrGray : clrDim;
						bBold = FALSE;
					}
					else if (((diff >> b) & 1) != 0)
					{
						clr   = WREG_RED_TEXT;                  // 变化高亮优先
						bBold = one;
					}
					else
					{
						clr   = one ? clrText : clrDim;
						bBold = one;
					}

					ch = (TCHAR)wReg_BitLetter(pDef, b);
					wReg_DrawChar(hDC, ch, brc.left, y, WREG_BIT_CELL, clr, bBold);

					// 位格边框(让"格"看得出来)
					if (wbit && !bsel)
					{
						SelectObject(hDC, (HGDIOBJ)hPenBit);
						MoveToEx(hDC, brc.left, y, NULL);
						LineTo(hDC, brc.left, y + wReg_iCharHeight);
						SelectObject(hDC, (HGDIOBJ)hPenLine);
					}

					if (bsel)
						wReg_StrokeFocus(hDC, &brc, hBrushSel);
				}
			}
		}
	}

	SelectObject(hDC, (HGDIOBJ)hOldPen);
	SelectObject(hDC, (HGDIOBJ)hOldFont);
	SetBkMode(hDC, OPAQUE);

	// ---- Tips ----
	if (wReg_TipsActive())
	{
		RECT   box;
		SIZE   sz;
		int    nLen = (int)_tcslen(s_tips);

		GetTextExtentPoint32(hDC, s_tips, nLen, &sz);

		box.left   = wReg_iPadX + 4;
		box.right  = box.left + sz.cx + 12;
		box.bottom = rcClient.bottom - wReg_iPadY - wReg_iCharHeight;
		box.top    = box.bottom - wReg_iCharHeight - 6;

		FillRect(hDC, &box, hBrushGroup);
		FrameRect(hDC, &box, hBrushLine);

		SetBkMode(hDC, TRANSPARENT);
		SetTextColor(hDC, clrText);
		TextOut(hDC, box.left + 6, box.top + 3, s_tips, nLen);
		SetBkMode(hDC, OPAQUE);
	}
	else if (s_tips[0] != 0)
	{
		s_tips[0] = 0;                                     // 过期后清空
	}

wReg_draw_cleanup:
	DeleteObject((HGDIOBJ)hBrushOff);
	DeleteObject((HGDIOBJ)hBrushBG);
	DeleteObject((HGDIOBJ)hBrushGray);
	DeleteObject((HGDIOBJ)hBrushSel);
	DeleteObject((HGDIOBJ)hBrushRowSel);
	DeleteObject((HGDIOBJ)hBrushGroup);
	DeleteObject((HGDIOBJ)hBrushLine);
	DeleteObject((HGDIOBJ)hPenLine);
	DeleteObject((HGDIOBJ)hPenBit);
}


static VOID wReg_OnDraw(HWND hWnd, HDC hPaintDC)
{
	HDC       hMemDC;
	HBITMAP   hMemBitmap;
	HBITMAP   hOldBitmap;
	RECT      rcClient;
	RECT      rcClip;
	HRGN      hClipRgn;
	int       width;
	int       height;

	GetClientRect(hWnd, &rcClient);
	GetClipBox(hPaintDC, &rcClip);

	width  = rcClient.right - rcClient.left;
	height = rcClient.bottom - rcClient.top;

	hMemDC     = CreateCompatibleDC(hPaintDC);
	hMemBitmap = CreateCompatibleBitmap(hPaintDC, width, height);
	hClipRgn   = NULL;
	hOldBitmap = NULL;

	if ((hMemDC != NULL) && (hMemBitmap != NULL))
	{
		hOldBitmap = (HBITMAP)SelectObject(hMemDC, (HGDIOBJ)hMemBitmap);
		hClipRgn = CreateRectRgnIndirect(&rcClip);
		SelectClipRgn(hMemDC, hClipRgn);

		wReg_OnDrawEx(hWnd, hMemDC, &rcClip);

		BitBlt(hPaintDC, rcClip.left, rcClip.top, width, height, hMemDC, rcClip.left, rcClip.top, SRCCOPY);

		SelectObject(hMemDC, (HGDIOBJ)hOldBitmap);
	}
	else
	{
		wReg_OnDrawEx(hWnd, hPaintDC, &rcClip);
	}

	if (hMemBitmap != NULL)
		DeleteObject((HGDIOBJ)hMemBitmap);
	if (hMemDC != NULL)
		DeleteDC(hMemDC);
	if (hClipRgn != NULL)
		DeleteObject((HGDIOBJ)hClipRgn);
}


static VOID wReg_OnPaint(HWND hWnd)
{
	HDC           hPaintDC;
	PAINTSTRUCT   ps;

	hPaintDC = BeginPaint(hWnd, &ps);

	wReg_OnDraw(hWnd, hPaintDC);

	EndPaint(hWnd, &ps);
}


// ---------------------------------------------------------------------
// 窗口
// ---------------------------------------------------------------------
static ATOM wReg_RegisterClass(HINSTANCE  hInstance)
{
	WNDCLASSEX wcex;

	wcex.cbSize = sizeof(WNDCLASSEX);

	wcex.style			= CS_HREDRAW | CS_VREDRAW | CS_OWNDC | CS_DBLCLKS;
	wcex.lpfnWndProc	= wReg_WindowProc;
	wcex.cbClsExtra		= 0;
	wcex.cbWndExtra		= 0;
	wcex.hInstance		= hInstance;
	wcex.hIcon			= NULL;
	wcex.hCursor		= LoadCursor(NULL, IDC_ARROW);
	wcex.hbrBackground	= (HBRUSH)GetStockObject(NULL_BRUSH);
	wcex.lpszMenuName	= NULL;
	wcex.lpszClassName	= wReg_szClassName;
	wcex.hIconSm		= NULL;

	return RegisterClassEx(&wcex);
}


// 定义表是 UTF-8 常量, 建窗口时转成 TCHAR 一次(绘制热路径不再转换)
static VOID wReg_InitTexts(VOID)
{
	ines_int_t  i;

	for (i = 0; i < WREG_REG_COUNT; i++)
	{
		wReg_Utf8ToTChar(s_nameText[i], count_of(s_nameText[i]), s_defs[i].name);
		wReg_Utf8ToTChar(s_addrText[i], count_of(s_addrText[i]), s_defs[i].addr);
		wReg_Utf8ToTChar(s_noteText[i], count_of(s_noteText[i]), s_defs[i].note);
	}

	for (i = 0; i < WREG_GROUP_COUNT; i++)
		wReg_Utf8ToTChar(s_groupText[i], count_of(s_groupText[i]), s_groups[i].title);

	wReg_Utf8ToTChar(s_headNote, count_of(s_headNote), "说明");
}


BOOL wReg_Create(HINSTANCE hInstance, HWND hParentWnd)
{
	// already created
	if (wReg_hWnd != NULL)
	{
		return TRUE;
	}

	LoadString(hInstance, IDS_WND_REG_TITLE, wReg_szTitle, count_of(wReg_szTitle));

	wReg_RegisterClass(hInstance);

	wReg_InitTexts();

	wReg_hWnd = CreateWindowEx(0 & WS_EX_TOOLWINDOW, wReg_szClassName, wReg_szTitle,
		WS_POPUPWINDOW | WS_CAPTION | WS_OVERLAPPED | WS_SIZEBOX | WS_MINIMIZEBOX | WS_VSCROLL,
		CW_USEDEFAULT, CW_USEDEFAULT, WREG_WIN_WIDTH, WREG_WIN_HEIGHT, NULL, NULL, hInstance, NULL);

	if (wReg_hWnd == NULL)
	{
		INES_LOG(LOG_ERR, MOD_SYS, ISTR("%s: 创建寄存器查看器窗口失败\n"), __TFUNCTION__);
		return FALSE;
	}

	return TRUE;
}


VOID wReg_Show(BOOL bShow)
{
	if (wReg_hWnd != NULL)
	{
		ShowWindow(wReg_hWnd, bShow ? SW_SHOWNORMAL : SW_HIDE);
		wReg_bShow = bShow;
		if (bShow)
		{
			SetForegroundWindow(wReg_hWnd);
		}
	}
}


BOOL wReg_IsShow()
{
	return wReg_bShow;
}


VOID wReg_SetUpdate()
{
	wReg_iUpdate = 1;
}


VOID wReg_Destroy()
{
	if (wReg_hWnd != NULL)
	{
		DestroyWindow(wReg_hWnd);
	}
}


static BOOL wReg_OnCreate(HWND hWnd, LPCREATESTRUCT  lpCreateStruct)
{
	LOGFONT    lfo;
	RECT       rc;
	HDC        hWndDC;
	HFONT      hOldFont;

	(void)lpCreateStruct;

	// create font
	memset(&lfo, 0, sizeof(lfo));
	GetObject(GetStockObject(SYSTEM_FONT), sizeof(LOGFONT), &lfo);
	lfo.lfHeight = 12;
	lfo.lfWidth = 0;
	lfo.lfWeight = 300;
	_tcsncpy(lfo.lfFaceName, _T("Courier New"), count_of(lfo.lfFaceName));
	wReg_hFont = CreateFontIndirect(&lfo);

	// 位值 1 用粗体: 与"0 = 浅灰"形成双重冗余, 不依赖单一颜色;
	// Courier New Bold 与常规体等宽, 不破坏字符网格
	lfo.lfWeight = FW_BOLD;
	wReg_hFontBold = CreateFontIndirect(&lfo);

	if (wReg_hFont == NULL)
	{
		return FALSE;
	}
	if (wReg_hFontBold == NULL)
	{
		wReg_hFontBold = wReg_hFont;
	}

	hWndDC = GetDC(hWnd);
	hOldFont = (HFONT)SelectObject(hWndDC, (HGDIOBJ)wReg_hFont);

	SetRect(&rc, 0, 0, 0, 0);
	DrawText(hWndDC, _T("X"), 1, &rc, DT_CALCRECT | DT_NOPREFIX);
	wReg_iCharWidth = rc.right - rc.left;
	wReg_iCharHeight = rc.bottom - rc.top;

	SelectObject(hWndDC, (HGDIOBJ)hOldFont);
	ReleaseDC(hWnd, hWndDC);

	if (wReg_iCharWidth < 1)
		wReg_iCharWidth = 1;
	if (wReg_iCharHeight < 1)
		wReg_iCharHeight = 1;

	// 初始化行模型
	s_startLine = 0;
	s_curLine   = 1;                       // 默认选中 CPU 组的第一个寄存器(A)
	s_cursorCol = WREG_CUR_VALUE;
	s_bitIndex  = 0;
	wReg_RebuildLineMap();
	wReg_UpdateScrollBar(hWnd);

	SetTimer(hWnd, 1, 50, NULL);           // 与 wMemory 一致的 50ms 刷新节奏

	return TRUE;
}


static VOID wReg_OnDestroy(HWND hWnd)
{
	KillTimer(hWnd, 1);

	if (wReg_hFontBold != wReg_hFont)
	{
		if (wReg_hFontBold != NULL)
			DeleteObject((HGDIOBJ)wReg_hFontBold);
	}
	wReg_hFontBold = NULL;

	if (wReg_hFont != NULL)
	{
		DeleteObject((HGDIOBJ)wReg_hFont);
		wReg_hFont = NULL;
	}

	wReg_iCharHeight = 0;
	wReg_iCharWidth = 0;

	wReg_hWnd = NULL;
	wReg_bShow = FALSE;
}


static LRESULT CALLBACK	wReg_WindowProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
	switch (uMsg)
	{
	case WM_CREATE:
		if (wReg_OnCreate(hWnd, (LPCREATESTRUCT)lParam))
			return 0;
		else
			return -1;
	case WM_CLOSE:
		DestroyWindow(hWnd);
		return 0;
	case WM_PAINT:
		wReg_OnPaint(hWnd);
		return 0;
	case WM_TIMER:
		wReg_OnTimer(hWnd);
		return 0;
	case WM_SIZE:
		wReg_UpdateScrollBar(hWnd);
		return 0;
	case WM_VSCROLL:
		wReg_OnScroll(hWnd, (int)LOWORD(wParam));
		return 0;
	case WM_MOUSEWHEEL:
		wReg_OnMouseWheel(hWnd, GET_WHEEL_DELTA_WPARAM(wParam));
		return 0;
	case WM_LBUTTONDOWN:
		wReg_OnLButtonDown(hWnd, (short)LOWORD(lParam), (short)HIWORD(lParam));
		return 0;
	case WM_LBUTTONDBLCLK:
		wReg_OnLButtonDblClk(hWnd, (short)LOWORD(lParam), (short)HIWORD(lParam));
		return 0;
	case WM_RBUTTONDOWN:
		wReg_OnContextMenu(hWnd, (short)LOWORD(lParam), (short)HIWORD(lParam));
		return 0;
	case WM_CHAR:
		wReg_OnChar(hWnd, (TCHAR)wParam);
		return 0;
	case WM_KEYDOWN:
		wReg_OnKeyDown(hWnd, (UINT)wParam);
		return 0;
	case WM_DESTROY:
		wReg_OnDestroy(hWnd);
		return 0;
	}

	return DefWindowProc(hWnd, uMsg, wParam, lParam);
}
