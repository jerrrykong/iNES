
/**
 * core/mapper/60.c -- Reset-based NROM-128 4-in-1 多合一（iNES Mapper 060）
 *
 * 硬件要点：卡带里是四个（或若干个）**各自独立的 NROM-128 游戏**，每块 16KB PRG + 8KB CHR。
 * 16KB PRG 同时出现在 $8000-$BFFF 与 $C000-$FFFF（NROM-128 的镜像），CHR 是整块 8KB。
 *
 * 本 mapper 最特殊的一点：**没有任何 bank 寄存器**，当前块由一个片内计数器决定，
 * 而该计数器**只能靠软复位（按 Reset）递增** —— 要换游戏就得复位，这也是
 * "Reset Based 4-in-1" 名字的由来。Disch 推测该计数器是 2 位宽（对应 4 个块）。
 *
 *  - PRG：16KB 块 #N → 8KB 页 2N、2N+1，镜像到 $8000 与 $C000 两个窗口。
 *  - CHR：8KB 块 #N → 1KB 页 8N .. 8N+7（整块切换）；卡带用 CHR-RAM 时不切换。
 *  - 块数 = min(PRG 的 16KB 块数, CHR 的 8KB 块数)，计数器对块数取模（4-in-1 时即 2 位）。
 *  - 无 IRQ、无 PRG-RAM、无 $8000+ 写寄存器（写操作仅记日志，便于诊断误标 ROM）。
 *  - 镜像由卡带硬线决定，mapper 不干预（宿主已按 ROM 头设置过）。
 *  - 上电（加载 ROM 后的第一次复位）停在块 0，之后每次复位 +1。
 *
 * 注意：FCEUX 等模拟器把 T3H53 放在 iNES Mapper 060、把本卡带挤走，
 * 因此"标着 60 但不是 reset-based 多合一"的 ROM 实际属于 mapper 59。
 */

#include "../../comm/idef.h"
#include "../../comm/log.h"
#include "../nes.h"
#include "../mapper.h"


/** Mapper 60 私有数据 */
typedef struct _ines_M60_
{
	ines_byte_t  block;    /* 当前块号（计数器） */
	ines_byte_t  started;  /* 非 0：已经过上电后的第一次复位 */
} M60_data_t;

#define mapper2M60data(p_mapper)  ((M60_data_t*)((p_mapper)->p_data))


/**
 * 求卡带里的块数：PRG 与 CHR 中较小的那个决定能切出几个完整块。
 * @param p_host 宿主
 * @return 块数（至少为 1）
 */
static ines_word_t M60_block_num(ines_host_t* p_host)
{
	ines_word_t  prg = (ines_word_t)(p_host->prom_8k_num >> 1);  /* 16KB 块数 */
	ines_word_t  chr = (ines_word_t)(p_host->vrom_1k_num >> 3);  /* 8KB CHR 块数 */

	if(prg == 0)
		prg = 1;

	if(chr == 0)
		return prg;  /* CHR-RAM 卡带：块数只由 PRG 决定 */

	return (prg < chr) ? prg : chr;
}

/**
 * 把 16KB 块 #block 镜像到 $8000-$BFFF 与 $C000-$FFFF。
 * @param p_host 宿主
 * @param block  块号（已取模）
 */
static void M60_set_cpu_bank(ines_host_t* p_host, ines_word_t block)
{
	ines_word_t  num = (ines_word_t)((p_host->prom_8k_num > 0) ? p_host->prom_8k_num : 1);
	ines_word_t  b0 = (ines_word_t)((block * 2 + 0) % num);
	ines_word_t  b1 = (ines_word_t)((block * 2 + 1) % num);

	ines_set_prom_bank_4(p_host, b0, b1, b0, b1);
}

/**
 * 整块切换 8KB CHR。
 * @param p_host 宿主
 * @param block  块号（已取模）
 */
static void M60_set_ppu_bank(ines_host_t* p_host, ines_word_t block)
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

	bank = (ines_word_t)(block % num_8k);

	ines_set_vrom_bank_8(p_host,
						 (ines_word_t)(bank * 8 + 0), (ines_word_t)(bank * 8 + 1),
						 (ines_word_t)(bank * 8 + 2), (ines_word_t)(bank * 8 + 3),
						 (ines_word_t)(bank * 8 + 4), (ines_word_t)(bank * 8 + 5),
						 (ines_word_t)(bank * 8 + 6), (ines_word_t)(bank * 8 + 7));
}

static void mapper60_reset(ines_mapper_t* p_mapper)
{
	ines_host_t*  p_host = mapper2host(p_mapper);
	M60_data_t*   p = mapper2M60data(p_mapper);
	ines_word_t   blocks = M60_block_num(p_host);

	if(p == NULL)
		return;

	/* 上电后的第一次复位停在块 0；此后每复位一次，片内计数器 +1 */
	if(p->started)
		p->block = (ines_byte_t)(((ines_word_t)p->block + 1) % blocks);
	else
		p->started = 1;

	if(p->block >= blocks)
		p->block = 0;

	M60_set_cpu_bank(p_host, (ines_word_t)p->block);
	M60_set_ppu_bank(p_host, (ines_word_t)p->block);

	INES_LOG(LOG_INF, MOD_MMC, ISTR("mapper60: reset-based NROM-128 multicart, block %d/%d (PRG %dKB, CHR %dKB)\n"),
			 (ines_int_t)p->block, (ines_int_t)blocks,
			 (ines_int_t)(p_host->prom_8k_num * 8), (ines_int_t)p_host->vrom_1k_num);
}

/**
 * 本 mapper 没有 $8000+ 写寄存器，这里只记日志：
 * 若某 ROM 频繁写 $8000+，多半是被误标成 60 的其它卡带（如 mapper 59 的 T3H53）。
 */
static void mapper60_writehigh(ines_mapper_t* p_mapper, ines_word_t addr, ines_byte_t val)
{
	INES_LOG(LOG_DBG, MOD_MMC, ISTR("M60: unexpected write $%04X = #$%02X (no register on this board)\n"),
			 (ines_int_t)addr, (ines_int_t)val);
}

ines_bool_t  mapper60_create(ines_mapper_t* p_mapper)
{
	INIT_MAPPER_DATA_ST(p_mapper, M60_data_t);

	p_mapper->reset = mapper60_reset;
	p_mapper->writehigh = mapper60_writehigh;

	return ines_true;
}

