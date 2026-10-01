
/**
 * core/mapper/58.c -- 简单 NROM/CNROM 型多合一（iNES Mapper 058）
 *
 * 用于 21-in-1 (AS-5321)、50-in-1 (WQ1806 B)、55-in-1 (WQ2006 B)、68-in-1 (HKX5268)、
 * 75-in-1 (WQ1905 E)、86-in-1 (AP-5486)、92-in-1 (WQ1605 E)、97-in-1 (WQ1708 B) 等卡带。
 *
 * 硬件要点：**Address Latch** —— bank 号由"写到哪个地址"决定，写入的数据只用来选镜像。
 * （与 mapper 41 / 225 / 255 同族，别按数据线锁存器去实现。）
 *
 *   $8000-$FFFF 写，地址位：
 *       A6      PRG Mode：0 = NROM-256，1 = NROM-128
 *       A5..A3  CHR A15..A13（8KB CHR bank）
 *       A2..A0  PRG A16..A14
 *     数据位：
 *       D1      镜像：1 = 垂直，0 = 水平
 *
 *  - PRG：
 *      A6=0（NROM-256）：整个 32KB 窗口 = 32KB bank #((A2..A1) >> 1)，A0 无用
 *                        （PRG A14 直接跟 CPU A14）。
 *      A6=1（NROM-128）：16KB bank #(A2..A0) 镜像到 $8000-$BFFF 与 $C000-$FFFF。
 *  - CHR：整块 8KB 切换，bank = A5..A3。
 *  - 无 IRQ，无 PRG-RAM，无状态寄存器（每次写都把三个窗口全部重设）。
 *  - 上电：16KB bank 0 镜像（8KB 页 0,1,0,1）+ CHR bank 0。
 *  - iNES Mapper 213 是本编号的重复（多见于带菜单音乐的 168-in-1 / 9999999-in-1）。
 *
 * 实现参照 VirtuaNES 0.97 的 NES/Mapper/Mapper058.cpp。
 */

#include "../../comm/idef.h"
#include "../../comm/log.h"
#include "../nes.h"
#include "../mapper.h"


/* 地址位 */
#define M58_PRG_MODE_ADDR   0x40    /* A6：0 = NROM-256，1 = NROM-128 */
#define M58_CHR_SHIFT       3
#define M58_CHR_MASK        0x07    /* A5..A3 */
#define M58_PRG_16K_MASK    0x07    /* A2..A0 */
#define M58_PRG_32K_MASK    0x06    /* A2..A1 */

/* 数据位 */
#define M58_MIRROR_DATA     0x02    /* D1：1 = 垂直，0 = 水平 */


/**
 * 按地址译码刷新 PRG 窗口。
 * @param addr   写入地址
 * @param p_host 宿主
 */
static void M58_set_cpu_bank(ines_word_t addr, ines_host_t* p_host)
{
	ines_word_t  num = (ines_word_t)((p_host->prom_8k_num > 0) ? p_host->prom_8k_num : 1);
	ines_word_t  b0, b1, b2, b3;

	if((addr & M58_PRG_MODE_ADDR) != 0)
	{
		/* NROM-128：16KB bank 镜像到两个 16KB 窗口（一个 16KB bank = 2 个 8KB 页） */
		ines_word_t  reg = (ines_word_t)(addr & M58_PRG_16K_MASK);

		b0 = (ines_word_t)((reg * 2 + 0) % num);
		b1 = (ines_word_t)((reg * 2 + 1) % num);
		b2 = b0;
		b3 = b1;
	}
	else
	{
		/* NROM-256：整个 32KB 窗口 = 32KB bank #(A2..A1 >> 1)（一个 32KB bank = 4 个 8KB 页） */
		ines_word_t  reg = (ines_word_t)((addr & M58_PRG_32K_MASK) >> 1);

		b0 = (ines_word_t)((reg * 4 + 0) % num);
		b1 = (ines_word_t)((reg * 4 + 1) % num);
		b2 = (ines_word_t)((reg * 4 + 2) % num);
		b3 = (ines_word_t)((reg * 4 + 3) % num);
	}

	ines_set_prom_bank_4(p_host, b0, b1, b2, b3);
}

/**
 * 按 A5..A3 整块切换 8KB CHR。
 * @param addr   写入地址
 * @param p_host 宿主
 */
static void M58_set_ppu_bank(ines_word_t addr, ines_host_t* p_host)
{
	ines_word_t  num_8k = (ines_word_t)(p_host->vrom_1k_num >> 3);
	ines_word_t  bank;

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

	bank = (ines_word_t)(((addr >> M58_CHR_SHIFT) & M58_CHR_MASK) % num_8k);

	ines_set_vrom_bank_8(p_host,
						 (ines_word_t)(bank * 8 + 0), (ines_word_t)(bank * 8 + 1),
						 (ines_word_t)(bank * 8 + 2), (ines_word_t)(bank * 8 + 3),
						 (ines_word_t)(bank * 8 + 4), (ines_word_t)(bank * 8 + 5),
						 (ines_word_t)(bank * 8 + 6), (ines_word_t)(bank * 8 + 7));
}

/**
 * 按数据 bit1 应用镜像；四屏卡带由硬件决定，Mapper 不得改写。
 * @param val    写入数据
 * @param p_host 宿主
 */
static void M58_set_mirror(ines_byte_t val, ines_host_t* p_host)
{
	if(p_host->rom.mirror_type == MIRROR_FOUR_SCREEN)
		return;

	ines_ppu_set_mirror_type(&p_host->ppu, (val & M58_MIRROR_DATA) ? MIRROR_VERT : MIRROR_HORZ);
}

static void mapper58_reset(ines_mapper_t* p_mapper)
{
	ines_host_t*  p_host = mapper2host(p_mapper);

	/* 上电：16KB bank 0 镜像到 $8000-$BFFF 与 $C000-$FFFF */
	ines_word_t  num = (ines_word_t)((p_host->prom_8k_num > 0) ? p_host->prom_8k_num : 1);

	ines_set_prom_bank_4(p_host,
						 (ines_word_t)(0 % num), (ines_word_t)(1 % num),
						 (ines_word_t)(0 % num), (ines_word_t)(1 % num));

	M58_set_ppu_bank(0, p_host);
	M58_set_mirror(0, p_host);

	INES_LOG(LOG_INF, MOD_MMC, ISTR("mapper58: NROM/CNROM multicart, address latch (PRG %dKB, CHR %dKB)\n"),
			 (int)(p_host->prom_8k_num * 8), (int)p_host->vrom_1k_num);
}

static void mapper58_writehigh(ines_mapper_t* p_mapper, ines_word_t addr, ines_byte_t val)
{
	ines_host_t*  p_host = mapper2host(p_mapper);

	INES_LOG(LOG_DBG, MOD_MMC, ISTR("M58: write $%04X = #$%02X\n"), addr, val);

	M58_set_cpu_bank(addr, p_host);
	M58_set_ppu_bank(addr, p_host);
	M58_set_mirror(val, p_host);
}

ines_bool_t  mapper58_create(ines_mapper_t* p_mapper)
{
	p_mapper->reset = mapper58_reset;
	p_mapper->writehigh = mapper58_writehigh;

	return ines_true;
}

