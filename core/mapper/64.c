
/**
 * core/mapper/64.c -- Tengen RAMBO-1（iNES Mapper 064）
 *
 * 硬件要点（资料：NESDev "RAMBO-1"，IMA 笔记「RAMBO-1」）：
 * RAMBO-1 是 Tengen 版的 MMC3（40-pin PDIP，ASIC），PRG 最多 256KB、CHR 最多 256KB、
 * 无 PRG-RAM、有 IRQ。与 MMC3 相比多了两件事：
 *   1. **K 位（$8000 bit5）**：CHR 低区可以切成 4 个 1KB（K=1，多出 R8/R9 两个寄存器），
 *      而不是 MMC3 那样固定 2×2KB + 4×1KB。
 *   2. **IRQ 可选 CPU 周期模式**（$C001 bit0 = 1），计数器每 4 个 CPU 周期走一步。
 *
 * 窗口：
 *   CPU $8000-$9FFF / $A000-$BFFF / $C000-$DFFF = 三个 8KB 可切页，$E000-$FFFF 固定最后一页。
 *   PPU 三种配置（C 位与 K 位组合）：
 *     C=0 K=0：低区 2×2KB（$0000-$0FFF）+ 高区 4×1KB
 *     C=0 K=1：低区 4×1KB（$0000-$0FFF）+ 高区 4×1KB
 *     C=1：   低/高两区整体互换（即上面两种再把 $0000-$0FFF 与 $1000-$1FFF 对调）
 *
 * 寄存器（成对：偶地址选低寄存器、奇地址选高寄存器）：
 *   $8000-$9FFE even  Bank select  [CPKx RRRR]
 *        C(bit7) = CHR A12 反相（低区/高区互换）
 *        P(bit6) = PRG 页序（0: $8000←R6, $C000←RF；1: $8000←RF, $C000←R6；$A000 恒为 R7）
 *        K(bit5) = 1 时低区用 4×1KB（启用 R8/R9）
 *        RRRR    = 要更新的寄存器号：0-5 = CHR，6/7 = PRG，8/9 = K=1 时的额外 CHR，F = 第三个 PRG 页
 *   $8001-$9FFF odd   Bank data：8 位全用，写入上面选中的寄存器
 *   $A000-$BFFE even  镜像（bit0：0 = 垂直，1 = 水平）—— 仅 mapper 64；mapper 158 不用（见下）
 *   $A001-$BFFF odd   未实现（MMC3 这里是 PRG-RAM 控制，RAMBO-1 无 PRG-RAM）
 *   $C000-$DFFE even  IRQ latch（重载值）
 *   $C001-$DFFF odd   bit0 = IRQ 模式（0 = 扫描线，1 = CPU 周期）；写入同时请求重载
 *   $E000-$FFFE even  IRQ 应答/禁止
 *   $E001-$FFFF odd   IRQ 允许
 *
 * IRQ 计数器（两种模式共用同一套规则）：
 *   若上次计数后写过 $C001：counter = latch（非 0 时再 |= 1，即资料里说的 "extra kick"）
 *   否则若 counter == 0：   counter = latch
 *   否则：                  counter--
 *   若此时 counter == 0 且允许 IRQ：触发 IRQ（并置重载标志）
 * CPU 周期模式下每 4 个 CPU 周期走一步；实际硬件要比朴素算法晚一个 M2 周期，
 * 本项目只在每条扫描线的 hsync 处回调，故按 cpu.total_cycles 的真实增量批处理推进
 * （与 core/mapper/vrc.h 的 VRC IRQ 同一思路，量化误差在一行以内、不累积漂移）。
 *
 * 变体：iNES Mapper 158（Alien Syndrome）是同一颗芯片但镜像接法不同 ——
 * CIRAM A10 接 CHR A17，映射到 PPU $0000-$0FFF 的每个 CHR 页的 bit7 决定对应
 * nametable 用 CIRAM 的哪一页（即 TLSROM / mapper 118 的接法）。
 * 本文件用 tlsrom 标志区分，core/mapper/158.c 直接转发过来，不另写一份实现。
 */

#include "../../comm/idef.h"
#include "../../comm/log.h"
#include "../nes.h"
#include "../mapper.h"


/** $8000 命令字节的各位 */
#define M64_CMD_CHR_INVERT   0x80    /* C：低区/高区（$0000-$0FFF / $1000-$1FFF）互换 */
#define M64_CMD_PRG_MODE     0x40    /* P：PRG 页序 */
#define M64_CMD_CHR_1K       0x20    /* K：1 = 低区切成 4 个 1KB（启用 R8/R9） */
#define M64_CMD_REG_MASK     0x0F    /* RRRR：寄存器号 */

/** IRQ 模式（$C001 bit0） */
#define M64_IRQ_MODE_SCANLINE  0
#define M64_IRQ_MODE_CPUCYCLE  1

/** CPU 周期模式下每 4 个 CPU 周期计数一次 */
#define M64_CPU_CYCLES_PER_TICK  4


/** Mapper 64 / 158 私有数据 */
typedef struct _ines_M64_
{
	ines_byte_t   cmd;         /* $8000 写入的命令字节 */
	ines_byte_t   reg[16];     /* R0..R9 与 RF，按 RRRR 直接索引（RF 用下标 15） */
	ines_byte_t   tlsrom;      /* 非 0：mapper 158 的镜像接法（CIRAM A10 = CHR A17） */
	ines_byte_t   irq_enabled; /* IRQ 允许 */
	ines_byte_t   irq_mode;    /* IRQ 模式：0 = 扫描线，1 = CPU 周期 */
	ines_byte_t   irq_latch;   /* $C000 写入的重载值 */
	ines_byte_t   irq_reload;  /* 非 0：下次计数先重载（写过 $C001 或刚归零） */
	ines_word_t   irq_counter; /* 当前计数值 */
	ines_word_t   cycle_acc;   /* CPU 周期模式：不足 4 个周期的余量 */
	ines_int64_t  last_cycles; /* CPU 周期模式：上次推进时的 cpu.total_cycles */
} M64_data_t;

#define mapper2M64data(p_mapper)  ((M64_data_t*)((p_mapper)->p_data))


/**
 * 应用三个可切换的 PRG 页（$E000 恒为最后一页）。
 * @param p      私有数据
 * @param p_host 宿主
 */
static void M64_set_cpu_bank(M64_data_t* p, ines_host_t* p_host)
{
	ines_word_t  num  = (ines_word_t)((p_host->prom_8k_num > 0) ? p_host->prom_8k_num : 1);
	ines_word_t  last = (ines_word_t)(num - 1);
	ines_word_t  r6   = (ines_word_t)p->reg[6];
	ines_word_t  r7   = (ines_word_t)p->reg[7];
	ines_word_t  rf   = (ines_word_t)p->reg[15];

	if((p->cmd & M64_CMD_PRG_MODE) != 0)
	{
		/* P = 1：$8000 ← RF，$C000 ← R6 */
		ines_set_prom_bank_4(p_host,
							 (ines_word_t)(rf % num), (ines_word_t)(r7 % num),
							 (ines_word_t)(r6 % num), last);
	}
	else
	{
		/* P = 0：$8000 ← R6，$C000 ← RF */
		ines_set_prom_bank_4(p_host,
							 (ines_word_t)(r6 % num), (ines_word_t)(r7 % num),
							 (ines_word_t)(rf % num), last);
	}
}

/**
 * 应用 8 个 1KB CHR 窗口。
 * 2KB 模式（K=0）下寄存器最低位被忽略、由 PPU A10 直通 CHR A10，
 * 故低区两页是 (reg & 0xFE) 与 (reg & 0xFE) + 1。
 * @param p      私有数据
 * @param p_host 宿主
 */
static void M64_set_ppu_bank(M64_data_t* p, ines_host_t* p_host)
{
	ines_word_t  num1k = p_host->vrom_1k_num;
	ines_bool_t  k = (ines_bool_t)(0 != (p->cmd & M64_CMD_CHR_1K));
	ines_bool_t  c = (ines_bool_t)(0 != (p->cmd & M64_CMD_CHR_INVERT));
	ines_word_t  lo[4], hi[4];
	ines_word_t  b[8];
	ines_int_t   n;

	/* 低区四个 1KB 窗口（K=1 时用 R8/R9 补上 $0400 / $0C00） */
	lo[0] = (ines_word_t)(k ? p->reg[0] : ((p->reg[0] & 0xFE)));
	lo[1] = (ines_word_t)(k ? p->reg[8] : ((p->reg[0] & 0xFE) + 1));
	lo[2] = (ines_word_t)(k ? p->reg[1] : ((p->reg[1] & 0xFE)));
	lo[3] = (ines_word_t)(k ? p->reg[9] : ((p->reg[1] & 0xFE) + 1));

	/* 高区四个 1KB 窗口 */
	hi[0] = (ines_word_t)p->reg[2];
	hi[1] = (ines_word_t)p->reg[3];
	hi[2] = (ines_word_t)p->reg[4];
	hi[3] = (ines_word_t)p->reg[5];

	for(n = 0; n < 4; n++)
	{
		b[n]     = (ines_word_t)(c ? hi[n] : lo[n]);
		b[n + 4] = (ines_word_t)(c ? lo[n] : hi[n]);
	}

	if(num1k > 0)
	{
		ines_set_vrom_bank_8(p_host,
							 (ines_word_t)(b[0] % num1k), (ines_word_t)(b[1] % num1k),
							 (ines_word_t)(b[2] % num1k), (ines_word_t)(b[3] % num1k),
							 (ines_word_t)(b[4] % num1k), (ines_word_t)(b[5] % num1k),
							 (ines_word_t)(b[6] % num1k), (ines_word_t)(b[7] % num1k));
	}
	else
	{
		/* CHR-RAM：只有 8KB 的 pattern RAM，页号由 ines_set_vram_bank_n() 内部回卷 */
		for(n = 0; n < 8; n++)
		{
			ines_set_vram_bank_n(p_host, (ines_word_t)n, b[n]);
		}
	}

	if(p->tlsrom)
	{
		/* mapper 158：CIRAM A10 接 CHR A17 —— PPU $0000-$0FFF 四个 1KB 页的
		 * bit7 分别决定 $2000/$2400/$2800/$2C00 用 CIRAM 的哪一页（TLSROM 接法） */
		ines_ppu_set_mirror(&p_host->ppu,
							(ines_byte_t)((b[0] >> 7) & 1), (ines_byte_t)((b[1] >> 7) & 1),
							(ines_byte_t)((b[2] >> 7) & 1), (ines_byte_t)((b[3] >> 7) & 1));
	}
}

/**
 * IRQ 计数器走一步（扫描线模式与 CPU 周期模式共用）。
 * @param p      私有数据
 * @param p_host 宿主
 */
static void M64_clock_irq(M64_data_t* p, ines_host_t* p_host)
{
	if(p->irq_reload)
	{
		/* 刚写过 $C001（或上次刚归零）：直接用重载值，非 0 时再 OR 1（资料：extra kick） */
		p->irq_counter = (ines_word_t)p->irq_latch;
		p->irq_reload = 0;
		if(p->irq_counter != 0)
		{
			p->irq_counter = (ines_word_t)(p->irq_counter | 1);
		}
	}
	else if(p->irq_counter == 0)
	{
		p->irq_counter = (ines_word_t)p->irq_latch;
	}
	else
	{
		p->irq_counter = (ines_word_t)(p->irq_counter - 1);
	}

	if(p->irq_counter == 0 && p->irq_enabled)
	{
		p->irq_reload = 1;
		ines_cpu_IRQ(&p_host->cpu, MMC_IRQ_MASK, ines_true);
	}
}

static void mapper64_reset(ines_mapper_t* p_mapper)
{
	ines_host_t*  p_host = mapper2host(p_mapper);
	M64_data_t*   p = mapper2M64data(p_mapper);
	ines_word_t   num = (ines_word_t)((p_host->prom_8k_num > 0) ? p_host->prom_8k_num : 1);

	if(p == NULL)
		return;

	/* 上电时 R6/R7/RF/$8000 的值在硬件上是未定义的（资料要求复位向量必须在 $E000-$FFFF），
	 * 这里按 MMC3 的上电习惯给一份确定的初值：0、1、倒数第二页，最后一页固定在 $E000 */
	p->reg[6]  = 0;
	p->reg[7]  = 1;
	p->reg[15] = (ines_byte_t)((num >= 4) ? (num - 2) : 0);

	p->reg[0] = 0;
	p->reg[1] = 2;
	p->reg[2] = 4;
	p->reg[3] = 5;
	p->reg[4] = 6;
	p->reg[5] = 7;

	M64_set_cpu_bank(p, p_host);
	M64_set_ppu_bank(p, p_host);

	if(!p->tlsrom)
	{
		if(p_host->rom.mirror_type != MIRROR_FOUR_SCREEN)
		{
			ines_ppu_set_mirror_type(&p_host->ppu, MIRROR_VERT);
		}
	}

	INES_LOG(LOG_INF, MOD_MMC, ISTR("mapper64: Tengen RAMBO-1 (PRG %dKB, CHR %dKB%s)\n"),
			 (ines_int_t)(p_host->prom_8k_num * 8), (ines_int_t)p_host->vrom_1k_num,
			 p->tlsrom ? ISTR(", TLSROM mirror") : ISTR(""));
}

static void mapper64_writehigh(ines_mapper_t* p_mapper, ines_word_t addr, ines_byte_t val)
{
	M64_data_t*  p = mapper2M64data(p_mapper);
	ines_host_t*  p_host = mapper2host(p_mapper);
	ines_byte_t   r;

	if(p == NULL)
		return;

	INES_LOG(LOG_DBG, MOD_MMC, ISTR("M64: $%04X = #$%02X.\n"), (ines_int_t)addr, (ines_int_t)val);

	switch(addr & 0xE001)
	{
	case 0x8000:   /* Bank select [CPKx RRRR] */
		p->cmd = val;
		/* 与 MMC3 一样，改 C/P/K 会立刻改变窗口映射 */
		M64_set_cpu_bank(p, p_host);
		M64_set_ppu_bank(p, p_host);
		break;

	case 0x8001:   /* Bank data：写入 RRRR 选中的寄存器 */
		r = (ines_byte_t)(p->cmd & M64_CMD_REG_MASK);
		p->reg[r] = val;
		if(r <= 5 || r == 8 || r == 9)
		{
			M64_set_ppu_bank(p, p_host);
		}
		else if(r == 6 || r == 7 || r == 15)
		{
			M64_set_cpu_bank(p, p_host);
		}
		else
		{
			/* RRRR = A..E 在硬件上未定义，忽略 */
		}
		break;

	case 0xA000:   /* 镜像（仅 mapper 64；158 的镜像由 CHR 页 bit7 决定） */
		if(!p->tlsrom && p_host->rom.mirror_type != MIRROR_FOUR_SCREEN)
		{
			ines_ppu_set_mirror_type(&p_host->ppu, (val & 0x01) ? MIRROR_HORZ : MIRROR_VERT);
		}
		break;

	case 0xA001:   /* 未实现（RAMBO-1 无 PRG-RAM） */
		break;

	case 0xC000:   /* IRQ latch */
		p->irq_latch = val;
		break;

	case 0xC001:   /* IRQ 模式 + 请求重载 */
		p->irq_mode = (ines_byte_t)(val & 0x01);
		p->irq_reload = 1;
		break;

	case 0xE000:   /* IRQ 应答 / 禁止 */
		p->irq_enabled = 0;
		ines_cpu_IRQ(&p_host->cpu, MMC_IRQ_MASK, ines_false);
		break;

	case 0xE001:   /* IRQ 允许 */
		p->irq_enabled = 1;
		break;

	default:
		break;
	}
}

static void mapper64_hsync(ines_mapper_t* p_mapper, ines_int_t line)
{
	M64_data_t*  p = mapper2M64data(p_mapper);
	ines_host_t*  p_host = mapper2host(p_mapper);
	ines_int64_t  now;
	ines_int_t    delta;

	if(p == NULL)
		return;

	now = p_host->cpu.total_cycles;

	if(!p->irq_enabled)
	{
		/* 未使能时也要推进时间基准，否则下次的增量会把关中断期间的周期一次性灌进来 */
		p->last_cycles = now;
		return;
	}

	if(p->irq_mode == M64_IRQ_MODE_CPUCYCLE)
	{
		/* CPU 周期模式：把自上次回调以来真实流逝的 CPU 周期按 4 个一批推进 */
		delta = (ines_int_t)(now - p->last_cycles);
		p->last_cycles = now;
		if(delta <= 0)
			return;

		p->cycle_acc = (ines_word_t)(p->cycle_acc + delta);
		while(p->cycle_acc >= M64_CPU_CYCLES_PER_TICK)
		{
			p->cycle_acc = (ines_word_t)(p->cycle_acc - M64_CPU_CYCLES_PER_TICK);
			M64_clock_irq(p, p_host);
		}
		return;
	}

	/* 扫描线模式：只在可见扫描线且开屏时计数 */
	if(line < 0 || line > 239)
		return;
	if((p_host->ppu.reg_ctrl_2 & (PPU_ENABLE_BG | PPU_ENABLE_SPR)) == 0)
		return;

	M64_clock_irq(p, p_host);
}

void mapper64_fini(ines_mapper_t* p_mapper)
{
	ines_free(p_mapper->p_data);
	p_mapper->p_data = NULL;
}

/**
 * 创建 RAMBO-1（tlsrom = 0 时是 mapper 64 的镜像接法，非 0 时是 mapper 158 的）。
 * @param p_mapper mapper 对象
 * @param tlsrom   非 0 表示 CIRAM A10 接 CHR A17（mapper 158 / Alien Syndrome）
 * @return ines_true（已实现）
 */
ines_bool_t  mapper64_create_variant(ines_mapper_t* p_mapper, ines_int_t tlsrom)
{
	if(p_mapper == NULL)
		return ines_false;

	INIT_MAPPER_DATA_ST(p_mapper, M64_data_t);
	mapper2M64data(p_mapper)->tlsrom = (ines_byte_t)(tlsrom ? 1 : 0);

	p_mapper->reset = mapper64_reset;
	p_mapper->writehigh = mapper64_writehigh;
	p_mapper->hsync = mapper64_hsync;
	p_mapper->fini = mapper64_fini;

	return ines_true;
}

ines_bool_t  mapper64_create(ines_mapper_t* p_mapper)
{
	return mapper64_create_variant(p_mapper, 0);
}

