/**
 * core/mapper/68.c -- Sunsoft-4（iNES Mapper 068）
 *
 * 硬件要点（资料：NESDev "INES Mapper 068"，IMA 知识库「了然记」笔记「INES Mapper 068」）：
 *   - 已知卡带：After Burner (U / J)、Maharaja (J)、Nantettatte!! Baseball (J)。
 *     美国只在 After Burner 上用过这块芯片。
 *   - 上限：PRG 256KB（16KB 分页）、CHR 256KB（2KB 分页）、PRG-RAM 8KB。
 *   - **最大特点是能把 CHR ROM 映射进 PPU 的 nametable 区**（$2000-$2FFF），
 *     所以 nametable 可以是只读的 ROM 数据；镜像由 mapper 控制，且镜像表的含义
 *     在 CIRAM 与 CHR-ROM 两种来源下**完全相同**（都是"该窗口用低位还是高位那块"）。
 *   - 无 IRQ、无总线冲突（bus conflicts: No）。
 *
 * 内存布局：
 *   CPU $6000-$7FFF   8KB PRG-RAM（由 $F000.6 使能；未使能时读回 open bus）
 *   CPU $8000-$BFFF   16KB 可切 PRG 页
 *   CPU $C000-$FFFF   16KB PRG 页，**固定为最后一个内部页**
 *   PPU $0000/$0800/$1000/$1800   四个 2KB 可切 CHR 窗口
 *   PPU $2000-$2FFF               四个 1KB nametable 窗口（CIRAM 或 CHR ROM）
 *
 * 寄存器（地址掩码 $F000，区间内低位不译码）：
 *   $8000  2KB CHR 页 @ PPU $0000-$07FF
 *   $9000  2KB CHR 页 @ PPU $0800-$0FFF
 *   $A000  2KB CHR 页 @ PPU $1000-$17FF
 *   $B000  2KB CHR 页 @ PPU $1800-$1FFF
 *   $C000  1KB CHR 页 → **低位** nametable（CIRAM $000-$3FF 原本所在的位置）
 *   $D000  1KB CHR 页 → **高位** nametable（CIRAM $400-$7FF 原本所在的位置）
 *          这两个只在 $E000.4 = 1（nametable 取自 CHR ROM）时生效；
 *          **只有 D6-D0 有效，D7 被忽略并且恒为 1** —— 也就是说 nametable
 *          只能落在 CHR ROM 的**后 128KB**（CHR 不足 256KB 时靠地址线回卷）。
 *   $E000  [...R ..MM]  bit0-1 = 镜像模式（00 垂直 / 01 水平 / 10 单屏 NT0 / 11 单屏 NT1）
 *                       bit4   = nametable 来源：0 = 内部 CIRAM、1 = CHR ROM
 *   $F000  [.E.. BBBB]  bit0-3 = 16KB PRG 页 @ $8000-$BFFF
 *                       bit6   = 1 使能 PRG-RAM（WRAM +CS2）
 *
 * 未模拟：Nantettatte!! Baseball (J) 把 $F000 改写成 [.E RBBB]（bit3 = 选内部/外部 ROM、
 * bit6 = 使能 WRAM 还是外部"授权 IC"），并用 $6000-$7FFF 的写去重置外部子卡的授权计时器，
 * 再校验外部 ROM 末尾的签名在 107516-107575 个 M2 周期后失效。这是那张卡的外接子卡特性，
 * 本清单没有这张卡；且本核心没有"断开 $6000 让读返回 open bus"的接口，故未实现。
 */

#include "../../comm/idef.h"
#include "../../comm/log.h"
#include "../nes.h"
#include "../mapper.h"


/** $F000 的 PRG 页号掩码（bit0-3） */
#define M68_PRG_MASK       0x0F

/** $F000 的 PRG-RAM 使能位（bit6） */
#define M68_WRAM_EN_BIT    0x40

/** $E000 的镜像模式位（bit0-1） */
#define M68_MIRROR_MASK    0x03

/** $E000 的 nametable 来源选择位（bit4）：1 = CHR ROM */
#define M68_NT_CHR_BIT     0x10

/** $C000/$D000 的有效页号位：D7 被忽略且恒为 1 */
#define M68_NT_PAGE_MASK   0x7F
#define M68_NT_PAGE_HIGH   0x80


/** Mapper 68 私有数据 */
typedef struct _ines_M68_
{
	ines_byte_t   chr[4];      /* 四个 2KB CHR 窗口的页号（$8000/$9000/$A000/$B000） */
	ines_byte_t   nt[2];       /* nametable 用的 1KB CHR 页（$C000 = 低位 / $D000 = 高位） */
	ines_byte_t   prg;         /* $F000 bit0-3：16KB PRG 页号 */
	ines_byte_t   wram_en;     /* $F000 bit6：1 = 使能 PRG-RAM */
	ines_byte_t   ctrl;        /* $E000：bit0-1 镜像模式 + bit4 nametable 来源 */
} M68_data_t;

#define mapper2M68data(p_mapper)  ((M68_data_t*)((p_mapper)->p_data))


/**
 * 应用 16KB PRG 窗口（$8000-$BFFF 可切，$C000-$FFFF 固定最后一个 16KB 页）。
 * @param p      私有数据
 * @param p_host 宿主
 * @param val    $F000 的写入值（只取 bit0-3）
 */
static void M68_set_cpu_bank(M68_data_t* p, ines_host_t* p_host, ines_byte_t val)
{
	ines_word_t  num8k  = (ines_word_t)((p_host->prom_8k_num > 0) ? p_host->prom_8k_num : 1);
	ines_word_t  num16k = (ines_word_t)(num8k >> 1);   /* 16KB 页数 */
	ines_word_t  bank;

	p->prg = (ines_byte_t)(val & M68_PRG_MASK);

	if(num8k < 2)
	{
		/* 不足 16KB（清单标错等极端情况）：整片 PRG 镜像四次，避免算出负的窗口号 */
		ines_set_prom_bank_4(p_host, 0, 0, 0, 0);
		return;
	}

	if(num16k < 1)
		num16k = 1;

	bank = (ines_word_t)((p->prg % num16k) << 1);   /* 16KB 页号 → 8KB 窗口号 */

	ines_set_prom_bank_4(p_host, bank, (ines_word_t)(bank + 1),
						 (ines_word_t)(num8k - 2), (ines_word_t)(num8k - 1));
}

/**
 * 应用一个 2KB CHR 窗口（n = 0..3，对应 PPU $0000/$0800/$1000/$1800）。
 * @param p      私有数据
 * @param p_host 宿主
 * @param n      2KB 窗口号
 * @param val    写入值（2KB 页号，按卡带 CHR 容量回卷）
 */
static void M68_set_ppu_bank(M68_data_t* p, ines_host_t* p_host, ines_int_t n, ines_byte_t val)
{
	ines_word_t  num1k = (ines_word_t)p_host->vrom_1k_num;
	ines_word_t  base;

	if(n < 0 || n > 3)
		return;

	p->chr[n] = val;

	base = (ines_word_t)((ines_word_t)val << 1);   /* 2KB 页号 → 1KB 页号 */

	if(num1k > 0)
	{
		ines_set_vrom_bank_n(p_host, (ines_word_t)(n << 1),       (ines_word_t)(base % num1k));
		ines_set_vrom_bank_n(p_host, (ines_word_t)((n << 1) + 1), (ines_word_t)((base + 1) % num1k));
	}
	else
	{
		/* 无 CHR-ROM（CHR 为 RAM）：页号由 ines_set_vram_bank_n() 内部回卷 */
		ines_set_vram_bank_n(p_host, (ines_word_t)(n << 1),       base);
		ines_set_vram_bank_n(p_host, (ines_word_t)((n << 1) + 1), (ines_word_t)(base + 1));
	}
}

/**
 * 应用 nametable 布局（$E000 + $C000/$D000）。
 *
 * 镜像表给出四个 nametable 窗口各自用"低位"还是"高位"那块：
 *   mode  $2000  $2400  $2800  $2C00
 *   00    Low    High   Low    High     垂直
 *   01    Low    Low    High   High     水平
 *   10    Low    Low    Low    Low      单屏 NT0
 *   11    High   High   High   High     单屏 NT1
 * CIRAM 模式下 Low/High 是内部 nametable RAM 的第 0/1 页；
 * CHR-ROM 模式下则是 $C000 / $D000 选出的 1KB CHR 页。
 *
 * @param p      私有数据
 * @param p_host 宿主
 */
static void M68_apply_nt(M68_data_t* p, ines_host_t* p_host)
{
	ines_byte_t  mode = (ines_byte_t)(p->ctrl & M68_MIRROR_MASK);
	ines_word_t  num1k = (ines_word_t)p_host->vrom_1k_num;
	ines_word_t  page[2];
	ines_int_t   n;

	/* [镜像模式][窗口] = 0 用低位、1 用高位 */
	static const ines_byte_t  nt_sel[4][4] =
	{
		{0, 1, 0, 1},   /* 00 垂直 */
		{0, 0, 1, 1},   /* 01 水平 */
		{0, 0, 0, 0},   /* 10 单屏 NT0 */
		{1, 1, 1, 1}    /* 11 单屏 NT1 */
	};

	if(p_host->rom.mirror_type == MIRROR_FOUR_SCREEN)
		return;

	if((p->ctrl & M68_NT_CHR_BIT) == 0 || num1k == 0)
	{
		/*
		 * nametable 取自内部 CIRAM（或卡带根本没有 CHR-ROM 可映射）。
		 * ines_ppu_set_mirror() 同时会把四个窗口的 nt_type 复位成 0，
		 * 所以从 CHR-ROM 模式切回来也必须走这里。
		 */
		ines_ppu_set_mirror(&p_host->ppu, nt_sel[mode][0], nt_sel[mode][1],
							nt_sel[mode][2], nt_sel[mode][3]);
		return;
	}

	/* D7 被忽略且恒为 1 → nametable 只能落在 CHR 的后 128KB，再按卡带容量回卷 */
	page[0] = (ines_word_t)((((ines_word_t)(p->nt[0] & M68_NT_PAGE_MASK)) | M68_NT_PAGE_HIGH) % num1k);
	page[1] = (ines_word_t)((((ines_word_t)(p->nt[1] & M68_NT_PAGE_MASK)) | M68_NT_PAGE_HIGH) % num1k);

	INES_LOG(LOG_DBG, MOD_MMC, ISTR("M68: NT 取自 CHR，低位页 = %d、高位页 = %d\n"),
			 (ines_int_t)page[0], (ines_int_t)page[1]);

	for(n = 0; n < 4; n++)
	{
		ines_set_nt_chr_bank_n(p_host, (ines_word_t)n, page[nt_sel[mode][n]]);
	}
}

static void mapper68_reset(ines_mapper_t* p_mapper)
{
	ines_host_t*  p_host = mapper2host(p_mapper);
	M68_data_t*   p = mapper2M68data(p_mapper);
	ines_int_t    n;

	if(p == NULL)
		return;

	p->prg = 0;
	p->wram_en = 0;
	p->nt[0] = 0;
	p->nt[1] = 0;

	for(n = 0; n < 4; n++)
		p->chr[n] = (ines_byte_t)n;

	M68_set_cpu_bank(p, p_host, 0);

	for(n = 0; n < 4; n++)
		M68_set_ppu_bank(p, p_host, n, p->chr[n]);

	/* 上电：nametable 用内部 CIRAM（bit4 = 0），镜像沿用卡带头 */
	if(p_host->rom.mirror_type != MIRROR_FOUR_SCREEN)
	{
		p->ctrl = (ines_byte_t)((p_host->rom.mirror_type == MIRROR_HORZ) ? 0x01 : 0x00);
		M68_apply_nt(p, p_host);
	}

	INES_LOG(LOG_INF, MOD_MMC, ISTR("mapper68: Sunsoft-4 (PRG %dKB, CHR %dKB)\n"),
			 (ines_int_t)(p_host->prom_8k_num * 8), (ines_int_t)p_host->vrom_1k_num);
}

static void mapper68_writehigh(ines_mapper_t* p_mapper, ines_word_t addr, ines_byte_t val)
{
	ines_host_t*  p_host = mapper2host(p_mapper);
	M68_data_t*   p = mapper2M68data(p_mapper);

	if(p == NULL)
		return;

	INES_LOG(LOG_DBG, MOD_MMC, ISTR("M68: $%04X = #$%02X.\n"), (ines_int_t)addr, (ines_int_t)val);

	switch(addr & 0xF000)
	{
	case 0x8000:   /* 2KB CHR @ $0000-$07FF */
		M68_set_ppu_bank(p, p_host, 0, val);
		break;

	case 0x9000:   /* 2KB CHR @ $0800-$0FFF */
		M68_set_ppu_bank(p, p_host, 1, val);
		break;

	case 0xA000:   /* 2KB CHR @ $1000-$17FF */
		M68_set_ppu_bank(p, p_host, 2, val);
		break;

	case 0xB000:   /* 2KB CHR @ $1800-$1FFF */
		M68_set_ppu_bank(p, p_host, 3, val);
		break;

	case 0xC000:   /* 1KB CHR → 低位 nametable */
		p->nt[0] = val;
		M68_apply_nt(p, p_host);
		break;

	case 0xD000:   /* 1KB CHR → 高位 nametable */
		p->nt[1] = val;
		M68_apply_nt(p, p_host);
		break;

	case 0xE000:   /* bit0-1 镜像模式、bit4 nametable 来源 */
		p->ctrl = (ines_byte_t)(val & (M68_MIRROR_MASK | M68_NT_CHR_BIT));
		M68_apply_nt(p, p_host);
		break;

	case 0xF000:   /* bit0-3 PRG 页、bit6 PRG-RAM 使能 */
		p->wram_en = (ines_byte_t)((val & M68_WRAM_EN_BIT) ? 1 : 0);
		M68_set_cpu_bank(p, p_host, val);
		break;

	default:
		break;
	}
}

void mapper68_fini(ines_mapper_t* p_mapper)
{
	ines_free(p_mapper->p_data);
	p_mapper->p_data = NULL;
}

ines_bool_t  mapper68_create(ines_mapper_t* p_mapper)
{
	if(p_mapper == NULL)
		return ines_false;

	INIT_MAPPER_DATA_ST(p_mapper, M68_data_t);

	p_mapper->reset = mapper68_reset;
	p_mapper->writehigh = mapper68_writehigh;
	p_mapper->fini = mapper68_fini;

	return ines_true;
}



