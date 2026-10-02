
/**
 * core/mapper/65.c -- Irem H3001（iNES Mapper 065）
 *
 * 硬件要点（资料：NESDev "INES Mapper 065"，IMA 笔记「INES Mapper 065」）：
 *   - 已知游戏：Daiku no Gen San 2、Kaiketsu Yanchamaru 3、Spartan X 2 等。
 *   - 资料特别纠正了 Disch 的旧文档：**不存在可切换的 $C000 页**（"incorrectly believing
 *     in a changeable bank at $C000. It doesn't exist"），所以 $C000 写入一律忽略。
 *   - 地址掩码 $F007。
 *
 * 寄存器：
 *   $8000-$8007  PRG Reg 0：8KB 页，映射到 $8000 或 $C000（由 $9000 bit7 决定）
 *   $A000-$A007  PRG Reg 1：8KB 页，恒映射到 $A000-$BFFF
 *   $B000-$B007  CHR 页：$0000/$0400/.../$1C00 八个 1KB 窗口
 *   $9000        [X... ....] PRG 布局：0 = $8000 ← Reg 0 且 $C000 恒 $3E；
 *                                     1 = $C000 ← Reg 0 且 $8000 恒 $3E；
 *                                     $E000 恒为 $3F（$3E/$3F 按卡带 PRG 容量回卷）
 *   $9001        [MM.. ....] 镜像：%00 = 垂直，%10 = 水平，%01/%11 = 单屏 A
 *   $9003        [E... ....] IRQ 允许（bit7）；写入同时应答（清除）已挂起的 IRQ
 *   $9004        [.... ....] 把 16 位重载值装入计数器；写入同时应答 IRQ
 *   $9005        IRQ 重载值**高** 8 位（注意是高字节，不是低字节）
 *   $9006        IRQ 重载值**低** 8 位
 *
 * IRQ：一个 16 位递减计数器，允许时**每个 CPU 周期减 1**；减到 0 触发 IRQ，
 * 并**停在 0**（不回卷、不自动重载），只有写 $9004 才会重新装入重载值。
 * 本项目只在每条扫描线的 hsync 处回调 mapper，故按 cpu.total_cycles 的真实增量
 * 批处理推进（与 core/mapper/vrc.h、mapper 64 的 CPU 周期模式同一思路）。
 *
 * 上电：PRG Reg 0 = $00、Reg 1 = $01 —— 资料明确写了"Games do rely on this and will
 * crash otherwise"，故复位时必须给出这两个初值。
 */

#include "../../comm/idef.h"
#include "../../comm/log.h"
#include "../nes.h"
#include "../mapper.h"


/** $9000 的 PRG 布局位（bit7） */
#define M65_LAYOUT_BIT      0x80

/** $9001 镜像位（bit7..6） */
#define M65_MIRROR_SHIFT    6

/** $9003 IRQ 允许位（bit7） */
#define M65_IRQ_ENABLE_BIT  0x80

/** PRG 固定的两个页（$C000 / $E000），按卡带容量回卷 */
#define M65_PRG_FIXED_C000  0x3E
#define M65_PRG_FIXED_E000  0x3F


/** Mapper 65 私有数据 */
typedef struct _ines_M65_
{
	ines_byte_t   prg_reg0;    /* $8000 写入值（映射到 $8000 或 $C000） */
	ines_byte_t   prg_reg1;    /* $A000 写入值（恒映射到 $A000） */
	ines_byte_t   layout;      /* $9000 bit7：非 0 时 Reg 0 映射到 $C000 */
	ines_byte_t   irq_enabled; /* $9003 bit7 */
	ines_word_t   irq_latch;   /* 16 位重载值（$9005 高字节 + $9006 低字节） */
	ines_word_t   irq_counter; /* 16 位递减计数器，到 0 停住 */
	ines_int64_t  last_cycles; /* 上次推进时的 cpu.total_cycles */
} M65_data_t;

#define mapper2M65data(p_mapper)  ((M65_data_t*)((p_mapper)->p_data))


/**
 * 按当前布局应用 4 个 PRG 8KB 窗口。
 * @param p      私有数据
 * @param p_host 宿主
 */
static void M65_set_cpu_bank(M65_data_t* p, ines_host_t* p_host)
{
	ines_word_t  num = (ines_word_t)((p_host->prom_8k_num > 0) ? p_host->prom_8k_num : 1);
	ines_word_t  r0  = (ines_word_t)p->prg_reg0;
	ines_word_t  r1  = (ines_word_t)p->prg_reg1;
	ines_word_t  fixed_c = (ines_word_t)(M65_PRG_FIXED_C000 % num);
	ines_word_t  fixed_e = (ines_word_t)(M65_PRG_FIXED_E000 % num);

	if(p->layout)
	{
		/* $8000 恒 $3E，$C000 ← Reg 0 */
		ines_set_prom_bank_4(p_host, fixed_c, (ines_word_t)(r1 % num),
							 (ines_word_t)(r0 % num), fixed_e);
	}
	else
	{
		/* $8000 ← Reg 0，$C000 恒 $3E */
		ines_set_prom_bank_4(p_host, (ines_word_t)(r0 % num), (ines_word_t)(r1 % num),
							 fixed_c, fixed_e);
	}
}

/**
 * 应用 8 个 1KB CHR 窗口。
 * @param p      私有数据
 * @param p_host 宿主
 */
static void M65_set_ppu_bank(M65_data_t* p, ines_host_t* p_host, ines_int_t n, ines_byte_t val)
{
	ines_word_t  num1k = p_host->vrom_1k_num;

	if(num1k > 0)
	{
		ines_set_vrom_bank_n(p_host, (ines_word_t)n, (ines_word_t)(val % num1k));
	}
	else
	{
		/* CHR-RAM：页号由 ines_set_vram_bank_n() 内部回卷 */
		ines_set_vram_bank_n(p_host, (ines_word_t)n, (ines_word_t)val);
	}
}

static void mapper65_reset(ines_mapper_t* p_mapper)
{
	ines_host_t*  p_host = mapper2host(p_mapper);
	M65_data_t*   p = mapper2M65data(p_mapper);
	ines_int_t    n;

	if(p == NULL)
		return;

	/* 资料：上电时 PRG Reg 0 = $00、Reg 1 = $01，游戏依赖这两个初值 */
	p->prg_reg0 = 0;
	p->prg_reg1 = 1;
	p->layout = 0;

	M65_set_cpu_bank(p, p_host);

	for(n = 0; n < 8; n++)
	{
		M65_set_ppu_bank(p, p_host, n, (ines_byte_t)n);
	}

	if(p_host->rom.mirror_type != MIRROR_FOUR_SCREEN)
	{
		ines_ppu_set_mirror_type(&p_host->ppu, MIRROR_VERT);
	}

	p->irq_enabled = 0;
	p->irq_latch = 0;
	p->irq_counter = 0;
	p->last_cycles = p_host->cpu.total_cycles;

	INES_LOG(LOG_INF, MOD_MMC, ISTR("mapper65: Irem H3001 (PRG %dKB, CHR %dKB)\n"),
			 (ines_int_t)(p_host->prom_8k_num * 8), (ines_int_t)p_host->vrom_1k_num);
}

static void mapper65_writehigh(ines_mapper_t* p_mapper, ines_word_t addr, ines_byte_t val)
{
	M65_data_t*  p = mapper2M65data(p_mapper);
	ines_host_t*  p_host = mapper2host(p_mapper);

	if(p == NULL)
		return;

	INES_LOG(LOG_DBG, MOD_MMC, ISTR("M65: $%04X = #$%02X.\n"), (ines_int_t)addr, (ines_int_t)val);

	switch(addr & 0xF007)
	{
	case 0x8000:   /* PRG Reg 0（映射到 $8000 或 $C000，取决于 $9000 bit7） */
		p->prg_reg0 = val;
		M65_set_cpu_bank(p, p_host);
		break;

	case 0xA000:   /* PRG Reg 1：恒映射到 $A000 */
		p->prg_reg1 = val;
		M65_set_cpu_bank(p, p_host);
		break;

	case 0x9000:   /* PRG 布局 */
		p->layout = (ines_byte_t)(val & M65_LAYOUT_BIT);
		M65_set_cpu_bank(p, p_host);
		break;

	case 0x9001:   /* 镜像 */
		switch((val >> M65_MIRROR_SHIFT) & 0x03)
		{
		case 0x00:   /* %00 = 垂直 */
			ines_ppu_set_mirror_type(&p_host->ppu, MIRROR_VERT);
			break;
		case 0x02:   /* %10 = 水平 */
			ines_ppu_set_mirror_type(&p_host->ppu, MIRROR_HORZ);
			break;
		default:     /* %01 / %11 = 单屏 A */
			ines_ppu_set_mirror_type(&p_host->ppu, MIRROR_SINGLE_SCREEN);
			break;
		}
		break;

	case 0x9003:   /* IRQ 允许 + 应答 */
		p->irq_enabled = (ines_byte_t)(val & M65_IRQ_ENABLE_BIT);
		ines_cpu_IRQ(&p_host->cpu, MMC_IRQ_MASK, ines_false);
		break;

	case 0x9004:   /* 装入重载值 + 应答 */
		p->irq_counter = p->irq_latch;
		ines_cpu_IRQ(&p_host->cpu, MMC_IRQ_MASK, ines_false);
		break;

	case 0x9005:   /* 重载值高 8 位（注意是高字节） */
		p->irq_latch = (ines_word_t)((p->irq_latch & 0x00FF) | ((ines_word_t)val << 8));
		break;

	case 0x9006:   /* 重载值低 8 位 */
		p->irq_latch = (ines_word_t)((p->irq_latch & 0xFF00) | val);
		break;

	case 0xB000:   /* CHR 八个 1KB 窗口 */
	case 0xB001:
	case 0xB002:
	case 0xB003:
	case 0xB004:
	case 0xB005:
	case 0xB006:
	case 0xB007:
		M65_set_ppu_bank(p, p_host, (ines_int_t)(addr & 0x0007), val);
		break;

	default:
		/* $9002、$9007、$C000 等在硬件上不存在（$C000 不可切换，见文件头说明） */
		break;
	}
}

static void mapper65_hsync(ines_mapper_t* p_mapper, ines_int_t line)
{
	M65_data_t*  p = mapper2M65data(p_mapper);
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

	/* 每 CPU 周期减 1；减到 0 触发一次并停住（不回卷、不自动重载） */
	if(p->irq_counter > 0)
	{
		if(delta >= (ines_int_t)p->irq_counter)
		{
			p->irq_counter = 0;
			ines_cpu_IRQ(&p_host->cpu, MMC_IRQ_MASK, ines_true);
		}
		else
		{
			p->irq_counter = (ines_word_t)(p->irq_counter - delta);
		}
	}
}

void mapper65_fini(ines_mapper_t* p_mapper)
{
	ines_free(p_mapper->p_data);
	p_mapper->p_data = NULL;
}

ines_bool_t  mapper65_create(ines_mapper_t* p_mapper)
{
	if(p_mapper == NULL)
		return ines_false;

	INIT_MAPPER_DATA_ST(p_mapper, M65_data_t);

	p_mapper->reset = mapper65_reset;
	p_mapper->writehigh = mapper65_writehigh;
	p_mapper->hsync = mapper65_hsync;
	p_mapper->fini = mapper65_fini;

	return ines_true;
}

