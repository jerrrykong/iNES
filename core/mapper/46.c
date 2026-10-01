
/**
 * core/mapper/46.c -- Rumble Station 15-in-1（iNES Mapper 046）
 *
 * 硬件要点：Mapper 46 是 **Rumble Station** —— 把 NES-on-a-Chip 与多合一打包在一块板上，
 * 收录了一批已授权的 Color Dreams 游戏。它分**两级**选页：
 *
 *   - PRG-ROM 最多 1MB：CPU $8000-$FFFF 一个 32KB 窗口（32 个 bank）
 *   - CHR-ROM 最多 1MB：PPU $0000-$1FFF 一个 8KB 窗口（128 个 bank）
 *   - **无 PRG-RAM**：$6000-$7FFF 是寄存器而不是 RAM
 *   - 无 IRQ、无扩展音、不控制镜像（沿用 ROM 头的设定）
 *
 * 两个寄存器，上电时外层锁存为 0：
 *
 *  1) 外层 —— $6000-$7FFF，锁存**写入值** [CCCC PPPP]：
 *
 *         bit7-4 = 64KB CHR bank（0..15）
 *         bit3-0 = 64KB PRG bank（0..15）
 *
 *  2) 内层 —— $8000-$FFFF，锁存**写入值** [.CCC ...P]，是 Color Dreams（mapper 11）
 *     的**缩减子集**（原本 4 位 CHR + 4 位 PRG，这里各只留下与外层粒度匹配的几位）：
 *
 *         bit6-4 = 64KB CHR bank 内的 8KB bank（0..7）
 *         bit0   = 64KB PRG bank 内的低 / 高 32KB
 *
 * 于是合成后的 bank 号：
 *
 *         32KB PRG bank = (外层 PPPP << 1) | 内层 P     -> 最多 32 个（1MB）
 *         8KB  CHR bank = (外层 CCCC << 3) | 内层 CCC   -> 最多 128 个（1MB）
 *
 * 总线冲突：Disch 原注明明"不确定这块板是否有总线冲突"。它是 NES-on-a-Chip 而非真正的
 * ROM 芯片，且项目内的 Color Dreams（11.c）同样不处理，因此这里也**不做** AND 型冲突
 * —— 做错会把 bank 号按 ROM 内容截掉，直接黑屏。
 */

#include "../../comm/idef.h"
#include "../../comm/log.h"
#include "../nes.h"
#include "../mapper.h"


/* 外层寄存器写入值的各位含义 */
#define RUMBLE46_CHR_OUT_SHIFT    4       /* bit7-4：64KB CHR bank */
#define RUMBLE46_PRG_OUT_MASK     0x0F    /* bit3-0：64KB PRG bank */

/* 内层寄存器写入值的各位含义 */
#define RUMBLE46_CHR_IN_MASK      0x70    /* bit6-4：64KB bank 内的 8KB bank */
#define RUMBLE46_CHR_IN_SHIFT     4
#define RUMBLE46_PRG_IN_MASK      0x01    /* bit0：64KB bank 内的低 / 高 32KB */

/* 外层一个 64KB bank 内含 2 个 32KB PRG bank、8 个 8KB CHR bank */
#define RUMBLE46_PRG_IN_COUNT     2
#define RUMBLE46_CHR_IN_COUNT     8

/* 32KB PRG = 4 个 8KB 页；8KB CHR = 8 个 1KB 页 */
#define RUMBLE46_PRG_PAGES        4
#define RUMBLE46_CHR_PAGES        8

/* 外层寄存器的地址范围 */
#define RUMBLE46_OUTER_BEGIN      0x6000
#define RUMBLE46_OUTER_END        0x7FFF


struct _RUMBLE46_data_
{
	ines_byte_t   outer;    /* $6000-$7FFF 锁存的外层寄存器 [CCCC PPPP] */
	ines_byte_t   inner;    /* $8000-$FFFF 锁存的内层寄存器 [.CCC ...P] */
};

typedef struct _RUMBLE46_data_   RUMBLE46_data_t;

#define mapper2RUMBLE46data(mapper)   ((RUMBLE46_data_t*)((mapper)->p_data))


/**
 * 按两个寄存器刷新 PRG / CHR 窗口。
 * 所有 bank 号都对实际容量取模，容量不足的畸形 ROM 也不会越界。
 * @param p_mapper Mapper
 */
static void RUMBLE46_apply(ines_mapper_t* p_mapper)
{
	ines_host_t*       p_host = mapper2host(p_mapper);
	RUMBLE46_data_t*   p = mapper2RUMBLE46data(p_mapper);
	ines_word_t        num_32k, num_8k, prg32k, chr8k, n;

	if(p == NULL)
		return;

	/* CPU $8000-$FFFF：32KB 窗口 = (外层 64KB bank × 2) + 内层低 / 高半 */
	num_32k = (ines_word_t)(p_host->prom_8k_num >> 2);
	if(num_32k == 0)
		num_32k = 1;      /* PRG 不足 32KB：只有 bank 0 */

	prg32k = (ines_word_t)(((((p->outer & RUMBLE46_PRG_OUT_MASK) * RUMBLE46_PRG_IN_COUNT)
							 + (p->inner & RUMBLE46_PRG_IN_MASK)) % num_32k) << 2);

	ines_set_prom_bank_4(p_host,
						 prg32k,
						 (ines_word_t)(prg32k + 1),
						 (ines_word_t)(prg32k + 2),
						 (ines_word_t)(prg32k + 3));

	/* PPU $0000-$1FFF：8KB 窗口 = (外层 64KB bank × 8) + 内层 8KB bank */
	if(p_host->vrom_1k_num > 0)
	{
		num_8k = (ines_word_t)(p_host->vrom_1k_num >> 3);
		if(num_8k == 0)
			num_8k = 1;      /* CHR 不足 8KB：只有 bank 0 */

		chr8k = (ines_word_t)((((((p->outer >> RUMBLE46_CHR_OUT_SHIFT) & 0x0F)
								  * RUMBLE46_CHR_IN_COUNT)
								 + ((p->inner & RUMBLE46_CHR_IN_MASK) >> RUMBLE46_CHR_IN_SHIFT))
								% num_8k) << 3);

		for(n = 0; n < RUMBLE46_CHR_PAGES; n++)
			ines_set_vrom_bank_n(p_host, n, (ines_word_t)(chr8k + n));
	}
	else
	{
		/* 硬件必带 CHR-ROM；没有就退化成 8KB pattern RAM 并忽略 bank 切换 */
		for(n = 0; n < RUMBLE46_CHR_PAGES; n++)
			ines_set_vram_bank_n(p_host, n, n);
	}
}

/**
 * 复位：上电时外层锁存为 0（内层未规定，同样按 0 处理）。
 * @param p_mapper Mapper
 */
static void mapper46_reset(ines_mapper_t* p_mapper)
{
	ines_host_t*       p_host = mapper2host(p_mapper);
	RUMBLE46_data_t*   p = mapper2RUMBLE46data(p_mapper);

	if(p == NULL)
		return;

	p->outer = 0;
	p->inner = 0;

	RUMBLE46_apply(p_mapper);

	INES_LOG(LOG_INF, MOD_MMC, ISTR("mapper46: Rumble Station multicart (PRG %dKB, CHR %dKB)\n"),
			 (int)(p_host->prom_8k_num * 8), (int)p_host->vrom_1k_num);
}

/**
 * $6000-$7FFF 的写入：外层 64KB bank 选择（写入值 [CCCC PPPP]）。
 * @param p_mapper Mapper
 * @param addr     $6000-$7FFF
 * @param val      写入值
 */
static void mapper46_writelow(ines_mapper_t* p_mapper, ines_word_t addr, ines_byte_t val)
{
	RUMBLE46_data_t*   p = mapper2RUMBLE46data(p_mapper);

	if(p == NULL)
		return;

	if((addr < RUMBLE46_OUTER_BEGIN) || (addr > RUMBLE46_OUTER_END))
		return;      /* 不是外层寄存器窗口，写入丢弃 */

	p->outer = val;

	INES_LOG(LOG_DBG, MOD_MMC,
			 ISTR("Rumble46: outer $%04X = #$%02X -> chr64k=%d prg64k=%d\n"),
			 addr,
			 val,
			 (int)((p->outer >> RUMBLE46_CHR_OUT_SHIFT) & 0x0F),
			 (int)(p->outer & RUMBLE46_PRG_OUT_MASK));

	RUMBLE46_apply(p_mapper);
}

/**
 * $8000-$FFFF 的写入：内层 bank 选择（写入值 [.CCC ...P]），Color Dreams 的缩减子集。
 * @param p_mapper Mapper
 * @param addr     $8000-$FFFF
 * @param val      写入值
 */
static void mapper46_writehigh(ines_mapper_t* p_mapper, ines_word_t addr, ines_byte_t val)
{
	RUMBLE46_data_t*   p = mapper2RUMBLE46data(p_mapper);

	if(p == NULL)
		return;

	p->inner = val;

	INES_LOG(LOG_DBG, MOD_MMC,
			 ISTR("Rumble46: inner $%04X = #$%02X -> chr8k=%d prg_half=%d\n"),
			 addr,
			 val,
			 (int)((p->inner & RUMBLE46_CHR_IN_MASK) >> RUMBLE46_CHR_IN_SHIFT),
			 (int)(p->inner & RUMBLE46_PRG_IN_MASK));

	RUMBLE46_apply(p_mapper);
}

static void mapper46_fini(ines_mapper_t* p_mapper)
{
	INES_LOG(LOG_DBG, MOD_MMC, ISTR("mapper46_fini.\n"));
	ines_free(p_mapper->p_data);
	p_mapper->p_data = NULL;
}

ines_bool_t  mapper46_create(ines_mapper_t* p_mapper)
{
	INIT_MAPPER_DATA_ST(p_mapper, RUMBLE46_data_t);

	if(mapper2RUMBLE46data(p_mapper) == NULL)
		return ines_false;

	p_mapper->reset = mapper46_reset;
	p_mapper->writelow = mapper46_writelow;
	p_mapper->writehigh = mapper46_writehigh;
	p_mapper->fini = mapper46_fini;

	/* 卡带没有 PRG-RAM：$6000-$7FFF 是寄存器，不让宿主在 $6000 挂默认的 8KB RAM */
	p_mapper->custom_sram = 1;

	return ines_true;
}

