
/**
 * core/mapper/45.c -- GA23C 多合一（iNES Mapper 045）
 *
 * 硬件要点：Mapper 45 是一块**以 MMC3 为内核、外面再套一层"外层 bank 寄存器"**的多合一卡带
 * （GA23C ASIC 的标准配置）。菜单先写外层寄存器把一大片 PRG/CHR 划成"当前窗口"，
 * 之后 MMC3 各寄存器照常工作，只是它选出的页号要经外层 AND / OR 变换后才落到实际 ROM 上。
 *
 *   - MMC3 部分（$8000-$FFFF，掩码 $E001）**与 MMC3 完全一致**：
 *     $8000/$8001 选择+数据、$A000 镜像、$A001 PRG-RAM 保护、
 *     $C000/$C001 与 $E000/$E001 扫描线 IRQ。
 *
 *   - 外层部分：四个寄存器都写在 $6000，按**写入次序**轮流填入（掩码 $F001）：
 *
 *         第 1 次写 $6000 -> #0，第 2 次 -> #1，第 3 次 -> #2，第 4 次 -> #3，第 5 次又回到 #0
 *         写 $6001（任意值）-> 四个寄存器按软复位清零、解除锁定，下一次写 $6000 回到 #0
 *
 *         #0:  [CCCC CCCC]  CHR-OR 低 8 位（CHR A10-A17）
 *         #1:  [ppPP PPPP]  PRG-OR 低 8 位（PRG A13-A20）
 *         #2:  [PPCC cccc]  bit3-0 = 取自 MMC3 的 CHR 地址线位数
 *                                    （$F: 256KiB、$E: 128KiB …，$7-$0 均为 1KiB）
 *                           bit5-4 = CHR A18-A19
 *                           bit7-6 = CHR A20-A21 **与** PRG A21-A22（共用）
 *         #3:  [1LPP PPPP]  bit5-0 = PRG-AND 掩码（**取反**：$00->512KiB、$20->256KiB …）
 *                           bit6   = Lock：置 1 后 $6000 的写入不再生效，直到写 $6001 解锁
 *                           bit7   = 恒为 1
 *
 *     由此换算出两条变换（PRG 以 8KB 页计、CHR 以 1KB 页计）：
 *
 *         页号 = ((MMC3 页号 AND and) OR or) % 总页数
 *         PRG: and = (~#3) & 0x3F          or = #1 | ((#2 & 0xC0) << 2)
 *         CHR: and = (1 << bits) - 1       or = #0 | ((#2 & 0xF0) << 4)
 *              bits = (#2 & 0x0F) - 7，小于 0 时取 0
 *
 *     MMC3 的两个**固定页**同样要过这道变换：硬件上 MMC3 输出的是 A13-A18 = 111110/111111
 *     （即 0x3E / 0x3F），再被外层 AND/OR 处理，而不是"整卡的最后两页"。
 *   - 外层寄存器**叠在 WRAM 上**，且**不受 MMC3 的 WRAM 使能位控制**：
 *     $6000 的写同时进 WRAM 和寄存器，读回的是 WRAM 值。
 *   - 上电 / 复位：四个外层寄存器与写入次序全部清零。全部为零时 PRG-AND = $3F（最大窗口）、
 *     CHR-AND = 0（CHR 完全由 OR 决定），这正是菜单运行时的映射。
 */

#include "../../comm/idef.h"
#include "../../comm/log.h"
#include "../nes.h"
#include "../mapper.h"


/* 外层寄存器窗口与次序控制 */
#define GA23C_OUTER_ADDR_MASK   0xF001   /* $6000 = 写寄存器，$6001 = 复位并解锁 */
#define GA23C_OUTER_DATA        0x6000
#define GA23C_OUTER_RESET       0x6001
#define GA23C_OUTER_NUM         4        /* 四个外层寄存器，轮流写入 */

/* MMC3 固定页：A13-A18 = 111110 / 111111，再经外层 AND/OR */
#define GA23C_PRG_FIXED_2       0x3E
#define GA23C_PRG_FIXED_1       0x3F

/* #3 的 Lock 位 */
#define GA23C_LOCK_BIT          0x40


struct _GA23C_data_
{
	/* MMC3 内核 */
	ines_byte_t   reg[8];       /* MMC3 的 8 个内部寄存器 */
	ines_word_t   prg0, prg1;
	ines_word_t   chr01, chr23, chr4, chr5, chr6, chr7;
	ines_byte_t   irq_enabled;
	ines_byte_t   irq_counter;
	ines_byte_t   irq_latch;
	ines_byte_t   irq_reload;

	/* GA23C 外层 */
	ines_byte_t   outer[GA23C_OUTER_NUM];
	ines_byte_t   pos;          /* 下一次写 $6000 落到哪个寄存器 */
	ines_byte_t   locked;       /* #3 的 Lock 位置 1 后不再接受 $6000 写入 */
};

typedef struct _GA23C_data_   GA23C_data_t;

#define mapper2GA23Cdata(mapper)   ((GA23C_data_t*)((mapper)->p_data))
#define GA23C_chr_swap(p)          (0 != ((p)->reg[0] & 0x80))
#define GA23C_prg_swap(p)          (0 != ((p)->reg[0] & 0x40))


/**
 * 外层寄存器 #2 的 bit3-0 -> 取自 MMC3 的 CHR 地址线位数（$F = 8 位 = 256KiB）。
 * $7 及以下都是 0 位（CHR 页号完全由 CHR-OR 决定）。
 * @param p 私有数据
 * @return CHR-AND 掩码（作用在 MMC3 的 8 位 CHR 页号上）
 */
static ines_word_t GA23C_chr_and(const GA23C_data_t* p)
{
	ines_int_t   bits = (ines_int_t)(p->outer[2] & 0x0F) - 7;

	if(bits <= 0)
		return 0;

	return (ines_word_t)(((ines_word_t)1 << bits) - 1);
}

/**
 * 把 MMC3 选出的 PRG 页号（8KB 页）映射到当前外层窗口内。结果再对总页数取模，
 * 容量不足的畸形 ROM 也不会越界。
 * @param p_host 宿主
 * @param p      私有数据
 * @param page   MMC3 的页号
 * @return 实际 8KB 页号
 */
static ines_word_t GA23C_prg_page(ines_host_t* p_host, const GA23C_data_t* p, ines_word_t page)
{
	ines_word_t   num = (ines_word_t)p_host->prom_8k_num;
	ines_word_t   and = (ines_word_t)((~(p->outer[3])) & 0x3F);
	ines_word_t   or  = (ines_word_t)(p->outer[1] | (((ines_word_t)(p->outer[2] & 0xC0)) << 2));

	if(num == 0)
		return 0;

	return (ines_word_t)((((page & and) | or)) % num);
}

/**
 * 把 MMC3 选出的 CHR 页号（1KB 页）映射到当前外层窗口内。
 * @param p_host 宿主
 * @param p      私有数据
 * @param page   MMC3 的页号
 * @return 实际 1KB 页号
 */
static ines_word_t GA23C_chr_page(ines_host_t* p_host, const GA23C_data_t* p, ines_word_t page)
{
	ines_word_t   num = (ines_word_t)p_host->vrom_1k_num;
	ines_word_t   or  = (ines_word_t)(p->outer[0] | (((ines_word_t)(p->outer[2] & 0xF0)) << 4));

	if(num == 0)
		return 0;

	return (ines_word_t)((((page & GA23C_chr_and(p)) | or)) % num);
}

/**
 * CPU $8000-$FFFF：8KB×4。两个固定页同样要过外层变换。
 * @param p_mapper Mapper
 */
static void GA23C_set_cpu_bank(ines_mapper_t* p_mapper)
{
	ines_host_t*   p_host = mapper2host(p_mapper);
	GA23C_data_t*  p = mapper2GA23Cdata(p_mapper);

	if((p == NULL) || (p_host->prom_8k_num == 0))
		return;

	if(GA23C_prg_swap(p))
	{
		ines_set_prom_bank_4(p_host,
							 GA23C_prg_page(p_host, p, GA23C_PRG_FIXED_2),
							 GA23C_prg_page(p_host, p, p->prg1),
							 GA23C_prg_page(p_host, p, p->prg0),
							 GA23C_prg_page(p_host, p, GA23C_PRG_FIXED_1));
	}
	else
	{
		ines_set_prom_bank_4(p_host,
							 GA23C_prg_page(p_host, p, p->prg0),
							 GA23C_prg_page(p_host, p, p->prg1),
							 GA23C_prg_page(p_host, p, GA23C_PRG_FIXED_2),
							 GA23C_prg_page(p_host, p, GA23C_PRG_FIXED_1));
	}
}

/**
 * PPU $0000-$1FFF：MMC3 的 8 个 1KB 窗口，全部映射到当前外层窗口内。
 * @param p_mapper Mapper
 */
static void GA23C_set_ppu_bank(ines_mapper_t* p_mapper)
{
	ines_host_t*   p_host = mapper2host(p_mapper);
	GA23C_data_t*  p = mapper2GA23Cdata(p_mapper);

	if(p == NULL)
		return;

	if(p_host->vrom_1k_num > 0)
	{
		if(GA23C_chr_swap(p))
		{
			ines_set_vrom_bank_8(p_host,
								 GA23C_chr_page(p_host, p, p->chr4),
								 GA23C_chr_page(p_host, p, p->chr5),
								 GA23C_chr_page(p_host, p, p->chr6),
								 GA23C_chr_page(p_host, p, p->chr7),
								 GA23C_chr_page(p_host, p, p->chr01),
								 GA23C_chr_page(p_host, p, (ines_word_t)(p->chr01 + 1)),
								 GA23C_chr_page(p_host, p, p->chr23),
								 GA23C_chr_page(p_host, p, (ines_word_t)(p->chr23 + 1)));
		}
		else
		{
			ines_set_vrom_bank_8(p_host,
								 GA23C_chr_page(p_host, p, p->chr01),
								 GA23C_chr_page(p_host, p, (ines_word_t)(p->chr01 + 1)),
								 GA23C_chr_page(p_host, p, p->chr23),
								 GA23C_chr_page(p_host, p, (ines_word_t)(p->chr23 + 1)),
								 GA23C_chr_page(p_host, p, p->chr4),
								 GA23C_chr_page(p_host, p, p->chr5),
								 GA23C_chr_page(p_host, p, p->chr6),
								 GA23C_chr_page(p_host, p, p->chr7));
		}
	}
	else
	{
		/* 该板必带 CHR-ROM；没有就退化成 VRAM，此时外层变换不适用（VRAM 页数太少） */
		if(GA23C_chr_swap(p))
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
 * 复位：MMC3 各寄存器与四个外层寄存器全部清零，写入次序回到 #0、解除锁定。
 * 全部为零时 PRG-AND = $3F、CHR-AND = 0，即菜单运行时的映射。
 * @param p_mapper Mapper
 */
static void mapper45_reset(ines_mapper_t* p_mapper)
{
	ines_host_t*   p_host = mapper2host(p_mapper);
	GA23C_data_t*  p = mapper2GA23Cdata(p_mapper);
	ines_int_t     n;

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
	p->irq_enabled = 0;
	p->irq_counter = 0;
	p->irq_latch = 0;
	p->irq_reload = 0;

	for(n = 0; n < GA23C_OUTER_NUM; n++)
		p->outer[n] = 0;

	p->pos = 0;
	p->locked = 0;

	/* 外层寄存器叠在 WRAM 上：写要同时进 WRAM 与寄存器，故拦截 $6000 段的默认写，
	   由 mapper45_writelow 两边都写。读仍走 mem_bank[3]（写保护不影响读）。 */
	ines_set_sram_bank_n(p_host, 3, 0);
	p_host->cpu.bank_writeable[3] = NES_BANK_WRITE_PROTECTED;

	GA23C_set_cpu_bank(p_mapper);
	GA23C_set_ppu_bank(p_mapper);

	INES_LOG(LOG_INF, MOD_MMC, ISTR("mapper45: GA23C multicart (PRG %dKB, CHR %dKB)\n"),
			 (int)(p_host->prom_8k_num * 8), (int)p_host->vrom_1k_num);
}

/**
 * $4020-$7FFF 的写入：$6000 按次序填外层寄存器、$6001 复位并解锁，其余交给 WRAM。
 * 外层寄存器叠在 WRAM 上，所以写**同时**进 WRAM（读回的是 WRAM 值）。
 * @param p_mapper Mapper
 * @param addr     $4020-$7FFF
 * @param val      写入值
 */
static void mapper45_writelow(ines_mapper_t* p_mapper, ines_word_t addr, ines_byte_t val)
{
	ines_host_t*   p_host = mapper2host(p_mapper);
	GA23C_data_t*  p = mapper2GA23Cdata(p_mapper);

	if(p == NULL)
		return;

	/* 叠在 WRAM 上：无论是不是寄存器地址，写都要落到 SRAM（$6000-$7FFF 共 8KB） */
	if(addr >= 0x6000)
	{
		p_host->SRAM[addr & 0x1FFF] = val;
		p_host->SRAM_write_flag = 1;
	}

	switch(addr & GA23C_OUTER_ADDR_MASK)
	{
	case GA23C_OUTER_DATA:
		if(p->locked)
			break;                       /* 已锁定：$6000 的写入不再生效 */

		p->outer[p->pos & 0x03] = val;

		INES_LOG(LOG_DBG, MOD_MMC,
				 ISTR("GA23C: outer #%d = #$%02X (prg_and=$%02X chr_and=$%02X)\n"),
				 (int)(p->pos & 0x03), (int)val,
				 (int)((~p->outer[3]) & 0x3F), (int)GA23C_chr_and(p));

		/* 写 #3 时若 Lock 位置 1，则此后的写入全部忽略，直到 $6001 解锁 */
		if(((p->pos & 0x03) == 3) && (0 != (val & GA23C_LOCK_BIT)))
			p->locked = 1;

		p->pos = (ines_byte_t)((p->pos + 1) & 0x03);

		GA23C_set_cpu_bank(p_mapper);
		GA23C_set_ppu_bank(p_mapper);
		break;

	case GA23C_OUTER_RESET:
		p->outer[0] = 0;
		p->outer[1] = 0;
		p->outer[2] = 0;
		p->outer[3] = 0;
		p->pos = 0;
		p->locked = 0;

		INES_LOG(LOG_DBG, MOD_MMC, ISTR("GA23C: outer reset (unlock)\n"));

		GA23C_set_cpu_bank(p_mapper);
		GA23C_set_ppu_bank(p_mapper);
		break;

	default:
		break;                           /* $4020-$5FFF、$6002-$7FFF：只有 WRAM */
	}
}

/**
 * $8000-$FFFF 写入：与 MMC3 完全一致（$A001 在 45 上就是普通的 PRG-RAM 保护，
 * 不像 mapper 44 那样承载块选择）。
 * @param p_mapper Mapper
 * @param addr     $8000-$FFFF
 * @param val      写入值
 */
static void mapper45_writehigh(ines_mapper_t* p_mapper, ines_word_t addr, ines_byte_t val)
{
	ines_host_t*   p_host = mapper2host(p_mapper);
	GA23C_data_t*  p = mapper2GA23Cdata(p_mapper);
	ines_word_t    bank_num;

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
			GA23C_set_cpu_bank(p_mapper);
			GA23C_set_ppu_bank(p_mapper);
		}
		break;

	case 0x8001:
		bank_num = p->reg[1] = val;
		switch(p->reg[0] & 0x07)
		{
		case 0:
			bank_num &= 0xFE;
			p->chr01 = bank_num;
			GA23C_set_ppu_bank(p_mapper);
			break;
		case 1:
			bank_num &= 0xFE;
			p->chr23 = bank_num;
			GA23C_set_ppu_bank(p_mapper);
			break;
		case 2:
			p->chr4 = bank_num;
			GA23C_set_ppu_bank(p_mapper);
			break;
		case 3:
			p->chr5 = bank_num;
			GA23C_set_ppu_bank(p_mapper);
			break;
		case 4:
			p->chr6 = bank_num;
			GA23C_set_ppu_bank(p_mapper);
			break;
		case 5:
			p->chr7 = bank_num;
			GA23C_set_ppu_bank(p_mapper);
			break;
		case 6:
			p->prg0 = bank_num;
			GA23C_set_cpu_bank(p_mapper);
			break;
		case 7:
			p->prg1 = bank_num;
			GA23C_set_cpu_bank(p_mapper);
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
		p->reg[3] = val;      /* PRG-RAM 保护，行为同 MMC3；外层寄存器不受它影响 */
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
static void mapper45_hsync(ines_mapper_t* p_mapper, ines_int_t line)
{
	ines_host_t*   p_host = mapper2host(p_mapper);
	GA23C_data_t*  p = mapper2GA23Cdata(p_mapper);

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

static void mapper45_fini(ines_mapper_t* p_mapper)
{
	INES_LOG(LOG_DBG, MOD_MMC, ISTR("mapper45_fini.\n"));
	ines_free(p_mapper->p_data);
	p_mapper->p_data = NULL;
}

ines_bool_t  mapper45_create(ines_mapper_t* p_mapper)
{
	INIT_MAPPER_DATA_ST(p_mapper, GA23C_data_t);

	if(mapper2GA23Cdata(p_mapper) == NULL)
		return ines_false;

	p_mapper->reset = mapper45_reset;
	p_mapper->writelow = mapper45_writelow;
	p_mapper->writehigh = mapper45_writehigh;
	p_mapper->hsync = mapper45_hsync;
	p_mapper->fini = mapper45_fini;

	return ines_true;
}

