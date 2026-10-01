
/**
 * core/mapper/57.c -- GK 47-in-1 / SuperGK 6-in-1（iNES Mapper 057）
 *
 * 硬件要点（基于 Disch 的原始笔记）：
 *  - 寄存器区间 $8000-$FFFF，掩码 $8800（只按 A11 分成两组）：
 *      $8000  [CH.. ..AA]
 *              C = CHR Mode（0 = CNROM 模式，1 = NROM 模式）
 *              H = CHR A16
 *              A = CNROM 模式下的 CHR A13-A14（仅 C=0 时有效）
 *      $8800  [PPPO MBbb]
 *              P = PRG Reg（3 位）
 *              O = PRG Mode（0 = 16KB ×2 镜像，1 = 32KB）
 *              M = 镜像（0 垂直 / 1 水平）
 *              B = CHR A15
 *              b = NROM 模式下的 CHR A13-A14（仅 C=1 时有效）
 *  - CHR：整块 8KB 一起切换，bank 号为
 *              C=0 -> H B A A      （低 2 位取 $8000 的 bit1-0）
 *              C=1 -> H B b b      （低 2 位取 $8800 的 bit1-0）
 *    即 8KB bank = (H << 3) | (B << 2) | 低 2 位，其中 H = A16、B = A15。
 *  - PRG：
 *      Mode 0（O=0）：两个 16KB 窗口都用 PRG Reg，即 16KB bank 同时出现在
 *                     $8000-$BFFF 与 $C000-$FFFF（16KB 游戏靠这个镜像跑起来）。
 *      Mode 1（O=1）：整个 32KB 窗口 = 32KB bank #PRG Reg。
 *  - 无 IRQ，无 PRG-RAM。
 *
 * 关于 PRG Mode 0 / 1 的粒度：Disch 原注把 Mode 0 画成两个 16KB 格、都标 $8800，
 * Mode 1 画成单独一个 32KB 格、标 <$8800>，故 Mode 0 实现为"16KB bank 同时出现
 * 在两个窗口"、Mode 1 实现为"32KB bank = PRG Reg"。
 *
 * 实测（6in1_SuperGK-L02A.nes / 6in1.nes / 54in1.nes，均为 128KB PRG + 128KB CHR）：
 * 三个 ROM 的菜单都完全正常，SuperGK 进游戏时写入 $8800 = #$22（PRG Reg = 1、
 * Mode 0），随后游戏正常运行 —— Mode 0 的解读由此得到确认。
 * 注意：手头三个 ROM 都只用到 Mode 0，**Mode 1（32KB）未被任何实测 ROM 触发**，
 * 它的"32KB bank = PRG Reg"是按记法推断的；若日后遇到走 Mode 1 的卡带黑屏，
 * 优先怀疑这里应改为 (PRG Reg >> 1)。
 */

#include "../../comm/idef.h"
#include "../../comm/log.h"
#include "../nes.h"
#include "../mapper.h"


/* $8000：[CH.. ..AA] */
#define GK57_CHR_MODE_BIT     0x80    /* C */
#define GK57_CHR_A16_BIT      0x40    /* H */
#define GK57_CHR_LOW_MASK     0x03    /* AA */

/* $8800：[PPPO MBbb] */
#define GK57_PRG_REG_SHIFT    5
#define GK57_PRG_REG_MASK     0x07    /* PPP */
#define GK57_PRG_MODE_BIT     0x10    /* O */
#define GK57_MIRROR_BIT       0x08    /* M */
#define GK57_CHR_A15_BIT      0x04    /* B */
#define GK57_CHR_LOW_NROM     0x03    /* bb */


struct _GK57_data_
{
	ines_byte_t   chr_reg;     /* $8000 锁存值 */
	ines_byte_t   prg_reg;     /* $8800 锁存值 */
};

typedef struct _GK57_data_   GK57_data_t;

#define mapper2GK57data(mapper)   ((GK57_data_t*)((mapper)->p_data))


/**
 * 按 PRG Reg 与 PRG Mode 刷新 16KB/32KB 窗口。
 * @param p      私有数据
 * @param p_host 宿主
 */
static void GK57_set_cpu_bank(GK57_data_t* p, ines_host_t* p_host)
{
	ines_word_t  num = (ines_word_t)((p_host->prom_8k_num > 0) ? p_host->prom_8k_num : 1);
	ines_word_t  reg = (ines_word_t)((p->prg_reg >> GK57_PRG_REG_SHIFT) & GK57_PRG_REG_MASK);
	ines_word_t  b0, b1, b2, b3;

	if((p->prg_reg & GK57_PRG_MODE_BIT) != 0)
	{
		/* Mode 1：32KB 窗口 = 32KB bank #PRG Reg（一个 32KB bank = 4 个 8KB 页） */
		b0 = (ines_word_t)((reg * 4 + 0) % num);
		b1 = (ines_word_t)((reg * 4 + 1) % num);
		b2 = (ines_word_t)((reg * 4 + 2) % num);
		b3 = (ines_word_t)((reg * 4 + 3) % num);
	}
	else
	{
		/* Mode 0：16KB bank #PRG Reg 同时出现在两个 16KB 窗口 */
		b0 = (ines_word_t)((reg * 2 + 0) % num);
		b1 = (ines_word_t)((reg * 2 + 1) % num);
		b2 = b0;
		b3 = b1;
	}

	ines_set_prom_bank_4(p_host, b0, b1, b2, b3);
}

/**
 * 按 CHR Mode 合成 8KB CHR bank 并整块切换。
 * @param p      私有数据
 * @param p_host 宿主
 */
static void GK57_set_ppu_bank(GK57_data_t* p, ines_host_t* p_host)
{
	ines_word_t  num_8k = (ines_word_t)(p_host->vrom_1k_num >> 3);
	ines_word_t  bank;
	ines_word_t  low;

	if(num_8k == 0)
	{
		/* 无 CHR-ROM：8KB 的 pattern RAM 只有一页 */
		ines_set_vram_bank_n(p_host, 0, 0);
		ines_set_vram_bank_n(p_host, 1, 1);
		ines_set_vram_bank_n(p_host, 2, 2);
		ines_set_vram_bank_n(p_host, 3, 3);
		ines_set_vram_bank_n(p_host, 4, 4);
		ines_set_vram_bank_n(p_host, 5, 5);
		ines_set_vram_bank_n(p_host, 6, 6);
		ines_set_vram_bank_n(p_host, 7, 7);
		return;
	}

	/* 低 2 位：C=0 取 $8000 的 AA，C=1 取 $8800 的 bb */
	low = (ines_word_t)(((p->chr_reg & GK57_CHR_MODE_BIT) != 0)
						? (p->prg_reg & GK57_CHR_LOW_NROM)
						: (p->chr_reg & GK57_CHR_LOW_MASK));

	bank = (ines_word_t)((((p->chr_reg & GK57_CHR_A16_BIT) != 0) ? 8 : 0)
						 | (((p->prg_reg & GK57_CHR_A15_BIT) != 0) ? 4 : 0)
						 | low);

	bank = (ines_word_t)(bank % num_8k);

	ines_set_vrom_bank_8(p_host,
						 (ines_word_t)(bank * 8 + 0), (ines_word_t)(bank * 8 + 1),
						 (ines_word_t)(bank * 8 + 2), (ines_word_t)(bank * 8 + 3),
						 (ines_word_t)(bank * 8 + 4), (ines_word_t)(bank * 8 + 5),
						 (ines_word_t)(bank * 8 + 6), (ines_word_t)(bank * 8 + 7));
}

/**
 * 按 $8800 的 bit3 应用镜像；四屏卡带由硬件决定，Mapper 不得改写。
 * @param p      私有数据
 * @param p_host 宿主
 */
static void GK57_set_mirror(GK57_data_t* p, ines_host_t* p_host)
{
	if(p_host->rom.mirror_type == MIRROR_FOUR_SCREEN)
		return;

	ines_ppu_set_mirror_type(&p_host->ppu, (p->prg_reg & GK57_MIRROR_BIT) ? MIRROR_HORZ : MIRROR_VERT);
}

static void mapper57_reset(ines_mapper_t* p_mapper)
{
	ines_host_t*  p_host = mapper2host(p_mapper);
	GK57_data_t*  p = mapper2GK57data(p_mapper);

	if(p == NULL)
		return;

	p->chr_reg = 0;
	p->prg_reg = 0;

	GK57_set_cpu_bank(p, p_host);
	GK57_set_ppu_bank(p, p_host);
	GK57_set_mirror(p, p_host);

	INES_LOG(LOG_INF, MOD_MMC, ISTR("mapper57: GK 47-in-1 / SuperGK (PRG %dKB, CHR %dKB)\n"),
			 (int)(p_host->prom_8k_num * 8), (int)p_host->vrom_1k_num);
}

static void mapper57_writehigh(ines_mapper_t* p_mapper, ines_word_t addr, ines_byte_t val)
{
	ines_host_t*  p_host = mapper2host(p_mapper);
	GK57_data_t*  p = mapper2GK57data(p_mapper);

	if(p == NULL)
		return;

	INES_LOG(LOG_DBG, MOD_MMC, ISTR("GK57: write $%04X = #$%02X\n"), addr, val);

	switch(addr & 0x8800)
	{
	case 0x8000:
		p->chr_reg = val;
		GK57_set_ppu_bank(p, p_host);
		break;

	case 0x8800:
		p->prg_reg = val;
		GK57_set_cpu_bank(p, p_host);
		GK57_set_ppu_bank(p, p_host);
		GK57_set_mirror(p, p_host);
		break;

	default:
		break;
	}
}

static void mapper57_fini(ines_mapper_t* p_mapper)
{
	INES_LOG(LOG_DBG, MOD_MMC, ISTR("mapper57_fini.\n"));
	ines_free(p_mapper->p_data);
	p_mapper->p_data = NULL;
}

ines_bool_t  mapper57_create(ines_mapper_t* p_mapper)
{
	INIT_MAPPER_DATA_ST(p_mapper, GK57_data_t);

	if(mapper2GK57data(p_mapper) == NULL)
		return ines_false;

	p_mapper->reset = mapper57_reset;
	p_mapper->writehigh = mapper57_writehigh;
	p_mapper->fini = mapper57_fini;

	return ines_true;
}

