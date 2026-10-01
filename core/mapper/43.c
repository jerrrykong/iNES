
/**
 * core/mapper/43.c -- TONY-I / YS-612（iNES Mapper 043）
 *
 * 硬件要点：这两块板是 **《超级马力欧兄弟 2》(日版) 从 Famicom Disk System 改成 ROM 卡带**
 * 的盗版转接板，TONY-I 与 YS-612 的差别只有 IRQ 控制寄存器的地址（前者 $4122、后者 $8122）。
 *
 * PRG-ROM 共 80KB，iNES 映像里的排列顺序是：
 *
 *     [0]  两个 32KB 芯片（64KB = 8 个 8KB 页，页 0..7）
 *     [1]  2KB 芯片的数据**重复四遍**填满 8KB（页 8）
 *     [2]  8KB 芯片（页 9）
 *
 * CPU 窗口：
 *
 *     $5000-$5FFF   2KB 芯片，重复一次填满这 4KB（只读；本核心由 readlow 提供）
 *     $6000-$7FFF   8KB，固定 #2
 *     $8000-$9FFF   8KB，固定 #1
 *     $A000-$BFFF   8KB，固定 #0
 *     $C000-$DFFF   8KB，可切换（$4022）
 *     $E000-$FFFF   8KB 芯片（页 9）
 *
 * PPU $0000-$1FFF 是**不分页**的 8KB CHR-ROM。
 *
 * 寄存器：
 *
 *  1) PRG Bank Select —— $4022，掩码 $71FF，只用 bit2-0：
 *     选 $C000-$DFFF 的 8KB bank，但硬件译码不是恒等的，实际映射为
 *
 *         写入值    0  1  2  3  4  5  6  7
 *         实际页    4  3  4  4  4  7  5  6
 *
 *  2) IRQ Control —— $4122（TONY-I）/ $8122（YS-612），掩码 $71FF，只用 bit0：
 *        0 = 应答中断、关闭计数并把计数器清零
 *        1 = 允许计数
 *     使能后一个 **12 位计数器随每个 M2（CPU）周期递增**，溢出（计满 4096）时触发 IRQ。
 *
 * 无卡带 WRAM（$6000-$7FFF 是 PRG 页 2），因此 custom_sram = 1。
 * 不控制镜像（沿用 ROM 头）。
 *
 * 实现说明：$5000-$5FFF 落在 CPU 的 bank 2（$4000-$5FFF），而宿主只提供 bank 3~7 的
 * PRG 映射接口（ines_set_prom_bank_n 有 assert(3 <= n && n <= 7)）。好在 $4020-$5FFF
 * 的读写都由宿主转给 mapper 的 readlow / writelow，所以这 4KB 直接由 readlow 按
 * "2KB 芯片 + (addr & 0x7FF)" 提供即可，无需改动宿主的地址空间划分。
 */

#include "../../comm/idef.h"
#include "../../comm/log.h"
#include "../nes.h"
#include "../mapper.h"


/* 寄存器掩码：A15、A11 不参与译码，其余保留 */
#define TONYI_REG_MASK        0x71FF

/* $5000-$5FFF：2KB PRG 芯片重复一次 */
#define TONYI_CHIP2K_BEGIN    0x5000
#define TONYI_CHIP2K_END      0x5FFF
#define TONYI_CHIP2K_SIZE     0x0800

/* $4022 的 bit2-0 -> $C000 实际 8KB 页号（硬件译码表，不是恒等映射） */
static const ines_byte_t   TONYI_PRG_MAP[8] = { 4, 3, 4, 4, 4, 7, 5, 6 };

/* IRQ 计数器是 12 位：计满 4096 个 M2 周期溢出 */
#define TONYI_IRQ_PERIOD      0x1000


struct _TONYI_data_
{
	ines_byte_t   prg_bank_sel;      /* $4022 锁存的 bit2-0 */
	ines_byte_t   irq_enabled;       /* IRQ 计数使能（寄存器 bit0） */
	ines_int_t    irq_counter;       /* 12 位 M2 周期计数器 */
	ines_int64_t  irq_synced_cycles; /* 上次 hsync 时的 cpu.total_cycles */
};

typedef struct _TONYI_data_   TONYI_data_t;

#define mapper2TONYIdata(mapper)   ((TONYI_data_t*)((mapper)->p_data))


/**
 * 求出 2KB 芯片页与 8KB 芯片页在 PRG 中的 8KB 页号。
 * 标准 80KB 映像里分别是倒数第二页（页 8）与最后一页（页 9）；
 * 容量不足的畸形映像退化为不越界的页号。
 * @param p_host   宿主
 * @param p_page2k 出参：2KB 芯片所在 8KB 页
 * @param p_page8k 出参：8KB 芯片所在 8KB 页
 */
static void TONYI_get_chip_pages(ines_host_t* p_host, ines_word_t* p_page2k, ines_word_t* p_page8k)
{
	ines_word_t  num = (ines_word_t)p_host->prom_8k_num;

	if(p_page2k != NULL)
		*p_page2k = (num >= 2) ? (ines_word_t)(num - 2) : 0;

	if(p_page8k != NULL)
		*p_page8k = (num > 0) ? (ines_word_t)(num - 1) : 0;
}

/**
 * 刷新 CPU $6000-$FFFF 的 PRG 窗口（$5000-$5FFF 由 readlow 负责）。
 * @param p_mapper Mapper
 */
static void TONYI_apply_cpu_bank(ines_mapper_t* p_mapper)
{
	ines_host_t*   p_host = mapper2host(p_mapper);
	TONYI_data_t*  p = mapper2TONYIdata(p_mapper);
	ines_word_t    num, page_2k, page_8k, sel, c_bank;

	if(p == NULL)
		return;

	num = (ines_word_t)((p_host->prom_8k_num > 0) ? p_host->prom_8k_num : 1);

	TONYI_get_chip_pages(p_host, &page_2k, &page_8k);

	sel = (ines_word_t)(p->prg_bank_sel & 0x07);
	c_bank = (ines_word_t)(TONYI_PRG_MAP[sel] % num);

	/* b3..b7 = $6000 / $8000 / $A000 / $C000 / $E000 */
	ines_set_prom_bank_5(p_host,
						 (ines_word_t)(2 % num),      /* $6000：固定 #2 */
						 (ines_word_t)(1 % num),      /* $8000：固定 #1 */
						 (ines_word_t)(0 % num),      /* $A000：固定 #0 */
						 c_bank,                      /* $C000：可切换 */
						 (ines_word_t)(page_8k % num));/* $E000：8KB 芯片 */
}

/**
 * $4020-$5FFF 的读：$5000-$5FFF 是 2KB 芯片重复一次，其余按开放总线处理。
 * @param p_mapper Mapper
 * @param addr     $4020-$5FFF
 * @return 读到的数据
 */
static ines_byte_t mapper43_readlow(ines_mapper_t* p_mapper, ines_word_t addr)
{
	ines_host_t*   p_host = mapper2host(p_mapper);
	ines_word_t    page_2k;
	ines_word_t    num = (ines_word_t)((p_host->prom_8k_num > 0) ? p_host->prom_8k_num : 1);
	ines_size_t    off;

	if((addr < TONYI_CHIP2K_BEGIN) || (addr > TONYI_CHIP2K_END))
		return (ines_byte_t)(addr >> 8);      /* 开放总线，与 readlow 的默认行为一致 */

	TONYI_get_chip_pages(p_host, &page_2k, NULL);
	page_2k = (ines_word_t)(page_2k % num);

	/* 8KB 页内 2KB 芯片重复四遍，任取一份即可：(addr & 0x7FF) 就是芯片内偏移 */
	off = (ines_size_t)(((ines_size_t)page_2k << 13) + (addr & (TONYI_CHIP2K_SIZE - 1)));

	if(p_host->rom.pPROMs == NULL)
		return (ines_byte_t)(addr >> 8);

	return p_host->rom.pPROMs[off];
}

static void mapper43_reset(ines_mapper_t* p_mapper)
{
	ines_host_t*   p_host = mapper2host(p_mapper);
	TONYI_data_t*  p = mapper2TONYIdata(p_mapper);

	if(p == NULL)
		return;

	p->prg_bank_sel = 0;
	p->irq_enabled = 0;
	p->irq_counter = 0;
	p->irq_synced_cycles = p_host->cpu.total_cycles;

	ines_cpu_IRQ(&p_host->cpu, MMC_IRQ_MASK, ines_false);

	TONYI_apply_cpu_bank(p_mapper);

	/* CHR 不分页：整块 8KB 顺序映射 */
	if(p_host->vrom_1k_num > 0)
		ines_set_vrom_bank_8(p_host, 0, 1, 2, 3, 4, 5, 6, 7);
	else
	{
		ines_word_t  n;
		for(n = 0; n < 8; n++)
			ines_set_vram_bank_n(p_host, n, n);
	}

	INES_LOG(LOG_INF, MOD_MMC, ISTR("mapper43: TONY-I / YS-612 SMB2J conversion (PRG %dKB, CHR %dKB)\n"),
			 (int)(p_host->prom_8k_num * 8), (int)p_host->vrom_1k_num);
}

/**
 * IRQ 控制：bit0 = 1 允许计数，0 = 应答中断 + 关闭计数 + 计数器清零。
 * @param p_mapper Mapper
 * @param val      写入值
 */
static void TONYI_write_irq(ines_mapper_t* p_mapper, ines_byte_t val)
{
	ines_host_t*   p_host = mapper2host(p_mapper);
	TONYI_data_t*  p = mapper2TONYIdata(p_mapper);

	if(p == NULL)
		return;

	if((val & 0x01) != 0)
	{
		p->irq_enabled = 1;
	}
	else
	{
		p->irq_enabled = 0;
		p->irq_counter = 0;
	}

	/* 关中断时同时撤掉 IRQ 线（bit0 = 0 的语义就是应答） */
	if(p->irq_enabled == 0)
		ines_cpu_IRQ(&p_host->cpu, MMC_IRQ_MASK, ines_false);

	INES_LOG(LOG_DBG, MOD_MMC, ISTR("TONY-I: IRQ %s (val #$%02X)\n"),
			 p->irq_enabled ? ISTR("enable") : ISTR("ack/disable"), val);
}

/**
 * $4020-$5FFF 的写：$4022 选 PRG bank，$4122 是 TONY-I 的 IRQ 控制。
 * @param p_mapper Mapper
 * @param addr     $4020-$5FFF（$6000-$7FFF 只读时也会转到这里）
 * @param val      写入值
 */
static void mapper43_writelow(ines_mapper_t* p_mapper, ines_word_t addr, ines_byte_t val)
{
	TONYI_data_t*  p = mapper2TONYIdata(p_mapper);

	if(p == NULL)
		return;

	switch(addr & TONYI_REG_MASK)
	{
	case 0x4022:
		p->prg_bank_sel = val;
		INES_LOG(LOG_DBG, MOD_MMC, ISTR("TONY-I: $4022 = #$%02X -> $C000 bank %d\n"),
				 val, (int)TONYI_PRG_MAP[val & 0x07]);
		TONYI_apply_cpu_bank(p_mapper);
		break;

	case 0x4122:
		TONYI_write_irq(p_mapper, val);
		break;

	default:
		break;      /* $6000-$7FFF 是 PRG，写入丢弃 */
	}
}

/**
 * $8000-$FFFF 的写：$8122 是 YS-612 的 IRQ 控制（$8122 & $71FF = $0122）。
 * @param p_mapper Mapper
 * @param addr     $8000-$FFFF
 * @param val      写入值
 */
static void mapper43_writehigh(ines_mapper_t* p_mapper, ines_word_t addr, ines_byte_t val)
{
	if((addr & TONYI_REG_MASK) == 0x0122)
		TONYI_write_irq(p_mapper, val);
}

/**
 * 扫描线回调：把自上次以来真实流逝的 CPU 周期补进 12 位计数器。
 * 计数器是 M2（CPU）周期驱动而非扫描线驱动，本核心只在 hsync 处回调 mapper，
 * 因此照 VRC 的做法用 cpu.total_cycles 的真实增量做批处理推进（不漂移）。
 * @param p_mapper  Mapper
 * @param scanline  扫描线号（未使用）
 */
static void mapper43_hsync(ines_mapper_t* p_mapper, ines_int_t scanline)
{
	ines_host_t*   p_host = mapper2host(p_mapper);
	TONYI_data_t*  p = mapper2TONYIdata(p_mapper);
	ines_int64_t   now;
	ines_int64_t   delta;
	ines_int_t     sum;

	(void)scanline;

	if(p == NULL)
		return;

	now = p_host->cpu.total_cycles;

	if(p->irq_enabled == 0)
	{
		p->irq_synced_cycles = now;      /* 未使能：只同步基准，不计数 */
		return;
	}

	delta = (now >= p->irq_synced_cycles) ? (now - p->irq_synced_cycles) : 0;
	p->irq_synced_cycles = now;

	if(delta <= 0)
		return;

	/* 夹到一个计数周期内，避免异常大的增量在换算时溢出 */
	if(delta > TONYI_IRQ_PERIOD)
		delta = TONYI_IRQ_PERIOD;

	sum = p->irq_counter + (ines_int_t)delta;

	if(sum >= TONYI_IRQ_PERIOD)
	{
		p->irq_counter = sum - TONYI_IRQ_PERIOD;      /* 12 位回绕，继续下一段计数 */
		ines_cpu_IRQ(&p_host->cpu, MMC_IRQ_MASK, ines_true);
		INES_LOG(LOG_DBG, MOD_MMC, ISTR("TONY-I IRQ fired (counter overflow)\n"));
	}
	else
	{
		p->irq_counter = sum;
	}
}

static void mapper43_fini(ines_mapper_t* p_mapper)
{
	INES_LOG(LOG_DBG, MOD_MMC, ISTR("mapper43_fini.\n"));
	ines_free(p_mapper->p_data);
	p_mapper->p_data = NULL;
}

ines_bool_t  mapper43_create(ines_mapper_t* p_mapper)
{
	INIT_MAPPER_DATA_ST(p_mapper, TONYI_data_t);

	if(mapper2TONYIdata(p_mapper) == NULL)
		return ines_false;

	p_mapper->reset = mapper43_reset;
	p_mapper->readlow = mapper43_readlow;
	p_mapper->writelow = mapper43_writelow;
	p_mapper->writehigh = mapper43_writehigh;
	p_mapper->hsync = mapper43_hsync;
	p_mapper->fini = mapper43_fini;

	/* $6000-$7FFF 是 PRG 页 2 而不是 SRAM，不让宿主挂默认的 8KB RAM */
	p_mapper->custom_sram = 1;

	return ines_true;
}

