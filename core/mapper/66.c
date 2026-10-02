
/**
 * core/mapper/66.c -- GxROM（NES-GNROM / NES-MHROM，iNES Mapper 066）
 *
 * 硬件要点（资料：NESDev "GxROM"，IMA 笔记「GxROM」）：
 *   - 任天堂的 GNROM / MHROM 板（及 HVC 对应板），用一片 74HC161 二进制计数器当
 *     4 位 D 锁存器来选页，没有 IRQ、没有 PRG-RAM。
 *   - 窗口：CPU $8000-$FFFF 是一个 **32KB** PRG 窗口；PPU $0000-$1FFF 是一个
 *     **8KB** CHR 窗口。两者由同一次写入同时切换。
 *   - 寄存器在 $8000-$FFFF 的**任意地址**（低位不译码）：
 *       [xxPP xxCC]    bit4-5 = 32KB PRG 页，bit0-1 = 8KB CHR 页。
 *     资料标注 bit2/3/6/7 未使用，但 74HC377 的 oversize 变体可达 512KB PRG /
 *     128KB CHR，故这里按整个 nibble 取值，再按卡带实际页数回卷
 *     （清单里 Bio Senshi Dan、Thunder & Lightning 就是 128KB CHR 的卡带）。
 *   - 镜像由**焊盘**固定（H/V），mapper 无法控制 → 沿用 ROM 头的设置，不去改。
 *   - 存在总线冲突（写寄存器时 PRG ROM 同时驱动数据线，实际写入值是两者的 AND），
 *     模拟器通常不模拟，本项目同样忽略。
 *   - $6000-$7FFF 的写入**不**切页：那是 mapper 140 的位置。VirtuaNES 的
 *     Mapper066::WriteLow 会响应 $6000 以上写入，与 NESDev 资料不符，未照抄。
 *
 * 上电：74HC161 清零 → PRG 32KB 页 0 + CHR 8KB 页 0。
 */

#include "../../comm/idef.h"
#include "../../comm/log.h"
#include "../nes.h"
#include "../mapper.h"


/** 写入值里 PRG 页字段的位偏移（高 4 位） */
#define M66_PRG_SHIFT   4

/** 页号字段掩码（各取 4 位，兼容 oversize 变体） */
#define M66_BANK_MASK   0x0F


/**
 * 按写入值同时切换 32KB PRG 与 8KB CHR 窗口。
 * @param p_host 宿主
 * @param val    写入值：高 4 位 = PRG 32KB 页号，低 4 位 = CHR 8KB 页号
 */
static void M66_apply(ines_host_t* p_host, ines_byte_t val)
{
	ines_word_t  prg_num;   /* 32KB 页数（4 个 8KB 页） */
	ines_word_t  chr_num;   /* 8KB 页数（8 个 1KB 页） */
	ines_word_t  bank;
	ines_word_t  base;
	ines_int_t   n;

	prg_num = (ines_word_t)(p_host->prom_8k_num >> 2);
	if(prg_num == 0)
		prg_num = 1;

	bank = (ines_word_t)(((val >> M66_PRG_SHIFT) & M66_BANK_MASK) % prg_num);
	base = (ines_word_t)(bank << 2);

	ines_set_prom_bank_4(p_host, base, (ines_word_t)(base + 1),
						 (ines_word_t)(base + 2), (ines_word_t)(base + 3));

	chr_num = (ines_word_t)(p_host->vrom_1k_num >> 3);
	if(chr_num == 0)
	{
		/* 无 CHR-ROM：卡带上只有 8KB 的 pattern RAM，页号由 ines_set_vram_bank_n() 内部回卷 */
		for(n = 0; n < 8; n++)
		{
			ines_set_vram_bank_n(p_host, (ines_word_t)n, (ines_word_t)n);
		}
		return;
	}

	bank = (ines_word_t)((val & M66_BANK_MASK) % chr_num);
	base = (ines_word_t)(bank << 3);

	ines_set_vrom_bank_8(p_host, base, (ines_word_t)(base + 1),
						 (ines_word_t)(base + 2), (ines_word_t)(base + 3),
						 (ines_word_t)(base + 4), (ines_word_t)(base + 5),
						 (ines_word_t)(base + 6), (ines_word_t)(base + 7));
}

static void mapper66_reset(ines_mapper_t* p_mapper)
{
	ines_host_t*  p_host = mapper2host(p_mapper);
	ines_word_t   prg_num;

	if(p_host == NULL)
		return;

	/* 上电：PRG 取**最后一个** 32KB 页 + CHR 8KB 页 0。
	   资料没有规定 74HC161 的上电值，但实测这些卡的向量都在最后一个 32KB 页里
	   （Bio Senshi Dan / CASTDRAG / CONAN 的第 0 页末尾是无效向量，按页 0 上电会直接跑飞，
	     表现为"只写了一次 $8000 就死"）；VirtuaNES 也是让 $C000-$FFFF 落在最后 16KB。
	   镜像由焊盘固定（H/V），这里沿用 ROM 头的设置，不做改动 */
	prg_num = (ines_word_t)(p_host->prom_8k_num >> 2);
	if(prg_num == 0)
		prg_num = 1;

	M66_apply(p_host, (ines_byte_t)((prg_num - 1) << M66_PRG_SHIFT));

	INES_LOG(LOG_INF, MOD_MMC, ISTR("mapper66: GxROM (PRG %dKB, CHR %dKB)\n"),
			 (ines_int_t)(p_host->prom_8k_num * 8), (ines_int_t)p_host->vrom_1k_num);
}

static void mapper66_writehigh(ines_mapper_t* p_mapper, ines_word_t addr, ines_byte_t val)
{
	ines_host_t*  p_host = mapper2host(p_mapper);

	if(p_host == NULL)
		return;

	INES_LOG(LOG_DBG, MOD_MMC, ISTR("M66: $%04X = #$%02X.\n"), (ines_int_t)addr, (ines_int_t)val);

	/* $8000-$FFFF 任意地址都锁存，低位不译码 */
	M66_apply(p_host, val);
}

ines_bool_t  mapper66_create(ines_mapper_t* p_mapper)
{
	if(p_mapper == NULL)
		return ines_false;

	p_mapper->reset = mapper66_reset;
	p_mapper->writehigh = mapper66_writehigh;

	return ines_true;
}

