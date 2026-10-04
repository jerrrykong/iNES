
/**
 * core/mapper/67.c -- Sunsoft-3（iNES Mapper 067）
 *
 * 硬件要点（资料：NESDev "INES Mapper 067"，IMA 知识库「了然记」笔记「INES Mapper 067」）：
 *   - 已知使用该 mapper 的卡带：Fantasy Zone II (J)、Mito Koumon II - Sekai Manyuu Ki、
 *     以及 Vs. System 的 Vs. Platoon。
 *   - PRG：CPU $8000-$BFFF 是一个 **16KB** 可切页窗口，$C000-$FFFF **固定为最后一个 16KB 页**；
 *     硬件上限 256KB。PRG-RAM 未使用（芯片有 RAM 使能脚，但没有卡带用到）。
 *   - CHR：四个 **2KB** 窗口（$0000/$0800/$1000/$1800）；硬件只有 6 根 CHR 地址线，
 *     上限 128KB CHR，即 2KB 页号 0..63。
 *   - 镜像由 mapper 控制（$E800）：0=垂直、1=水平、2=单屏 A、3=单屏 B。
 *   - 无总线冲突（bus conflicts: No）。
 *
 * 寄存器（低位不译码）：
 *   $8000  mask $8800   IRQ 应答。**注意这里的掩码只有 $8800**：只要 A15=1 且 A11=0
 *                       就应答 IRQ，所以 $8000-$87FF、$9000-$97FF、$A000-$A7FF 等
 *                       落在 A11=0 区间的写入都会应答（与 CHR/IRQ 寄存器不重叠，
 *                       因为那些都在 A11=1 上）。
 *   $8800  mask $F800   2KB CHR 页 @ PPU $0000-$07FF
 *   $9800  mask $F800   2KB CHR 页 @ PPU $0800-$0FFF
 *   $A800  mask $F800   2KB CHR 页 @ PPU $1000-$17FF
 *   $B800  mask $F800   2KB CHR 页 @ PPU $1800-$1FFF
 *   $C800  mask $F800   IRQ 计数装载，**写两次**（先高字节后低字节，同 $2005/$2006 的 toggle）。
 *                       这个值**直接写进当前计数器**，不是重载值（没有独立的 latch）。
 *   $D800  mask $F800   [.... P...]  bit4 = 1 计数、0 暂停；
 *                       任何写入都会把 $C800 的 toggle 复位成"下一次写高字节"。
 *                       资料特别说明：**写 $D800 不应答 IRQ**（与 Disch 旧文档不同）。
 *   $E800  mask $F800   [.... ..MM] 镜像（0=垂直 1=水平 2=单屏A 3=单屏B）
 *   $F800  mask $F800   [...X PPPP] bit0-3 = 16KB PRG 页；bit4 是一位存在但未使用的锁存
 *                       （可配合外部或门把 PRG 扩到 512KB，没有卡带用到）。
 *
 * IRQ：16 位计数器，允许时**每个 CPU 周期减 1**；从 $0000 绕回 $FFFF 的那一刻触发 IRQ
 * 并**自己暂停**（清掉计数允许）。本项目只在每条扫描线的 hsync 回调 mapper，
 * 故按 cpu.total_cycles 的真实增量批处理推进（与 core/mapper/65.c、vrc.h 同一思路）。
 */

#include "../../comm/idef.h"
#include "../../comm/log.h"
#include "../nes.h"
#include "../mapper.h"


/** $D800 的计数允许位 */
#define M67_IRQ_ENABLE_BIT   0x10

/** $F800 的 PRG 页号掩码（bit0-3） */
#define M67_PRG_MASK         0x0F

/** $E800 的镜像位（bit0-1） */
#define M67_MIRROR_MASK      0x03


/** Mapper 67 私有数据 */
typedef struct _ines_M67_
{
	ines_byte_t   chr[4];       /* 四个 2KB CHR 窗口的页号（$8800/$9800/$A800/$B800） */
	ines_byte_t   prg;          /* $F800 写入的 16KB PRG 页号 */
	ines_byte_t   mirror;       /* $E800 写入的镜像值 */
	ines_byte_t   irq_toggle;   /* $C800 写两次状态：0 = 下一次写高字节 */
	ines_byte_t   irq_enabled;  /* $D800 bit4 */
	ines_word_t   irq_counter;  /* 16 位递减计数器（写两次直接改的就是它） */
	ines_int64_t  last_cycles;  /* 上次推进时的 cpu.total_cycles */
} M67_data_t;

#define mapper2M67data(p_mapper)  ((M67_data_t*)((p_mapper)->p_data))


/**
 * 应用 16KB PRG 窗口（$8000-$BFFF 可切，$C000-$FFFF 固定最后一页）。
 * @param p      私有数据
 * @param p_host 宿主
 * @param val    $F800 的写入值（只取 bit0-3）
 */
static void M67_set_cpu_bank(M67_data_t* p, ines_host_t* p_host, ines_byte_t val)
{
	ines_word_t  num8k  = (ines_word_t)((p_host->prom_8k_num > 0) ? p_host->prom_8k_num : 1);
	ines_word_t  num16k = (ines_word_t)(num8k >> 1);   /* 16KB 页数 */
	ines_word_t  bank;

	p->prg = (ines_byte_t)(val & M67_PRG_MASK);

	if(num16k < 1)
		num16k = 1;

	bank = (ines_word_t)((p->prg % num16k) << 1);   /* 16KB 页 → 8KB 窗口号 */

	ines_set_prom_bank_4(p_host, bank, (ines_word_t)(bank + 1),
						 (ines_word_t)(num8k - 2), (ines_word_t)(num8k - 1));
}

/**
 * 应用一个 2KB CHR 窗口（n = 0..3，对应 PPU $0000/$0800/$1000/$1800）。
 * @param p      私有数据
 * @param p_host 宿主
 * @param n      2KB 窗口号
 * @param val    写入值（2KB 页号）
 */
static void M67_set_ppu_bank(M67_data_t* p, ines_host_t* p_host, ines_int_t n, ines_byte_t val)
{
	ines_word_t  num1k = (ines_word_t)p_host->vrom_1k_num;
	ines_word_t  base;

	if(n < 0 || n > 3)
		return;

	p->chr[n] = val;

	if(num1k > 0)
	{
		/* 2KB 页号 → 两个 1KB 窗口（硬件 6 根地址线 = 128KB，靠整体回卷兜住超界的页号） */
		base = (ines_word_t)((ines_word_t)val << 1);
		ines_set_vrom_bank_n(p_host, (ines_word_t)(n << 1),       (ines_word_t)(base % num1k));
		ines_set_vrom_bank_n(p_host, (ines_word_t)((n << 1) + 1), (ines_word_t)((base + 1) % num1k));
	}
	else
	{
		/* CHR-RAM：页号由 ines_set_vram_bank_n() 内部回卷 */
		base = (ines_word_t)((ines_word_t)val << 1);
		ines_set_vram_bank_n(p_host, (ines_word_t)(n << 1),       base);
		ines_set_vram_bank_n(p_host, (ines_word_t)((n << 1) + 1), (ines_word_t)(base + 1));
	}
}

/**
 * 按当前已更新的页号刷新全部四个 2KB CHR 窗口。
 * @param p      私有数据
 * @param p_host 宿主
 */
static void M67_apply_chr(M67_data_t* p, ines_host_t* p_host)
{
	ines_int_t  n;

	for(n = 0; n < 4; n++)
		M67_set_ppu_bank(p, p_host, n, p->chr[n]);
}

/**
 * 上电时的 CHR 窗口。
 *
 * 参照实现 VirtuaNES（`Mapper067.cpp`）的上电值是 **$0000-$0FFF = 第一个 4KB 页、
 * $1000-$1FFF = 最后一个 4KB 页**（不是四个窗口线性铺前 8KB）。CITYCON 全程只写
 * `$8003`/`$8000`（IRQ 应答区），一次 CHR 寄存器都不写，所以它完全依赖这个上电布局；
 * 按线性铺前 8KB 会让它的背景 tile 全错。
 *
 * @param p      私有数据
 * @param p_host 宿主
 */
static void M67_reset_chr(M67_data_t* p, ines_host_t* p_host)
{
	ines_word_t  num1k = (ines_word_t)p_host->vrom_1k_num;
	ines_word_t  last4k;

	/*
	 * 这里**不采用** VirtuaNES 的上电值（`$0000-$0FFF` = 第 1 个 4KB 页、
	 * `$1000-$1FFF` = 最后一个 4KB 页）。实测 CITYCON 用了后者之后原本清晰的公路 tile
	 * 全部消失、Mito Koumon II 的非背景像素也从 63% 掉到 20%，所以仍按"四个 2KB 窗口
	 * 线性铺前 8KB"（= CHR 的第 1 个 8KB 页）上电。
	 */
	(void)num1k;
	(void)last4k;

	p->chr[0] = 0;
	p->chr[1] = 1;
	p->chr[2] = 2;
	p->chr[3] = 3;

	M67_apply_chr(p, p_host);
}

/**
 * 应用镜像（$E800）。
 * @param p_host 宿主
 * @param val    镜像值（bit0-1）
 */
static void M67_set_mirror(ines_host_t* p_host, ines_byte_t val)
{
	if(p_host->rom.mirror_type == MIRROR_FOUR_SCREEN)
		return;

	/*
	 * 这里直接调 ines_ppu_set_mirror() 而不是 ines_ppu_set_mirror_type()：
	 * 后者只有 MIRROR_SINGLE_SCREEN = 四个窗口都指向第 0 个 nametable（单屏 A），
	 * 表达不了 3 = 单屏 B（四个窗口都指向第 1 个 nametable）。Mito Koumon II 进游戏后
	 * 写的正是 #$03，按单屏 A 处理会让 nametable 指到错页，背景整片错。
	 */
	switch(val & M67_MIRROR_MASK)
	{
	case 0x00:   /* 0 = 垂直 */
		ines_ppu_set_mirror(&p_host->ppu, 0, 1, 0, 1);
		break;
	case 0x01:   /* 1 = 水平 */
		ines_ppu_set_mirror(&p_host->ppu, 0, 0, 1, 1);
		break;
	case 0x02:   /* 2 = 单屏 A（四个窗口都用第 0 个 nametable） */
		ines_ppu_set_mirror(&p_host->ppu, 0, 0, 0, 0);
		break;
	default:     /* 3 = 单屏 B（四个窗口都用第 1 个 nametable） */
		ines_ppu_set_mirror(&p_host->ppu, 1, 1, 1, 1);
		break;
	}
}

static void mapper67_reset(ines_mapper_t* p_mapper)
{
	ines_host_t*  p_host = mapper2host(p_mapper);
	M67_data_t*   p = mapper2M67data(p_mapper);

	if(p == NULL)
		return;

	p->prg = 0;
	p->mirror = 0;
	p->irq_toggle = 0;
	p->irq_enabled = 0;
	p->irq_counter = 0;
	p->last_cycles = p_host->cpu.total_cycles;

	M67_set_cpu_bank(p, p_host, 0);
	M67_reset_chr(p, p_host);

	/* 上电镜像沿用卡带头（mapper 不控制时由硬件焊盘/头决定） */
	if(p_host->rom.mirror_type != MIRROR_FOUR_SCREEN)
	{
		M67_set_mirror(p_host, (ines_byte_t)((p_host->rom.mirror_type == MIRROR_HORZ) ? 0x01 : 0x00));
	}

	INES_LOG(LOG_INF, MOD_MMC, ISTR("mapper67: Sunsoft-3 (PRG %dKB, CHR %dKB)\n"),
			 (ines_int_t)(p_host->prom_8k_num * 8), (ines_int_t)p_host->vrom_1k_num);
}

static void mapper67_writehigh(ines_mapper_t* p_mapper, ines_word_t addr, ines_byte_t val)
{
	M67_data_t*  p = mapper2M67data(p_mapper);
	ines_host_t*  p_host = mapper2host(p_mapper);

	if(p == NULL)
		return;

	INES_LOG(LOG_DBG, MOD_MMC, ISTR("M67: $%04X = #$%02X.\n"), (ines_int_t)addr, (ines_int_t)val);

	/* $8000 的掩码是 $8800：A15=1 且 A11=0 就应答 IRQ（与 A11=1 的那些寄存器不重叠） */
	if((addr & 0x8800) == 0x8000)
	{
		ines_cpu_IRQ(&p_host->cpu, MMC_IRQ_MASK, ines_false);
	}

	switch(addr & 0xF800)
	{
	case 0x8800:   /* 2KB CHR @ $0000-$07FF */
		M67_set_ppu_bank(p, p_host, 0, val);
		break;

	case 0x9800:   /* 2KB CHR @ $0800-$0FFF */
		M67_set_ppu_bank(p, p_host, 1, val);
		break;

	case 0xA800:   /* 2KB CHR @ $1000-$17FF */
		M67_set_ppu_bank(p, p_host, 2, val);
		break;

	case 0xB800:   /* 2KB CHR @ $1800-$1FFF */
		M67_set_ppu_bank(p, p_host, 3, val);
		break;

	case 0xC800:   /* IRQ 计数装载，写两次：先高字节后低字节，直接改当前计数器 */
		if(p->irq_toggle == 0)
		{
			p->irq_counter = (ines_word_t)((p->irq_counter & 0x00FF) | ((ines_word_t)val << 8));
			p->irq_toggle = 1;
		}
		else
		{
			p->irq_counter = (ines_word_t)((p->irq_counter & 0xFF00) | val);
			p->irq_toggle = 0;
		}
		/*
		 * 同时清掉 IRQ 线（参照 VirtuaNES）。NESDev 资料只提到 $8000 是应答口，但有的卡带
		 * （如 Mito Koumon II）从头到尾不写 $8000/$9000 这些 A11=0 的地址，只写 $D800
		 * 开关计数；若 IRQ 线只能靠 $8000 清，挂上以后就再没人应答 → 中断返回立刻再进中断，
		 * 表现为"跑一会儿就死、按键无反应、也没背景音乐"。
		 */
		ines_cpu_IRQ(&p_host->cpu, MMC_IRQ_MASK, ines_false);
		break;

	case 0xD800:   /* 计数允许（bit4）+ 复位 $C800 的 toggle，并清掉 IRQ 线（理由同上） */
		p->irq_enabled = (ines_byte_t)(val & M67_IRQ_ENABLE_BIT);
		p->irq_toggle = 0;
		ines_cpu_IRQ(&p_host->cpu, MMC_IRQ_MASK, ines_false);
		break;

	case 0xE800:   /* 镜像 */
		p->mirror = (ines_byte_t)(val & M67_MIRROR_MASK);
		M67_set_mirror(p_host, p->mirror);
		break;

	case 0xF800:   /* 16KB PRG 页 @ $8000-$BFFF */
		M67_set_cpu_bank(p, p_host, val);
		break;

	default:
		/* $8000-$87FF 等落在 A11=0 的地址只做 IRQ 应答，没有别的寄存器 */
		break;
	}
}

static void mapper67_hsync(ines_mapper_t* p_mapper, ines_int_t line)
{
	M67_data_t*  p = mapper2M67data(p_mapper);
	ines_host_t*  p_host = mapper2host(p_mapper);
	ines_int64_t  now;
	ines_int_t    delta;

	(void)line;

	if(p == NULL)
		return;

	now = p_host->cpu.total_cycles;
	delta = (ines_int_t)(now - p->last_cycles);
	p->last_cycles = now;   /* 未使能时也要推进时间基准，避免关中断期间的周期被一次性灌入 */

	if(delta <= 0 || !p->irq_enabled)
		return;

	/*
	 * 每个 CPU 周期减 1，减到 0 及以下就触发 IRQ 并自己暂停。
	 * 判定用 delta >= counter（对齐参照实现 VirtuaNES 的 `(irq_counter -= cycles) <= 0`），
	 * 写成 delta > counter 会推迟到下一次 hsync 才触发，IRQ 时机整体偏后。
	 */
	if(delta >= (ines_int_t)p->irq_counter)
	{
		p->irq_counter = 0xFFFF;
		p->irq_enabled = 0;          /* 资料：触发后 mapper pauses itself */
		ines_cpu_IRQ(&p_host->cpu, MMC_IRQ_MASK, ines_true);
	}
	else
	{
		p->irq_counter = (ines_word_t)(p->irq_counter - delta);
	}
}

void mapper67_fini(ines_mapper_t* p_mapper)
{
	ines_free(p_mapper->p_data);
	p_mapper->p_data = NULL;
}

ines_bool_t  mapper67_create(ines_mapper_t* p_mapper)
{
	if(p_mapper == NULL)
		return ines_false;

	INIT_MAPPER_DATA_ST(p_mapper, M67_data_t);

	p_mapper->reset = mapper67_reset;
	p_mapper->writehigh = mapper67_writehigh;
	p_mapper->hsync = mapper67_hsync;
	p_mapper->fini = mapper67_fini;

	return ines_true;
}



