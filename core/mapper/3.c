/**
 * core/mapper/3.c -- CNROM（iNES Mapper 003）
 *
 * 硬件要点（资料：NESDev "CNROM"）：
 *   - 任天堂的 CNROM 板，用一片 74HC161 锁存器选 CHR 页；PRG 固定、无 IRQ、无 PRG-RAM。
 *   - PRG：16KB 或 32KB 固定。32KB 直接线性落到 $8000-$FFFF；16KB 则镜像两份。
 *   - CHR：PPU $0000-$1FFF 是一个 **8KB** 窗口，写 $8000-$FFFF 的**任意地址**即切换
 *     （低位不译码）；页号 = 写入值低位（多数板用 bit1-0，部分板用 bit2-0）。
 *   - 镜像由**焊盘**固定（H/V），mapper 无法控制 → 沿用 ROM 头的设置，不去改。
 *   - 存在 AND 型总线冲突（写入值与 PRG 该地址的内容相与），模拟器一般不模拟，
 *     本项目同样忽略。
 *
 * 上电：CHR 8KB 页 0、PRG 全映射。
 *
 * 页号回卷必须用**取模**而不是位与掩码：CHR 容量不是 2 的幂时（例如 24KB），
 * 掩码回卷后仍可能 >= 实际页数，而 ines_set_vrom_bank_8() 对越界页号的处理是
 * 打 ERR 日志并**整次放弃切换**（而非取模修正），会导致 8 个 CHR 页一个都不换。
 */

#include "../../comm/idef.h"
#include "../../comm/log.h"
#include "../nes.h"
#include "../mapper.h"


/**
 * 把 PPU $0000-$1FFF 挂成卡带的 8KB CHR 页（无 CHR-ROM 时挂 pattern RAM）。
 * @param p_host 宿主
 * @param bank   8KB CHR 页号；无 CHR-ROM 时忽略
 */
static void M3_apply(ines_host_t* p_host, ines_word_t bank)
{
	ines_word_t  chr_8k_num;
	ines_word_t  base;
	ines_int_t   n;

	chr_8k_num = (ines_word_t)(p_host->vrom_1k_num >> 3);

	if(chr_8k_num == 0)
	{
		/* 无 CHR-ROM（清单标错或 CHR 实为 RAM）：只有 8KB pattern RAM，切页无意义 */
		for(n = 0; n < 8; n++)
		{
			ines_set_vram_bank_n(p_host, (ines_word_t)n, (ines_word_t)n);
		}
		return;
	}

	/* 按实际 8KB 页数取模回卷，页数非 2 的幂时也安全 */
	bank = (ines_word_t)(bank % chr_8k_num);
	base = (ines_word_t)(bank << 3);

	ines_set_vrom_bank_8(p_host, base, (ines_word_t)(base + 1),
						 (ines_word_t)(base + 2), (ines_word_t)(base + 3),
						 (ines_word_t)(base + 4), (ines_word_t)(base + 5),
						 (ines_word_t)(base + 6), (ines_word_t)(base + 7));
}

static void mapper3_reset(ines_mapper_t* p_mapper)
{
	ines_host_t*  p_host = mapper2host(p_mapper);

	if(p_host == NULL)
		return;

	/* PRG 固定：32KB 线性映射；16KB 镜像两份；更小的（清单标错）按 8KB 镜像四份 */
	if(p_host->prom_8k_num > 2)
	{
		ines_set_prom_bank_4(p_host, 0,1,2,3);
	}
	else if(p_host->prom_8k_num == 2)
	{
		ines_set_prom_bank_4(p_host, 0,1,0,1);
	}
	else
	{
		ines_set_prom_bank_4(p_host, 0,0,0,0);
	}

	M3_apply(p_host, 0);
}

static void mapper3_writehigh(ines_mapper_t* p_mapper, ines_word_t addr, ines_byte_t val)
{
	ines_host_t*  p_host = mapper2host(p_mapper);

	if(p_host == NULL)
		return;

	INES_LOG(LOG_DBG, MOD_MMC, ISTR("M03: $%04X = #$%02X.\n"), (ines_int_t)addr, (ines_int_t)val);

	/* $8000-$FFFF 任意地址都锁存，低位不译码 */
	M3_apply(p_host, (ines_word_t)val);
}


ines_bool_t  mapper3_create(ines_mapper_t* p_mapper)
{
	if(p_mapper == NULL)
		return ines_false;

	p_mapper->reset = mapper3_reset;
	p_mapper->writehigh = mapper3_writehigh;

	return ines_true;
}



