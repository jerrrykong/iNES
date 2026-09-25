
/**
 * core/mapper/33.c -- Taito TC0190（iNES Mapper 033）
 *
 * 硬件要点：
 *  - 寄存器地址区间 $8000-$BFFF，掩码 $A003（A0-A1 选组内寄存器，A13 选组，A14 未解码）：
 *      $8000 [.MPP PPPP]  bit6 = 镜像（0 垂直 / 1 水平），bit0-5 = PRG Reg 0（8KB @ $8000）
 *      $8001 [..PP PPPP]  bit0-5 = PRG Reg 1（8KB @ $A000）
 *      $8002 [CCCC CCCC]  CHR Reg 0（2KB @ $0000）
 *      $8003 [CCCC CCCC]  CHR Reg 1（2KB @ $0800）
 *      $A000 [CCCC CCCC]  CHR Reg 2（1KB @ $1000）
 *      $A001 [CCCC CCCC]  CHR Reg 3（1KB @ $1400）
 *      $A002 [CCCC CCCC]  CHR Reg 4（1KB @ $1800）
 *      $A003 [CCCC CCCC]  CHR Reg 5（1KB @ $1C00）
 *  - PRG：$C000 固定倒数第二页，$E000 固定最后一页。
 *  - CHR：两个 2KB 窗口的寄存器值以 2KB 为单位（不像 MMC3 那样丢 LSB），且 8 位全部有效，
 *    因此可寻址 512KB CHR；四个 1KB 窗口只有 8 位，覆盖前 256KB。
 *  - 无 IRQ：TC0190 不带中断能力，带同样中断能力的超集是 TC0690（iNES Mapper 048）。
 *  - 无卡带 WRAM，保持 custom_sram = 0，由宿主挂默认 8K SRAM。
 *
 * 注意：流传的 ROM 里大量 mapper 048 卡带被错误标注为 033（048 比 033 多一套 IRQ、
 * 镜像处理也不同）。这类卡带按本实现运行会缺少中断，需要 Mapper 48 实现并按校验值区分，
 * 本文件只实现真正的 033。
 */

#include "../../comm/idef.h"
#include "../../comm/log.h"
#include "../nes.h"
#include "../mapper.h"


/* $8000 的 bit6：镜像（0 垂直 / 1 水平） */
#define TC0190_MIRROR_BIT     0x40
/* PRG 寄存器有效位：bit0-5 */
#define TC0190_PRG_MASK       0x3F
/* 无 CHR-ROM 时 pattern RAM 为 8KB：2KB 窗口 4 页、1KB 窗口 8 页 */
#define TC0190_CHR_RAM_2K_MASK   0x03
#define TC0190_CHR_RAM_1K_MASK   0x07


struct _TC0190_data_
{
	ines_byte_t   prg_reg0;    /* $8000 写入值：bit0-5 PRG Reg 0 + bit6 镜像 */
	ines_byte_t   prg_reg1;    /* $8001 写入值：bit0-5 PRG Reg 1 */
	ines_byte_t   chr_2k[2];   /* $8002/$8003：两个 2KB CHR 窗口，单位为 2KB */
	ines_byte_t   chr_1k[4];   /* $A000-$A003：四个 1KB CHR 窗口 */
};

typedef struct _TC0190_data_   TC0190_data_t;

#define mapper2TC0190data(mapper)   ((TC0190_data_t*)((mapper)->p_data))


/**
 * 按当前寄存器刷新 4 个 8KB PRG 窗口（$C000/$E000 固定倒数第二/最后一页）。
 * @param p      私有数据
 * @param p_host 宿主
 */
static void TC0190_set_cpu_bank(TC0190_data_t* p, ines_host_t* p_host)
{
	ines_word_t  num = (p_host->prom_8k_num > 0) ? p_host->prom_8k_num : 1;
	ines_word_t  b0 = (p->prg_reg0 & TC0190_PRG_MASK) % num;
	ines_word_t  b1 = (p->prg_reg1 & TC0190_PRG_MASK) % num;
	ines_word_t  last = num - 1;
	ines_word_t  second_last = (num >= 2) ? (ines_word_t)(num - 2) : 0;

	ines_set_prom_bank_4(p_host, b0, b1, second_last, last);
}

/**
 * 按当前寄存器刷新 PPU 的 CHR 窗口：$0000/$0800 为 2KB 粒度，$1000-$1FFF 为 1KB 粒度。
 * 有 CHR-ROM 时切 VROM 页，纯 CHR-RAM 卡带切 pattern RAM 页。
 * @param p      私有数据
 * @param p_host 宿主
 */
static void TC0190_set_ppu_bank(TC0190_data_t* p, ines_host_t* p_host)
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
		b0 = (ines_word_t)((p->chr_2k[0] & TC0190_CHR_RAM_2K_MASK) << 1);
		b1 = (ines_word_t)((p->chr_2k[1] & TC0190_CHR_RAM_2K_MASK) << 1);

		ines_set_vram_bank_n(p_host, 0, b0);
		ines_set_vram_bank_n(p_host, 1, b0 + 1);
		ines_set_vram_bank_n(p_host, 2, b1);
		ines_set_vram_bank_n(p_host, 3, b1 + 1);

		ines_set_vram_bank_n(p_host, 4, p->chr_1k[0] & TC0190_CHR_RAM_1K_MASK);
		ines_set_vram_bank_n(p_host, 5, p->chr_1k[1] & TC0190_CHR_RAM_1K_MASK);
		ines_set_vram_bank_n(p_host, 6, p->chr_1k[2] & TC0190_CHR_RAM_1K_MASK);
		ines_set_vram_bank_n(p_host, 7, p->chr_1k[3] & TC0190_CHR_RAM_1K_MASK);
	}
}

/**
 * 按 $8000 的 bit6 应用镜像；四屏卡带由硬件决定，Mapper 不得改写。
 * @param p      私有数据
 * @param p_host 宿主
 */
static void TC0190_set_mirror(TC0190_data_t* p, ines_host_t* p_host)
{
	if(p_host->rom.mirror_type == MIRROR_FOUR_SCREEN)
		return;

	ines_ppu_set_mirror_type(&p_host->ppu, (p->prg_reg0 & TC0190_MIRROR_BIT) ? MIRROR_HORZ : MIRROR_VERT);
}

static void mapper33_reset(ines_mapper_t* p_mapper)
{
	ines_host_t*  p_host = mapper2host(p_mapper);
	TC0190_data_t*  p = mapper2TC0190data(p_mapper);

	p->prg_reg0 = 0;
	p->prg_reg1 = 1;

	/* 上电时寄存器内容在真实硬件上未定义，这里让镜像初值与卡带头声明的硬线方式一致
	   （宿主已在 ines_mapper_reset() 里据此设置过一次），PRG 取第 0/1 页。 */
	if(p_host->rom.mirror_type == MIRROR_HORZ)
		p->prg_reg0 |= TC0190_MIRROR_BIT;

	p->chr_2k[0] = 0;
	p->chr_2k[1] = 1;
	p->chr_1k[0] = 4;
	p->chr_1k[1] = 5;
	p->chr_1k[2] = 6;
	p->chr_1k[3] = 7;

	TC0190_set_cpu_bank(p, p_host);
	TC0190_set_ppu_bank(p, p_host);
	TC0190_set_mirror(p, p_host);
}

static void mapper33_writehigh(ines_mapper_t* p_mapper, ines_word_t addr, ines_byte_t  val)
{
	ines_host_t*  p_host = mapper2host(p_mapper);
	TC0190_data_t*  p = mapper2TC0190data(p_mapper);

	INES_LOG(LOG_DBG, MOD_MMC, ISTR("TC0190: write $%04X = #$%02X\n"), addr, val);

	switch(addr & 0xA003)
	{
	case 0x8000:
		p->prg_reg0 = val;
		TC0190_set_cpu_bank(p, p_host);
		TC0190_set_mirror(p, p_host);
		break;

	case 0x8001:
		p->prg_reg1 = val;
		TC0190_set_cpu_bank(p, p_host);
		break;

	case 0x8002:
		p->chr_2k[0] = val;
		TC0190_set_ppu_bank(p, p_host);
		break;

	case 0x8003:
		p->chr_2k[1] = val;
		TC0190_set_ppu_bank(p, p_host);
		break;

	case 0xA000:
	case 0xA001:
	case 0xA002:
	case 0xA003:
		p->chr_1k[addr & 0x03] = val;
		TC0190_set_ppu_bank(p, p_host);
		break;

	default:
		break;
	}
}

static void mapper33_fini(ines_mapper_t* p_mapper)
{
	INES_LOG(LOG_DBG, MOD_MMC, ISTR("mapper33_fini.\n"));
	ines_free(p_mapper->p_data);
	p_mapper->p_data = NULL;
}

ines_bool_t  mapper33_create(ines_mapper_t* p_mapper)
{
	INIT_MAPPER_DATA_ST(p_mapper, TC0190_data_t);
	p_mapper->reset = mapper33_reset;
	p_mapper->writehigh = mapper33_writehigh;
	p_mapper->fini = mapper33_fini;
	return ines_true;
}


