
#include "../../comm/idef.h"
#include "../../comm/log.h"
#include "../nes.h"
#include "../mapper.h"

/************************************************************************
 * Mapper 017 -- Front Fareast Super Magic Card(SMC-801) RAM 卡带
 *
 * 硬件：512KB PRG DRAM(8KB 粒度) + 32KB WRAM(8KB 粒度) + 256KB CHR-RAM(1KB 粒度)。
 * iNES 镜像是从 SMC 的磁盘镜像里提取出来的游戏，初始状态由 iNES Mapper 017 规范给出：
 *   $4500 = $47   Play 模式 / WRAM 页 0 / 1KB CHR 模式 / MMC4 关闭 / nametable 用 CIRAM
 *   $42FF = $20 | (垂直镜像 ? $00 : $10)     PRG 写保护(锁存使能) + 双屏镜像
 *   $43FC = $00                              4M PRG banking 模式
 *   $4504-$4507 = 8KB PRG 页数 -4 / -3 / -2 / -1
 *
 * 寄存器($4020-$5FFF，全部走 writelow/readlow)：
 *   $42FC-$42FF  1M banking：数据 D7-D5 锁存模式、D4 镜像设定；地址 A1 PRG 写保护、A0 镜像类型
 *   $43FC-$43FF  2M/4M banking：数据 D1-D0 公共 8KB CHR 页；地址 A0 使能(0=启用)、A1 2M/4M 选择
 *   $4500        SMC 模式：PMWW ImNC(写) / WBLL LMPm(读)
 *   $4501        写：应答并关闭 IRQ；读：状态寄存器 2
 *   $4502/$4503  IRQ 计数器 LSB/MSB(写 $4503 启动计数)
 *   $4504-$4507  4M 模式的四个 8KB PRG 页(2M 模式下是 $8000-$FFFF 窗口寄存器的镜像)
 *   $4510-$4517  1KB CHR 页 8 个
 *   $4518-$451B  CHR nametable 页 4 个(N=0 时 $2000-$2FFF 指向 CHR)
 *   $5000-$5FFF  4KB scratch RAM
 *
 * 未模拟：GUI 模式($4500 的 M=0，需要 16KB BIOS ROM，镜像里没有)、82077A 软驱控制器与
 * 并口($4508-$450F 按开总线处理)、FDS RAM Adapter 的 $4024/$4025 定时 IRQ(SMC 自带 IRQ 计数器)。
 ************************************************************************/

#define SMC_SCRATCH_SIZE       0x1000   /* $5000-$5FFF：4KB scratch RAM */
#define SMC_WRAM_BANK_MASK     0x03     /* 32KB WRAM = 4 x 8KB，占用宿主 SRAM 块 0-3 */
#define SMC_TRAINER_SRAM_OFF   0x1000   /* trainer 装在 WRAM 页 0 的 $1000，即 CPU $7000 */
#define SMC_TRAINER_ADDR       0x7000   /* NES 2.0 submapper 0 的 trainer 加载地址 */
#define SMC_PRG_BANK_MASK      0x3f     /* 8KB PRG 页号有效位 PPPPPP */
#define SMC_CHR_PATTERN_NUM    8        /* $4510-$4517 */
#define SMC_CHR_NT_NUM         4        /* $4518-$451B */
#define SMC_PRG_REG_NUM        4        /* $4504-$4507 */


struct _Mapper17_data_
{
	ines_byte_t   smc_mode;      /* $4500 写入值(PMWW ImNC) */
	ines_byte_t   mode_1m_data;  /* $42Fx 数据位 D7-D4(D7-D5 锁存模式，D4 镜像设定) */
	ines_byte_t   mode_1m_addr;  /* $42Fx 地址位：bit1 = A1(PRG 写保护)，bit0 = A0(镜像类型) */
	ines_byte_t   mode_24m_addr; /* $43Fx 地址位：bit1 = A1(2M/4M 选择)，bit0 = A0(使能，0=启用) */
	ines_byte_t   latch;         /* $8000-$FFFF 的锁存值(仅 PRG 写保护时更新) */
	ines_byte_t   wram_bank;     /* $6000-$7FFF 的 8KB WRAM 页 */
	ines_byte_t   chr_8k;        /* 公共 8KB CHR 页(CC，0-3) */
	ines_byte_t   chr_ram;       /* 1: CHR 走 pattern RAM(可写)；0: 走 CHR-ROM(只读) */
	ines_byte_t   irq_enabled;   /* IRQ 计数使能(写 $4503 置位，写 $4501 清除) */
	ines_byte_t   mmc4_sel[2];   /* MMC4 模式下低/高 4KB 当前选中的 $451x 寄存器下标 */
	ines_word_t   irq_counter;   /* 16 位向上计数器，$FFFF -> $0000 翻转时产生 IRQ */
	ines_int64_t  irq_synced;    /* 上次对账的 CPU 周期数 */
	ines_byte_t   prg[SMC_PRG_REG_NUM];        /* 四个 8KB PRG 寄存器 */
	ines_byte_t   chr[SMC_CHR_PATTERN_NUM];    /* 1KB CHR 页 */
	ines_byte_t   chr_nt[SMC_CHR_NT_NUM];      /* CHR nametable 页 */
	ines_byte_t   scratch[SMC_SCRATCH_SIZE];   /* $5000-$5FFF */
};

typedef struct _Mapper17_data_  Mapper17_data_t;

#define mapper2Mapper17data(p)   ((Mapper17_data_t*)((p)->p_data))


/**
 * 把一个 8KB PRG 页映射到 $8000-$FFFF 的某个 8KB 窗口(页号按卡带容量取模)。
 * @param p_host 宿主
 * @param win    窗口号(4-7)
 * @param bank   8KB PRG 页号
 */
static void mapper17_set_prg8k(ines_host_t* p_host, ines_word_t win, ines_word_t bank)
{
	if(p_host->prom_8k_num == 0)
		return;
	ines_set_prom_bank_n(p_host, win, (ines_word_t)(bank % p_host->prom_8k_num));
}

/**
 * 把一个 16KB PRG 页映射到 $8000-$BFFF(win=4) 或 $C000-$FFFF(win=6)。
 * @param p_host 宿主
 * @param win    起始窗口号(4 或 6)
 * @param bank16 16KB PRG 页号
 */
static void mapper17_set_prg16k(ines_host_t* p_host, ines_word_t win, ines_word_t bank16)
{
	ines_word_t num = (ines_word_t)(p_host->prom_8k_num >> 1);
	ines_word_t bn;

	if(num == 0)
		return;

	bn = (ines_word_t)((bank16 % num) << 1);
	mapper17_set_prg8k(p_host, win, bn);
	mapper17_set_prg8k(p_host, (ines_word_t)(win + 1), (ines_word_t)(bn + 1));
}

/**
 * 把一个 32KB PRG 页映射到 $8000-$FFFF。
 * @param p_host 宿主
 * @param bank32 32KB PRG 页号
 */
static void mapper17_set_prg32k(ines_host_t* p_host, ines_word_t bank32)
{
	ines_word_t num = (ines_word_t)(p_host->prom_8k_num >> 2);
	ines_word_t bn;
	ines_word_t n;

	if(num == 0)
		return;

	bn = (ines_word_t)((bank32 % num) << 2);
	for(n = 0; n < 4; n++)
	{
		mapper17_set_prg8k(p_host, (ines_word_t)(4 + n), (ines_word_t)(bn + n));
	}
}

/**
 * 把一个 1KB CHR 页映射到 PPU 的 pattern 窗口。
 * @param p      mapper 私有数据
 * @param p_host 宿主
 * @param win    窗口号(0-7)
 * @param bank   1KB CHR 页号(0-255)
 * @note SMC 的 CHR 是 RAM，容量不超过 32KB 时镜像里的 CHR 数据已拷进 pattern RAM，
 *       这里走 CHR-RAM 路径(可写)；超过 32KB 装不下时退回 CHR-ROM 映射(只读)。
 */
static void mapper17_set_chr1k(Mapper17_data_t* p, ines_host_t* p_host, ines_word_t win, ines_word_t bank)
{
	if(p->chr_ram)
	{
		ines_set_vram_bank_n(p_host, win, (ines_word_t)(bank & (NES_MAX_VRAM_BANKS - 1)));
	}
	else if(p_host->vrom_1k_num > 0)
	{
		ines_set_vrom_bank_n(p_host, win, (ines_word_t)(bank % p_host->vrom_1k_num));
	}
}

/**
 * MMC4 模式：把 $451x 寄存器里的 4KB 页号展开成 4 个 1KB 窗口。
 * @param p       mapper 私有数据
 * @param p_host  宿主
 * @param win     起始窗口号(0 或 4)
 * @param reg_idx $451x 寄存器下标
 */
static void mapper17_set_chr4k(Mapper17_data_t* p, ines_host_t* p_host, ines_word_t win, ines_word_t reg_idx)
{
	ines_word_t bank4 = (ines_word_t)((p->chr[reg_idx] >> 2) & SMC_PRG_BANK_MASK);
	ines_word_t n;

	for(n = 0; n < 4; n++)
	{
		mapper17_set_chr1k(p, p_host, (ines_word_t)(win + n), (ines_word_t)((bank4 << 2) + n));
	}
}

/**
 * 按当前寄存器状态刷新 PRG 映射。
 * @param p      mapper 私有数据
 * @param p_host 宿主
 * @note $43Fx 的 A0(M) 为 0 时 2M/4M 模式生效(两者共用同一组 8KB PRG 寄存器)，
 *       为 1 时回到 $42Fx 选出的 8 种锁存模式之一。
 */
static void mapper17_apply_prg(Mapper17_data_t* p, ines_host_t* p_host)
{
	ines_byte_t mode;
	ines_word_t n;

	if((p->mode_24m_addr & 0x01) == 0)
	{
		for(n = 0; n < SMC_PRG_REG_NUM; n++)
		{
			mapper17_set_prg8k(p_host, (ines_word_t)(4 + n), p->prg[n]);
		}
		return;
	}

	mode = (ines_byte_t)((p->mode_1m_data >> 5) & 0x07);
	switch(mode)
	{
	case 0: /* UNROM：$8000 = PPP(0-7)，$C000 = 固定 #7 */
		mapper17_set_prg16k(p_host, 4, (ines_word_t)(p->latch & 0x07));
		mapper17_set_prg16k(p_host, 6, 7);
		break;
	case 1: /* UN1ROM+CHRSW：$8000 = PPPP(0-15)，$C000 = 固定 #7 */
		mapper17_set_prg16k(p_host, 4, (ines_word_t)((p->latch >> 2) & 0x0f));
		mapper17_set_prg16k(p_host, 6, 7);
		break;
	case 2: /* UOROM：$8000 = PPPP(0-15)，$C000 = 固定 #15 */
		mapper17_set_prg16k(p_host, 4, (ines_word_t)(p->latch & 0x0f));
		mapper17_set_prg16k(p_host, 6, 15);
		break;
	case 3: /* Reverse UOROM+CHRSW：$C000 = PPPP(0-15)，$8000 = 固定 #15 */
		mapper17_set_prg16k(p_host, 6, (ines_word_t)(p->latch & 0x0f));
		mapper17_set_prg16k(p_host, 4, 15);
		break;
	case 4: /* GNROM：$8000-$FFFF = 32KB 页 PP(0-3) */
		mapper17_set_prg32k(p_host, (ines_word_t)((p->latch >> 4) & 0x03));
		break;
	default: /* 5/6/7：CNROM-256 / CNROM-128 / NROM-256，PRG 固定 32KB 页 #3 */
		mapper17_set_prg32k(p_host, 3);
		break;
	}
}

/**
 * 按当前寄存器状态刷新 PPU 映射(pattern 区与 nametable 区)。
 * @param p      mapper 私有数据
 * @param p_host 宿主
 */
static void mapper17_apply_ppu(Mapper17_data_t* p, ines_host_t* p_host)
{
	ines_byte_t mirror;
	ines_word_t n;

	if(p->smc_mode & 0x01)   /* C=1：1KB CHR 模式，覆盖 8KB 模式的 CC 位 */
	{
		if((p->smc_mode & 0x04) == 0)   /* m=0：MMC4 模式启用 */
		{
			mapper17_set_chr4k(p, p_host, 0, p->mmc4_sel[0]);
			mapper17_set_chr4k(p, p_host, 4, p->mmc4_sel[1]);
		}
		else
		{
			for(n = 0; n < SMC_CHR_PATTERN_NUM; n++)
			{
				mapper17_set_chr1k(p, p_host, n, p->chr[n]);
			}
		}
	}
	else
	{
		for(n = 0; n < SMC_CHR_PATTERN_NUM; n++)
		{
			mapper17_set_chr1k(p, p_host, n, (ines_word_t)((p->chr_8k << 3) + n));
		}
	}

	if(p->smc_mode & 0x02)   /* N=1：nametable 用 CIRAM，按 $42Fx 镜像 */
	{
		if(p_host->rom.mirror_type == MIRROR_FOUR_SCREEN)
		{
			/* 四屏卡带：SMC 只有 2KB CIRAM，镜像不改写，保持宿主的四屏布局 */
		}
		else
		{
			mirror = (ines_byte_t)(((p->mode_1m_addr & 0x01) << 1) | ((p->mode_1m_data >> 4) & 0x01));
			switch(mirror)
			{
			case 0:  ines_ppu_set_mirror(&p_host->ppu, 0, 0, 0, 0); break; /* 单屏 page 0 */
			case 1:  ines_ppu_set_mirror(&p_host->ppu, 1, 1, 1, 1); break; /* 单屏 page 1 */
			case 2:  ines_ppu_set_mirror(&p_host->ppu, 0, 1, 0, 1); break; /* 垂直 */
			default: ines_ppu_set_mirror(&p_host->ppu, 0, 0, 1, 1); break; /* 水平 */
			}
		}
	}
	else                     /* N=0：nametable 指向 CHR 页($4518-$451B) */
	{
		for(n = 0; n < SMC_CHR_NT_NUM; n++)
		{
			if(p->chr_ram)
			{
				ines_set_nt_pattern_bank_n(p_host, n, (ines_word_t)(p->chr_nt[n] & (NES_MAX_VRAM_BANKS - 1)));
			}
			else
			{
				ines_set_nt_chr_bank_n(p_host, n, (ines_word_t)(p->chr_nt[n] % p_host->vrom_1k_num));
			}
		}
	}
}


/**
 * 软件复位：装载 trainer、置寄存器初值并刷新全部映射。
 * @param p_mapper mapper 实例
 */
static void mapper17_reset(ines_mapper_t* p_mapper)
{
	ines_host_t*     p_host = mapper2host(p_mapper);
	Mapper17_data_t* p      = mapper2Mapper17data(p_mapper);
	ines_word_t      n;

	/* WRAM：32KB(4 x 8KB)占宿主 SRAM 块 0-3，页号由 $4500 的 WW 位给出 */
	p->wram_bank = 0;
	ines_set_sram_bank_n(p_host, 3, 0);

	/* CHR：SMC 的 CHR 是 RAM，镜像里的 CHR 数据就是"被载入 CHR-RAM 的内容"。
	   容量不超过 32KB 时拷进 pattern RAM 让游戏可以改写；超过则退回 CHR-ROM 映射(只读)。 */
	p->chr_ram = 1;
	if(p_host->vrom_1k_num > NES_MAX_VRAM_BANKS)
	{
		p->chr_ram = 0;
	}
	else if(p_host->vrom_1k_num > 0)
	{
		memcpy(p_host->ppu.pattern_table, p_host->rom.pVROMs, (ines_size_t)p_host->vrom_1k_num << 10);
		/* 逐页映射一次以登记 pattern_table_used，保证即时存档能带走整片 CHR-RAM */
		for(n = 0; n < p_host->vrom_1k_num; n++)
		{
			ines_set_vram_bank_n(p_host, (ines_word_t)(n & 0x07), n);
		}
	}

	/* 寄存器初值(iNES Mapper 017 规定) */
	p->smc_mode      = 0x47;   /* Play 模式 / WRAM 页 0 / 1KB CHR / MMC4 关闭 / NT 用 CIRAM */
	p->mode_24m_addr = 0x00;   /* $43FC：4M PRG banking 模式启用 */
	p->mode_1m_addr  = 0x03;   /* $42FF：A1=1 写保护(锁存使能)，A0=1 双屏 */
	p->mode_1m_data  = (ines_byte_t)(0x20 |
		(p_host->rom.mirror_type == MIRROR_VERT ? 0x00 : 0x10)); /* 锁存模式 1 + 镜像设定 */
	p->chr_8k        = 0;
	p->latch         = 0;
	p->mmc4_sel[0]   = 0;
	p->mmc4_sel[1]   = 4;

	for(n = 0; n < SMC_PRG_REG_NUM; n++)
	{
		p->prg[n] = (ines_byte_t)((p_host->prom_8k_num >= 4 ? p_host->prom_8k_num - 4 + n : n) & SMC_PRG_BANK_MASK);
	}
	for(n = 0; n < SMC_CHR_PATTERN_NUM; n++)
	{
		p->chr[n] = (ines_byte_t)n;
	}
	for(n = 0; n < SMC_CHR_NT_NUM; n++)
	{
		p->chr_nt[n] = (ines_byte_t)n;
	}

	p->irq_counter = 0;
	p->irq_enabled = ines_false;
	p->irq_synced  = 0;   /* 紧随其后的 ines_cpu_reset() 会把 cpu.total_cycles 清零 */
	ines_cpu_IRQ(&p_host->cpu, MMC_IRQ_MASK, ines_false);

	/* trainer：固定加载到 $7000(本核心只解析 iNES 1.0，即 NES 2.0 submapper 0)，
	   存在时硬复位改跳 trainer 入口而不是游戏复位向量。 */
	p_host->reset_entry = 0;
	if(p_host->rom.has_trainer)
	{
		memcpy(p_host->SRAM + SMC_TRAINER_SRAM_OFF, p_host->rom.trainer_data, INES_TRAINER_BLOCK_SIZE);
		p_host->reset_entry = SMC_TRAINER_ADDR;
		INES_LOG(LOG_INF, MOD_MMC, ISTR("mapper17: trainer loaded, hard reset entry = $%04X.\n"),
			(ines_int_t)SMC_TRAINER_ADDR);
	}

	mapper17_apply_prg(p, p_host);
	mapper17_apply_ppu(p, p_host);
}


/**
 * $4020-$5FFF 读：状态寄存器与 4KB scratch RAM。
 * @param p_mapper mapper 实例
 * @param addr     CPU 地址
 * @return 读到的字节(未命中返回开总线值)
 */
static ines_byte_t mapper17_readlow(ines_mapper_t* p_mapper, ines_word_t addr)
{
	Mapper17_data_t* p = mapper2Mapper17data(p_mapper);

	if(addr >= 0x5000)
	{
		return p->scratch[addr & (SMC_SCRATCH_SIZE - 1)];
	}

	switch(addr)
	{
	case 0x4500:
		/* WBLL LMPm：按钮未按下(B=1)、并口无数据待读(W=0) */
		return (ines_byte_t)(0x40 | ((p->mode_1m_data >> 2) & 0x3c) | (p->mode_1m_addr & 0x03));
	case 0x4501:
		/* LLLL LL42：高 6 位是锁存值，低 2 位是 $43Fx 的 A1/A0 */
		return (ines_byte_t)((p->latch & 0xfc) | (p->mode_24m_addr & 0x03));
	default:
		return (ines_byte_t)(addr >> 8);   /* $4508-$450F 软驱/并口等：开总线 */
	}
}


/**
 * $4020-$5FFF 写：全部卡带寄存器。
 * @param p_mapper mapper 实例
 * @param addr     CPU 地址
 * @param val      写入值
 */
static void mapper17_writelow(ines_mapper_t* p_mapper, ines_word_t addr, ines_byte_t val)
{
	ines_host_t*     p_host = mapper2host(p_mapper);
	Mapper17_data_t* p      = mapper2Mapper17data(p_mapper);

	if(addr >= 0x5000)
	{
		p->scratch[addr & (SMC_SCRATCH_SIZE - 1)] = val;
		return;
	}

	switch(addr)
	{
	case 0x42FC:
	case 0x42FD:
	case 0x42FE:
	case 0x42FF:
		/* 1M banking：数据位取 D7-D4，地址位 A1/A0 分别决定写保护与镜像类型 */
		p->mode_1m_data = (ines_byte_t)(val & 0xf0);
		p->mode_1m_addr = (ines_byte_t)(addr & 0x03);
		mapper17_apply_prg(p, p_host);
		mapper17_apply_ppu(p, p_host);
		break;
	case 0x43FC:
	case 0x43FD:
	case 0x43FE:
	case 0x43FF:
		/* 2M/4M banking：A0=0 启用(此时 A1 选 4M/2M)，A0=1 关闭；D1-D0 是公共 8KB CHR 页 */
		p->mode_24m_addr = (ines_byte_t)(addr & 0x03);
		p->chr_8k        = (ines_byte_t)(val & 0x03);
		mapper17_apply_prg(p, p_host);
		mapper17_apply_ppu(p, p_host);
		break;
	case 0x4500:
		p->smc_mode  = val;
		p->wram_bank = (ines_byte_t)((val >> 4) & SMC_WRAM_BANK_MASK);
		ines_set_sram_bank_n(p_host, 3, p->wram_bank);
		mapper17_apply_ppu(p, p_host);
		break;
	case 0x4501:
		/* 应答 IRQ 并停止计数 */
		p->irq_enabled = ines_false;
		ines_cpu_IRQ(&p_host->cpu, MMC_IRQ_MASK, ines_false);
		break;
	case 0x4502:
		p->irq_counter = (ines_word_t)((p->irq_counter & 0xff00) | val);
		ines_cpu_IRQ(&p_host->cpu, MMC_IRQ_MASK, ines_false);
		break;
	case 0x4503:
		p->irq_counter = (ines_word_t)(((ines_word_t)val << 8) | (p->irq_counter & 0x00ff));
		p->irq_enabled = ines_true;
		ines_cpu_IRQ(&p_host->cpu, MMC_IRQ_MASK, ines_false);
		break;
	case 0x4504:
	case 0x4505:
	case 0x4506:
	case 0x4507:
		/* 4M 模式的 8KB PRG 寄存器(2M 模式下是窗口寄存器的镜像，不影响 CHR) */
		p->prg[addr & 0x03] = (ines_byte_t)(val & SMC_PRG_BANK_MASK);
		mapper17_apply_prg(p, p_host);
		break;
	case 0x4510:
	case 0x4511:
	case 0x4512:
	case 0x4513:
	case 0x4514:
	case 0x4515:
	case 0x4516:
	case 0x4517:
		p->chr[addr & 0x07] = val;
		mapper17_apply_ppu(p, p_host);
		break;
	case 0x4518:
	case 0x4519:
	case 0x451A:
	case 0x451B:
		p->chr_nt[addr & 0x03] = val;
		mapper17_apply_ppu(p, p_host);
		break;
	default:
		/* $4024/$4025(FDS)、$4508-$450F(软驱/并口)等不模拟 */
		break;
	}
}


/**
 * $6000-$FFFF 写：$8000-$FFFF 同时写锁存器与 2M 窗口寄存器。
 * @param p_mapper mapper 实例
 * @param addr     CPU 地址
 * @param val      写入值(PPPP PPCC)
 */
static void mapper17_writehigh(ines_mapper_t* p_mapper, ines_word_t addr, ines_byte_t val)
{
	ines_host_t*     p_host = mapper2host(p_mapper);
	Mapper17_data_t* p      = mapper2Mapper17data(p_mapper);
	ines_word_t      n;

	/* $6000-$7FFF 只在 WRAM 被写保护时才走到这里，本 mapper 不处理 */
	if(addr < 0x8000)
		return;

	n = (ines_word_t)((addr >> 13) - 4);

	/* 2M 窗口寄存器：即使 2M 模式未启用也照样接受写入 */
	p->prg[n] = (ines_byte_t)((val >> 2) & SMC_PRG_BANK_MASK);
	p->chr_8k = (ines_byte_t)(val & 0x03);

	/* 锁存器只在 PRG 写保护时更新($42Fx 的 A1) */
	if(p->mode_1m_addr & 0x02)
	{
		p->latch = val;
	}

	mapper17_apply_prg(p, p_host);
	mapper17_apply_ppu(p, p_host);
}


/**
 * 扫描线回调：推进 IRQ 计数器。
 * @param p_mapper mapper 实例
 * @param scanline 当前扫描线
 * @note $4500 的 D3 决定计数源：0 = M2 上升沿(每个 CPU 周期一次，这里按扫描线对账真实周期增量)，
 *       1 = PA12 上升沿(未滤波，每扫描线 8 次，只在可见扫描线且开屏时产生)。
 *       本核心没有逐周期回调，两种源的触发时刻都量化到扫描线边界。
 */
static void mapper17_hsync(ines_mapper_t* p_mapper, ines_int_t scanline)
{
	ines_host_t*     p_host = mapper2host(p_mapper);
	Mapper17_data_t* p      = mapper2Mapper17data(p_mapper);
	ines_int64_t     now    = p_host->cpu.total_cycles;
	ines_int64_t     delta;
	ines_dword_t     step;
	ines_dword_t     next;

	delta = (now >= p->irq_synced) ? (now - p->irq_synced) : 0;
	p->irq_synced = now;

	if(!p->irq_enabled || delta == 0)
		return;

	if(p->smc_mode & 0x08)
	{
		/* PA12：开屏($2001 的 D3/D4)且可见扫描线才有取图，每线 8 次 */
		if(scanline < 0 || scanline > 239 || (p_host->ppu.reg_ctrl_2 & 0x18) == 0)
			return;
		step = 8;
	}
	else
	{
		step = (ines_dword_t)delta;   /* M2：vblank 期间 CPU 照样跑，周期照计 */
	}

	next = (ines_dword_t)p->irq_counter + step;
	if(next > 0xffff)   /* $FFFF -> $0000 翻转时产生 IRQ */
	{
		p->irq_counter = (ines_word_t)(next & 0xffff);
		ines_cpu_IRQ(&p_host->cpu, MMC_IRQ_MASK, ines_true);
	}
	else
	{
		p->irq_counter = (ines_word_t)next;
	}
}


/**
 * PPU 取图回调：MMC4 模式的 $FD/$FE 切换。
 * @param p_mapper mapper 实例
 * @param addr     本次取到的 pattern 地址
 */
static void mapper17_PPU_latch_FDFE(ines_mapper_t* p_mapper, ines_word_t addr)
{
	ines_host_t*     p_host = mapper2host(p_mapper);
	Mapper17_data_t* p      = mapper2Mapper17data(p_mapper);
	ines_byte_t      sel;

	if((p->smc_mode & 0x01) == 0 || (p->smc_mode & 0x04))
		return;   /* 仅 1KB CHR 模式且 MMC4 模式启用时有效 */

	switch(addr & 0x1ff8)
	{
	case 0x0fd8: sel = 0; break;   /* $0FD8-$0FDF -> $4510 */
	case 0x0fe8: sel = 2; break;   /* $0FE8-$0FEF -> $4512 */
	case 0x1fd8: sel = 4; break;   /* $1FD8-$1FDF -> $4514 */
	case 0x1fe8: sel = 6; break;   /* $1FE8-$1FEF -> $4516 */
	default:     return;
	}

	if(addr < 0x1000)
	{
		if(p->mmc4_sel[0] == sel)
			return;
		p->mmc4_sel[0] = sel;
	}
	else
	{
		if(p->mmc4_sel[1] == sel)
			return;
		p->mmc4_sel[1] = sel;
	}

	mapper17_apply_ppu(p, p_host);
}


/**
 * 释放私有数据。
 * @param p_mapper mapper 实例
 */
static void mapper17_fini(ines_mapper_t* p_mapper)
{
	INES_LOG(LOG_DBG, MOD_MMC, ISTR("mapper17_fini.\n"));
	ines_free(p_mapper->p_data);
	p_mapper->p_data = NULL;
}


/**
 * 创建 mapper 17(Front Fareast Super Magic Card)。
 * @param p_mapper mapper 实例
 * @return 恒为 ines_true(已实现)
 */
ines_bool_t  mapper17_create(ines_mapper_t* p_mapper)
{
	INIT_MAPPER_DATA_ST(p_mapper, Mapper17_data_t);
	p_mapper->custom_sram    = 1;   /* $6000-$7FFF 的 WRAM 由本 mapper 自己切页 */
	p_mapper->reset          = mapper17_reset;
	p_mapper->hsync          = mapper17_hsync;
	p_mapper->readlow        = mapper17_readlow;
	p_mapper->writelow       = mapper17_writelow;
	p_mapper->writehigh      = mapper17_writehigh;
	p_mapper->PPU_latch_FDFE = mapper17_PPU_latch_FDFE;
	p_mapper->fini           = mapper17_fini;
	return ines_true;
}



