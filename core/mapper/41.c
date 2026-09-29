
/**
 * core/mapper/41.c -- Caltron 6-in-1（iNES Mapper 041）
 *
 * 硬件要点：Mapper 41 是一块**离散逻辑**多合一卡带（Caltron 6-in-1），设计上容纳最多四个
 * 未改动的 CNROM 与 NROM 游戏（NROM 的 CHR 塞在 CNROM 游戏没用掉的 CHR 空间里）。
 *
 *   - PRG-ROM 256KB：CPU $8000-$FFFF 一个 32KB 窗口（8 个 bank）
 *   - CHR-ROM 128KB：**两级**选择——外层 32KB（4 个）× 内层 8KB（4 个）
 *   - **无 PRG-RAM**：$6000-$67FF 是寄存器，不是 RAM
 *   - 镜像由 Mapper 控制（H / V）；无 IRQ、无扩展音
 *   - 总线冲突"部分存在"：只有内层 CHR 寄存器那一侧有（写的是 PRG-ROM 区）
 *
 * 两个寄存器，上电与按住 reset 键期间都清零：
 *
 *  1) 外层 bank 选择 —— $6000-$67FF，**bank 号取自地址线而不是数据线**：
 *
 *        15   11   7  bit  0 (address lines)
 *        ---- ---- ---- ----
 *        0110 0xxx xxMC CEPP
 *                    ││ ││││
 *                    ││ │└┴┴── 32KB PRG bank @ $8000-$FFFF（bit2-0）
 *                    ││ └───── bit2 同时充当"允许写内层 CHR"的使能，所以只有
 *                    ││        PRG bank 为 4..7 时内层寄存器才写得进去
 *                    │└─────── 外层 32KB CHR bank（bit4-3）
 *                    └──────── 镜像（0 = 垂直，1 = 水平）
 *
 *     A15-A12 = 0110 且 A11 = 0（因此是 $6000-$67FF，A10-A6 未解码），写入值不参与译码。
 *
 *  2) 内层 CHR 选择 —— $8000-$FFFF，写入值 bit1-0 = 外层 32KB bank 内的 8KB bank
 *     （bit5-4 在硬件上存在但未使用）。写的是 PRG-ROM 区，因此有 AND 型总线冲突。
 *
 * 两个寄存器都清零对应：PRG bank 0、CHR 外层 0 / 内层 0、垂直镜像。
 */

#include "../../comm/idef.h"
#include "../../comm/log.h"
#include "../nes.h"
#include "../mapper.h"


/* 外层寄存器：$6000-$67FF（A15-A12 = 0110、A11 = 0） */
#define CALTRON41_OUTER_BEGIN    0x6000
#define CALTRON41_OUTER_END      0x67FF

/* 外层寄存器低 6 位（A5..A0）的各位含义 */
#define CALTRON41_PRG_MASK       0x07    /* bit2-0：32KB PRG bank（0..7） */
#define CALTRON41_INNER_ENABLE   0x04    /* bit2：为 1 时内层 CHR 寄存器可写（PRG bank 4..7） */
#define CALTRON41_CHR_OUT_MASK   0x18    /* bit4-3：外层 32KB CHR bank（0..3） */
#define CALTRON41_CHR_OUT_SHIFT  3       /* 外层 bank 的右移位数 */
#define CALTRON41_MIRROR_BIT     0x20    /* bit5：0 = 垂直，1 = 水平 */

/* 内层寄存器：写入值 bit1-0 = 外层 32KB 内的 8KB bank */
#define CALTRON41_CHR_IN_MASK    0x03

/* 32KB PRG bank = 4 个 8KB 页；8KB CHR bank = 8 个 1KB 页；外层 32KB 内有 4 个 8KB bank */
#define CALTRON41_PRG_PAGES      4
#define CALTRON41_CHR_PAGES      8
#define CALTRON41_CHR_IN_COUNT   4


struct _CALTRON41_data_
{
	ines_byte_t   outer;    /* $6000-$67FF 锁存的外层寄存器（A5..A0） */
	ines_byte_t   inner;    /* $8000-$FFFF 锁存的内层 8KB CHR bank（bit1-0） */
};

typedef struct _CALTRON41_data_   CALTRON41_data_t;

#define mapper2CALTRON41data(mapper)   ((CALTRON41_data_t*)((mapper)->p_data))


/**
 * 按两个寄存器刷新 PRG / CHR 窗口与镜像。
 * 所有页号都对总页数取模，容量不足的畸形 ROM 也不会越界。
 * @param p_mapper Mapper
 */
static void CALTRON41_apply(ines_mapper_t* p_mapper)
{
	ines_host_t*       p_host = mapper2host(p_mapper);
	CALTRON41_data_t*  p = mapper2CALTRON41data(p_mapper);
	ines_word_t        num_32k, num_8k, base, chr8k, n;

	if(p == NULL)
		return;

	/* CPU $8000-$FFFF：32KB 窗口 */
	num_32k = (ines_word_t)(p_host->prom_8k_num >> 2);
	if(num_32k == 0)
		num_32k = 1;      /* PRG 不足 32KB：只有 bank 0 */

	base = (ines_word_t)(((p->outer & CALTRON41_PRG_MASK) % num_32k) << 2);

	ines_set_prom_bank_4(p_host,
						 base,
						 (ines_word_t)(base + 1),
						 (ines_word_t)(base + 2),
						 (ines_word_t)(base + 3));

	/* PPU $0000-$1FFF：外层 32KB bank 内的 8KB bank */
	if(p_host->vrom_1k_num > 0)
	{
		num_8k = (ines_word_t)(p_host->vrom_1k_num >> 3);
		if(num_8k == 0)
			num_8k = 1;      /* CHR 不足 8KB：只有 bank 0 */

		chr8k = (ines_word_t)((((((p->outer & CALTRON41_CHR_OUT_MASK) >> CALTRON41_CHR_OUT_SHIFT)
								  * CALTRON41_CHR_IN_COUNT)
								 + (p->inner & CALTRON41_CHR_IN_MASK)) % num_8k) << 3);

		for(n = 0; n < CALTRON41_CHR_PAGES; n++)
			ines_set_vrom_bank_n(p_host, n, (ines_word_t)(chr8k + n));
	}
	else
	{
		/* 硬件必带 CHR-ROM；没有就退化成 8KB pattern RAM 并忽略 bank 切换 */
		for(n = 0; n < CALTRON41_CHR_PAGES; n++)
			ines_set_vram_bank_n(p_host, n, n);
	}

	/* 四屏卡带的镜像由卡带硬件提供，Mapper 不得改写 */
	if(p_host->rom.mirror_type != MIRROR_FOUR_SCREEN)
	{
		ines_ppu_set_mirror_type(&p_host->ppu,
								 (p->outer & CALTRON41_MIRROR_BIT) ? MIRROR_HORZ : MIRROR_VERT);
	}
}

/**
 * 复位：硬件在上电与按住 reset 键期间都会把两个寄存器清零。
 * @param p_mapper Mapper
 */
static void mapper41_reset(ines_mapper_t* p_mapper)
{
	ines_host_t*       p_host = mapper2host(p_mapper);
	CALTRON41_data_t*  p = mapper2CALTRON41data(p_mapper);

	if(p == NULL)
		return;

	p->outer = 0;
	p->inner = 0;

	CALTRON41_apply(p_mapper);

	INES_LOG(LOG_INF, MOD_MMC, ISTR("mapper41: Caltron 6-in-1 (PRG %dKB, CHR %dKB)\n"),
			 (int)(p_host->prom_8k_num * 8), (int)p_host->vrom_1k_num);
}

/**
 * $6000-$7FFF 的写入。$6000-$67FF 是外层 bank 寄存器，**bank 号取自地址线**，
 * 写入值不参与译码（与 mapper 225/255 同类的"地址译码"写法）。
 * @param p_mapper Mapper
 * @param addr     $6000-$7FFF
 * @param val      写入值（本 Mapper 不使用）
 */
static void mapper41_writelow(ines_mapper_t* p_mapper, ines_word_t addr, ines_byte_t val)
{
	CALTRON41_data_t*  p = mapper2CALTRON41data(p_mapper);

	(void)val;

	if(p == NULL)
		return;

	if((addr < CALTRON41_OUTER_BEGIN) || (addr > CALTRON41_OUTER_END))
		return;      /* $6800-$7FFF 不是寄存器，写入丢弃 */

	p->outer = (ines_byte_t)(addr & 0x3F);

	INES_LOG(LOG_DBG, MOD_MMC,
			 ISTR("Caltron41: outer $%04X -> mirror=%d chr32k=%d prg=%d\n"),
			 addr,
			 (int)((p->outer & CALTRON41_MIRROR_BIT) ? 1 : 0),
			 (int)((p->outer & CALTRON41_CHR_OUT_MASK) >> CALTRON41_CHR_OUT_SHIFT),
			 (int)(p->outer & CALTRON41_PRG_MASK));

	CALTRON41_apply(p_mapper);
}

/**
 * $8000-$FFFF 的写入：内层 8KB CHR bank（写入值 bit1-0）。
 * 只有 PRG bank 为 4..7（外层寄存器 bit2 = 1）时才写得进去；写的是 PRG-ROM 区，
 * 因此复现 AND 型总线冲突。
 * @param p_mapper Mapper
 * @param addr     $8000-$FFFF
 * @param val      写入值
 */
static void mapper41_writehigh(ines_mapper_t* p_mapper, ines_word_t addr, ines_byte_t val)
{
	ines_host_t*       p_host = mapper2host(p_mapper);
	CALTRON41_data_t*  p = mapper2CALTRON41data(p_mapper);

	if(p == NULL)
		return;

	/* 游戏必须"执行经过"高四个 PRG bank 才能选内层 CHR（硬件说明里点名的限制） */
	if((p->outer & CALTRON41_INNER_ENABLE) == 0)
		return;

	val = (ines_byte_t)(val & p_host->cpu.mem_bank[addr >> 13][addr & 0x1FFF]);

	p->inner = (ines_byte_t)(val & CALTRON41_CHR_IN_MASK);

	INES_LOG(LOG_DBG, MOD_MMC, ISTR("Caltron41: inner $%04X = #$%02X -> chr8k=%d\n"),
			 addr, val, (int)p->inner);

	CALTRON41_apply(p_mapper);
}

static void mapper41_fini(ines_mapper_t* p_mapper)
{
	INES_LOG(LOG_DBG, MOD_MMC, ISTR("mapper41_fini.\n"));
	ines_free(p_mapper->p_data);
	p_mapper->p_data = NULL;
}

ines_bool_t  mapper41_create(ines_mapper_t* p_mapper)
{
	INIT_MAPPER_DATA_ST(p_mapper, CALTRON41_data_t);

	if(mapper2CALTRON41data(p_mapper) == NULL)
		return ines_false;

	p_mapper->reset = mapper41_reset;
	p_mapper->writelow = mapper41_writelow;
	p_mapper->writehigh = mapper41_writehigh;
	p_mapper->fini = mapper41_fini;

	/* 卡带没有 PRG-RAM：$6000-$67FF 是寄存器，不让宿主在 $6000 挂默认的 8KB RAM */
	p_mapper->custom_sram = 1;

	return ines_true;
}

