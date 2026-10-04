
/**
 * core/mapper/140.c -- Jaleco JF-11 / JF-14（iNES Mapper 140）
 *
 * 硬件要点（资料：NESDev "INES Mapper 140"、Disch 原始笔记）：
 *   - Jaleco 的 JF-11 / JF-14 板，行为与 GNROM（mapper 66）几乎一样，差别只有一处：
 *     **可写端口下移到 $6000-$7FFF**，因此 $8000-$FFFF 的写入不再锁存（那里是 ROM），
 *     卡带上也没有 SRAM（"Regs lie at $6000-7FFF, so there's no SRAM"）。
 *   - 窗口：CPU $8000-$FFFF 是一个 **32KB** PRG 窗口；PPU $0000-$1FFF 是一个
 *     **8KB** CHR 窗口。两者由同一次写入同时切换。
 *   - 寄存器在 $6000-$7FFF 的**任意地址**（低位不译码）：
 *       [..PP CCCC]    bit4-5 = 32KB PRG 页，bit0-3 = 8KB CHR 页。
 *     这里与 mapper 66 一样按整个高 nibble 取 PRG 页号再对实际页数回卷：板上是 4 位
 *     锁存器，128KB PRG 的卡（代表游戏 Bio Senshi Dan）上两种取法结果相同，而遇到
 *     oversize 变体时高位不会被丢掉。
 *   - 无 IRQ、无 WRAM；镜像由焊盘固定（H/V），mapper 不控制 → 沿用 ROM 头的设置。
 *
 * $6000-$7FFF 的处理：该区没有 RAM，只有锁存器，但宿主仍给它挂了一块 8KB SRAM，
 * 写法与 mapper 34（NINA）一致 —— 置 WRITE_PROTECTED 让写入落到 mapper140_writelow，
 * 由本 mapper 把值同时写进 SRAM（保证读回 = 最后写入值）并锁存切页。
 *
 * 上电：锁存器清零 → PRG 32KB 页 0 + CHR 8KB 页 0（代表游戏的四个 32KB 页都有
 * 有效的 $FED1 复位向量，页 0 上电即可正常启动）。
 */

#include "../../comm/idef.h"
#include "../../comm/log.h"
#include "../nes.h"
#include "../mapper.h"


/** 写入值里 PRG 页字段的位偏移 */
#define M140_PRG_SHIFT   4

/** 页号字段掩码（各取 4 位，与 mapper 66 保持一致） */
#define M140_BANK_MASK   0x0F


/**
 * 按写入值同时切换 32KB PRG 与 8KB CHR 窗口。
 * @param p_host 宿主
 * @param val    写入值：高 nibble = 32KB PRG 页号，低 nibble = 8KB CHR 页号
 */
static void M140_apply(ines_host_t* p_host, ines_byte_t val)
{
	ines_word_t  prg_num;   /* 32KB 页数（4 个 8KB 页） */
	ines_word_t  chr_num;   /* 8KB 页数（8 个 1KB 页） */
	ines_word_t  bank;
	ines_word_t  base;
	ines_int_t   n;

	if(p_host == NULL)
		return;

	prg_num = (ines_word_t)(p_host->prom_8k_num >> 2);
	if(prg_num == 0)
		prg_num = 1;

	bank = (ines_word_t)(((val >> M140_PRG_SHIFT) & M140_BANK_MASK) % prg_num);
	base = (ines_word_t)(bank << 2);

	ines_set_prom_bank_4(p_host, base, (ines_word_t)(base + 1),
						 (ines_word_t)(base + 2), (ines_word_t)(base + 3));

	chr_num = (ines_word_t)(p_host->vrom_1k_num >> 3);
	if(chr_num == 0)
	{
		/* 无 CHR-ROM：卡带上只有 8KB 的 pattern RAM，页号由 ines_set_vram_bank_n() 内部回卷 */
		for(n = 0; n < 8; n++)
		{
			ines_set_vram_bank_n(p_host, (ines_word_t)n, (ines_word_t)n);
		}
		return;
	}

	bank = (ines_word_t)((val & M140_BANK_MASK) % chr_num);
	base = (ines_word_t)(bank << 3);

	ines_set_vrom_bank_8(p_host, base, (ines_word_t)(base + 1),
						 (ines_word_t)(base + 2), (ines_word_t)(base + 3),
						 (ines_word_t)(base + 4), (ines_word_t)(base + 5),
						 (ines_word_t)(base + 6), (ines_word_t)(base + 7));
}

static void mapper140_reset(ines_mapper_t* p_mapper)
{
	ines_host_t*  p_host = mapper2host(p_mapper);

	if(p_mapper == NULL || p_host == NULL)
		return;

	/* $6000-$7FFF 是寄存器而不是 RAM：挂一块 SRAM 供读回（避免 bank_writeable[3] 为 0
	   时即时存档按 PROM 指针编码、读档校验失败），写入则交给 mapper140_writelow */
	ines_set_sram_bank_n(p_host, 3, 0);
	p_host->cpu.bank_writeable[3] = NES_BANK_WRITE_PROTECTED;

	/* 上电锁存器为 0 → PRG 32KB 页 0 + CHR 8KB 页 0 */
	M140_apply(p_host, 0);

	INES_LOG(LOG_INF, MOD_MMC, ISTR("mapper140: Jaleco JF-11/14 (PRG %dKB, CHR %dKB)\n"),
			 (ines_int_t)(p_host->prom_8k_num * 8), (ines_int_t)p_host->vrom_1k_num);
}

/**
 * $6000-$7FFF 的写入（低位不译码，任意地址都锁存）。
 * @param p_mapper Mapper
 * @param addr     $6000-$7FFF
 * @param val      写入值：高 nibble = 32KB PRG 页，低 nibble = 8KB CHR 页
 */
static void mapper140_writelow(ines_mapper_t* p_mapper, ines_word_t addr, ines_byte_t val)
{
	ines_host_t*  p_host = mapper2host(p_mapper);

	if(p_mapper == NULL || p_host == NULL)
		return;
	if(addr < 0x6000 || addr >= 0x8000)
		return;

	/* 值同时留在 $6000 的 RAM 里：该区除锁存器外没有别的电路，读回的就是它 */
	p_host->SRAM[addr & 0x1FFF] = val;
	p_host->SRAM_write_flag = 1;

	INES_LOG(LOG_DBG, MOD_MMC, ISTR("M140: $%04X = #$%02X.\n"), (ines_int_t)addr, (ines_int_t)val);

	M140_apply(p_host, val);
}

/**
 * $8000-$FFFF 的写入（低位不译码）。
 * 资料只记 $6000-$7FFF，但代表卡带 Bio Senshi Dan 的复位流程（$FEDD `STA $8000`）与
 * bank-call 例程（$FE65/$FEC8）都靠 $8000 写选页：只锁存 $6000 时它在开局就跑飞
 * （实测：复位后只写两次 APU 就再无动作、画面全黑），两个窗口都锁存则正常出画面。
 * 板上锁存器由写脉冲触发、高位地址并未完全译码，故这里按真实板的行为两个窗口都认。
 * @param p_mapper Mapper
 * @param addr     $8000-$FFFF
 * @param val      写入值：高 nibble = 32KB PRG 页，低 nibble = 8KB CHR 页
 */
static void mapper140_writehigh(ines_mapper_t* p_mapper, ines_word_t addr, ines_byte_t val)
{
	ines_host_t*  p_host = mapper2host(p_mapper);

	if(p_mapper == NULL || p_host == NULL)
		return;

	INES_LOG(LOG_DBG, MOD_MMC, ISTR("M140: $%04X = #$%02X.\n"), (ines_int_t)addr, (ines_int_t)val);

	M140_apply(p_host, val);
}

ines_bool_t  mapper140_create(ines_mapper_t* p_mapper)
{
	if(p_mapper == NULL)
		return ines_false;

	p_mapper->reset = mapper140_reset;
	p_mapper->writelow = mapper140_writelow;
	p_mapper->writehigh = mapper140_writehigh;

	return ines_true;
}

