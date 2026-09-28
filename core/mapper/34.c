
/**
 * core/mapper/34.c -- BNROM / NINA-001、NINA-002（iNES Mapper 034）
 *
 * 硬件要点：Mapper 34 是**两种互不相干**的板共用一个编号，无 NES 2.0 submapper 时
 * 按 CHR 容量区分（见 mapper34_reset）：
 *
 *  1) BNROM / I-IM（Irem、Nintendo）—— CHR-ROM ≤ 8KB 时按此处理
 *     - CPU $8000-$FFFF：32KB 窗口，写入值的 bit0-1 = PRG A16..A15（最多 4 个 32KB bank）
 *     - PPU $0000-$1FFF：8KB CHR，不分页（CHR-ROM 或 CHR-RAM 皆可）
 *     - 无 PRG-RAM、无 IRQ、无扩展音；镜像由 PCB 焊盘固定，Mapper 不改写（沿用卡带头）
 *     - **必然发生 AND 型总线冲突**：真正锁进寄存器的是"写入值 AND 当前 PRG-ROM 在该
 *       地址的字节"，《Mashou》/《Deadly Towers》即依赖该行为，不能省
 *
 *  2) NINA-001 / NINA-002（American Video Entertainment）—— CHR-ROM > 8KB 时按此处理
 *     - CPU $6000-$7FFF：8KB PRG-RAM；三个寄存器"叠"在这片 RAM 的末尾（完全解码，
 *       A14..A2 必须全为 1）：
 *         $7FFD [.... ...A]  PRG A15（32KB bank）
 *         $7FFE [.... DCBA]  CHR A15..A12（4KB bank @ PPU $0000-$0FFF）
 *         $7FFF [.... DCBA]  CHR A15..A12（4KB bank @ PPU $1000-$1FFF）
 *       写入同时进寄存器和 RAM，所以读这些地址返回的就是最后一次写入的值。
 *     - CPU $8000-$FFFF：32KB 窗口（64KB PRG），**不响应** $8000+ 的写入
 *     - 无总线冲突、无 IRQ、无扩展音；镜像固定 H/V（沿用卡带头）
 *
 * 上电值在两种板的真实硬件上都未定义（游戏必须在每个 PRG bank 里都放复位向量），
 * 这里一律取 0。
 */

#include "../../comm/idef.h"
#include "../../comm/log.h"
#include "../nes.h"
#include "../mapper.h"


/* NINA 的三个寄存器（完全解码，不是区间） */
#define NINA34_REG_PRG       0x7FFD
#define NINA34_REG_CHR0      0x7FFE
#define NINA34_REG_CHR1      0x7FFF

/* NINA 的 8KB PRG-RAM 用宿主的 SRAM 块 0（对应 $6000-$7FFF） */
#define NINA34_SRAM_BANK     0

/* 32KB 窗口 = 4 个 8KB 页 */
#define NINA34_PRG_32K_SHIFT 2

/* 判定为 NINA 的 CHR 容量门槛：CHR-ROM 超过 8KB（8 个 1KB 页） */
#define NINA34_CHR_8K_PAGES  8


struct _NINA34_data_
{
	ines_bool_t   is_nina;      /* ines_true = NINA-001/NINA-002；ines_false = BNROM */
	ines_byte_t   prg_bank;     /* 32KB PRG bank（NINA 只用 bit0，BNROM 用 bit0-1） */
	ines_byte_t   chr_4k[2];    /* NINA 两个 4KB CHR 窗口的页号（bit0-3） */
};

typedef struct _NINA34_data_   NINA34_data_t;

#define mapper2NINA34data(mapper)   ((NINA34_data_t*)((mapper)->p_data))


/**
 * 按当前 PRG bank 寄存器刷新 CPU $8000-$FFFF 的 32KB 窗口。
 * 四个 8KB 页号都各自对总页数取模，PRG 不足 32KB 的畸形 ROM 也不会越界。
 * @param p      私有数据
 * @param p_host 宿主
 */
static void NINA34_set_cpu_bank(NINA34_data_t* p, ines_host_t* p_host)
{
	ines_word_t  num = (p_host->prom_8k_num > 0) ? p_host->prom_8k_num : 1;
	ines_word_t  num_32k = (ines_word_t)(num >> NINA34_PRG_32K_SHIFT);
	ines_word_t  base;

	if(num_32k == 0)
		num_32k = 1;      /* PRG 不足 32KB：只有 bank 0 */

	base = (ines_word_t)((p->prg_bank % num_32k) << NINA34_PRG_32K_SHIFT);

	ines_set_prom_bank_4(p_host,
						 (ines_word_t)(base % num),
						 (ines_word_t)((base + 1) % num),
						 (ines_word_t)((base + 2) % num),
						 (ines_word_t)((base + 3) % num));
}

/**
 * 刷新 PPU 的 CHR 窗口。
 *  - BNROM：8KB 不分页，有 CHR-ROM 就映射前 8 页，纯 CHR-RAM 卡带映射 pattern RAM；
 *  - NINA：两个独立的 4KB 窗口，各有 4 位页号（CHR 最多 64KB）。
 * @param p      私有数据
 * @param p_host 宿主
 */
static void NINA34_set_ppu_bank(NINA34_data_t* p, ines_host_t* p_host)
{
	ines_word_t  n;

	if(!p->is_nina || (p_host->vrom_1k_num == 0))
	{
		/* BNROM 的 8KB 不分页；NINA 按定义必带 CHR-ROM，走到这里说明 ROM 头没给 CHR，
		   退化成 8KB pattern RAM，两边都是同一套映射 */
		if(p_host->vrom_1k_num > 0)
		{
			ines_set_vrom_bank_8(p_host, 0, 1, 2, 3, 4, 5, 6, 7);
		}
		else
		{
			for(n = 0; n < 8; n++)
				ines_set_vram_bank_n(p_host, n, n);
		}

		return;
	}

	{
		ines_word_t  num_4k = (ines_word_t)(p_host->vrom_1k_num >> 2);
		ines_word_t  b0, b1;

		if(num_4k == 0)
			num_4k = 1;      /* CHR 不足 4KB：只有 bank 0 */

		b0 = (ines_word_t)((p->chr_4k[0] % num_4k) << 2);
		b1 = (ines_word_t)((p->chr_4k[1] % num_4k) << 2);

		for(n = 0; n < 4; n++)
		{
			ines_set_vrom_bank_n(p_host, n,     (ines_word_t)(b0 + n));
			ines_set_vrom_bank_n(p_host, n + 4, (ines_word_t)(b1 + n));
		}
	}
}

static void mapper34_reset(ines_mapper_t* p_mapper)
{
	ines_host_t*  p_host = mapper2host(p_mapper);
	NINA34_data_t*  p = mapper2NINA34data(p_mapper);

	/* 没有 submapper 信息时按 CHR-ROM 容量区分：超过 8KB 才是 NINA */
	p->is_nina = (p_host->vrom_1k_num > NINA34_CHR_8K_PAGES) ? ines_true : ines_false;

	p->prg_bank  = 0;
	p->chr_4k[0] = 0;
	p->chr_4k[1] = 0;

	if(p->is_nina)
	{
		/* 8KB PRG-RAM 由宿主提供，但写入必须交给本 mapper：置 WRITE_PROTECTED 后
		   $6000-$7FFF 的写落到 mapper34_writelow，读仍是 CAN_READ 为真、直接读
		   mem_bank[3]（RAM），与"读回最后写入的值"一致。
		   用 WRITE_PROTECTED 而不是把 bank_writeable[3] 清零：非 0 值能被即时存档
		   按 SRAM 块号正确还原（清零会被当成 PROM 指针编码，读档校验失败）。 */
		ines_set_sram_bank_n(p_host, 3, NINA34_SRAM_BANK);
		p_host->cpu.bank_writeable[3] = NES_BANK_WRITE_PROTECTED;
	}

	/* BNROM 卡带实际没有 PRG-RAM，但这里刻意保持 custom_sram = 0(默认)，由宿主挂一块
	   8KB RAM：既不破坏 $6000 的读值，也避免 bank_writeable[3] 为 0 时即时存档按 PROM
	   指针编码、读档校验失败（同 mapper 210 的取舍）。 */


	NINA34_set_cpu_bank(p, p_host);
	NINA34_set_ppu_bank(p, p_host);

	/* 两种板的镜像都由硬件固定（焊盘 / 卡带头），Mapper 不改写 */

	INES_LOG(LOG_INF, MOD_MMC, ISTR("mapper34: %s selected (PRG %dKB, CHR %dKB)\n"),
			 p->is_nina ? ISTR("NINA-001/002") : ISTR("BNROM"),
			 (int)(p_host->prom_8k_num * 8), (int)p_host->vrom_1k_num);
}

/**
 * $6000-$7FFF 的写入（仅 NINA 会走到这里）。
 * 寄存器与 PRG-RAM 同地址：写入值既要锁进寄存器，也要落进 RAM（读回的就是它）。
 * @param p_mapper Mapper
 * @param addr     $6000-$7FFF
 * @param val      写入值
 */
static void mapper34_writelow(ines_mapper_t* p_mapper, ines_word_t addr, ines_byte_t val)
{
	ines_host_t*  p_host = mapper2host(p_mapper);
	NINA34_data_t*  p = mapper2NINA34data(p_mapper);

	p_host->SRAM[((ines_dword_t)NINA34_SRAM_BANK << 13) + (addr & 0x1FFF)] = val;
	p_host->SRAM_write_flag = 1;

	if(!p->is_nina)
		return;      /* BNROM 的 $6000-$7FFF 由宿主直接写入，不会落到这里 */

	INES_LOG(LOG_DBG, MOD_MMC, ISTR("NINA: write $%04X = #$%02X\n"), addr, val);

	switch(addr)
	{
	case NINA34_REG_PRG:
		p->prg_bank = val;
		NINA34_set_cpu_bank(p, p_host);
		break;

	case NINA34_REG_CHR0:
	case NINA34_REG_CHR1:
		p->chr_4k[addr - NINA34_REG_CHR0] = val;
		NINA34_set_ppu_bank(p, p_host);
		break;

	default:
		break;      /* 其余地址只是普通的 PRG-RAM 写入 */
	}
}

/**
 * $8000+ 的写入（仅 BNROM 响应）：32KB PRG bank 号在 bit0-1。
 * I-IM/BNROM 板必然发生 AND 型总线冲突，锁存的是"写入值 AND 当前映射在该地址的
 * PRG-ROM 字节"；NINA 的寄存器在 $7FFD-$7FFF，不响应这里的写入。
 * @param p_mapper Mapper
 * @param addr     $8000-$FFFF
 * @param val      写入值
 */
static void mapper34_writehigh(ines_mapper_t* p_mapper, ines_word_t addr, ines_byte_t val)
{
	ines_host_t*  p_host = mapper2host(p_mapper);
	NINA34_data_t*  p = mapper2NINA34data(p_mapper);

	if(p->is_nina)
		return;

	val = (ines_byte_t)(val & p_host->cpu.mem_bank[addr >> 13][addr & 0x1FFF]);

	INES_LOG(LOG_DBG, MOD_MMC, ISTR("BNROM: write $%04X = #$%02X (bank %d)\n"),
			 addr, val, (int)(val & 0x03));

	p->prg_bank = val;
	NINA34_set_cpu_bank(p, p_host);
}

static void mapper34_fini(ines_mapper_t* p_mapper)
{
	INES_LOG(LOG_DBG, MOD_MMC, ISTR("mapper34_fini.\n"));
	ines_free(p_mapper->p_data);
	p_mapper->p_data = NULL;
}

ines_bool_t  mapper34_create(ines_mapper_t* p_mapper)
{
	INIT_MAPPER_DATA_ST(p_mapper, NINA34_data_t);
	p_mapper->reset = mapper34_reset;
	p_mapper->writehigh = mapper34_writehigh;
	p_mapper->writelow = mapper34_writelow;
	p_mapper->fini = mapper34_fini;
	return ines_true;
}


