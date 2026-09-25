
/**
 * core/mapper/32.c -- Irem G-101（iNES Mapper 032）
 *
 * 硬件要点：
 *  - PRG：4 个 8KB 窗口。$A000 固定由 $A000-$AFFF 寄存器控制，$E000 固定最后一页；
 *    $8000 与 $C000 的角色由 $9000 的 bit1（PRG 模式）决定：
 *      模式 0：$8000 = $800x 寄存器，$C000 = 倒数第二页；
 *      模式 1：$8000 = 倒数第二页，$C000 = $800x 寄存器。
 *  - CHR：8 个 1KB 窗口各自独立，寄存器地址 $B000-$B007（掩码 $F007，A3-A11 未解码）。
 *  - 镜像：$9000 的 bit0（0 = 垂直，1 = 水平）。
 *  - 无 IRQ、无扩展音、无卡带 WRAM，因此保持 custom_sram = 0，由宿主挂默认 8K SRAM。
 *
 * 已知硬件差异：Major League 的 CIRAM A10 硬线接 +5V（固定单屏表 1），且 $9000 寄存器整体失效
 * （只能用 PRG 模式 0）。iNES 头无法表达该差异（NES 2.0 用 submapper 1 区分），
 * 因此按 PROM 校验值（rom.crc32_p）识别该卡带并走硬线分支。
 */

#include "../../comm/idef.h"
#include "../../comm/log.h"
#include "../nes.h"
#include "../mapper.h"


/* $9000-$9FFF 的有效位：bit0 镜像，bit1 PRG 模式 */
#define G101_MIRROR_BIT        0x01
#define G101_PRG_MODE_BIT      0x02

/* 无 CHR-ROM（CHR-RAM 卡带）时，pattern RAM 按 8KB = 8 个 1KB 页取模 */
#define G101_CHR_RAM_1K_MASK   0x07

/* Major League (J) 的 PROM 校验值：硬线单屏 + $9000 失效，只能靠校验区分 */
#define G101_CRC_MAJOR_LEAGUE   0xC0FED437UL


struct _G101_data_
{
	ines_byte_t   prg_reg0;    /* $8000-$8FFF 写入值：模式 0 时指向 $8000，模式 1 时指向 $C000 */
	ines_byte_t   prg_reg1;    /* $A000-$AFFF 写入值：$A000-$BFFF 窗口 */
	ines_byte_t   reg_9000;    /* $9000-$9FFF 写入值：bit0 镜像 + bit1 PRG 模式 */
	ines_byte_t   chr_reg[8];  /* $B000-$B007：PPU $0000-$1FFF 的八个 1KB 窗口 */
	ines_byte_t   hardwired;   /* 非 0 = Major League 类硬线卡带：镜像固定单屏表 1，$9000 写入无效 */
};

typedef struct _G101_data_   G101_data_t;

#define mapper2G101data(mapper)   ((G101_data_t*)((mapper)->p_data))


/**
 * 取两个固定 PRG 页：最后一页与倒数第二页。
 * @param p_host        宿主
 * @param p_last        返回最后一页（$E000 窗口）
 * @param p_second_last 返回倒数第二页（不足 2 页时退化为第 0 页）
 */
static void G101_get_fixed_prg(ines_host_t* p_host, ines_word_t* p_last, ines_word_t* p_second_last)
{
	ines_word_t  num = p_host->prom_8k_num;

	if(num == 0)
		num = 1;

	*p_last = num - 1;
	*p_second_last = (num >= 2) ? (ines_word_t)(num - 2) : 0;
}

/**
 * 按当前寄存器刷新 4 个 8KB PRG 窗口，页号对总页数取模。
 * @param p      私有数据
 * @param p_host 宿主
 */
static void G101_set_cpu_bank(G101_data_t* p, ines_host_t* p_host)
{
	ines_word_t  last;
	ines_word_t  second_last;
	ines_word_t  num = (p_host->prom_8k_num > 0) ? p_host->prom_8k_num : 1;
	ines_word_t  b0 = p->prg_reg0 % num;
	ines_word_t  b1 = p->prg_reg1 % num;

	G101_get_fixed_prg(p_host, &last, &second_last);

	if(p->reg_9000 & G101_PRG_MODE_BIT)
	{
		/* PRG 模式 1：$8000 固定倒数第二页，$C000 由 $800x 寄存器控制 */
		ines_set_prom_bank_4(p_host, second_last, b1, b0, last);
	}
	else
	{
		/* PRG 模式 0：$8000 由 $800x 寄存器控制，$C000 固定倒数第二页 */
		ines_set_prom_bank_4(p_host, b0, b1, second_last, last);
	}
}

/**
 * 按当前寄存器刷新 PPU 的八个 1KB CHR 窗口。
 * 有 CHR-ROM 时切 VROM 页，纯 CHR-RAM 卡带切 pattern RAM 页。
 * @param p      私有数据
 * @param p_host 宿主
 */
static void G101_set_ppu_bank(G101_data_t* p, ines_host_t* p_host)
{
	ines_int_t  n;

	if(p_host->vrom_1k_num > 0)
	{
		for(n = 0; n < 8; n++)
		{
			ines_set_vrom_bank_n(p_host, n, p->chr_reg[n] % p_host->vrom_1k_num);
		}
	}
	else
	{
		for(n = 0; n < 8; n++)
		{
			ines_set_vram_bank_n(p_host, n, p->chr_reg[n] & G101_CHR_RAM_1K_MASK);
		}
	}
}

/**
 * 应用镜像：硬线卡带固定单屏表 1；普通卡带按 $9000 的 bit0；四屏卡带由硬件决定，Mapper 不得改写。
 * @param p      私有数据
 * @param p_host 宿主
 */
static void G101_set_mirror(G101_data_t* p, ines_host_t* p_host)
{
	if(p_host->rom.mirror_type == MIRROR_FOUR_SCREEN)
		return;

	if(p->hardwired)
	{
		/* CIRAM A10 硬线接 +5V：四个 nametable 窗口都指向第 1 页 */
		ines_ppu_set_mirror(&p_host->ppu, 1, 1, 1, 1);
		return;
	}

	ines_ppu_set_mirror_type(&p_host->ppu, (p->reg_9000 & G101_MIRROR_BIT) ? MIRROR_HORZ : MIRROR_VERT);
}

static void mapper32_reset(ines_mapper_t* p_mapper)
{
	ines_host_t*  p_host = mapper2host(p_mapper);
	G101_data_t*  p = mapper2G101data(p_mapper);
	ines_int_t    n;

	p->prg_reg0 = 0;
	p->prg_reg1 = 0;

	/* Major League 把 CIRAM A10 硬线接 +5V 且未连 $9000，iNES 头无法表达，只能按校验值识别 */
	p->hardwired = (p_host->rom.crc32_p == G101_CRC_MAJOR_LEAGUE) ? 1 : 0;
	if(p->hardwired)
	{
		INES_LOG(LOG_INF, MOD_MMC, ISTR("G-101: Major League 硬线卡带(固定单屏表 1, $9000 失效)\n"));
	}

	/* 上电时 $9000 内容在真实硬件上未定义，这里让镜像初值与卡带头声明的硬线方式一致
	   （宿主已在 ines_mapper_reset() 里据此设置过一次），PRG 模式取 0。 */
	p->reg_9000 = (p_host->rom.mirror_type == MIRROR_HORZ) ? G101_MIRROR_BIT : 0;

	for(n = 0; n < 8; n++)
	{
		p->chr_reg[n] = (ines_byte_t)n;
	}

	G101_set_cpu_bank(p, p_host);
	G101_set_ppu_bank(p, p_host);
	G101_set_mirror(p, p_host);
}

static void mapper32_writehigh(ines_mapper_t* p_mapper, ines_word_t addr, ines_byte_t  val)
{
	ines_host_t*  p_host = mapper2host(p_mapper);
	G101_data_t*  p = mapper2G101data(p_mapper);

	INES_LOG(LOG_DBG, MOD_MMC, ISTR("G-101: write $%04X = #$%02X\n"), addr, val);

	switch(addr & 0xF000)
	{
	case 0x8000:
		p->prg_reg0 = val;
		G101_set_cpu_bank(p, p_host);
		break;

	case 0x9000:
		if(p->hardwired)
		{
			/* 该卡带未连接 $9000：写入无效，PRG 模式恒为 0 */
			break;
		}
		p->reg_9000 = val & (G101_MIRROR_BIT | G101_PRG_MODE_BIT);
		G101_set_cpu_bank(p, p_host);
		G101_set_mirror(p, p_host);
		break;

	case 0xA000:
		p->prg_reg1 = val;
		G101_set_cpu_bank(p, p_host);
		break;

	case 0xB000:
		/* A0-A2 选择八个 1KB CHR 窗口之一，A3-A11 未解码 */
		p->chr_reg[addr & 0x07] = val;
		G101_set_ppu_bank(p, p_host);
		break;

	default:
		break;
	}
}

static void mapper32_fini(ines_mapper_t* p_mapper)
{
	INES_LOG(LOG_DBG, MOD_MMC, ISTR("mapper32_fini.\n"));
	ines_free(p_mapper->p_data);
	p_mapper->p_data = NULL;
}

ines_bool_t  mapper32_create(ines_mapper_t* p_mapper)
{
	INIT_MAPPER_DATA_ST(p_mapper, G101_data_t);
	p_mapper->reset = mapper32_reset;
	p_mapper->writehigh = mapper32_writehigh;
	p_mapper->fini = mapper32_fini;
	return ines_true;
}


