
// ============================================================================
// vrc.h -- Konami VRC 家族（VRC2/VRC4/VRC6/VRC7）共享实现
// ----------------------------------------------------------------------------
// 提供：
//   1. 公共 IRQ 计数器（VRC4/6/7 相同硬件语义，参考 FCEUX/真实芯片）
//   2. VRC2/VRC4 (mapper 21/22/23/25) 的完整银行/镜像/IRQ 逻辑
//   3. VRC6 (mapper 24/26) 的完整数字逻辑 + 扩展音寄存器状态捕获
//   4. VRC7 (mapper 85) 的完整数字逻辑 + FM 寄存器状态捕获
//
// 说明：
//   - 本项目宿主帧循环只在每条扫描线的 hsync 处回调 mapper；而 VRC 的
//     IRQ 计数器是 CPU 周期驱动（无扫描线递减硬件）。因此 hsync 中按
//     host->cpu.total_cycles 的真实增量做批处理推进，二者仅差一个
//     "行内触发时刻" 的量化误差（约 1 个扫描线以内），累计不漂移。
//   - VRC6/VRC7 的扩展音源（3 路 PSG / YM2413 FM）需要 APU 侧增加扩展
//     声道混音接口后才能发声，本文件只负责捕获音频寄存器状态。
// ============================================================================
#ifndef __INES_MAPPER_VRC_H__
#define __INES_MAPPER_VRC_H__

#include "../../comm/idef.h"
#include "../../comm/log.h"
#include "../nes.h"
#include "../mapper.h"

// ============================================================================
// 1. 公共 IRQ 计数器
//    控制寄存器位：(写 $X002/$X001/$X000 视芯片而定)
//      bit0 : delayed-enable（锁存给 acknowledge 用的使能）
//      bit1 : enable（置位时使能并使计数重载）
//      bit2 : 模式  0=341 PPU 周期(约1扫描线) +1 ; 1=每 CPU 周期 +1
//    计数器从 latch 开始递增，进位(>=0x100) 时触发 IRQ 并从 latch 重载。
// ============================================================================
typedef struct _VRC_IRQ_state_
{
	ines_int64_t  last_cycles;    // 上一次 hsync 时 cpu.total_cycles
	ines_int_t    latch;          // 重载值（锁存）
	ines_int_t    counter;        // 当前计数值
	ines_int_t    rem;            // 未消费的推进余量（cycles 或 ppu dots）
	ines_byte_t   enabled;        // 当前使能
	ines_byte_t   delayed;        // acknowledge 用延迟使能 (bit0)
	ines_byte_t   mode;           // bit2: 1=CPU周期模式, 0=扫描线(341 dot)模式
	ines_byte_t   has_irq;        // 芯片是否有 IRQ 硬件 (VRC2a 无)
} VRC_IRQ_state_t;

static inline void vrc_irq_reset(VRC_IRQ_state_t* p_irq)
{
	p_irq->last_cycles = 0;
	p_irq->latch = 0;
	p_irq->counter = 0;
	p_irq->rem = 0;
	p_irq->enabled = 0;
	p_irq->delayed = 0;
	p_irq->mode = 0;
}

// 每个 hsync 调用一次：把自上次以来真实流逝的 CPU 周期推进到计数器。
static inline void vrc_irq_tick(VRC_IRQ_state_t* p_irq, ines_host_t* p_host)
{
	ines_int64_t  now = p_host->cpu.total_cycles;
	ines_int_t    delta = (ines_int_t)(now - p_irq->last_cycles);
	p_irq->last_cycles = now;      // 自愈：状态恢复后 delta 可能为负，下次即正常

	if(!p_irq->has_irq || !p_irq->enabled || delta <= 0)
		return;

	if(p_irq->mode)
	{
		// CPU 周期模式：每 1 个 CPU 周期 +1
		p_irq->rem += delta;
		while(p_irq->rem > 0)
		{
			p_irq->rem--;
			p_irq->counter++;
			if(p_irq->counter >= 0x100)
			{
				p_irq->counter = p_irq->latch;
				ines_cpu_IRQ(&p_host->cpu, MMC_IRQ_MASK, ines_true);
			}
		}
	}
	else
	{
		// 扫描线(预分频)模式：每 341 个 PPU dot（=约1扫描线，CPU 1 周期 = 3 dots）+1
		p_irq->rem += delta * 3;
		while(p_irq->rem >= 341)
		{
			p_irq->rem -= 341;
			p_irq->counter++;
			if(p_irq->counter >= 0x100)
			{
				p_irq->counter = p_irq->latch;
				ines_cpu_IRQ(&p_host->cpu, MMC_IRQ_MASK, ines_true);
			}
		}
	}
}

// 清除挂起 IRQ（控制/锁存寄存器写入时调用）
static inline void vrc_irq_clear(VRC_IRQ_state_t* p_irq, ines_host_t* p_host)
{
	(void)p_irq;
	ines_cpu_IRQ(&p_host->cpu, MMC_IRQ_MASK, ines_false);
}

// ============================================================================
// 2. VRC 通用镜像设置
//    VRC 系列寄存器值: 0=垂直 1=水平 2=单屏(上) 3=单屏(下)
// ============================================================================
static inline void vrc_set_mirror(ines_mapper_t* p_mapper, ines_byte_t val)
{
	ines_host_t* p_host = mapper2host(p_mapper);
	if(p_host->rom.mirror_type == MIRROR_FOUR_SCREEN)
		return;
	switch(val & 0x03)
	{
	case 0:  ines_ppu_set_mirror_type(&p_host->ppu, MIRROR_VERT);          break;
	case 1:  ines_ppu_set_mirror_type(&p_host->ppu, MIRROR_HORZ);          break;
	case 2:  ines_ppu_set_mirror(&p_host->ppu, 0, 0, 0, 0);                break; // 单屏表0
	default: ines_ppu_set_mirror(&p_host->ppu, 1, 1, 1, 1);                break; // 单屏表1
	}
}

// ============================================================================
// 3. VRC2 / VRC4 (mapper 21 / 22 / 23 / 25)
//    - PRG: 4 x 8KB 槽; $8000/$A000 可切换, $C000/$E000 固定（reg_cmd bit1 可交换）
//    - CHR: 8 x 1KB 页; 每页由 "偶偏移写低4位 + 奇偏移写高4位" 两写拼成 8 位值
//           （VRC2a is_vrc2=1: 芯片为 2KB 粒度，映射时值右移1位）
//    - 引脚错位：物理 A0~A3 等线经 reg_mask1/reg_mask2 对齐成统一偏移
//    - IRQ: VRC4a/b/c/d/e/f 具备; VRC2a 无（is_vrc2）
// ============================================================================
typedef struct _VRC24_data_
{
	ines_byte_t    prg_bank[2];   // [0]=$8000 8K 槽, [1]=$A000 8K 槽
	ines_byte_t    reg_cmd;       // $9002/$9003 命令寄存器(bit1 交换固定/切换页)
	ines_byte_t    chr_bank[8];   // 8 x 1KB 页(拼接后的 8 位值)
	ines_byte_t    mirror;        // $9000/$9001 镜像寄存器值
	ines_byte_t    is_vrc2;       // 1 = VRC2a (mapper22)
	ines_byte_t    reg_mask1;     // 地址对齐掩码 → 寄存器偏移 bit0
	ines_byte_t    reg_mask2;     // 地址对齐掩码 → 寄存器偏移 bit1
	VRC_IRQ_state_t irq;
} VRC24_data_t;

#define mapper2VRC24data(m)      ((VRC24_data_t*)((m)->p_data))

static inline void vrc24_fini(ines_mapper_t* p_mapper)
{
	ines_free(p_mapper->p_data);
	p_mapper->p_data = NULL;
}

static inline void vrc24_set_prg(VRC24_data_t* p, ines_host_t* p_host)
{
	ines_word_t last = (ines_word_t)(p_host->prom_8k_num - 1);

	if(p->reg_cmd & 0x02)   // $C000 变可切换页, $8000 变固定页(last-1)
		ines_set_prom_bank_4(p_host, last - 1, p->prg_bank[1], p->prg_bank[0], last);
	else                    // 常规: $8000/$A000 可切换, $C000=last-1, $E000=last
		ines_set_prom_bank_4(p_host, p->prg_bank[0], p->prg_bank[1], last - 1, last);
}

static inline void vrc24_set_chr(VRC24_data_t* p, ines_host_t* p_host)
{
	ines_int_t n;

	for(n = 0; n < 8; n++)
	{
		ines_word_t bn = p->chr_bank[n];
		if(p->is_vrc2)
			bn >>= 1;               // VRC2a: 2KB 粒度
		if(p_host->vrom_1k_num > 0)
			ines_set_vrom_bank_n(p_host, n, bn);
		else
			ines_set_vram_bank_n(p_host, n, bn);
	}
}

static inline void vrc24_reset(ines_mapper_t* p_mapper)
{
	VRC24_data_t* p = mapper2VRC24data(p_mapper);
	ines_host_t*  p_host = mapper2host(p_mapper);
	ines_int_t    n;

	for(n = 0; n < 2; n++)
		p->prg_bank[n] = (ines_byte_t)n;   // 初始线性映射 $8000/$A000
	p->reg_cmd = 0;
	p->mirror = 0;

	vrc_irq_reset(&p->irq);
	p->irq.has_irq = (ines_byte_t)(p->is_vrc2 ? 0 : 1);

	vrc24_set_prg(p, p_host);

	if(p_host->vrom_1k_num > 0)
	{
		for(n = 0; n < 8; n++)
			p->chr_bank[n] = (ines_byte_t)n;
		vrc24_set_chr(p, p_host);
	}
}

static inline void vrc24_writehigh(ines_mapper_t* p_mapper, ines_word_t addr, ines_byte_t val)
{
	VRC24_data_t* p = mapper2VRC24data(p_mapper);
	ines_host_t*  p_host = mapper2host(p_mapper);
	ines_word_t   A;
	ines_word_t   i, nib;

	// 引脚错位对齐：高 4 位保持段选，低 2 位由两根有效地址线决定
	A = (addr & 0xF000)
	    | (ines_word_t)((addr & p->reg_mask2) ? 0x02 : 0x00)
	    | (ines_word_t)((addr & p->reg_mask1) ? 0x01 : 0x00);

	// CHR: $B000~$E003, 每段 2 个页寄存器, 偶/奇写低/高 nibble
	if(A >= 0xB000 && A <= 0xE003)
	{
		i   = ((A >> 1) & 1) | ((A - 0xB000) >> 11);
		nib = (A & 1) << 2;
		p->chr_bank[i] = (ines_byte_t)((p->chr_bank[i] & (0xF0 >> nib)) | ((val & 0x0F) << nib));
		vrc24_set_chr(p, p_host);
		return;
	}

	switch(A & 0xF003)
	{
	case 0x8000: case 0x8001: case 0x8002: case 0x8003: // PRG $8000 8K 槽
		p->prg_bank[0] = val & 0x1F;
		vrc24_set_prg(p, p_host);
		break;
	case 0xA000: case 0xA001: case 0xA002: case 0xA003: // PRG $A000 8K 槽
		p->prg_bank[1] = val & 0x1F;
		vrc24_set_prg(p, p_host);
		break;
	case 0x9000: case 0x9001:                            // 镜像
		if(val != 0xFF)
			p->mirror = val;
		vrc_set_mirror(p_mapper, p->mirror);
		break;
	case 0x9002: case 0x9003:                            // 命令寄存器
		p->reg_cmd = val;
		vrc24_set_prg(p, p_host);
		break;
	case 0xF000: case 0xF001: case 0xF002: case 0xF003: // IRQ
		if(p->irq.has_irq)
		{
			vrc_irq_clear(&p->irq, p_host);
			switch(A & 0x03)
			{
			case 0:                                      // latch 低 4 位
				p->irq.latch = (p->irq.latch & 0xF0) | (val & 0x0F);
				break;
			case 1:                                      // latch 高 4 位
				p->irq.latch = (p->irq.latch & 0x0F) | ((val & 0x0F) << 4);
				break;
			case 2:                                      // 控制：重载+使能+模式
				p->irq.rem = 0;
				p->irq.counter = p->irq.latch;
				p->irq.mode = (ines_byte_t)((val & 0x04) ? 1 : 0);
				p->irq.enabled = (ines_byte_t)((val & 0x02) ? 1 : 0);
				p->irq.delayed = (ines_byte_t)((val & 0x01) ? 1 : 0);
				break;
			case 3:                                      // acknowledge
				p->irq.enabled = p->irq.delayed;
				break;
			}
		}
		break;
	}
}

static inline void vrc24_hsync(ines_mapper_t* p_mapper, ines_int_t scanline)
{
	VRC24_data_t* p = mapper2VRC24data(p_mapper);
	(void)scanline;
	vrc_irq_tick(&p->irq, mapper2host(p_mapper));
}

// ============================================================================
// 4. VRC6 (mapper 24 / 26)
//    - PRG: 16K($8000) + 8K($C000) 可切换, $E000 固定末页
//    - CHR: 8 x 1KB 直写 8 位（无需拼 nibble）
//    - 扩展音: $9000 脉冲1, $A000 脉冲2, $B000 锯齿 (仅捕获寄存器)
//    - mapper24 = VRC6a (A0/A1 直接寻址, 无 WRAM)
//      mapper26 = VRC6b (A0/A1 交换, 带 8K WRAM @$6000)
// ============================================================================
typedef struct _VRC6_data_
{
	ines_byte_t    prg_16k;       // $8000 : $8000-$BFFF 16K 槽
	ines_byte_t    prg_8k;        // $C000 : $C000-$DFFF 8K 槽
	ines_byte_t    chr_bank[8];   // $D000-$E003 : 8 x 1KB 页
	ines_byte_t    mirror;        // $B003 : 镜像
	ines_byte_t    is_vrc6b;      // 1 = mapper26 (VRC6b, A0/A1 交换)
	ines_byte_t    pulse1[3];     // $9000-$9002 脉冲1 扩展音寄存器(仅捕获)
	ines_byte_t    pulse2[3];     // $A000-$A002 脉冲2 扩展音寄存器(仅捕获)
	ines_byte_t    saw[3];        // $B000-$B002 锯齿波 扩展音寄存器(仅捕获)
	VRC_IRQ_state_t irq;
} VRC6_data_t;

#define mapper2VRC6data(m)       ((VRC6_data_t*)((m)->p_data))

static inline void vrc6_fini(ines_mapper_t* p_mapper)
{
	ines_free(p_mapper->p_data);
	p_mapper->p_data = NULL;
}

// mapper26 (VRC6b)：寄存器由 A1/A2 引出，等价于把 A0/A1 交换
static inline ines_word_t vrc6_map_addr(VRC6_data_t* p, ines_word_t addr)
{
	if(p->is_vrc6b)
		addr = (ines_word_t)((addr & 0xFFFC) | ((addr >> 1) & 0x01) | ((addr << 1) & 0x02));
	return addr;
}

static inline void vrc6_set_prg(VRC6_data_t* p, ines_host_t* p_host)
{
	ines_word_t last = (ines_word_t)(p_host->prom_8k_num - 1);
	ines_word_t b16  = (ines_word_t)p->prg_16k << 1;   // 16K 槽 → 2 x 8K 页

	ines_set_prom_bank_4(p_host, b16, b16 + 1, p->prg_8k, last);
}

static inline void vrc6_set_chr(VRC6_data_t* p, ines_host_t* p_host)
{
	ines_int_t n;

	for(n = 0; n < 8; n++)
	{
		if(p_host->vrom_1k_num > 0)
			ines_set_vrom_bank_n(p_host, n, p->chr_bank[n]);
		else
			ines_set_vram_bank_n(p_host, n, p->chr_bank[n]);
	}
}

static inline void vrc6_reset(ines_mapper_t* p_mapper)
{
	VRC6_data_t* p = mapper2VRC6data(p_mapper);
	ines_host_t*  p_host = mapper2host(p_mapper);
	ines_int_t    n;

	p->prg_16k = 0;
	p->prg_8k  = 2;              // $C000 初始页(线性延续 $8000 16K)
	p->mirror  = 0;

	vrc_irq_reset(&p->irq);
	p->irq.has_irq = 1;          // VRC6a/b 均按 FCEUX 提供 IRQ 支持

	vrc6_set_prg(p, p_host);

	if(p_host->vrom_1k_num > 0)
	{
		for(n = 0; n < 8; n++)
			p->chr_bank[n] = (ines_byte_t)n;
		vrc6_set_chr(p, p_host);
	}
}

static inline void vrc6_writehigh(ines_mapper_t* p_mapper, ines_word_t addr, ines_byte_t val)
{
	VRC6_data_t* p = mapper2VRC6data(p_mapper);
	ines_host_t*  p_host = mapper2host(p_mapper);
	ines_word_t   A;

	addr = vrc6_map_addr(p, addr);
	A = addr & 0xF003;

	// 扩展音声道 (仅捕获状态, 混音需 APU 扩展接口)
	if(A >= 0x9000 && A <= 0x9002)
	{
		p->pulse1[A & 0x03] = val;
		return;
	}
	if(A >= 0xA000 && A <= 0xA002)
	{
		p->pulse2[A & 0x03] = val;
		return;
	}
	if(A >= 0xB000 && A <= 0xB002)
	{
		p->saw[A & 0x03] = val;
		return;
	}

	switch(A)
	{
	case 0x8000:                                     // PRG 16K @ $8000
		p->prg_16k = val & 0x0F;
		vrc6_set_prg(p, p_host);
		break;
	case 0xB003:                                     // 镜像 (值>>2)
		p->mirror = (ines_byte_t)((val >> 2) & 0x03);
		vrc_set_mirror(p_mapper, p->mirror);
		break;
	case 0xC000:                                     // PRG 8K @ $C000
		p->prg_8k = val & 0x1F;
		vrc6_set_prg(p, p_host);
		break;
	case 0xD000: case 0xD001: case 0xD002: case 0xD003:
		p->chr_bank[A & 0x03] = val;                 // CHR 0-3
		vrc6_set_chr(p, p_host);
		break;
	case 0xE000: case 0xE001: case 0xE002: case 0xE003:
		p->chr_bank[4 + (A & 0x03)] = val;           // CHR 4-7
		vrc6_set_chr(p, p_host);
		break;
	case 0xF000:                                     // IRQ latch
		vrc_irq_clear(&p->irq, p_host);
		p->irq.latch = val;
		break;
	case 0xF001:                                     // IRQ 控制
		vrc_irq_clear(&p->irq, p_host);
		p->irq.mode = (ines_byte_t)((val & 0x04) ? 1 : 0);
		p->irq.enabled = (ines_byte_t)((val & 0x02) ? 1 : 0);
		p->irq.delayed = (ines_byte_t)((val & 0x01) ? 1 : 0);
		if(val & 0x02)
		{
			p->irq.rem = 0;
			p->irq.counter = p->irq.latch;
		}
		break;
	case 0xF002:                                     // IRQ acknowledge
		vrc_irq_clear(&p->irq, p_host);
		p->irq.enabled = p->irq.delayed;
		break;
	}
}

static inline void vrc6_hsync(ines_mapper_t* p_mapper, ines_int_t scanline)
{
	VRC6_data_t* p = mapper2VRC6data(p_mapper);
	(void)scanline;
	vrc_irq_tick(&p->irq, mapper2host(p_mapper));
}

// ============================================================================
// 5. VRC7 (mapper 85)
//    - PRG: 3 x 8K 可切换($8000/$A000/$C000) + $E000 固定末页
//    - CHR: 8 x 1KB 直写
//    - FM: $9010 索引锁存, $9030 数据写 (YM2413 未实现, 捕获寄存器镜像)
//    - IRQ latch 在 $E010, 控制 $F000, acknowledge $F010
//    - WRAM 8K @$6000 (host 默认 SRAM, battery 依 iNES 头)
// ============================================================================
typedef struct _VRC7_data_
{
	ines_byte_t    prg_bank[3];   // $8000/$A000/$C000 三 8K 槽
	ines_byte_t    chr_bank[8];   // 8 x 1KB 页
	ines_byte_t    mirror;        // $E000
	ines_byte_t    fm_index;      // $9010 YM2413 地址锁存
	ines_byte_t    fm_reg[0x40];  // $9030 YM2413 寄存器镜像(仅捕获)
	VRC_IRQ_state_t irq;
} VRC7_data_t;

#define mapper2VRC7data(m)       ((VRC7_data_t*)((m)->p_data))

static inline void vrc7_fini(ines_mapper_t* p_mapper)
{
	ines_free(p_mapper->p_data);
	p_mapper->p_data = NULL;
}

static inline void vrc7_set_prg(VRC7_data_t* p, ines_host_t* p_host)
{
	ines_word_t last = (ines_word_t)(p_host->prom_8k_num - 1);

	ines_set_prom_bank_4(p_host, p->prg_bank[0], p->prg_bank[1], p->prg_bank[2], last);
}

static inline void vrc7_set_chr(VRC7_data_t* p, ines_host_t* p_host)
{
	ines_int_t n;

	for(n = 0; n < 8; n++)
	{
		if(p_host->vrom_1k_num > 0)
			ines_set_vrom_bank_n(p_host, n, p->chr_bank[n]);
		else
			ines_set_vram_bank_n(p_host, n, p->chr_bank[n]);
	}
}

static inline void vrc7_reset(ines_mapper_t* p_mapper)
{
	VRC7_data_t* p = mapper2VRC7data(p_mapper);
	ines_host_t*  p_host = mapper2host(p_mapper);
	ines_int_t    n;

	for(n = 0; n < 3; n++)
		p->prg_bank[n] = (ines_byte_t)n;
	p->mirror  = 0;
	p->fm_index = 0;

	vrc_irq_reset(&p->irq);
	p->irq.has_irq = 1;

	vrc7_set_prg(p, p_host);

	if(p_host->vrom_1k_num > 0)
	{
		for(n = 0; n < 8; n++)
			p->chr_bank[n] = (ines_byte_t)n;
		vrc7_set_chr(p, p_host);
	}
}

static inline void vrc7_writehigh(ines_mapper_t* p_mapper, ines_word_t addr, ines_byte_t val)
{
	VRC7_data_t* p = mapper2VRC7data(p_mapper);
	ines_host_t*  p_host = mapper2host(p_mapper);
	ines_word_t   A;

	// 某些盗版 2-in-1 卡把 A3 接到 A4；标准卡 A3=0 时此变换无效
	A = (ines_word_t)(addr | ((addr & 0x08) << 1));

	// CHR: 0xA000-0xDFFF 区间按 (A4 + A12/A13) 折叠为 8 个页寄存器
	if(A >= 0xA000 && A <= 0xDFFF)
	{
		ines_word_t i = ((A >> 4) & 1) | ((A - 0xA000) >> 11);
		if(i < 8)
		{
			p->chr_bank[i] = val;
			vrc7_set_chr(p, p_host);
		}
		return;
	}

	// FM 数据写 (YM2413 未集成, 捕获寄存器镜像)
	if(A == 0x9030)
	{
		p->fm_reg[p->fm_index & 0x3F] = val;
		return;
	}

	switch(A & 0xF010)
	{
	case 0x8000:                                     // PRG $8000-$9FFF
		p->prg_bank[0] = val & 0x3F;
		vrc7_set_prg(p, p_host);
		break;
	case 0x8010:                                     // PRG $A000-$BFFF
		p->prg_bank[1] = val & 0x3F;
		vrc7_set_prg(p, p_host);
		break;
	case 0x9000:                                     // PRG $C000-$DFFF
		p->prg_bank[2] = val & 0x3F;
		vrc7_set_prg(p, p_host);
		break;
	case 0x9010:                                     // FM 地址锁存
		p->fm_index = val & 0x3F;
		break;
	case 0xE000:                                     // 镜像
		p->mirror = val;
		vrc_set_mirror(p_mapper, p->mirror);
		break;
	case 0xE010:                                     // IRQ latch
		vrc_irq_clear(&p->irq, p_host);
		p->irq.latch = val;
		break;
	case 0xF000:                                     // IRQ 控制
		vrc_irq_clear(&p->irq, p_host);
		p->irq.mode = (ines_byte_t)((val & 0x04) ? 1 : 0);
		p->irq.enabled = (ines_byte_t)((val & 0x02) ? 1 : 0);
		p->irq.delayed = (ines_byte_t)((val & 0x01) ? 1 : 0);
		if(val & 0x02)
		{
			p->irq.rem = 0;
			p->irq.counter = p->irq.latch;
		}
		break;
	case 0xF010:                                     // IRQ acknowledge
		vrc_irq_clear(&p->irq, p_host);
		p->irq.enabled = p->irq.delayed;
		break;
	}
}

static inline void vrc7_hsync(ines_mapper_t* p_mapper, ines_int_t scanline)
{
	VRC7_data_t* p = mapper2VRC7data(p_mapper);
	(void)scanline;
	vrc_irq_tick(&p->irq, mapper2host(p_mapper));
}

#endif // __INES_MAPPER_VRC_H__
