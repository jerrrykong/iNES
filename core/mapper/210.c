/**
 * @file    210.c
 * @brief   iNES Mapper 210 - Namco 175 / Namco 340
 *
 * 硬件要点(依据 NESdev wiki "INES Mapper 210" oldid=23707)：
 *
 * 175 与 340 都是 Namco 163 的降本版本，iNES 头都用 210 表示(不能用 iNES 头区分)，
 * NES 2.0 用 submapper 区分(1 = 175、2 = 340)：
 *
 *   功能         | N163 | N175 | N340
 *   IRQ 计数器   | Yes  | No   | No
 *   ROM nametable| Yes  | No   | No
 *   可选 WRAM    | Yes  | Yes  | No
 *   扩展音       | Yes  | No   | No
 *   ASIC 内部 RAM| Yes  | No   | No
 *   镜像         | 可扩展| 硬连 H/V | 可选 H/V/单屏
 *
 * CPU 地址空间($8000-$FFFF 共 12 个寄存器，每个占 $800 字节)：
 *   $8000-$BFFF  8 个 1KB CHR 窗口：$8000/$8800/.../$B800 依次对应
 *                PPU $0000/$0400/.../$1C00，写入值 $00-$FF 即 1KB CHR-ROM 页号
 *   $C000-$C7FF  Namco 175 专用：外部 PRG RAM 使能(b0 = 1 使能)
 *   $E000-$E7FF  MMPP PPPP：P = $8000-$9FFF 的 8KB PRG 页；
 *                M = 镜像选择(Namco 340 专用)：0 = One-screen A、1 = Vertical、
 *                    2 = One-screen B、3 = Horizontal
 *   $E800-$EFFF  ..PP PPPP：P = $A000-$BFFF 的 8KB PRG 页
 *   $F000-$F7FF  ..PP PPPP：P = $C000-$DFFF 的 8KB PRG 页
 *   $F800-$FFFF  无寄存器(写入丢弃)
 *   $E000-$FFFF  最后一个 8KB PRG 页固定
 *
 * 175/340 均无总线冲突，写入值直接采信。
 *
 * 已决事项：
 *   1. 8KB PRG RAM($6000-$7FFF) 统一由宿主挂载(`custom_sram` 保持默认 0)：
 *      Namco 340 卡带实际没有这片 RAM，但不解析 NES 2.0 submapper 就无法可靠区分变体
 *      (wiki 建议的"按 iNES 电池位猜"只能覆盖带电池的 175)，而"不挂 RAM"的代价大于收益：
 *      ① $6000 拿不到干净的开总线值——读走 `NES_BANK_CAN_READ(bank_writeable[3])`，
 *         该位为 0 时仍为真，于是落到复位时指向 CPU 内部 `dead_mem` 哑缓冲的 `mem_bank[3]`；
 *      ② 更严重的：CPU 即时存档对 `bank_writeable[n] == 0` 的块按 PROM 指针编码
 *         (`PROM28KNUM(mem_bank[n])`)，`dead_mem` 不在 `rom.pPROMs` 里 → 存出垃圾页号，
 *         读档校验 `BANK >= prom_8k_num` 直接失败。多给 8KB RAM 后 `mem_bank[3]` 指向 SRAM，
 *         两个问题都不存在，且对 340 游戏无可见影响(它们不会去读自己没有的 RAM)，
 *         175 的隐藏自检(要求 RAM 存在)反而能通过。无电池卡带不会被写盘(`has_sram == 0`)。
 *   2. $C000-$C7FF 的 PRG RAM 使能位不模拟(不按位门控)：直接常开的可见行为一致，
 *      且不会因漏写而丢存档(Family Circuit '91 / Splatterworld 初始化时都会写它使能)。
 *   3. Family Circuit '91 的 2KB PRG RAM 镜像到整个 $6000-$7FFF、以及"短接扩展端口触发的隐藏测试"
 *      不模拟：只影响彩蛋，不影响正常游戏。
 *   4. 部分 210 卡带的 iNES 头被错标成 mapper 19(反之亦然)，本实现不做启发式纠正。
 */

#include "../../comm/idef.h"
#include "../../comm/log.h"
#include "../nes.h"
#include "../mapper.h"


#define M210_CHR_WINDOW_NUM    8      // CHR 1KB 窗口数量
#define M210_PRG_BANK_MASK     0x3f   // PRG 页号 6 位(512KB / 8KB = 64 页)
#define M210_MIRROR_SHIFT      6      // $E000.bit6-7 = 镜像选择(Namco 340)
#define M210_MIRROR_MASK       0x03


/**
 * 应用 Namco 340 的镜像选择。
 * @param p_host 宿主
 * @param m      $E000.bit6-7 的值：0 = One-screen A、1 = Vertical、
 *               2 = One-screen B、3 = Horizontal
 * @note Namco 175 是硬连 H/V 的，但它的商用卡带都会把 $E000 高位写成与硬连一致的镜像，
 *       因此统一按 340 处理对两者都成立。
 */
static void M210_set_mirror(ines_host_t* p_host, ines_byte_t m)
{
	switch(m)
	{
	case 0:                                            // One-screen A：四方格全指向 A 页
		ines_ppu_set_mirror(&p_host->ppu, 0, 0, 0, 0);
		break;
	case 1:                                            // Vertical：$2000/$2800 = A，$2400/$2C00 = B
		ines_ppu_set_mirror(&p_host->ppu, 0, 1, 0, 1);
		break;
	case 2:                                            // One-screen B
		ines_ppu_set_mirror(&p_host->ppu, 1, 1, 1, 1);
		break;
	default:                                           // Horizontal：$2000/$2400 = A，$2800/$2C00 = B
		ines_ppu_set_mirror(&p_host->ppu, 0, 0, 1, 1);
		break;
	}
}


/**
 * 软件复位。
 * @param p_mapper mapper 实例
 */
static void mapper210_reset(ines_mapper_t* p_mapper)
{
	ines_host_t* p_host = mapper2host(p_mapper);

	// $6000-$7FFF 的 8KB PRG RAM 由宿主统一挂载(`custom_sram = 0`，见文件头"已决事项"第 1 条)。
	// PRG：$8000/$A000/$C000 三个可切槽 + $E000 固定最后一个 8KB 页
	ines_set_prom_bank_4(p_host,
	                     0,
	                     (p_host->prom_8k_num > 1) ? 1 : 0,
	                     (p_host->prom_8k_num > 2) ? 2 : 0,
	                     (ines_word_t)(p_host->prom_8k_num - 1));

	// CHR 8 个 1KB 窗口复位为线性 0-7(无 CHR-ROM 的卡带由宿主统一挂 pattern RAM)；
	// 镜像沿用 iNES 头声明的排布，340 会在首次写 $E000 时按 M 位切换。
	if(p_host->vrom_1k_num > 0)
		ines_set_vrom_bank_8(p_host, 0, 1, 2, 3, 4, 5, 6, 7);
}


/**
 * $8000-$FFFF 写：CHR 窗口、PRG 页、Namco 340 镜像选择。
 * @param p_mapper mapper 实例
 * @param addr     CPU 地址
 * @param val      写入值
 */
static void mapper210_writehigh(ines_mapper_t* p_mapper, ines_word_t addr, ines_byte_t val)
{
	ines_host_t* p_host = mapper2host(p_mapper);

	if(addr < 0xc000)                                     // $8000-$BFFF：8 个 1KB CHR 窗口
	{
		ines_word_t n = (ines_word_t)((addr - 0x8000) >> 11);

		if(n >= M210_CHR_WINDOW_NUM)
			return;                                       // 防御：不会发生，窗口号由地址决定

		if(p_host->vrom_1k_num > 0)
			ines_set_vrom_bank_n(p_host, n, (ines_word_t)val);
		else
			ines_set_vram_bank_n(p_host, n, (ines_word_t)(val & 0x1f));
		return;
	}

	if(addr < 0xe000)
	{
		// $C000-$DFFF：175 的 PRG RAM 使能(不模拟)与 Namco 163 的 NT 选择寄存器
		// (Splatterhouse 会按 163 的习惯每帧写 $C000-$DFFF)在此被丢弃。
		return;
	}

	if(addr < 0xe800)                                     // $E000-$E7FF：MMPP PPPP
	{
		if(p_host->rom.mirror_type != MIRROR_FOUR_SCREEN)
			M210_set_mirror(p_host, (ines_byte_t)((val >> M210_MIRROR_SHIFT) & M210_MIRROR_MASK));
		ines_set_prom_bank_n(p_host, 4, (ines_word_t)(val & M210_PRG_BANK_MASK));
		return;
	}

	if(addr < 0xf000)                                     // $E800-$EFFF：..PP PPPP
	{
		ines_set_prom_bank_n(p_host, 5, (ines_word_t)(val & M210_PRG_BANK_MASK));
		return;
	}

	if(addr < 0xf800)                                     // $F000-$F7FF：..PP PPPP
	{
		ines_set_prom_bank_n(p_host, 6, (ines_word_t)(val & M210_PRG_BANK_MASK));
		return;
	}

	// $F800-$FFFF：无寄存器
}


/**
 * 创建 mapper 210(Namco 175 / Namco 340)。
 * @param p_mapper mapper 实例
 * @return 恒为 ines_true(已实现)
 */
ines_bool_t mapper210_create(ines_mapper_t* p_mapper)
{
	// `custom_sram` 保持默认 0：$6000-$7FFF 交宿主挂 8KB PRG RAM(见文件头"已决事项"第 1 条)。
	// 本 mapper 无私有状态：bank/镜像都是立即生效且由宿主存档覆盖。
	p_mapper->reset     = mapper210_reset;
	p_mapper->writehigh = mapper210_writehigh;

	return ines_true;
}
