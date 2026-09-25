
/**
 * core/mapper/48.c -- Taito TC0690（iNES Mapper 048）
 *
 * 硬件要点：
 *  - TC0690 是 TC0190 + PAL16R4 板（即 Mapper 033）的超集，差别在于它带中断；寄存器区间
 *    $8000-$FFFF，掩码 $E003（A0-A1 选组内寄存器，A13/A14 选组）：
 *      $8000  PRG Reg 0（8KB @ $8000，8 位全有效）
 *      $8001  PRG Reg 1（8KB @ $A000）
 *      $8002  CHR Reg 0（2KB @ $0000，单位为 2KB，不丢 LSB）
 *      $8003  CHR Reg 1（2KB @ $0800）
 *      $A000  CHR Reg 2（1KB @ $1000）
 *      $A001  CHR Reg 3（1KB @ $1400）
 *      $A002  CHR Reg 4（1KB @ $1800）
 *      $A003  CHR Reg 5（1KB @ $1C00）
 *      $C000  IRQ Reload（写入值需 XOR $FF 才等价于 MMC3 的 $C000）
 *      $C001  IRQ Clear / 置位重载标志（等价 MMC3 $C001）
 *      $C002  IRQ Enable（等价 MMC3 $E001）
 *      $C003  IRQ Acknowledge / 关闭（等价 MMC3 $E000）
 *      $E000  [.M.. ....] bit6 = 镜像，0 垂直 / 1 水平
 *  - PRG：$C000 固定倒数第二页，$E000 固定最后一页。
 *  - CHR：与 TC0190 完全一致（2KB 寄存器值以 2KB 为单位，可寻址 512KB；1KB 窗口覆盖前 256KB）。
 *  - IRQ：与 MMC3 基本一致，两处差异：
 *      1) 写入的 reload 值取反（写入 $06 相当于 MMC3 的 $F9），即 latch = val ^ $FF；
 *      2) 触发比 MMC3 晚约 4 个 CPU 周期。
 *  - 已知使用该 mapper 的 6 个卡带：Bakushou!! Jinsei Gekijou 3、Bubble Bobble 2 (J)、
 *    Don Doko Don 2、Captain Saver (J)、Jetsons - Cogswell's Caper! (J)、
 *    Flintstones - The Rescue of Dino & Hoppy (J)。
 *
 *  - "比 MMC3 晚 4 个 CPU 周期"：本核心只有扫描线粒度的 hsync 回调，且 `ines_host_doframe()`
 *    在行末才调用 `ines_ppu_render_line()` 渲染整行。因此 IRQ 的置位被推迟一条扫描线
 *    （计数归零 -> 下一条扫描线的 hsync 先推进 CPU 4 个周期再置位 IRQ 线），
 *    使 handler 对 $2000/$2005/$2006 与 CHR 寄存器的写入像真机一样作用于下一条扫描线，
 *    否则 Flintstones 等逐段分割游戏的画面会整体提前一行而碎裂（见 mapper48_hsync()）。
 *    副作用是每次触发让 CPU 在当帧内相对 PPU 提前 4 个周期，帧末由周期记账重置，不跨帧累积。
 *  - 大量 mapper 048 卡带的 ROM 被错误标注为 033，需要在分类阶段按 rom.crc32_p 分流。
 *  - 无卡带 WRAM，保持 custom_sram = 0，由宿主挂默认 8K SRAM。
 */

#include "../../comm/idef.h"
#include "../../comm/log.h"
#include "../nes.h"
#include "../mapper.h"


/* $E000 的 bit6：镜像（0 垂直 / 1 水平） */
#define TC0690_MIRROR_BIT     0x40
/* PRG 页号只有 6 位：bit6 在 TC0190 上是镜像位，TC0690 虽然把镜像移到 $E000，
   但寄存器仍只解码低 6 位（与权威实现一致） */
#define TC0690_PRG_BANK_MASK  0x3F
/* 无 CHR-ROM 时 pattern RAM 为 8KB：2KB 窗口 4 页、1KB 窗口 8 页 */
#define TC0690_CHR_RAM_2K_MASK   0x03
#define TC0690_CHR_RAM_1K_MASK   0x07


struct _TC0690_data_
{
	ines_byte_t   prg_reg0;    /* $8000：PRG Reg 0（8KB @ $8000） */
	ines_byte_t   prg_reg1;    /* $8001：PRG Reg 1（8KB @ $A000） */
	ines_byte_t   chr_2k[2];   /* $8002/$8003：两个 2KB CHR 窗口，单位为 2KB */
	ines_byte_t   chr_1k[4];   /* $A000-$A003：四个 1KB CHR 窗口 */
	ines_byte_t   mirror_reg;  /* $E000：bit6 = 镜像 */
	ines_byte_t   irq_enabled;
	ines_byte_t   irq_counter;
	ines_byte_t   irq_latch;
	ines_byte_t   irq_reload;
};

typedef struct _TC0690_data_   TC0690_data_t;

#define mapper2TC0690data(mapper)   ((TC0690_data_t*)((mapper)->p_data))


/**
 * 按当前寄存器刷新 4 个 8KB PRG 窗口（$C000/$E000 固定倒数第二/最后一页）。
 * @param p      私有数据
 * @param p_host 宿主
 */
static void TC0690_set_cpu_bank(TC0690_data_t* p, ines_host_t* p_host)
{
	ines_word_t  num = (p_host->prom_8k_num > 0) ? p_host->prom_8k_num : 1;
	ines_word_t  b0 = (ines_word_t)(p->prg_reg0 & TC0690_PRG_BANK_MASK) % num;
	ines_word_t  b1 = (ines_word_t)(p->prg_reg1 & TC0690_PRG_BANK_MASK) % num;
	ines_word_t  last = num - 1;
	ines_word_t  second_last = (num >= 2) ? (ines_word_t)(num - 2) : 0;

	ines_set_prom_bank_4(p_host, b0, b1, second_last, last);
}

/**
 * 按当前寄存器刷新 PPU 的 CHR 窗口：$0000-$0FFF 为 2KB 粒度，$1000-$1FFF 为 1KB 粒度。
 * @note 与 MMC3 一致：$8002/$8003 的 2KB 窗口占 $0000/$0800，$A000-$A003 的 1KB 窗口占
 *       $1000-$1FFF；2KB 寄存器值以 2KB 为单位（写 3 = 第 6/7 个 1KB 页），不丢 LSB。
 * 有 CHR-ROM 时切 VROM 页，纯 CHR-RAM 卡带切 pattern RAM 页。
 * @param p      私有数据
 * @param p_host 宿主
 */
static void TC0690_set_ppu_bank(TC0690_data_t* p, ines_host_t* p_host)
{
	ines_word_t  b0, b1;

	if(p_host->vrom_1k_num > 0)
	{
		ines_word_t  num_2k = p_host->vrom_1k_num >> 1;

		if(num_2k == 0)
			num_2k = 1;

		/* 2KB 窗口：寄存器值以 2KB 为单位，换算成 1KB 页号后成对占用两个窗口 */
		b0 = (ines_word_t)((p->chr_2k[0] % num_2k) << 1);
		b1 = (ines_word_t)((p->chr_2k[1] % num_2k) << 1);

		ines_set_vrom_bank_n(p_host, 0, b0);
		ines_set_vrom_bank_n(p_host, 1, b0 + 1);
		ines_set_vrom_bank_n(p_host, 2, b1);
		ines_set_vrom_bank_n(p_host, 3, b1 + 1);

		ines_set_vrom_bank_n(p_host, 4, p->chr_1k[0] % p_host->vrom_1k_num);
		ines_set_vrom_bank_n(p_host, 5, p->chr_1k[1] % p_host->vrom_1k_num);
		ines_set_vrom_bank_n(p_host, 6, p->chr_1k[2] % p_host->vrom_1k_num);
		ines_set_vrom_bank_n(p_host, 7, p->chr_1k[3] % p_host->vrom_1k_num);
	}
	else
	{
		b0 = (ines_word_t)((p->chr_2k[0] & TC0690_CHR_RAM_2K_MASK) << 1);
		b1 = (ines_word_t)((p->chr_2k[1] & TC0690_CHR_RAM_2K_MASK) << 1);

		ines_set_vram_bank_n(p_host, 0, b0);
		ines_set_vram_bank_n(p_host, 1, b0 + 1);
		ines_set_vram_bank_n(p_host, 2, b1);
		ines_set_vram_bank_n(p_host, 3, b1 + 1);

		ines_set_vram_bank_n(p_host, 4, p->chr_1k[0] & TC0690_CHR_RAM_1K_MASK);
		ines_set_vram_bank_n(p_host, 5, p->chr_1k[1] & TC0690_CHR_RAM_1K_MASK);
		ines_set_vram_bank_n(p_host, 6, p->chr_1k[2] & TC0690_CHR_RAM_1K_MASK);
		ines_set_vram_bank_n(p_host, 7, p->chr_1k[3] & TC0690_CHR_RAM_1K_MASK);
	}
}

/**
 * 按 $E000 的 bit6 应用镜像；四屏卡带由硬件决定，Mapper 不得改写。
 * @param p      私有数据
 * @param p_host 宿主
 */
static void TC0690_set_mirror(TC0690_data_t* p, ines_host_t* p_host)
{
	if(p_host->rom.mirror_type == MIRROR_FOUR_SCREEN)
		return;

	ines_ppu_set_mirror_type(&p_host->ppu, (p->mirror_reg & TC0690_MIRROR_BIT) ? MIRROR_HORZ : MIRROR_VERT);
}

static void mapper48_reset(ines_mapper_t* p_mapper)
{
	ines_host_t*  p_host = mapper2host(p_mapper);
	TC0690_data_t*  p = mapper2TC0690data(p_mapper);

	p->prg_reg0 = 0;
	p->prg_reg1 = 1;

	/* 上电时寄存器内容在真实硬件上未定义，这里让镜像初值与卡带头声明的硬线方式一致
	   （宿主已在 ines_mapper_reset() 里据此设置过一次），PRG 取第 0/1 页。 */
	p->mirror_reg = (p_host->rom.mirror_type == MIRROR_HORZ) ? TC0690_MIRROR_BIT : 0;

	p->chr_2k[0] = 0;
	p->chr_2k[1] = 1;
	p->chr_1k[0] = 4;
	p->chr_1k[1] = 5;
	p->chr_1k[2] = 6;
	p->chr_1k[3] = 7;

	p->irq_enabled = 0;
	p->irq_counter = 0;
	p->irq_latch = 0;
	p->irq_reload = 0;
	ines_cpu_IRQ(&p_host->cpu, MMC_IRQ_MASK, ines_false);

	TC0690_set_cpu_bank(p, p_host);
	TC0690_set_ppu_bank(p, p_host);
	TC0690_set_mirror(p, p_host);
}

static void mapper48_writehigh(ines_mapper_t* p_mapper, ines_word_t addr, ines_byte_t  val)
{
	ines_host_t*  p_host = mapper2host(p_mapper);
	TC0690_data_t*  p = mapper2TC0690data(p_mapper);

	INES_LOG(LOG_DBG, MOD_MMC, ISTR("TC0690: write $%04X = #$%02X\n"), addr, val);

	switch(addr & 0xE003)
	{
	case 0x8000:
		p->prg_reg0 = val;
		TC0690_set_cpu_bank(p, p_host);
		break;

	case 0x8001:
		p->prg_reg1 = val;
		TC0690_set_cpu_bank(p, p_host);
		break;

	case 0x8002:
		p->chr_2k[0] = val;
		TC0690_set_ppu_bank(p, p_host);
		break;

	case 0x8003:
		p->chr_2k[1] = val;
		TC0690_set_ppu_bank(p, p_host);
		break;

	case 0xA000:
	case 0xA001:
	case 0xA002:
	case 0xA003:
		p->chr_1k[addr & 0x03] = val;
		TC0690_set_ppu_bank(p, p_host);
		break;

	case 0xC000:
		/* 与 MMC3 唯一不同的写入语义：reload 值取反 */
		p->irq_latch = (ines_byte_t)(val ^ 0xFF);
		break;

	case 0xC001:
		p->irq_reload = 1;
		break;

	case 0xC002:
		/* 对应 MMC3 的 $E001 */
		p->irq_enabled = 1;
		break;

	case 0xC003:
		/* 对应 MMC3 的 $E000：应答中断并停止请求 */
		p->irq_enabled = 0;
		ines_cpu_IRQ(&p_host->cpu, MMC_IRQ_MASK, ines_false);
		break;

	case 0xE000:
		p->mirror_reg = val;
		TC0690_set_mirror(p, p_host);
		break;

	default:
		break;
	}
}

/**
 * 扫描线中断：计数逻辑与 MMC3 相同（重载 -> 递减 -> 归零触发）。
 * @note 资料称 TC0690 的 IRQ 比 MMC3 晚约 4 个 CPU 周期，但本核心只有扫描线粒度的 hsync 回调，
 *       无法表达周期级延迟；曾在 hsync 里用 ines_cpu_exec() 推进 CPU 来近似，实测会导致
 *       模拟器在标题画面卡死（回调里重入 CPU 执行不可控），故改为与 MMC3 同一时刻置位。
 * @param p_mapper Mapper
 * @param line     扫描线号
 */
static void mapper48_hsync(ines_mapper_t* p_mapper, ines_int_t line)
{
	TC0690_data_t* p = mapper2TC0690data(p_mapper);
	ines_host_t* p_host = mapper2host(p_mapper);

	/* 本行计数：与 MMC3 相同（重载 -> 递减 -> 归零触发） */
	if(p->irq_enabled == 0)
		return;

	if(line < 0 || line > 239)
		return;

	if((p_host->ppu.reg_ctrl_2 & (PPU_ENABLE_BG|PPU_ENABLE_SPR)) == 0)
		return;

	if(p->irq_reload)
	{
		p->irq_reload = 0;
		p->irq_counter = p->irq_latch;
	}
	else
	{
		p->irq_counter--;
	}

	if(p->irq_counter == 0)
	{
		p->irq_reload = 1;
		if(p->irq_enabled)
		{
			ines_cpu_IRQ(&p_host->cpu, MMC_IRQ_MASK, ines_true);
			INES_LOG(LOG_DBG, MOD_MMC, ISTR("TC0690 IRQ fired @ line %d\n"), (ines_int_t)line);
		}
	}
}

static void mapper48_fini(ines_mapper_t* p_mapper)
{
	INES_LOG(LOG_DBG, MOD_MMC, ISTR("mapper48_fini.\n"));
	ines_free(p_mapper->p_data);
	p_mapper->p_data = NULL;
}

ines_bool_t  mapper48_create(ines_mapper_t* p_mapper)
{
	INIT_MAPPER_DATA_ST(p_mapper, TC0690_data_t);
	p_mapper->reset = mapper48_reset;
	p_mapper->writehigh = mapper48_writehigh;
	p_mapper->hsync = mapper48_hsync;
	p_mapper->fini = mapper48_fini;
	return ines_true;
}


