
/**
 * core/mapper/225.c -- ET-4310(60pin) / K-1010(72pin) 多合一卡带板（iNES Mapper 225）
 *
 * 硬件要点（NESdev "INES Mapper 225" + Disch 原始笔记）：
 *  - 这块板没有传统意义上的"写寄存器"：bank 号直接由**写入地址**译码得到，
 *    $8000-$FFFF 区间内任意一次写都会按下面的位域重排 PRG / CHR / 镜像：
 *
 *      A~[.HMO PPPP PPCC CCCC]
 *           A15 恒为 1（写落在 $8000-$FFFF）
 *           A14 = H  PRG / CHR 页号共用的高位（接两者页号的 bit6）
 *           A13 = M  镜像：0 = 垂直(PPUA10)，1 = 水平(PPUA11)
 *           A12 = O  PRG 页大小：0 = 32KB，1 = 16KB
 *           A11-A6 = PRG 页号 bit5-0
 *           A5-A0  = CHR 页号 bit5-0
 *
 *    因为 H 位接在 bit6 上，PRG 与 CHR 页号都是 7 位（0-127）。
 *  - PRG：O = 1 时同一个 16KB 页同时出现在 $8000 与 $C000；
 *         O = 0 时页号 LSB 被硬件忽略，一对 16KB 页拼成 32KB 放在 $8000。
 *  - CHR：PPU $0000-$1FFF 是整块 8KB 窗口，一次切换，没有细分窗口。
 *  - 附加 RAM：$5800-$5803 四个 4bit 单元（$5804-$5FFF 为镜像），可读可写。
 *    Nestopia 与 Mesen 都没实现这块 RAM，缺了它部分卡带的菜单会有小毛病。
 *  - 无 IRQ、无卡带 WRAM，因此保持 custom_sram = 0，由宿主挂默认 8K SRAM。
 *
 * 参考实现核对：Mesen(Mapper225.h)、puNES(mapper_225.c)、fceumm(boards/225.c) 三者的
 * PRG / CHR / 镜像换算完全一致，本文件与它们对齐。Mapper 255 见 core/mapper/255.c。
 */

#include "../../comm/idef.h"
#include "../../comm/log.h"
#include "../nes.h"
#include "../mapper.h"


/* bank 寄存器（= 最近一次 $8000-$FFFF 写入的地址）的位域 */
#define K1010_PRG_HIGH_BIT      0x4000  /* H：PRG / CHR 页号 bit6 */
#define K1010_MIRROR_BIT        0x2000  /* M：0 = 垂直，1 = 水平 */
#define K1010_PRG_MODE_BIT      0x1000  /* O：0 = 32KB，1 = 16KB */
#define K1010_PRG_MASK          0x0FC0  /* PRG 页号 bit5-0（A11-A6） */
#define K1010_PRG_SHIFT         6
#define K1010_CHR_MASK          0x003F  /* CHR 页号 bit5-0（A5-A0） */

/* H 位提供的页号高位：PRG / CHR 页号都是 7 位 */
#define K1010_BANK_HIGH         0x40

/* 无 CHR-ROM 时只有 8KB pattern RAM，页号按 8 个 1KB 页回卷 */
#define K1010_CHR_RAM_1K_MASK   0x07

/* 附加 RAM：$5800-$5FFF，4 个 4bit 单元按 A1-A0 选址，其余位为镜像 */
#define K1010_SCRATCH_BEGIN     0x5800
#define K1010_SCRATCH_END       0x5FFF
#define K1010_SCRATCH_NUM       4
#define K1010_SCRATCH_MASK      0x0F


struct _K1010_data_
{
	ines_word_t   reg;                         /* bank 寄存器：最近一次 $8000-$FFFF 写入的地址 */
	ines_byte_t   scratch[K1010_SCRATCH_NUM];  /* $5800-$5803 的 4×4bit 附加 RAM */
};

typedef struct _K1010_data_  K1010_data_t;

#define mapper2K1010data(mapper)   ((K1010_data_t*)((mapper)->p_data))


/**
 * 由 bank 寄存器解出 PRG 的 16KB 页号（含 H 位提供的 bit6）。
 * @param reg bank 寄存器（写入地址）
 * @return 16KB 页号，取值 0-127
 */
static ines_word_t K1010_get_prg_bank(ines_word_t reg)
{
	return (ines_word_t)( ((reg & K1010_PRG_MASK) >> K1010_PRG_SHIFT)
		| ((reg & K1010_PRG_HIGH_BIT) ? K1010_BANK_HIGH : 0) );
}

/**
 * 由 bank 寄存器解出 CHR 的 8KB 页号（含 H 位提供的 bit6）。
 * @param reg bank 寄存器（写入地址）
 * @return 8KB 页号，取值 0-127
 */
static ines_word_t K1010_get_chr_bank(ines_word_t reg)
{
	return (ines_word_t)( (reg & K1010_CHR_MASK)
		| ((reg & K1010_PRG_HIGH_BIT) ? K1010_BANK_HIGH : 0) );
}

/**
 * 按当前 bank 寄存器刷新 4 个 8KB PRG 窗口，页号对总页数取模。
 * @param p      私有数据
 * @param p_host 宿主
 */
static void K1010_set_cpu_bank(K1010_data_t* p, ines_host_t* p_host)
{
	ines_word_t  num = (p_host->prom_8k_num > 0) ? p_host->prom_8k_num : 1;
	ines_word_t  bank = K1010_get_prg_bank(p->reg);
	ines_word_t  b0;
	ines_word_t  b1;
	ines_word_t  b2;
	ines_word_t  b3;

	if(p->reg & K1010_PRG_MODE_BIT)
	{
		/* 16KB 模式：同一个 16KB 页同时出现在 $8000 与 $C000 */
		b0 = (ines_word_t)(bank << 1);
		b1 = (ines_word_t)(b0 + 1);
		b2 = b0;
		b3 = b1;
	}
	else
	{
		/* 32KB 模式：页号 LSB 被硬件忽略，取一对 16KB 页 */
		b0 = (ines_word_t)((bank & 0xFE) << 1);
		b1 = (ines_word_t)(b0 + 1);
		b2 = (ines_word_t)(b0 + 2);
		b3 = (ines_word_t)(b0 + 3);
	}

	ines_set_prom_bank_4(p_host, b0 % num, b1 % num, b2 % num, b3 % num);
}

/**
 * 按当前 bank 寄存器刷新 PPU 的整块 8KB CHR 窗口。
 * 有 CHR-ROM 时切 VROM 页；纯 CHR-RAM 卡带切 pattern RAM 页。
 * @param p      私有数据
 * @param p_host 宿主
 */
static void K1010_set_ppu_bank(K1010_data_t* p, ines_host_t* p_host)
{
	ines_word_t  bank = K1010_get_chr_bank(p->reg);
	ines_int_t   n;

	if(p_host->vrom_1k_num > 0)
	{
		for(n = 0; n < 8; n++)
		{
			ines_set_vrom_bank_n(p_host, n, (ines_word_t)(((bank << 3) + n) % p_host->vrom_1k_num));
		}
	}
	else
	{
		for(n = 0; n < 8; n++)
		{
			ines_set_vram_bank_n(p_host, n, (ines_word_t)(n & K1010_CHR_RAM_1K_MASK));
		}
	}
}

/**
 * 应用镜像；四屏卡带由硬件决定，Mapper 不得改写。
 * @param p      私有数据
 * @param p_host 宿主
 */
static void K1010_set_mirror(K1010_data_t* p, ines_host_t* p_host)
{
	if(p_host->rom.mirror_type == MIRROR_FOUR_SCREEN)
		return;

	ines_ppu_set_mirror_type(&p_host->ppu, (p->reg & K1010_MIRROR_BIT) ? MIRROR_HORZ : MIRROR_VERT);
}

static void mapper225_reset(ines_mapper_t* p_mapper)
{
	ines_host_t*   p_host = mapper2host(p_mapper);
	K1010_data_t*  p = mapper2K1010data(p_mapper);
	ines_int_t     n;

	for(n = 0; n < K1010_SCRATCH_NUM; n++)
	{
		p->scratch[n] = 0;
	}

	/* 硬件上两个锁存器的复位脚接 +5V，上电内容是随机的；参考实现统一按
	   "对 $8000 写过一次"初始化（PRG 32KB 第 0 页、CHR 第 0 页）。
	   镜像初值沿用卡带头声明（宿主已按 rom.mirror_type 设过一次）。 */
	p->reg = 0x8000;
	if(p_host->rom.mirror_type == MIRROR_HORZ)
		p->reg |= K1010_MIRROR_BIT;

	K1010_set_cpu_bank(p, p_host);
	K1010_set_ppu_bank(p, p_host);
	K1010_set_mirror(p, p_host);
}

static void mapper225_writehigh(ines_mapper_t* p_mapper, ines_word_t addr, ines_byte_t  val)
{
	ines_host_t*   p_host = mapper2host(p_mapper);
	K1010_data_t*  p = mapper2K1010data(p_mapper);

	/* 本卡带只译码地址，写入值不参与 bank 选择 */
	INES_LOG(LOG_DBG, MOD_MMC, ISTR("K-1010: bank write $%04X = #$%02X\n"), (ines_int_t)addr, (ines_int_t)val);

	p->reg = addr;
	K1010_set_cpu_bank(p, p_host);
	K1010_set_ppu_bank(p, p_host);
	K1010_set_mirror(p, p_host);
}

/**
 * $4020-$5FFF 读：命中附加 RAM 时返回其内容，否则返回开总线值。
 * @param p_mapper Mapper
 * @param addr     CPU 地址
 * @return 读到的字节
 */
static ines_byte_t mapper225_readlow(ines_mapper_t* p_mapper, ines_word_t addr)
{
	K1010_data_t*  p = mapper2K1010data(p_mapper);

	if(addr >= K1010_SCRATCH_BEGIN && addr <= K1010_SCRATCH_END)
		return p->scratch[addr & (K1010_SCRATCH_NUM - 1)];

	return (ines_byte_t)(addr >> 8);
}

/**
 * $4020-$5FFF 写：只处理附加 RAM（4bit 单元），其它地址忽略。
 * @param p_mapper Mapper
 * @param addr     CPU 地址
 * @param val      写入值
 */
static void mapper225_writelow(ines_mapper_t* p_mapper, ines_word_t addr, ines_byte_t val)
{
	K1010_data_t*  p = mapper2K1010data(p_mapper);

	if(addr >= K1010_SCRATCH_BEGIN && addr <= K1010_SCRATCH_END)
		p->scratch[addr & (K1010_SCRATCH_NUM - 1)] = (ines_byte_t)(val & K1010_SCRATCH_MASK);
}

static void mapper225_fini(ines_mapper_t* p_mapper)
{
	INES_LOG(LOG_DBG, MOD_MMC, ISTR("mapper225_fini.\n"));
	ines_free(p_mapper->p_data);
	p_mapper->p_data = NULL;
}

ines_bool_t  mapper225_create(ines_mapper_t* p_mapper)
{
	INIT_MAPPER_DATA_ST(p_mapper, K1010_data_t);
	p_mapper->reset     = mapper225_reset;
	p_mapper->writehigh = mapper225_writehigh;
	p_mapper->readlow   = mapper225_readlow;
	p_mapper->writelow  = mapper225_writelow;
	p_mapper->fini      = mapper225_fini;
	return ines_true;
}


