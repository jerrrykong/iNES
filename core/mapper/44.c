
/**
 * core/mapper/44.c -- Super Big 7-in-1（iNES Mapper 044）
 *
 * 硬件要点：这是一块**以 MMC3 为基础的多合一卡带**。多合一菜单先选一个"块（block）"，
 * 块内再按普通 MMC3 的方式换页。
 *
 *   - 寄存器窗口 $8000-$FFFF（掩码 $E001），行为**完全等同 MMC3**
 *     （$8000 选择 / $8001 数据 / $A000 镜像 / $C000、$C001、$E000、$E001 扫描线 IRQ），
 *     **唯一区别**在 $A001：
 *
 *         $A001:  [EW.. .BBB]
 *                  ││    └─── 块选择 bit2-0；选 7 等同选 6
 *                  │└──────── 写保护（同 MMC3）
 *                  └───────── PRG-RAM 使能（同 MMC3）
 *
 *   - 块 0..5 各 128KB PRG + 128KB CHR；块 6（与 7）256KB PRG + 256KB CHR
 *     → 整卡 1MB PRG + 1MB CHR
 *   - **MMC3 选出的所有页（含两个固定页）都落在当前块内**：
 *
 *         页号 = (MMC3 页号 AND and) OR or        PRG 以 8KB 页计、CHR 以 1KB 页计
 *
 *         block   PRG-AND  PRG-OR   CHR-AND  CHR-OR
 *           0      $0F      $00       $7F     $000
 *           1      $0F      $10       $7F     $080
 *           2      $0F      $20       $7F     $100
 *           3      $0F      $30       $7F     $180
 *           4      $0F      $40       $7F     $200
 *           5      $0F      $50       $7F     $280
 *         6,7      $1F      $60       $FF     $300
 *
 *     固定页（MMC3 的 -2 / -1）同样要过这道变换，所以块 6 的固定页是 126/127，
 *     块 0 的固定页是 14/15。
 *   - 上电（以及复位）必须选中块 0；镜像与 IRQ 完全沿用 MMC3。
 */

#include "../../comm/idef.h"
#include "../../comm/log.h"
#include "../nes.h"
#include "../mapper.h"


/* 块选择的 AND/OR 表 */
struct _SB7_BLOCK_
{
	ines_byte_t   prg_and;    /* PRG 页号掩码（8KB 页） */
	ines_word_t   prg_or;     /* PRG 块基址（8KB 页） */
	ines_word_t   chr_and;    /* CHR 页号掩码（1KB 页） */
	ines_word_t   chr_or;     /* CHR 块基址（1KB 页） */
};

typedef struct _SB7_BLOCK_   SB7_block_t;

static const SB7_block_t   sb7_blocks[8] =
{
	{ 0x0F, 0x00, 0x7F, 0x000 },   /* 块 0：PRG 0-15、CHR 0-127 */
	{ 0x0F, 0x10, 0x7F, 0x080 },   /* 块 1 */
	{ 0x0F, 0x20, 0x7F, 0x100 },   /* 块 2 */
	{ 0x0F, 0x30, 0x7F, 0x180 },   /* 块 3 */
	{ 0x0F, 0x40, 0x7F, 0x200 },   /* 块 4 */
	{ 0x0F, 0x50, 0x7F, 0x280 },   /* 块 5 */
	{ 0x1F, 0x60, 0xFF, 0x300 },   /* 块 6：256KB */
	{ 0x1F, 0x60, 0xFF, 0x300 }    /* 块 7 与块 6 相同 */
};


struct _SB7_data_
{
	ines_byte_t   reg[8];     /* MMC3 的 8 个内部寄存器 */
	ines_word_t   prg0, prg1;
	ines_word_t   chr01, chr23, chr4, chr5, chr6, chr7;
	ines_byte_t   block;      /* 当前块（0..7，7 按 6 处理） */
	ines_byte_t   irq_enabled;
	ines_byte_t   irq_counter;
	ines_byte_t   irq_latch;
	ines_byte_t   irq_reload;
};

typedef struct _SB7_data_   SB7_data_t;

#define mapper2SB7data(mapper)   ((SB7_data_t*)((mapper)->p_data))
#define SB7_chr_swap(p)          (0 != ((p)->reg[0] & 0x80))
#define SB7_prg_swap(p)          (0 != ((p)->reg[0] & 0x40))


/**
 * 把 MMC3 的 PRG 页号（8KB 页）映射到当前块内。结果再对总页数取模，
 * 容量不足的畸形 ROM 也不会越界。
 */
static ines_word_t SB7_prg_page(ines_host_t* p_host, SB7_data_t* p, ines_word_t page)
{
	const SB7_block_t*  b = &sb7_blocks[p->block & 0x07];
	ines_word_t         num = (ines_word_t)p_host->prom_8k_num;

	if(num == 0)
		return 0;

	return (ines_word_t)((((page & b->prg_and) | b->prg_or)) % num);
}

/**
 * 把 MMC3 的 CHR 页号（1KB 页）映射到当前块内。
 */
static ines_word_t SB7_chr_page(ines_host_t* p_host, SB7_data_t* p, ines_word_t page)
{
	const SB7_block_t*  b = &sb7_blocks[p->block & 0x07];
	ines_word_t         num = (ines_word_t)p_host->vrom_1k_num;

	if(num == 0)
		return 0;

	return (ines_word_t)((((page & b->chr_and) | b->chr_or)) % num);
}

/**
 * CPU $8000-$FFFF：8KB×4。两个固定页也要过块变换。
 */
static void SB7_set_cpu_bank(ines_mapper_t* p_mapper)
{
	ines_host_t*  p_host = mapper2host(p_mapper);
	SB7_data_t*   p = mapper2SB7data(p_mapper);
	ines_word_t   num = (ines_word_t)p_host->prom_8k_num;
	ines_word_t   fixed2;

	if((p == NULL) || (num == 0))
		return;

	fixed2 = (num >= 2) ? (ines_word_t)(num - 2) : 0;

	if(SB7_prg_swap(p))
	{
		ines_set_prom_bank_4(p_host,
							 SB7_prg_page(p_host, p, fixed2),
							 SB7_prg_page(p_host, p, p->prg1),
							 SB7_prg_page(p_host, p, p->prg0),
							 SB7_prg_page(p_host, p, (ines_word_t)(num - 1)));
	}
	else
	{
		ines_set_prom_bank_4(p_host,
							 SB7_prg_page(p_host, p, p->prg0),
							 SB7_prg_page(p_host, p, p->prg1),
							 SB7_prg_page(p_host, p, fixed2),
							 SB7_prg_page(p_host, p, (ines_word_t)(num - 1)));
	}
}

/**
 * PPU $0000-$1FFF：MMC3 的 8 个 1KB 窗口，全部映射到当前块内。
 */
static void SB7_set_ppu_bank(ines_mapper_t* p_mapper)
{
	ines_host_t*  p_host = mapper2host(p_mapper);
	SB7_data_t*   p = mapper2SB7data(p_mapper);

	if(p == NULL)
		return;

	if(p_host->vrom_1k_num > 0)
	{
		if(SB7_chr_swap(p))
		{
			ines_set_vrom_bank_8(p_host,
								 SB7_chr_page(p_host, p, p->chr4),
								 SB7_chr_page(p_host, p, p->chr5),
								 SB7_chr_page(p_host, p, p->chr6),
								 SB7_chr_page(p_host, p, p->chr7),
								 SB7_chr_page(p_host, p, p->chr01),
								 SB7_chr_page(p_host, p, (ines_word_t)(p->chr01 + 1)),
								 SB7_chr_page(p_host, p, p->chr23),
								 SB7_chr_page(p_host, p, (ines_word_t)(p->chr23 + 1)));
		}
		else
		{
			ines_set_vrom_bank_8(p_host,
								 SB7_chr_page(p_host, p, p->chr01),
								 SB7_chr_page(p_host, p, (ines_word_t)(p->chr01 + 1)),
								 SB7_chr_page(p_host, p, p->chr23),
								 SB7_chr_page(p_host, p, (ines_word_t)(p->chr23 + 1)),
								 SB7_chr_page(p_host, p, p->chr4),
								 SB7_chr_page(p_host, p, p->chr5),
								 SB7_chr_page(p_host, p, p->chr6),
								 SB7_chr_page(p_host, p, p->chr7));
		}
	}
	else
	{
		/* 该板必带 CHR-ROM；没有就退化成 VRAM，此时块变换不适用（VRAM 页数太少） */
		if(SB7_chr_swap(p))
		{
			ines_set_vram_bank_n(p_host, 0, p->chr4);
			ines_set_vram_bank_n(p_host, 1, p->chr5);
			ines_set_vram_bank_n(p_host, 2, p->chr6);
			ines_set_vram_bank_n(p_host, 3, p->chr7);
			ines_set_vram_bank_n(p_host, 4, p->chr01);
			ines_set_vram_bank_n(p_host, 5, (ines_word_t)(p->chr01 + 1));
			ines_set_vram_bank_n(p_host, 6, p->chr23);
			ines_set_vram_bank_n(p_host, 7, (ines_word_t)(p->chr23 + 1));
		}
		else
		{
			ines_set_vram_bank_n(p_host, 0, p->chr01);
			ines_set_vram_bank_n(p_host, 1, (ines_word_t)(p->chr01 + 1));
			ines_set_vram_bank_n(p_host, 2, p->chr23);
			ines_set_vram_bank_n(p_host, 3, (ines_word_t)(p->chr23 + 1));
			ines_set_vram_bank_n(p_host, 4, p->chr4);
			ines_set_vram_bank_n(p_host, 5, p->chr5);
			ines_set_vram_bank_n(p_host, 6, p->chr6);
			ines_set_vram_bank_n(p_host, 7, p->chr7);
		}
	}
}

/**
 * 复位：MMC3 各寄存器清零，并**选中块 0**（硬件要求）。
 * @param p_mapper Mapper
 */
static void mapper44_reset(ines_mapper_t* p_mapper)
{
	ines_host_t*  p_host = mapper2host(p_mapper);
	SB7_data_t*   p = mapper2SB7data(p_mapper);
	ines_int_t    n;

	if(p == NULL)
		return;

	for(n = 0; n < 8; n++)
		p->reg[n] = 0;

	p->prg0 = 0;
	p->prg1 = 1;
	p->chr01 = 0;
	p->chr23 = 2;
	p->chr4 = 4;
	p->chr5 = 5;
	p->chr6 = 6;
	p->chr7 = 7;
	p->block = 0;
	p->irq_enabled = 0;
	p->irq_counter = 0;
	p->irq_latch = 0;
	p->irq_reload = 0;

	SB7_set_cpu_bank(p_mapper);
	SB7_set_ppu_bank(p_mapper);

	INES_LOG(LOG_INF, MOD_MMC, ISTR("mapper44: Super Big 7-in-1 (PRG %dKB, CHR %dKB)\n"),
			 (int)(p_host->prom_8k_num * 8), (int)p_host->vrom_1k_num);
}

/**
 * $8000-$FFFF 写入：与 MMC3 完全一致，$A001 额外承载块选择。
 * @param p_mapper Mapper
 * @param addr     $8000-$FFFF
 * @param val      写入值
 */
static void mapper44_writehigh(ines_mapper_t* p_mapper, ines_word_t addr, ines_byte_t val)
{
	ines_host_t*  p_host = mapper2host(p_mapper);
	SB7_data_t*   p = mapper2SB7data(p_mapper);
	ines_word_t   bank_num;

	if(p == NULL)
		return;

	INES_LOG(LOG_DBG, MOD_MMC, ISTR("%s: $%04X = #$%02X.\n"), __TFUNCTION__, addr, val);

	switch(addr & 0xE001)
	{
	case 0x8000:
		if((p->reg[0] & 0xC0) == (val & 0xC0))
		{
			p->reg[0] = val;      /* 只换了要操作的寄存器编号，bank 无需重算 */
		}
		else
		{
			p->reg[0] = val;
			SB7_set_cpu_bank(p_mapper);
			SB7_set_ppu_bank(p_mapper);
		}
		break;

	case 0x8001:
		bank_num = p->reg[1] = val;
		switch(p->reg[0] & 0x07)
		{
		case 0:
			bank_num &= 0xFE;
			p->chr01 = bank_num;
			SB7_set_ppu_bank(p_mapper);
			break;
		case 1:
			bank_num &= 0xFE;
			p->chr23 = bank_num;
			SB7_set_ppu_bank(p_mapper);
			break;
		case 2:
			p->chr4 = bank_num;
			SB7_set_ppu_bank(p_mapper);
			break;
		case 3:
			p->chr5 = bank_num;
			SB7_set_ppu_bank(p_mapper);
			break;
		case 4:
			p->chr6 = bank_num;
			SB7_set_ppu_bank(p_mapper);
			break;
		case 5:
			p->chr7 = bank_num;
			SB7_set_ppu_bank(p_mapper);
			break;
		case 6:
			p->prg0 = bank_num;
			SB7_set_cpu_bank(p_mapper);
			break;
		case 7:
			p->prg1 = bank_num;
			SB7_set_cpu_bank(p_mapper);
			break;
		}
		break;

	case 0xA000:
		p->reg[2] = val;
		if(p_host->rom.mirror_type != MIRROR_FOUR_SCREEN)
		{
			ines_ppu_set_mirror_type(&p_host->ppu,
									 (val & 0x01) ? MIRROR_HORZ : MIRROR_VERT);
		}
		break;

	case 0xA001:
		p->reg[3] = val;
		/* bit7 = PRG-RAM 使能、bit6 = 写保护（行为同 MMC3）；bit2-0 = 块选择 */
		p->block = (ines_byte_t)(val & 0x07);

		INES_LOG(LOG_DBG, MOD_MMC, ISTR("SB7: $A001 = #$%02X -> block=%d\n"),
				 val, (int)p->block);

		SB7_set_cpu_bank(p_mapper);
		SB7_set_ppu_bank(p_mapper);
		break;

	case 0xC000:
		p->reg[4] = val;
		p->irq_latch = val;
		break;

	case 0xC001:
		p->reg[5] = val;
		p->irq_reload = 1;
		break;

	case 0xE000:
		p->reg[6] = val;
		p->irq_enabled = 0;
		ines_cpu_IRQ(&p_host->cpu, MMC_IRQ_MASK, ines_false);
		break;

	case 0xE001:
		p->reg[7] = val;
		p->irq_enabled = 1;
		break;
	}
}

/**
 * 扫描线计数器 IRQ，与 MMC3 完全相同。
 * @param p_mapper Mapper
 * @param line     扫描线
 */
static void mapper44_hsync(ines_mapper_t* p_mapper, ines_int_t line)
{
	ines_host_t*  p_host = mapper2host(p_mapper);
	SB7_data_t*   p = mapper2SB7data(p_mapper);

	if((p == NULL) || (p->irq_enabled == 0))
		return;

	if((line < 0) || (line > 239))
		return;

	if((p_host->ppu.reg_ctrl_2 & (PPU_ENABLE_BG | PPU_ENABLE_SPR)) == 0)
		return;

	if(p->irq_reload)
	{
		p->irq_reload = 0;
		p->irq_counter = p->irq_latch;
	}
	else
	{
		p->irq_counter--;
	}

	if(p->irq_counter == 0)
	{
		p->irq_reload = 1;
		if(p->irq_enabled)
			ines_cpu_IRQ(&p_host->cpu, MMC_IRQ_MASK, ines_true);
	}
}

static void mapper44_fini(ines_mapper_t* p_mapper)
{
	INES_LOG(LOG_DBG, MOD_MMC, ISTR("mapper44_fini.\n"));
	ines_free(p_mapper->p_data);
	p_mapper->p_data = NULL;
}

ines_bool_t  mapper44_create(ines_mapper_t* p_mapper)
{
	INIT_MAPPER_DATA_ST(p_mapper, SB7_data_t);

	if(mapper2SB7data(p_mapper) == NULL)
		return ines_false;

	p_mapper->reset = mapper44_reset;
	p_mapper->writehigh = mapper44_writehigh;
	p_mapper->hsync = mapper44_hsync;
	p_mapper->fini = mapper44_fini;

	return ines_true;
}

