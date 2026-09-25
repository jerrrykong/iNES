// =====================================================================
// iNES macOS 前端 —— 寄存器查看器
//
// 布局与交互见 docs/register-view-plan.md:
//   * 一行一个寄存器: 名称 / 地址 / 值 / 位格 / 说明; 四周留空半个字符;
//   * 位格每行只画 8 位: 16 位寄存器拆"低字节行 L / 高字节行 H"两行, 值列仍是 16 位整值;
//   * 选中与可写是两个正交维度: 可写选中态 = 反白, 只读 = 灰底(选中只加焦点框);
//   * 位格保留位名字母, 字色/字重表达位值: 1 = 常色粗体, 0 = 浅灰;
//   * 值列与位格都可选中, 单击只选中, 输入才改写(位格只接受 0/1);
//   * 值变化时红色高亮 0.5s。
//
// 等宽网格 + 翻转坐标, 与 iNESMemoryView 保持一致的排版与滚动手感。
// =====================================================================

#import "iNESRegisterView.h"
#import "iNESi18n.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#include "../comm/i18n.h"


// ---------------------------------------------------------------------
// 布局常量(单位: 字符列)
//
// 位格只画 8 列: 16 位寄存器拆成两个显示行(低字节 / 高字节), 避免横向过宽。
// ---------------------------------------------------------------------
#define IDBG_RV_NAME_COLS     10
#define IDBG_RV_ADDR_COLS      7
#define IDBG_RV_VALUE_COLS     6
#define IDBG_RV_TAG_COLS       1                                     // "L" / "H" 高低字节标记
#define IDBG_RV_BIT_CELL       3                                     // "[x]" 占 3 列
#define IDBG_RV_BIT_COLS      (8 * IDBG_RV_BIT_CELL)                 // 24(每行 8 位)
#define IDBG_RV_NOTE_COLS     26

#define IDBG_RV_LINE_COLS     (IDBG_RV_NAME_COLS + IDBG_RV_ADDR_COLS + IDBG_RV_VALUE_COLS + \
							   IDBG_RV_TAG_COLS + IDBG_RV_BIT_COLS + IDBG_RV_NOTE_COLS)

#define IDBG_RV_COL_ADDR      (IDBG_RV_NAME_COLS)                                    // 10
#define IDBG_RV_COL_VALUE     (IDBG_RV_COL_ADDR   + IDBG_RV_ADDR_COLS)               // 17
#define IDBG_RV_COL_TAG       (IDBG_RV_COL_VALUE  + IDBG_RV_VALUE_COLS)              // 23
#define IDBG_RV_COL_BITS      (IDBG_RV_COL_TAG    + IDBG_RV_TAG_COLS)                // 24
#define IDBG_RV_COL_NOTE      (IDBG_RV_COL_BITS   + IDBG_RV_BIT_COLS)                // 48

#define IDBG_RV_WIN_HEIGHT    560
#define IDBG_RV_HEAD_LINES    1

// 四周留空(单位: 字符, 半个字符)
#define IDBG_RV_PAD_CHARS     0.5

// 显示行上限: 4 个组标题 + 每个寄存器最多 2 行(16 位 = 低字节行 + 高字节行)
#define IDBG_RV_MAX_LINES     128

// 值列显示格式
#define IDBG_RV_FMT_HEX       0
#define IDBG_RV_FMT_DEC       1
#define IDBG_RV_FMT_TEXT      2

// 变化高亮保持时长(秒)
#define IDBG_RV_HILITE_TIME   0.5

// Tips 显示时长(秒)
#define IDBG_RV_TIPS_TIME     0.8

// 只读单元格的灰底(与"未载入 ROM"的中灰区分)
#define IDBG_RV_GRAY_FILL     (240.0 / 255.0)
#define IDBG_RV_OFF_GRAY      (128.0 / 255.0)

// 光标列
#define IDBG_RV_CUR_VALUE     0
#define IDBG_RV_CUR_BIT       1


// ---------------------------------------------------------------------
// 寄存器定义表(下标即 IDBG_REG_* 的顺序)
// ---------------------------------------------------------------------
typedef struct _ines_rv_def_
{
	ines_int_t     reg_id;
	const char*    name;
	const char*    addr;      // "$2000" / "-"
	ines_int_t     fmt;       // IDBG_RV_FMT_*
	ines_int_t     width;     // 8 / 16 / 64 / 0(纯文本行)
	unsigned int   wmask;     // 可写位掩码(0 = 整行只读)
	const char*    bits;      // 位缩写, MSB -> LSB
	const char*    note_zh;   // 说明(简体中文)
	const char*    note_zh_tw;// 说明(繁体中文)
	const char*    note_en;   // 说明(英文, 术语类字面量, 不进语言文件)
} ines_rv_def_t;


static const ines_rv_def_t  s_defs[IDBG_REG_COUNT] =
{
	/* ---------------- CPU ---------------- */
	{ IDBG_REG_A,          "A",         "-",      IDBG_RV_FMT_HEX,  8, 0x00FF, "76543210",         "累加器", "累加器", "Accumulator" },
	{ IDBG_REG_X,          "X",         "-",      IDBG_RV_FMT_HEX,  8, 0x00FF, "76543210",         "变址寄存器 X", "變址暫存器 X", "Index register X" },
	{ IDBG_REG_Y,          "Y",         "-",      IDBG_RV_FMT_HEX,  8, 0x00FF, "76543210",         "变址寄存器 Y", "變址暫存器 Y", "Index register Y" },
	{ IDBG_REG_P,          "P",         "-",      IDBG_RV_FMT_HEX,  8, 0x00FF, "NVRBDIZC",         "状态 N V R B D I Z C", "狀態 N V R B D I Z C", "Status N V R B D I Z C" },
	{ IDBG_REG_SP,         "SP",        "-",      IDBG_RV_FMT_HEX,  8, 0x00FF, "76543210",         "栈指针(页 1)", "堆疊指標（頁 1）", "Stack pointer (page 1)" },
	{ IDBG_REG_PC,         "PC",        "-",      IDBG_RV_FMT_HEX, 16, 0xFFFF, "FEDCBA9876543210", "程序计数器", "程式計數器", "Program counter" },
	{ IDBG_REG_IRQ_PEND,   "IRQ.PEND",  "-",      IDBG_RV_FMT_HEX,  8, 0x0007, "     AMN",         "NMI/MMC/APU 挂起", "NMI/MMC/APU 擱置", "NMI/MMC/APU pending" },
	{ IDBG_REG_CYCLES,     "CYCLES",    "-",      IDBG_RV_FMT_DEC, 64, 0x0000, "",                 "累计周期(只读)", "累計週期（唯讀）", "Total cycles (read-only)" },

	/* ---------------- PPU ---------------- */
	{ IDBG_REG_PPUCTRL,    "PPUCTRL",   "$2000",  IDBG_RV_FMT_HEX,  8, 0x00FF, "NMSBsInn",         "NMI/图样/尺寸/增量/NT", "NMI/圖樣/尺寸/增量/NT", "NMI/Pattern/Size/Incr/NT" },
	{ IDBG_REG_PPUMASK,    "PPUMASK",   "$2001",  IDBG_RV_FMT_HEX,  8, 0x00FF, "BGRsbmMg",         "色彩/BG/SPR 显示控制", "色彩/BG/SPR 顯示控制", "Color/BG/SPR show control" },
	{ IDBG_REG_PPUSTATUS,  "PPUSTATUS", "$2002",  IDBG_RV_FMT_HEX,  8, 0x0000, "VSO-----",         "VBlank/Spr0/溢出(只读)", "VBlank/Spr0/溢位（唯讀）", "VBlank/Spr0/Overflow (RO)" },
	{ IDBG_REG_OAMADDR,    "OAMADDR",   "$2003",  IDBG_RV_FMT_HEX,  8, 0x00FF, "76543210",         "OAM 地址", "OAM 位址", "OAM address" },
	{ IDBG_REG_OAMDATA,    "OAMDATA",   "$2004",  IDBG_RV_FMT_HEX,  8, 0x00FF, "76543210",         "OAM 数据(写后地址+1)", "OAM 資料（寫入後位址+1）", "OAM data (addr+1 on write)" },
	{ IDBG_REG_PPUSCROLL,  "PPUSCROLL", "$2005",  IDBG_RV_FMT_TEXT, 0, 0x0000, "",                 "X=--- FX=- Y=--- FY=-", "X=--- FX=- Y=--- FY=-", "X=--- FX=- Y=--- FY=-" },
	{ IDBG_REG_PPU_T,      "PPUADDR.T", "$2006T", IDBG_RV_FMT_HEX, 16, 0x7FFF, "0YYYNNYYYYYXXXXX", "内部 T(直改字段)", "內部 T（直接修改欄位）", "Internal T (direct edit)" },
	{ IDBG_REG_PPU_V,      "PPUADDR.V", "$2006V", IDBG_RV_FMT_HEX, 16, 0x7FFF, "0YYYNNYYYYYXXXXX", "当前 VRAM 地址 V", "目前 VRAM 位址 V", "Current VRAM address V" },
	{ IDBG_REG_PPUDATA,    "PPUDATA",   "$2007",  IDBG_RV_FMT_HEX,  8, 0x00FF, "76543210",         "写: VRAM; 读: 缓冲", "寫：VRAM；讀：緩衝", "Write: VRAM; Read: buffer" },
	{ IDBG_REG_SCANLINE,   "SCANLINE",  "-",      IDBG_RV_FMT_DEC, 16, 0x0000, "FEDCBA9876543210", "当前扫描行(只读)", "目前掃描行（唯讀）", "Current scanline (RO)" },
	{ IDBG_REG_VBLANK,     "VBLANK",    "-",      IDBG_RV_FMT_HEX,  8, 0x0000, "-------V",         "VBlank 标志(只读)", "VBlank 旗標（唯讀）", "VBlank flag (read-only)" },
	{ IDBG_REG_TOGGLE,     "TOGGLE",    "-",      IDBG_RV_FMT_HEX,  8, 0x0000, "-------T",         "0=首字节 1=次字节", "0=首位元組 1=次位元組", "0=first byte 1=second" },

	/* ---------------- APU ---------------- */
	{ IDBG_REG_P1VOL,      "P1VOL",     "$4000",  IDBG_RV_FMT_HEX,  8, 0x00FF, "ddLCvvvv",         "音量/包络/占空比", "音量/包絡/占空比", "Volume/Envelope/Duty" },
	{ IDBG_REG_P1SWP,      "P1SWP",     "$4001",  IDBG_RV_FMT_HEX,  8, 0x00FF, "EpppNsss",         "扫频", "掃頻", "Sweep" },
	{ IDBG_REG_P1TLO,      "P1TLO",     "$4002",  IDBG_RV_FMT_HEX,  8, 0x00FF, "tttttttt",         "定时器低 8 位", "計時器低 8 位元", "Timer low 8 bits" },
	{ IDBG_REG_P1THI,      "P1THI",     "$4003",  IDBG_RV_FMT_HEX,  8, 0x00FF, "lllllttt",         "长度/定时器高 3 位", "長度/計時器高 3 位元", "Length/Timer high 3" },
	{ IDBG_REG_P2VOL,      "P2VOL",     "$4004",  IDBG_RV_FMT_HEX,  8, 0x00FF, "ddLCvvvv",         "音量/包络/占空比", "音量/包絡/占空比", "Volume/Envelope/Duty" },
	{ IDBG_REG_P2SWP,      "P2SWP",     "$4005",  IDBG_RV_FMT_HEX,  8, 0x00FF, "EpppNsss",         "扫频", "掃頻", "Sweep" },
	{ IDBG_REG_P2TLO,      "P2TLO",     "$4006",  IDBG_RV_FMT_HEX,  8, 0x00FF, "tttttttt",         "定时器低 8 位", "計時器低 8 位元", "Timer low 8 bits" },
	{ IDBG_REG_P2THI,      "P2THI",     "$4007",  IDBG_RV_FMT_HEX,  8, 0x00FF, "lllllttt",         "长度/定时器高 3 位", "長度/計時器高 3 位元", "Length/Timer high 3" },
	{ IDBG_REG_TRLIN,      "TRLIN",     "$4008",  IDBG_RV_FMT_HEX,  8, 0x00FF, "Crrrrrrr",         "线性计数器", "線性計數器", "Linear counter" },
	{ IDBG_REG_TR_UNUSED,  "TR.UNUSED", "$4009",  IDBG_RV_FMT_HEX,  8, 0x0000, "--------",         "保留(未使用)", "保留（未使用）", "Reserved (unused)" },
	{ IDBG_REG_TRTLO,      "TRTLO",     "$400A",  IDBG_RV_FMT_HEX,  8, 0x00FF, "tttttttt",         "定时器低 8 位", "計時器低 8 位元", "Timer low 8 bits" },
	{ IDBG_REG_TRTHI,      "TRTHI",     "$400B",  IDBG_RV_FMT_HEX,  8, 0x00FF, "lllllttt",         "长度/定时器高 3 位", "長度/計時器高 3 位元", "Length/Timer high 3" },
	{ IDBG_REG_NSVOL,      "NSVOL",     "$400C",  IDBG_RV_FMT_HEX,  8, 0x003F, "--LCvvvv",         "音量/包络(位 7-6 未用)", "音量/包絡（位元 7-6 未用）", "Volume/Envelope (b7-6 NC)" },
	{ IDBG_REG_NS_UNUSED,  "NS.UNUSED", "$400D",  IDBG_RV_FMT_HEX,  8, 0x0000, "--------",         "保留(未使用)", "保留（未使用）", "Reserved (unused)" },
	{ IDBG_REG_NSFRQ,      "NSFRQ",     "$400E",  IDBG_RV_FMT_HEX,  8, 0x00FF, "Mppppppp",         "模式/周期", "模式/週期", "Mode/Period" },
	{ IDBG_REG_NSLEN,      "NSLEN",     "$400F",  IDBG_RV_FMT_HEX,  8, 0x00F8, "lllll---",         "长度(位 2-0 未用)", "長度（位元 2-0 未用）", "Length (b2-0 unused)" },
	{ IDBG_REG_DMFREQ,     "DMFREQ",    "$4010",  IDBG_RV_FMT_HEX,  8, 0x00FF, "ILrrrrrr",         "IRQ/循环/速率", "IRQ/循環/速率", "IRQ/Loop/Rate" },
	{ IDBG_REG_DMDAC,      "DMDAC",     "$4011",  IDBG_RV_FMT_HEX,  8, 0x007F, "-ddddddd",         "DAC 直写", "DAC 直接寫入", "DAC direct write" },
	{ IDBG_REG_DMADDR,     "DMADDR",    "$4012",  IDBG_RV_FMT_HEX,  8, 0x00FF, "aaaaaaaa",         "采样起始地址", "取樣起始位址", "Sample start address" },
	{ IDBG_REG_DMLEN,      "DMLEN",     "$4013",  IDBG_RV_FMT_HEX,  8, 0x00FF, "llllllll",         "采样长度", "取樣長度", "Sample length" },
	{ IDBG_REG_APUCTRL,    "APUCTRL",   "$4015",  IDBG_RV_FMT_HEX,  8, 0x001F, "---DNT21",         "声道使能(写)", "聲道啟用（寫入）", "Channel enable (write)" },
	{ IDBG_REG_APUSTAT,    "APUSTAT",   "$4015R", IDBG_RV_FMT_HEX,  8, 0x0000, "FD-dNT21",         "状态(只读, 不清 IRQ)", "狀態（唯讀，不清除 IRQ）", "Status (RO, no IRQ ack)" },
	{ IDBG_REG_FRAMECTR,   "FRAMECTR",  "$4017",  IDBG_RV_FMT_HEX,  8, 0x00C0, "MI------",         "帧计数器模式", "幀計數器模式", "Frame counter mode" },

	/* ---------------- I/O ---------------- */
	{ IDBG_REG_OAMDMA,     "OAMDMA",    "$4014",  IDBG_RV_FMT_HEX,  8, 0x00FF, "hhhhhhhh",         "写即触发 256B DMA", "寫入即觸發 256B DMA", "Write triggers 256B DMA" },
	{ IDBG_REG_JOYPAD1,    "JOYPAD1",   "$4016",  IDBG_RV_FMT_HEX,  8, 0x0001, "-------S",         "手柄 strobe(写)", "手把 strobe（寫入）", "Joypad strobe (write)" },
	{ IDBG_REG_JOYPAD2,    "JOYPAD2",   "$4017R", IDBG_RV_FMT_HEX,  8, 0x0000, "RLDUSsBA",         "手柄 2 按键(读)", "手把 2 按鍵（讀取）", "Joypad 2 buttons (read)" },
};


// 分组(下标与 s_defs 的区间一一对应)
#define IDBG_RV_GROUP_COUNT   4

static const struct _ines_rv_group_
{
	const char*   title;
	ines_int_t    start;
	ines_int_t    count;
} s_groups[IDBG_RV_GROUP_COUNT] =
{
	{ "CPU",  0,  8 },
	{ "PPU",  8, 12 },
	{ "APU", 20, 23 },
	{ "I/O", 43,  3 },
};


// ---------------------------------------------------------------------
// 说明文本缓存
// 与 win32/wRegister.c 的 wReg_InitTexts() 同构: 定义表是 UTF-8 常量,
// 建视图与切换语言时各转换一次, 绘制热路径(50ms tick)不再做编码转换。
// 说明属"术语类描述", 按方案 §10.1 不进语言文件: 简体中文用 note_zh,
// 繁体中文(TW/HK/Hant)用 note_zh_tw, 其余语言一律用 note_en(英文)。
// ---------------------------------------------------------------------
static NSString*  s_noteText[IDBG_REG_COUNT] = { NULL };

static void idbg_rv_init_texts(void)
{
	ines_int_t   i;
	const char*  lang = ines_i18n_language();
	ines_int_t   zh   = ((lang != NULL) && (lang[0] == 'z') && (lang[1] == 'h'));
	ines_int_t   tw   = ((zh != 0) &&
						 ((strstr(lang, "TW") != NULL) || (strstr(lang, "HK") != NULL) ||
						  (strstr(lang, "Hant") != NULL)));

	for (i = 0; i < IDBG_REG_COUNT; i++)
	{
		const char*  s;

		if (tw != 0)
			s = s_defs[i].note_zh_tw;
		else if (zh != 0)
			s = s_defs[i].note_zh;
		else
			s = s_defs[i].note_en;

		if ((s == NULL) || (s[0] == '\0'))
			s = s_defs[i].note_en;                  // 兜底: 缺中文说明时用英文

		s_noteText[i] = ((s != NULL) && (s[0] != '\0')) ? [NSString stringWithUTF8String:s] : @"";
	}
}


// ---------------------------------------------------------------------
// 显示行: 8 位寄存器占 1 行, 16 位寄存器占 2 行(低字节 + 高字节)
// ---------------------------------------------------------------------
typedef struct _ines_rv_row_
{
	ines_int_t    group;    // 所属组
	ines_int_t    reg;      // s_defs 下标, -1 = 组标题行
	ines_int_t    sub;      // 0 = 低字节行(首行), 1 = 高字节行(仅 16 位)
} ines_rv_row_t;


#pragma mark - 取值与文本

// 从快照取某个寄存器的当前值
static ines_int64_t idbg_rv_value(const ines_dbg_regs_t* pRegs, ines_int_t regIdx)
{
	switch (s_defs[regIdx].reg_id)
	{
	/* CPU */
	case IDBG_REG_A:          return pRegs->a;
	case IDBG_REG_X:          return pRegs->x;
	case IDBG_REG_Y:          return pRegs->y;
	case IDBG_REG_P:          return pRegs->p;
	case IDBG_REG_SP:         return pRegs->sp;
	case IDBG_REG_PC:         return pRegs->pc;
	case IDBG_REG_IRQ_PEND:   return pRegs->int_pending;
	case IDBG_REG_CYCLES:     return (ines_int64_t)pRegs->total_cycles;

	/* PPU */
	case IDBG_REG_PPUCTRL:    return pRegs->ctrl1;
	case IDBG_REG_PPUMASK:    return pRegs->ctrl2;
	case IDBG_REG_PPUSTATUS:  return pRegs->status;
	case IDBG_REG_OAMADDR:    return pRegs->oam_addr;
	case IDBG_REG_OAMDATA:    return pRegs->oam_data;
	case IDBG_REG_PPUSCROLL:  return 0;                      // 文本行, 值在说明列
	case IDBG_REG_PPU_T:      return pRegs->t;
	case IDBG_REG_PPU_V:      return pRegs->v;
	case IDBG_REG_PPUDATA:    return pRegs->read_2007_buffer;
	case IDBG_REG_SCANLINE:   return pRegs->scanline;
	case IDBG_REG_VBLANK:     return pRegs->in_vblank;
	case IDBG_REG_TOGGLE:     return pRegs->toggle;

	/* APU */
	case IDBG_REG_P1VOL:
	case IDBG_REG_P1SWP:
	case IDBG_REG_P1TLO:
	case IDBG_REG_P1THI:      return pRegs->pulse1[s_defs[regIdx].reg_id - IDBG_REG_P1VOL];
	case IDBG_REG_P2VOL:
	case IDBG_REG_P2SWP:
	case IDBG_REG_P2TLO:
	case IDBG_REG_P2THI:      return pRegs->pulse2[s_defs[regIdx].reg_id - IDBG_REG_P2VOL];
	case IDBG_REG_TRLIN:
	case IDBG_REG_TR_UNUSED:
	case IDBG_REG_TRTLO:
	case IDBG_REG_TRTHI:      return pRegs->triangle[s_defs[regIdx].reg_id - IDBG_REG_TRLIN];
	case IDBG_REG_NSVOL:
	case IDBG_REG_NS_UNUSED:
	case IDBG_REG_NSFRQ:
	case IDBG_REG_NSLEN:      return pRegs->noise[s_defs[regIdx].reg_id - IDBG_REG_NSVOL];
	case IDBG_REG_DMFREQ:
	case IDBG_REG_DMDAC:
	case IDBG_REG_DMADDR:
	case IDBG_REG_DMLEN:      return pRegs->dmc[s_defs[regIdx].reg_id - IDBG_REG_DMFREQ];
	case IDBG_REG_APUCTRL:    return pRegs->ctrl_4015;
	case IDBG_REG_APUSTAT:    return pRegs->apu_status;
	case IDBG_REG_FRAMECTR:   return pRegs->frame_4017;

	/* I/O */
	case IDBG_REG_OAMDMA:     return pRegs->dma_high;
	case IDBG_REG_JOYPAD1:    return pRegs->joy_strobe;
	case IDBG_REG_JOYPAD2:    return pRegs->joy2_bits;

	default:                  return 0;
	}
}


// $2005 的派生显示: 由 T 与 fine_x 反算卷轴值
static NSString* idbg_rv_scroll_text(const ines_dbg_regs_t* pRegs)
{
	ines_int_t  x  = (ines_int_t)(((pRegs->t & 0x001F) << 3) | (pRegs->fine_x & 0x07));
	ines_int_t  y  = (ines_int_t)(((pRegs->t >> 5) & 0x001F) << 3) | ((pRegs->t >> 12) & 0x07);

	return [NSString stringWithFormat:@"X=%03d FX=%d Y=%03d FY=%d",
			(int)x, (int)(pRegs->fine_x & 0x07), (int)y, (int)((pRegs->t >> 12) & 0x07)];
}


// 值列文本(编辑中显示编辑缓冲)
static NSString* idbg_rv_value_text(const ines_rv_def_t* pDef, ines_int64_t val)
{
	if (pDef->fmt == IDBG_RV_FMT_TEXT)
		return @"  -";

	if (pDef->fmt == IDBG_RV_FMT_DEC)
		return [NSString stringWithFormat:@"%lld", (long long)val];

	if (pDef->width == 16)
		return [NSString stringWithFormat:@"%04X", (unsigned int)(val & 0xFFFF)];

	return [NSString stringWithFormat:@"%02X", (unsigned int)(val & 0xFF)];
}


// 位缩写字母: bit 0 = 最低位
static char idbg_rv_bit_letter(const ines_rv_def_t* pDef, ines_int_t bit)
{
	size_t  len;

	if ((pDef->width <= 0) || (pDef->width > 16))
		return ' ';

	len = (pDef->bits != NULL) ? strlen(pDef->bits) : 0;
	if ((size_t)(pDef->width - 1 - bit) >= len)
		return '?';

	return pDef->bits[pDef->width - 1 - bit];
}


// 位格在"字节行"内的位置(右对齐: 最低位恒在最右), 每行只画 8 位
static inline ines_int_t idbg_rv_bit_cell(ines_int_t bit)
{
	return 7 - (bit & 7);
}

// 该寄存器占几个显示行(16 位 = 低字节行 + 高字节行)
static inline ines_int_t idbg_rv_row_span(ines_int_t width)
{
	return (width == 16) ? 2 : 1;
}


#pragma mark - 字体与颜色

static NSFont* idbg_rv_font(void)
{
	NSFont*  font = [NSFont fontWithName:@"Courier New" size:12.0];

	if (font == nil)
		font = [NSFont userFixedPitchFontOfSize:12.0];

	return font;
}


// 位值 1 用粗体: 与"0 = 浅灰"形成双重冗余, 不依赖单一颜色(深色模式/色弱同样可读)。
// Courier New Bold 与常规体等宽, 不破坏字符网格。
static NSFont* idbg_rv_bold_font(void)
{
	NSFont*  font = [NSFont fontWithName:@"Courier New Bold" size:12.0];

	if (font == nil)
		font = [[NSFontManager sharedFontManager] convertFont:idbg_rv_font()
												  toHaveTrait:NSBoldFontMask];

	return (font != nil) ? font : idbg_rv_font();
}


#pragma mark - iNESRegisterView

@interface iNESRegisterView ()
{
	ines_rv_row_t  _lineMap[IDBG_RV_MAX_LINES];           // 显示行 -> (组/寄存器/子行)
	ines_int_t     _lineCount;
	ines_int_t     _startLine;                            // 垂直滚动起点(显示行)
	ines_int_t     _curLine;                              // 当前寄存器的首行(显示行)
	ines_int_t     _cursorCol;                            // IDBG_RV_CUR_VALUE / IDBG_RV_CUR_BIT
	ines_int_t     _bitIndex;                             // 位格光标(0 = 最低位)
	BOOL           _editActive;                           // 值列编辑中
	ines_int64_t   _editValue;
	ines_int_t     _editNibble;                           // 0 = 最高 nibble
	BOOL           _folded[IDBG_RV_GROUP_COUNT];
	CGFloat        _charW;
	ines_int_t     _charH;
	ines_int_t     _rows;                                 // 可见行数
	ines_int_t     _totalRows;
	CGFloat        _wheelY;
	NSFont*        _font;
	NSDictionary*  _attrs;
	NSDictionary*  _attrsSel;
	NSDictionary*  _attrsGray;
	NSDictionary*  _attrsGrayDim;                         // 只读位且位值 0(更淡)
	NSDictionary*  _attrsNote;
	NSDictionary*  _attrsRed;
	NSDictionary*  _attrsBit1;                            // 位值 1: 常色 + 粗体
	NSDictionary*  _attrsBit0;                            // 位值 0: 浅灰 + 常规
	NSScroller*    _vScroller;
	ines_int64_t   _lastValue[IDBG_REG_COUNT];            // 上一帧值(变化高亮)
	ines_int64_t   _prevValue[IDBG_REG_COUNT];            // 变化前的值(算位级 diff)
	CFTimeInterval _lastChange[IDBG_REG_COUNT];
	BOOL           _lastValid[IDBG_REG_COUNT];
	ines_int64_t   _drawnValue[IDBG_REG_COUNT];           // 已标脏的绘制目标值(行级脏判定)
	BOOL           _drawnValid[IDBG_REG_COUNT];
	BOOL           _dirtyRow[IDBG_REG_COUNT];             // 待重绘行(累积, 由 drawRect 消费)
	BOOL           _drawRow[IDBG_REG_COUNT];              // drawRect 本次实际消费的行集
	ines_int_t     _drawnScrollKey;                       // $2005 说明列派生键(t<<3|fine_x)
	BOOL           _fullPending;                          // 待整幅重绘
	BOOL           _lastHadContent;                       // 有无内容(ROM 载入)的切换检测
	NSString*      _tips;
	CFTimeInterval _tipsExpire;
}

- (void)rebuildMetrics;
- (void)layoutScrollers;
- (void)updateScrollers;
- (CGFloat)padX;
- (CGFloat)padY;
- (NSRect)contentRect;
- (void)rebuildLineMap;
- (ines_int_t)totalLines;
- (BOOL)rowAt:(ines_int_t)line group:(ines_int_t*)pGroup reg:(ines_int_t*)pReg sub:(ines_int_t*)pSub;
- (ines_int_t)curReg;
- (void)selectLine:(ines_int_t)line column:(ines_int_t)col bit:(ines_int_t)bit;
- (void)moveRow:(ines_int_t)delta;
- (void)moveToWritableRow:(ines_int_t)delta;
- (void)ensureCursorVisible;
- (void)cancelEdit;
- (void)commitEdit;
- (void)commitValue:(ines_int64_t)val reg:(ines_int_t)regIdx;
- (void)toggleBit:(ines_int_t)bit value:(ines_int_t)bitVal;
- (void)showTips:(NSString*)text;
- (void)trackChanges;
- (void)refreshForTick;
- (void)setNeedsDisplayAll;
- (void)scrollBy:(ines_int_t)code;

@end


@implementation iNESRegisterView

- (instancetype)initWithFrame:(NSRect)frameRect
{
	NSSize  content = [iNESRegisterView suggestedContentSize];

	self = [super initWithFrame:NSMakeRect(0, 0, content.width, content.height)];
	if (self == nil)
		return nil;

	idbg_rv_init_texts();

	_startLine = 0;
	_curLine   = 1;                 // 默认选中 CPU 组的第一个寄存器(A)
	_cursorCol = IDBG_RV_CUR_VALUE;
	_bitIndex  = 0;

	_font = idbg_rv_font();

	_attrs      = @{ NSFontAttributeName: _font,
					 NSForegroundColorAttributeName: [NSColor textColor] };
	_attrsSel   = @{ NSFontAttributeName: _font,
					 NSForegroundColorAttributeName: [NSColor selectedTextColor] };
	_attrsGray  = @{ NSFontAttributeName: _font,
					 NSForegroundColorAttributeName: [NSColor disabledControlTextColor] };
	_attrsGrayDim = @{ NSFontAttributeName: _font,
					   NSForegroundColorAttributeName:
						   [[NSColor disabledControlTextColor] colorWithAlphaComponent:0.5] };
	_attrsNote  = @{ NSFontAttributeName: _font,
					 NSForegroundColorAttributeName: [NSColor secondaryLabelColor] };
	_attrsRed   = @{ NSFontAttributeName: _font,
					 NSForegroundColorAttributeName: [NSColor systemRedColor] };
	// 位格: 1 = 常色粗体, 0 = 浅灰常规(双重冗余, 不依赖单一颜色)
	_attrsBit1  = @{ NSFontAttributeName: idbg_rv_bold_font(),
					 NSForegroundColorAttributeName: [NSColor textColor] };
	_attrsBit0  = @{ NSFontAttributeName: _font,
					 NSForegroundColorAttributeName:
						 [[NSColor labelColor] colorWithAlphaComponent:0.45] };

	[self rebuildMetrics];
	[self rebuildLineMap];

	_vScroller = [[NSScroller alloc] initWithFrame:NSMakeRect(0, 0, 15, 100)];
	_vScroller.scrollerStyle = NSScrollerStyleLegacy;
	_vScroller.controlSize   = NSControlSizeSmall;
	_vScroller.target        = self;
	_vScroller.action        = @selector(scrollerAction:);
	[self addSubview:_vScroller];

	[self layoutScrollers];
	[self updateScrollers];

	// 切换界面语言后刷新说明列(名称/地址/值/位格是术语, 不翻译)
	[[NSNotificationCenter defaultCenter] addObserver:self
											 selector:@selector(ines_languageDidChange:)
												 name:INESLanguageDidChangeNotification
											   object:nil];

	return self;
}

- (instancetype)init
{
	return [self initWithFrame:NSZeroRect];
}

- (void)dealloc
{
	[[NSNotificationCenter defaultCenter] removeObserver:self];
}

- (void)ines_languageDidChange:(NSNotification*)note
{
	idbg_rv_init_texts();
	[self setNeedsDisplayAll];
}

+ (NSSize)suggestedContentSize
{
	NSSize   sz;
	CGFloat  charW;
	CGFloat  charH;
	CGFloat  scrollerW;

	sz        = [@"X" sizeWithAttributes:@{ NSFontAttributeName: idbg_rv_font() }];
	charW     = (sz.width  < 1.0) ? 1.0 : sz.width;
	charH     = (sz.height < 1.0) ? 1.0 : ceil(sz.height);
	scrollerW = [NSScroller scrollerWidthForControlSize:NSControlSizeSmall
										  scrollerStyle:NSScrollerStyleLegacy];

	// 宽度: 全部列 + 左右留空(各半个字符); 高度: 内容 + 上下留空
	return NSMakeSize(ceil((CGFloat)(IDBG_RV_LINE_COLS + 1) * charW + scrollerW),
					  ceil((CGFloat)IDBG_RV_WIN_HEIGHT + charH));
}

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

- (void)rebuildMetrics
{
	NSSize  sz = [@"X" sizeWithAttributes:@{ NSFontAttributeName: _font }];

	// 与 iNESMemoryView 一致: 宽度保留小数(正文按字体 advance 推进)
	_charW = (sz.width < 1.0) ? 1.0 : sz.width;
	_charH = (ines_int_t)ceil(sz.height);
	if (_charH < 1) _charH = 1;
}

// 四周留空: 半个字符(横向按字符宽, 纵向按行高)
- (CGFloat)padX
{
	return _charW * (CGFloat)IDBG_RV_PAD_CHARS;
}

- (CGFloat)padY
{
	return (CGFloat)_charH * (CGFloat)IDBG_RV_PAD_CHARS;
}

// 内容绘制区(已扣掉滚动条与四周留空)
- (NSRect)contentRect
{
	NSSize   sz = self.bounds.size;
	CGFloat  w  = [NSScroller scrollerWidthForControlSize:NSControlSizeSmall
											scrollerStyle:NSScrollerStyleLegacy];
	CGFloat  px = [self padX];
	CGFloat  py = [self padY];

	return NSMakeRect(px, py,
					  MAX(10.0, sz.width - w - px * 2.0),
					  MAX(10.0, sz.height - py * 2.0));
}

- (void)layoutScrollers
{
	NSSize   sz = self.bounds.size;
	CGFloat  w  = [NSScroller scrollerWidthForControlSize:NSControlSizeSmall
											scrollerStyle:NSScrollerStyleLegacy];

	_vScroller.frame = NSMakeRect(sz.width - w, 0, w, MAX(0.0, sz.height));
}

- (void)resizeSubviewsWithOldSize:(NSSize)oldSize
{
	[super resizeSubviewsWithOldSize:oldSize];

	[self layoutScrollers];
	[self updateScrollers];
}

- (void)viewDidMoveToWindow
{
	[super viewDidMoveToWindow];

	[self layoutScrollers];
	[self updateScrollers];
}

- (void)updateScrollers
{
	NSRect       content = [self contentRect];
	ines_int_t   pageRow;
	ines_int_t   maxRow;

	pageRow = (ines_int_t)(content.size.height / _charH) - IDBG_RV_HEAD_LINES;
	if (pageRow < 1) pageRow = 1;

	_rows      = pageRow;
	_totalRows = [self totalLines];

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

- (void)scrollerAction:(NSScroller*)sender
{
	NSScrollerPart  part = sender.hitPart;

	if (part == NSScrollerNoPart)
		[self scrollBy:NSScrollerKnob];
	else
		[self scrollBy:part];
}

- (void)scrollBy:(ines_int_t)code
{
	ines_int_t  pos    = _startLine;
	ines_int_t  maxPos = MAX(0, _totalRows - _rows);

	switch (code)
	{
	case NSScrollerDecrementLine: pos -= 1;    break;
	case NSScrollerIncrementLine: pos += 1;    break;
	case NSScrollerDecrementPage: pos -= _rows; break;
	case NSScrollerIncrementPage: pos += _rows; break;
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

	[self updateScrollers];
	[self setNeedsDisplayAll];
}

#pragma mark 行模型

// 按分组/折叠状态重建显示行表: 组标题 1 行; 8 位寄存器 1 行; 16 位寄存器 2 行(低/高字节)
- (void)rebuildLineMap
{
	ines_int_t  n = 0;
	ines_int_t  g;
	ines_int_t  i;

	for (g = 0; g < IDBG_RV_GROUP_COUNT; g++)
	{
		_lineMap[n].group = g;
		_lineMap[n].reg   = -1;
		_lineMap[n].sub   = 0;
		n++;

		if (_folded[g])
			continue;

		for (i = 0; i < s_groups[g].count; i++)
		{
			ines_int_t  reg = s_groups[g].start + i;

			_lineMap[n].group = g;
			_lineMap[n].reg   = reg;
			_lineMap[n].sub   = 0;
			n++;

			if (idbg_rv_row_span(s_defs[reg].width) == 2)
			{
				_lineMap[n].group = g;
				_lineMap[n].reg   = reg;
				_lineMap[n].sub   = 1;
				n++;
			}
		}
	}

	_lineCount = (n > IDBG_RV_MAX_LINES) ? IDBG_RV_MAX_LINES : n;
}

- (ines_int_t)totalLines
{
	return _lineCount;
}

// 当前选中的寄存器(s_defs 下标), 无效时返回 -1
- (ines_int_t)curReg
{
	if ((_curLine < 0) || (_curLine >= _lineCount))
		return -1;

	return _lineMap[_curLine].reg;
}

// 返回 YES 表示该行是组标题(*pGroup = 组号); NO 表示寄存器行(*pReg / *pSub)
- (BOOL)rowAt:(ines_int_t)line group:(ines_int_t*)pGroup reg:(ines_int_t*)pReg sub:(ines_int_t*)pSub
{
	if (pGroup != NULL) *pGroup = -1;
	if (pReg   != NULL) *pReg   = -1;
	if (pSub   != NULL) *pSub   = 0;

	if ((line < 0) || (line >= _lineCount))
		return NO;

	if (pGroup != NULL) *pGroup = _lineMap[line].group;
	if (pReg   != NULL) *pReg   = _lineMap[line].reg;
	if (pSub   != NULL) *pSub   = _lineMap[line].sub;

	return (_lineMap[line].reg < 0);
}

#pragma mark 选中与编辑

- (void)showTips:(NSString*)text
{
	_tips        = text;
	_tipsExpire  = CFAbsoluteTimeGetCurrent() + IDBG_RV_TIPS_TIME;
}

- (void)cancelEdit
{
	_editActive = NO;
	_editValue  = 0;
	_editNibble = 0;
}

// line 可以是寄存器的首行或(16 位)高字节行, 内部统一归一到首行
- (void)selectLine:(ines_int_t)line column:(ines_int_t)col bit:(ines_int_t)bit
{
	ines_int_t  group = -1;
	ines_int_t  reg   = -1;
	ines_int_t  sub   = 0;

	if ((line < 0) || (line >= _lineCount))
		return;
	if ([self rowAt:line group:&group reg:&reg sub:&sub])
		return;                                  // 组标题行不接受选中

	// 16 位寄存器的高字节行: 归一到首行
	while ((sub > 0) && (line > 0))
	{
		line--;
		sub = _lineMap[line].sub;
	}

	if (_editActive && (line != _curLine))
		[self cancelEdit];

	_curLine   = line;
	_cursorCol = col;

	if (col == IDBG_RV_CUR_BIT)
	{
		ines_int_t  width = s_defs[reg].width;

		if (width > 16) width = 16;
		if (width <= 0)
		{
			_cursorCol = IDBG_RV_CUR_VALUE;      // 无位格的行退回值列
		}
		else
		{
			if (bit < 0)      bit = 0;
			if (bit >= width) bit = width - 1;
			_bitIndex = bit;
		}
	}

	[self ensureCursorVisible];
	[self setNeedsDisplayAll];
}

- (void)ensureCursorVisible
{
	ines_int_t  reg  = [self curReg];
	ines_int_t  span = (reg >= 0) ? idbg_rv_row_span(s_defs[reg].width) : 1;

	if (_curLine < _startLine)
		_startLine = _curLine;
	else if ((_curLine + span - 1) > (_startLine + _rows - 1))
		_startLine = _curLine + span - 1 - _rows + 1;

	[self updateScrollers];
}

// 在"寄存器"之间移动(跳过组标题, 16 位寄存器的高字节行不停留)
- (void)moveRow:(ines_int_t)delta
{
	ines_int_t  line  = _curLine;

	for (;;)
	{
		line += delta;
		if ((line < 0) || (line >= _lineCount))
			return;
		if (_lineMap[line].reg < 0)
			continue;                            // 跳过组标题
		if (_lineMap[line].sub != 0)
			continue;                            // 停在首行

		[self selectLine:line column:_cursorCol bit:_bitIndex];
		return;
	}
}

- (void)moveToWritableRow:(ines_int_t)delta
{
	ines_int_t  line  = _curLine;

	for (;;)
	{
		line += delta;
		if ((line < 0) || (line >= _lineCount))
			return;
		if (_lineMap[line].reg < 0)
			continue;
		if (_lineMap[line].sub != 0)
			continue;
		if (s_defs[_lineMap[line].reg].wmask == 0)
			continue;                            // 只读行: Tab 跳过

		[self selectLine:line column:_cursorCol bit:_bitIndex];
		return;
	}
}

- (void)commitValue:(ines_int64_t)val reg:(ines_int_t)regIdx
{
	const ines_rv_def_t*  pDef = &s_defs[regIdx];
	ines_int64_t          mask;

	if (pDef->wmask == 0)
	{
		[self showTips:L10N("debug.reg.tip_reg_readonly")];
		[self setNeedsDisplayAll];
		return;
	}

	mask = (pDef->width >= 64) ? (ines_int64_t)~0ULL : ((1ULL << pDef->width) - 1ULL);
	val  = (val & mask) & (ines_int64_t)pDef->wmask;

	if ([_delegate respondsToSelector:@selector(registerView:confirmReg:value:)])
	{
		if (![_delegate registerView:self confirmReg:pDef->reg_id value:(ines_int_t)val])
		{
			[self setNeedsDisplayAll];
			return;
		}
	}

	[_delegate registerView:self writeReg:pDef->reg_id value:(ines_int_t)val];
	[self setNeedsDisplayAll];
}

- (void)commitEdit
{
	ines_int_t  reg = [self curReg];

	if (!_editActive || (reg < 0))
		return;

	[self cancelEdit];
	[self commitValue:_editValue reg:reg];
}

// 位格: 单比特改写并立即提交
- (void)toggleBit:(ines_int_t)bit value:(ines_int_t)bitVal
{
	ines_int_t            reg = [self curReg];
	const ines_rv_def_t*  pDef;
	ines_int64_t          cur;

	if (reg < 0)
		return;

	pDef = &s_defs[reg];

	if (((pDef->wmask >> bit) & 1) == 0)
	{
		[self showTips:L10N("debug.reg.tip_bit_readonly")];
		[self setNeedsDisplayAll];
		return;
	}

	cur = idbg_rv_value(&_snapshot->regs, reg);
	cur = (cur & ~(1ULL << bit)) | ((ines_int64_t)(bitVal ? 1 : 0) << bit);

	[self commitValue:cur reg:reg];
}

#pragma mark 交互

- (void)scrollWheel:(NSEvent*)event
{
	CGFloat     dy    = event.scrollingDeltaY;
	ines_int_t  dLine = 0;

	if (event.hasPreciseScrollingDeltas)
	{
		_wheelY += dy;
		while (_wheelY >= _charH)  { _wheelY -= _charH; dLine--; }
		while (_wheelY <= -_charH) { _wheelY += _charH; dLine++; }
	}
	else
	{
		dLine = -(ines_int_t)dy;
	}

	if (dLine == 0)
		return;

	_startLine += dLine;
	if (_startLine < 0) _startLine = 0;
	if (_startLine > MAX(0, _totalRows - _rows)) _startLine = MAX(0, _totalRows - _rows);

	[self updateScrollers];
	[self setNeedsDisplayAll];
}

- (void)mouseDown:(NSEvent*)event
{
	NSPoint     pt = [self convertPoint:event.locationInWindow fromView:nil];
	ines_int_t  line;
	ines_int_t  col;
	ines_int_t  group = -1;
	ines_int_t  reg   = -1;
	ines_int_t  sub   = 0;

	if (![self hasContent])
		return;

	pt.x -= [self padX];
	pt.y -= [self padY];

	line = (ines_int_t)(pt.y / _charH) - IDBG_RV_HEAD_LINES + _startLine;
	if (line < 0)
		return;

	if ([self rowAt:line group:&group reg:&reg sub:&sub])
	{
		// 组标题: 双击折叠/展开
		if ((group >= 0) && (event.clickCount >= 2))
		{
			_folded[group] = !_folded[group];
			[self rebuildLineMap];
			[self updateScrollers];
			[self setNeedsDisplayAll];
		}
		return;
	}

	if (reg < 0)
		return;

	col = (ines_int_t)(pt.x / _charW);

	if (col < IDBG_RV_COL_VALUE)
		return;                                              // 名称/地址列: 不选中
	else if (col < IDBG_RV_COL_BITS)
		[self selectLine:line column:IDBG_RV_CUR_VALUE bit:0];
	else if (col < IDBG_RV_COL_NOTE)
	{
		ines_int_t  cell = (col - IDBG_RV_COL_BITS) / IDBG_RV_BIT_CELL;
		ines_int_t  bit  = (sub * 8) + (7 - cell);           // 子行决定是低字节还是高字节
		ines_int_t  width = s_defs[reg].width;

		if (width > 16) width = 16;
		if ((width > 0) && (bit >= 0) && (bit < width))
			[self selectLine:line column:IDBG_RV_CUR_BIT bit:bit];
	}
}

- (void)keyDown:(NSEvent*)event
{
	unsigned short  code  = event.keyCode;
	NSString*       chars = [event charactersIgnoringModifiers];
	BOOL            shift = ((event.modifierFlags & NSEventModifierFlagShift) != 0);
	ines_int_t      reg   = -1;

	if (![self hasContent])
	{
		[super keyDown:event];
		return;
	}

	reg = [self curReg];

	switch (code)
	{
	case 126:   // 上
		[self cancelEdit];
		[self moveRow:-1];
		return;
	case 125:   // 下
		[self cancelEdit];
		[self moveRow:1];
		return;
	case 123:   // 左
		if (_cursorCol == IDBG_RV_CUR_BIT)
		{
			if ((reg >= 0) && (_bitIndex < s_defs[reg].width - 1))
				_bitIndex++;
			[self setNeedsDisplayAll];
		}
		else if (_editActive)
		{
			if (_editNibble > 0) _editNibble--;
			[self setNeedsDisplayAll];
		}
		return;
	case 124:   // 右
		if (_cursorCol == IDBG_RV_CUR_BIT)
		{
			if (_bitIndex > 0) _bitIndex--;
			[self setNeedsDisplayAll];
		}
		else if (_editActive)
		{
			if ((reg >= 0) && (_editNibble < (s_defs[reg].width / 4) - 1))
				_editNibble++;
			[self setNeedsDisplayAll];
		}
		return;
	case 116:   // PageUp
		[self scrollBy:NSScrollerDecrementPage];
		return;
	case 121:   // PageDown
		[self scrollBy:NSScrollerIncrementPage];
		return;
	case 115:   // Home
		if (_cursorCol == IDBG_RV_CUR_BIT)
		{
			if (reg >= 0) _bitIndex = MIN(s_defs[reg].width, 16) - 1;
			[self setNeedsDisplayAll];
		}
		else
		{
			_startLine = 0;
			[self updateScrollers];
			[self setNeedsDisplayAll];
		}
		return;
	case 119:   // End
		if (_cursorCol == IDBG_RV_CUR_BIT)
		{
			_bitIndex = 0;
			[self setNeedsDisplayAll];
		}
		else
		{
			_startLine = MAX(0, _totalRows - _rows);
			[self updateScrollers];
			[self setNeedsDisplayAll];
		}
		return;
	case 36:    // Enter
		[self commitEdit];
		return;
	case 53:    // Esc
		[self cancelEdit];
		[self setNeedsDisplayAll];
		return;
	case 48:    // Tab
		[self cancelEdit];
		[self moveToWritableRow:(shift ? -1 : 1)];
		return;
	case 49:    // 空格
		if (_cursorCol == IDBG_RV_CUR_VALUE)
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
				if (b < 0) b = s_defs[reg].width - 1;

				_cursorCol = IDBG_RV_CUR_BIT;
				_bitIndex  = b;
				[self setNeedsDisplayAll];
			}
		}
		else if (reg >= 0)
		{
			ines_int64_t  cur = idbg_rv_value(&_snapshot->regs, reg);

			[self toggleBit:_bitIndex value:((cur >> _bitIndex) & 1) ? 0 : 1];
		}
		return;
	default:
		break;
	}

	if ((chars != nil) && (chars.length > 0) && (reg >= 0))
	{
		unichar  c = [chars characterAtIndex:0];

		// ---- 位格: 只接受 0 / 1 ----
		if (_cursorCol == IDBG_RV_CUR_BIT)
		{
			if ((c == '0') || (c == '1'))
			{
				[self toggleBit:_bitIndex value:(c == '1') ? 1 : 0];
				if (_bitIndex > 0) _bitIndex--;               // 便于从高位往低位连续输入
				[self setNeedsDisplayAll];
			}
			else
				[self showTips:L10N("debug.reg.tip_binary_only")];

			return;
		}

		// ---- 值列: 十六进制 nibble ----
		{
			ines_int_t  val = -1;

			if ((c >= '0') && (c <= '9'))      val = (ines_int_t)(c - '0');
			else if ((c >= 'a') && (c <= 'f')) val = (ines_int_t)(c - 'a') + 10;
			else if ((c >= 'A') && (c <= 'F')) val = (ines_int_t)(c - 'A') + 10;

			if (val >= 0)
			{
				ines_int_t  width  = s_defs[reg].width;
				ines_int_t  digits = width / 4;

				if ((width != 8) && (width != 16))
				{
					[self showTips:L10N("debug.reg.tip_reg_readonly")];
					return;
				}
				if (s_defs[reg].wmask == 0)
				{
					[self showTips:L10N("debug.reg.tip_reg_readonly")];
					return;
				}

				if (!_editActive)
				{
					_editActive = YES;
					_editValue  = idbg_rv_value(&_snapshot->regs, reg);
					_editNibble = 0;
				}

				{
					ines_int_t  shift = (digits - 1 - _editNibble) * 4;

					_editValue = (_editValue & ~((ines_int64_t)0x0F << shift))
							   | ((ines_int64_t)val << shift);
				}

				_editNibble++;
				if (_editNibble >= digits)
					[self commitEdit];
				else
					[self setNeedsDisplayAll];

				return;
			}
		}
	}

	[super keyDown:event];
}

#pragma mark 复制

- (NSMenu*)menuForEvent:(NSEvent*)event
{
	NSMenu*      menu  = [[NSMenu alloc] initWithTitle:@""];
	NSMenuItem*  item;

	item = [[NSMenuItem alloc] initWithTitle:L10N("debug.reg.copy_value") action:@selector(copyValue:) keyEquivalent:@""];
	item.target = self;
	[menu addItem:item];

	item = [[NSMenuItem alloc] initWithTitle:L10N("debug.reg.copy_all") action:@selector(copyAll:) keyEquivalent:@""];
	item.target = self;
	[menu addItem:item];

	return menu;
}

- (void)copyValue:(id)sender
{
	ines_int_t  reg = [self curReg];

	if (![self hasContent] || (reg < 0))
		return;

	[[NSPasteboard generalPasteboard] clearContents];
	[[NSPasteboard generalPasteboard]
		setString:idbg_rv_value_text(&s_defs[reg], idbg_rv_value(&_snapshot->regs, reg))
		  forType:NSPasteboardTypeString];
}

- (void)copyAll:(id)sender
{
	NSMutableString*  text = [NSMutableString string];
	ines_int_t        i;

	if (![self hasContent])
		return;

	for (i = 0; i < IDBG_REG_COUNT; i++)
	{
		[text appendFormat:@"%-10s %-7s %@\n", s_defs[i].name, s_defs[i].addr,
						   idbg_rv_value_text(&s_defs[i], idbg_rv_value(&_snapshot->regs, i))];
	}

	[[NSPasteboard generalPasteboard] clearContents];
	[[NSPasteboard generalPasteboard] setString:text forType:NSPasteboardTypeString];
}

#pragma mark 绘制

// 变化跟踪: 与上一次绘制比较, 记录变化时刻(用于 0.5s 红色高亮)
- (void)trackChanges
{
	CFTimeInterval  now = CFAbsoluteTimeGetCurrent();
	ines_int_t      i;

	for (i = 0; i < IDBG_REG_COUNT; i++)
	{
		ines_int64_t  v = idbg_rv_value(&_snapshot->regs, i);

		if (!_lastValid[i])
		{
			_lastValid[i]  = YES;
			_lastValue[i]  = v;
			_lastChange[i] = 0.0;
			continue;
		}

		if (v != _lastValue[i])
		{
			_prevValue[i]  = _lastValue[i];
			_lastValue[i]  = v;
			_lastChange[i] = now;
		}
	}
}

// 整幅重绘: 滚动/折叠/选中移动/编辑等结构性变化走这里(与行级脏矩形路径区分)
- (void)setNeedsDisplayAll
{
	_fullPending = YES;
	[self setNeedsDisplay:YES];
}

// 供调试管理器 50ms tick 调用: 行级脏判定, 只重绘值变化的行。
// 暂停且无变化时不产生任何绘制与窗口表面上传, 主线程零开销;
// 红色高亮过期后补一拍重绘(余量 0.07s > 刷新间隔 0.05s)以清除红色。
- (void)refreshForTick
{
	const ines_dbg_regs_t*  pRegs;
	CFTimeInterval         now     = CFAbsoluteTimeGetCurrent();
	BOOL                    content = [self hasContent];
	NSRect                  contentRect;
	ines_int_t              iLine;
	ines_int_t              total;
	ines_int_t              i;

	if (content != _lastHadContent)
	{
		_lastHadContent = content;
		[self setNeedsDisplayAll];
		return;
	}

	if (!content)
		return;

	// 变化记账(原先在 drawRect 内, 移到这里保证跳过绘制时也持续跟踪)
	[self trackChanges];

	// Tips 淡出期间需要连续重绘(短促少见, 按整幅处理)
	if ((_tips != nil) && (now < _tipsExpire))
	{
		[self setNeedsDisplayAll];
		return;
	}

	pRegs = &_snapshot->regs;

	for (i = 0; i < IDBG_REG_COUNT; i++)
	{
		ines_int64_t  v = _lastValue[i];
		BOOL          dirty;

		dirty = (!_drawnValid[i]) || (v != _drawnValue[i]);

		// 红色高亮回落: 高亮有效期内或刚过期都要重绘
		if (!dirty && (_lastChange[i] > 0.0) &&
			((now - _lastChange[i]) < (IDBG_RV_HILITE_TIME + 0.07)))
			dirty = YES;

		// $2005 卷轴说明列由 t / fine_x 派生, 自身值恒 0, 需单独跟随标脏
		if (s_defs[i].reg_id == IDBG_REG_PPUSCROLL)
		{
			ines_int_t  key = (ines_int_t)(((unsigned)pRegs->t << 3) | (pRegs->fine_x & 0x07));

			if (key != _drawnScrollKey)
			{
				_drawnScrollKey = key;
				dirty = YES;
			}
		}

		if (dirty)
		{
			_drawnValue[i] = v;
			_drawnValid[i] = YES;
		}

		_dirtyRow[i] = (_dirtyRow[i] || dirty);
	}

	// 脏行 -> 矩形(仅可见范围; 视口外的行滚动时由整幅重绘兜底)
	contentRect = [self contentRect];
	total       = _lineCount;
	if (total - _startLine > _rows)
		total = _startLine + _rows;

	for (iLine = _startLine; iLine < total; iLine++)
	{
		ines_int_t  reg = _lineMap[iLine].reg;

		if ((reg < 0) || !_dirtyRow[reg])
			continue;

		[self setNeedsDisplayInRect:NSMakeRect(contentRect.origin.x,
											   contentRect.origin.y + (CGFloat)((iLine - _startLine) + IDBG_RV_HEAD_LINES) * _charH,
											   contentRect.size.width,
											   (CGFloat)_charH)];
	}
}

static NSString* idbg_rv_str(const char* text)
{
	return (text != NULL) ? [NSString stringWithUTF8String:text] : @"";
}

// 预生成的单字符字符串表(' '..'~'): 位格字母/位号/L-H 标记都是单 ASCII 字符,
// 等宽字体下单字符宽度恒为 _charW, 免去逐格测量与每次绘制的临时对象分配
static NSString* s_rv_letters[95] = { NULL };

static NSString* idbg_rv_letter(char c)
{
	if ((c < ' ') || (c > '~'))
		return @"";
	if (s_rv_letters[c - ' '] == nil)
		s_rv_letters[c - ' '] = [[NSString alloc] initWithFormat:@"%c", c];
	return s_rv_letters[c - ' '];
}

// 单元格填充(选中/只读两维度)
static void idbg_rv_fill_cell(NSRect rc, BOOL selected, BOOL writable)
{
	if (!writable)
	{
		[[NSColor colorWithSRGBRed:IDBG_RV_GRAY_FILL
							 green:IDBG_RV_GRAY_FILL
							  blue:IDBG_RV_GRAY_FILL
							 alpha:1.0] setFill];
		NSRectFill(rc);
		return;
	}

	if (selected)
	{
		[[NSColor selectedTextBackgroundColor] setFill];
		NSRectFill(rc);
	}
}

static void idbg_rv_stroke_focus(NSRect rc)
{
	NSBezierPath*  path = [NSBezierPath bezierPathWithRect:NSMakeRect(rc.origin.x + 0.5,
																	  rc.origin.y + 0.5,
																	  rc.size.width - 1.0,
																	  rc.size.height - 1.0)];

	[[NSColor controlAccentColor] setStroke];
	path.lineWidth = 1.5;
	[path stroke];
}

- (void)drawRect:(NSRect)dirtyRect
{
	const ines_dbg_snapshot_t*  pSnap = _snapshot;
	const ines_dbg_regs_t*      pRegs;
	NSRect                      content;
	CGFloat                     px;
	CGFloat                     py;
	ines_int_t                  iLine;
	ines_int_t                  total;
	ines_int_t                  bitIdx;
	ines_int_t                  curReg;
	CFTimeInterval              now;
	BOOL                        drawFull;

	// 消费待绘集: 整幅(结构变化) 或 行级脏集(refreshForTick 标记)
	drawFull     = _fullPending;
	_fullPending = NO;
	memcpy(_drawRow, _dirtyRow, sizeof(_dirtyRow));
	memset(_dirtyRow, 0, sizeof(_dirtyRow));

	if ((pSnap == NULL) || pSnap->rom_off)
	{
		[[NSColor colorWithSRGBRed:IDBG_RV_OFF_GRAY green:IDBG_RV_OFF_GRAY
							  blue:IDBG_RV_OFF_GRAY alpha:1.0] setFill];
		NSRectFill(self.bounds);
		return;
	}

	[[NSColor textBackgroundColor] setFill];
	NSRectFill(self.bounds);

	pRegs   = &pSnap->regs;
	content = [self contentRect];
	px      = content.origin.x;
	py      = content.origin.y;
	now     = CFAbsoluteTimeGetCurrent();
	curReg  = [self curReg];

	total = _lineCount;
	if (total - _startLine > _rows)
		total = _startLine + _rows;

	// ---- 头部(静态, 仅整幅重绘时画) ----
	if (drawFull)
	{
		CGFloat  y = py;

		[@"NAME"  drawAtPoint:NSMakePoint(px, y)                                     withAttributes:_attrs];
		[@"ADDR"  drawAtPoint:NSMakePoint(px + IDBG_RV_COL_ADDR  * _charW, y)         withAttributes:_attrs];
		[@"VALUE" drawAtPoint:NSMakePoint(px + IDBG_RV_COL_VALUE * _charW, y)         withAttributes:_attrs];
		[L10N("debug.reg.note") drawAtPoint:NSMakePoint(px + IDBG_RV_COL_NOTE * _charW, y) withAttributes:_attrs];

		// 位号: 每行只 8 位, 头部标 7..0(高低字节行共用同一组位号)
		for (bitIdx = 0; bitIdx < 8; bitIdx++)
		{
			NSString*  s = idbg_rv_letter((char)('0' + (7 - bitIdx)));
			CGFloat    x = px + (CGFloat)(IDBG_RV_COL_BITS + bitIdx * IDBG_RV_BIT_CELL) * _charW;

			[s drawAtPoint:NSMakePoint(x + (CGFloat)(IDBG_RV_BIT_CELL - 1) * _charW / 2.0, y)
			withAttributes:_attrs];
		}

		[[NSColor separatorColor] setStroke];
		[NSBezierPath strokeLineFromPoint:NSMakePoint(px, py + (CGFloat)_charH + 0.5)
								 toPoint:NSMakePoint(px + content.size.width, py + (CGFloat)_charH + 0.5)];
	}

	// ---- 正文 ----
	for (iLine = _startLine; iLine < total; iLine++)
	{
		ines_int_t   group = _lineMap[iLine].group;
		ines_int_t   reg   = _lineMap[iLine].reg;
		ines_int_t   sub   = _lineMap[iLine].sub;
		CGFloat      y  = py + (CGFloat)((iLine - _startLine) + IDBG_RV_HEAD_LINES) * _charH;
		BOOL         sel = ((reg >= 0) && (reg == curReg));

		if (reg < 0)
		{
			NSString*  title;

			if (!drawFull)                          // 组标题静态, 仅整幅重绘
				continue;

			[[NSColor controlBackgroundColor] setFill];
			NSRectFill(NSMakeRect(px, y, content.size.width, (CGFloat)_charH));

			title = [NSString stringWithFormat:@"%s %s",
					 (_folded[group] ? "▸" : "▾"), s_groups[group].title];
			[title drawAtPoint:NSMakePoint(px, y) withAttributes:_attrs];
			continue;
		}

		// 行级裁剪: 非脏行直接跳过(本次 drawRect 只画被标记的行)
		if (!drawFull && !_drawRow[reg])
			continue;

		{
			const ines_rv_def_t*  pDef  = &s_defs[reg];
			ines_int64_t          val   = idbg_rv_value(pRegs, reg);
			ines_int_t            width = pDef->width;
			BOOL                  writable = (pDef->wmask != 0);
			BOOL                  changed  = (_lastValid[reg] &&
											  (_lastChange[reg] > 0.0) &&
											  ((now - _lastChange[reg]) < IDBG_RV_HILITE_TIME));
			ines_int64_t          diff;
			NSDictionary*         attrs;
			NSString*             text;
			NSRect                rc;

			// 选中行的淡色行底(与"当前单元格"的反白区分); 16 位寄存器的两个子行都铺
			if (sel)
			{
				[[[NSColor selectedTextBackgroundColor] colorWithAlphaComponent:0.10] setFill];
				NSRectFill(NSMakeRect(px, y, content.size.width, (CGFloat)_charH));
			}

			// ---- 名称 / 地址 / 值 / 说明: 只在首行画 ----
			if (sub == 0)
			{
			attrs = changed ? _attrsRed : _attrs;
			[idbg_rv_str(pDef->name) drawAtPoint:NSMakePoint(px, y) withAttributes:attrs];
			[idbg_rv_str(pDef->addr) drawAtPoint:NSMakePoint(px + IDBG_RV_COL_ADDR * _charW, y)
								   withAttributes:_attrsGray];

			// ---- 值列 ----
			if (_editActive && sel)
				text = idbg_rv_value_text(pDef, _editValue);
			else
				text = idbg_rv_value_text(pDef, val);

			rc = NSMakeRect(px + IDBG_RV_COL_VALUE * _charW, y,
							(CGFloat)IDBG_RV_VALUE_COLS * _charW, (CGFloat)_charH);

			{
				BOOL  cellSel = (sel && (_cursorCol == IDBG_RV_CUR_VALUE));

				idbg_rv_fill_cell(rc, cellSel, writable);

				// 只读值: 灰底表示不可改, 但字色与可写值一致(黑/深色, 变化仍红)
				if (cellSel)
					attrs = _attrsSel;
				else
					attrs = (changed ? _attrsRed : _attrs);

				{
					// 等宽字体 + 纯 ASCII: 宽度 = 字符数 * _charW, 免测量
					CGFloat  w = (CGFloat)text.length * _charW;
					CGFloat  x = rc.origin.x + rc.size.width - w - _charW * 0.5;

					[text drawAtPoint:NSMakePoint(x, y) withAttributes:attrs];
				}

				// 编辑标记
				if (_editActive && sel)
					[@"*" drawAtPoint:NSMakePoint(rc.origin.x, y)
					   withAttributes:(cellSel ? _attrsSel : _attrsRed)];

				if (cellSel)
					idbg_rv_stroke_focus(rc);
			}

			// ---- 说明(只画在首行) ----
			{
				NSString*  note;

				if (pDef->reg_id == IDBG_REG_PPUSCROLL)
					note = idbg_rv_scroll_text(pRegs);
				else
					note = (s_noteText[reg] != nil) ? s_noteText[reg] : idbg_rv_str(pDef->note_en);

				[note drawAtPoint:NSMakePoint(px + IDBG_RV_COL_NOTE * _charW, y)
				   withAttributes:_attrsNote];
			}
			}   // sub == 0

			// ---- 位格: 每个子行画 8 位(sub 0 = 低字节, sub 1 = 高字节) ----
			if ((width > 0) && (width <= 16) && (sub * 8 < width))
			{
				ines_int_t  base = sub * 8;
				ines_int_t  bMax = (width < base + 8) ? width : (base + 8);
				ines_int_t  b;

				diff = changed ? (val ^ _prevValue[reg]) : 0;

				// 高低字节标记(仅 16 位寄存器)
				if (width == 16)
				{
					NSString*  tag = idbg_rv_letter(sub ? 'H' : 'L');

					[tag drawAtPoint:NSMakePoint(px + IDBG_RV_COL_TAG * _charW, y)
						withAttributes:_attrsNote];
				}

				for (b = base; b < bMax; b++)
				{
					ines_int_t  cell  = idbg_rv_bit_cell(b);
					CGFloat     bx    = px + (CGFloat)(IDBG_RV_COL_BITS + cell * IDBG_RV_BIT_CELL) * _charW;
					NSRect      brc   = NSMakeRect(bx, y, (CGFloat)IDBG_RV_BIT_CELL * _charW, (CGFloat)_charH);
					BOOL        wbit  = (((pDef->wmask >> b) & 1) != 0);
					BOOL        bsel  = (sel && (_cursorCol == IDBG_RV_CUR_BIT) && (_bitIndex == b));
					BOOL        one   = (((val >> b) & 1) != 0);
					char        letter = idbg_rv_bit_letter(pDef, b);
					NSString*   s;

					idbg_rv_fill_cell(brc, bsel, wbit);

					// 字色/字重表达 0 与 1: 1 = 常色粗体, 0 = 浅灰;
					// 只读位(灰底)同样区分, 只是整体更淡
					if (bsel)
						attrs = _attrsSel;
					else if (!wbit)
						attrs = one ? _attrsGray : _attrsGrayDim;
					else if (((diff >> b) & 1) != 0)
						attrs = _attrsRed;                       // 变化高亮优先
					else
						attrs = one ? _attrsBit1 : _attrsBit0;

					s = idbg_rv_letter(letter);
					{
						// 单字符宽度恒为 _charW, 免测量
						[s drawAtPoint:NSMakePoint(bx + (brc.size.width - _charW) / 2.0, y)
						withAttributes:attrs];
					}

					// 位格边框(让"格"看得出来)
					if (wbit && !bsel)
					{
						[[[NSColor separatorColor] colorWithAlphaComponent:0.5] setStroke];
						[NSBezierPath strokeLineFromPoint:NSMakePoint(bx, y)
												 toPoint:NSMakePoint(bx, y + (CGFloat)_charH)];
					}

					if (bsel)
						idbg_rv_stroke_focus(brc);
				}
			}
		}
	}

	// ---- Tips ----
	if ((_tips != nil) && (now < _tipsExpire))
	{
		CGFloat     alpha = (CGFloat)((_tipsExpire - now) / 0.3);
		NSSize      sz    = [_tips sizeWithAttributes:_attrs];
		NSRect      box;

		if (alpha > 1.0) alpha = 1.0;

		box = NSMakeRect(px + 4.0, py + content.size.height - (CGFloat)_charH * 2.0,
						 sz.width + 12.0, (CGFloat)_charH + 6.0);

		[[[NSColor controlBackgroundColor] colorWithAlphaComponent:(0.95 * alpha)] setFill];
		[NSBezierPath fillRect:box];

		[[[NSColor separatorColor] colorWithAlphaComponent:alpha] setStroke];
		[NSBezierPath strokeRect:box];

		[_tips drawAtPoint:NSMakePoint(box.origin.x + 6.0, box.origin.y + 3.0)
			withAttributes:_attrs];
	}
	else if ((_tips != nil) && (now >= _tipsExpire))
	{
		_tips = nil;
	}
}

@end
