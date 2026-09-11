
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
#include <math.h>
#include "../nes.h"
#include "../mapper.h"
#include "../apu.h"   // APU 扩展音源输入槽 (ines_apu_exp_t / attach API)

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
	ines_byte_t    pulse1[3];     // $9000-$9002 脉冲1 扩展音寄存器
	ines_byte_t    pulse2[3];     // $A000-$A002 脉冲2 扩展音寄存器
	ines_byte_t    saw[3];        // $B000-$B002 锯齿波 扩展音寄存器

	// VRC6 扩展音源引擎状态(见 §4b; 随 p_data 存档 blob 自动保存)
	ines_int_t     sq_reload[2];  // 方波相位步进周期(CPU 周期) = freq+1
	ines_int_t     sq_delay[2];   // 距下次相位步进剩余 CPU 周期(E=0 冻结)
	ines_int_t     sq_phase[2];   // 相位格 0..15
	ines_int_t     saw_reload;    // 锯齿累加周期 = 2*(freq+1)
	ines_int_t     saw_delay;     // 距下次累加剩余 CPU 周期(E=0 冻结)
	ines_int_t     saw_phase;     // 锯齿相位累加器(0~2047)
	ines_int_t     saw_step;      // 累加计数 0..6, 到 7 后相位清零
	VRC_IRQ_state_t irq;
} VRC6_data_t;

#define mapper2VRC6data(m)       ((VRC6_data_t*)((m)->p_data))

// ============================================================================
// 4b. VRC6 扩展音源引擎 (3 声道 PSG)
//    语义取自 FCEUX VRC6 实现(DoSQV/DoSawV), 转成 CPU 周期精确计数器:
//      方波: 相位共 16 格, 每 (freq+1) CPU 周期步进一格;
//            输出 = (相位 > thresh) ? vol 幅值 : 0;
//            寄存器 $X000 bit7=1 时"DAC 模式", 恒输出 vol 幅值(相位冻结);
//            $X000: bit0-3 音量, bit4-6 占空门限(0-7), bit7 DAC;
//            $X002 bit0-3 频率高位, bit7 E 使能(E=0 冻结并静音);
//      锯齿: 每 2*(freq+1) CPU 周期累加一次 (vol&0x3f), 累加 7 次后相位清零;
//            幅值 = ((phase>>3)&0x1F) 5bit 电平; $B002 bit7=E。
//    幅值常量是经验标定基准(vol15 ≈ 30000/32767), 整片响度由 exp.gain 校准。
// ============================================================================
#define VRC6_SQ_AMP_UNIT      2000    // 方波 vol=1 幅值 (vol=15 -> 30000)
#define VRC6_SAW_AMP_UNIT     500     // 锯齿每 5bit 电平幅值 (31 -> 15500)
#define VRC6_EXP_GAIN         0.45f   // 整片混音增益经验初值(待实录 A/B 校准)

static inline ines_int_t vrc6_sq_reload_of(const ines_byte_t* reg)
{
	return ((ines_int_t)(reg[1]) | ((ines_int_t)(reg[2] & 0x0F) << 8)) + 1;
}

static inline ines_int_t vrc6_saw_reload_of(const ines_byte_t* reg)
{
	return ((((ines_int_t)(reg[1]) | ((ines_int_t)(reg[2] & 0x0F) << 8)) + 1) << 1);
}

// 把 [from,to) 的每个输出样本填为 level(裁剪到缓冲上界)
static inline void vrc6_fill_level(ines_apu_exp_t* exp, ines_int_t ch,
                                   ines_int_t from, ines_int_t to, ines_int_t level)
{
	ines_int_t o = ines_cpu_cycles_to_samples(from);
	ines_int_t b = ines_cpu_cycles_to_samples(to);
	if(b > MAX_SAMPLE_PER_FRAME) b = MAX_SAMPLE_PER_FRAME;
	while(o < b)
		exp->buffer[ch][o++] = level;
}

// 方波声道(ch=0/1)。返回时把本次跨段余量/相位写回引擎状态。
static inline void vrc6_square_fill(ines_apu_exp_t* exp, ines_int_t ch,
                                    const ines_byte_t* reg, ines_int_t to)
{
	VRC6_data_t* p = (VRC6_data_t*)exp->p_chip;
	ines_int_t c = exp->cursor;
	ines_int_t delay, ph, reload, vol, thresh;
	ines_int_t amp;

	if(!(reg[2] & 0x80))                    // E=0: 冻结并静音
	{
		vrc6_fill_level(exp, ch, c, to, 0);
		return;
	}

	vol    = reg[0] & 0x0F;
	thresh = (reg[0] >> 4) & 0x07;
	reload = vrc6_sq_reload_of(reg);
	delay  = p->sq_delay[ch];
	ph     = p->sq_phase[ch];
	p->sq_reload[ch] = reload;
	if(delay < 0) delay = 0;

	if(reg[0] & 0x80)                       // DAC 模式: 恒高电平(相位冻结)
	{
		vrc6_fill_level(exp, ch, c, to, vol * VRC6_SQ_AMP_UNIT);
		return;
	}

	amp = (ph > thresh) ? vol * VRC6_SQ_AMP_UNIT : 0;
	while(c < to)
	{
		ines_int_t nxt;
		if(delay == 0)                      // 相位步进点落在当前段首
		{
			ph = (ph + 1) & 0x0F;
			delay = reload;
			amp = (ph > thresh) ? vol * VRC6_SQ_AMP_UNIT : 0;
			continue;
		}
		nxt = c + delay;
		if(nxt > to)                        // 区间延续到段末, 保留余量
		{
			delay -= (to - c);
			vrc6_fill_level(exp, ch, c, to, amp);
			c = to;
			break;
		}
		vrc6_fill_level(exp, ch, c, nxt, amp);
		c = nxt;
		delay = 0;
	}
	p->sq_delay[ch] = delay;
	p->sq_phase[ch] = ph;
}

// 锯齿声道(ch=2)
static inline void vrc6_saw_fill(ines_apu_exp_t* exp, ines_int_t to)
{
	VRC6_data_t* p = (VRC6_data_t*)exp->p_chip;
	ines_int_t c = exp->cursor;
	ines_int_t delay, phase, step, reload, vol, amp;
	const ines_byte_t* reg = p->saw;

	if(!(reg[2] & 0x80))                    // E=0: 冻结并静音
	{
		vrc6_fill_level(exp, 2, c, to, 0);
		return;
	}

	vol    = reg[0] & 0x3F;
	reload = vrc6_saw_reload_of(reg);
	delay  = p->saw_delay;
	phase  = p->saw_phase;
	step   = p->saw_step;
	p->saw_reload = reload;
	if(delay < 0) delay = 0;

	amp = ((phase >> 3) & 0x1F) * VRC6_SAW_AMP_UNIT;
	while(c < to)
	{
		ines_int_t nxt;
		if(delay == 0)                      // 累加点落在当前段首
		{
			delay = reload;
			phase += vol;
			step++;
			if(step >= 7)                   // 7 次累加后相位清零(斜坡回落)
			{
				step = 0;
				phase = 0;
			}
			amp = ((phase >> 3) & 0x1F) * VRC6_SAW_AMP_UNIT;
			continue;
		}
		nxt = c + delay;
		if(nxt > to)                        // 区间延续到段末, 保留余量
		{
			delay -= (to - c);
			vrc6_fill_level(exp, 2, c, to, amp);
			c = to;
			break;
		}
		vrc6_fill_level(exp, 2, c, nxt, amp);
		c = nxt;
		delay = 0;
	}
	p->saw_delay = delay;
	p->saw_phase = phase;
	p->saw_step  = step;
}

static inline void vrc6_audio_run(ines_apu_exp_t* p_exp, ines_int_t to)
{
	VRC6_data_t* p = (VRC6_data_t*)(p_exp ? p_exp->p_chip : NULL);
	if(!p || to <= p_exp->cursor) return;
	vrc6_square_fill(p_exp, 0, p->pulse1, to);
	vrc6_square_fill(p_exp, 1, p->pulse2, to);
	vrc6_saw_fill(p_exp, to);
}

static inline void vrc6_audio_reset(ines_apu_exp_t* p_exp)
{
	VRC6_data_t* p = (VRC6_data_t*)(p_exp ? p_exp->p_chip : NULL);
	if(!p) return;
	p->sq_delay[0] = p->sq_delay[1] = 0;
	p->sq_phase[0] = p->sq_phase[1] = 0;
	p->saw_delay = 0;
	p->saw_phase = 0;
	p->saw_step  = 0;
}

// 把引擎挂到 APU 扩展音源槽
static inline void vrc6_exp_attach(ines_mapper_t* p_mapper)
{
	VRC6_data_t* p = mapper2VRC6data(p_mapper);
	ines_host_t*  p_host = mapper2host(p_mapper);
	ines_apu_exp_attach(&p_host->apu, p, 3, VRC6_EXP_GAIN, vrc6_audio_run, vrc6_audio_reset);
}

// mapper 扩展音寄存器写: 先把 2A03+扩展引擎推进到当前 CPU 周期, 使变更精确落在
// 写入时刻; 频率寄存器写时重载分频计时, 新音高即时生效。
static inline void vrc6_audio_reg_write(ines_mapper_t* p_mapper, ines_int_t sel,
                                        ines_int_t idx, ines_byte_t val)
{
	VRC6_data_t* p = mapper2VRC6data(p_mapper);
	ines_host_t*  p_host = mapper2host(p_mapper);

	ines_apu_flush_run(&p_host->apu);

	if(sel == 2)
	{
		p->saw[idx] = val;
		if(idx == 1 || idx == 2)            // 频率低/高字节写: 重载计时
			p->saw_delay = vrc6_saw_reload_of(p->saw);
		return;
	}
	if(sel == 0)
		p->pulse1[idx] = val;
	else
		p->pulse2[idx] = val;
	if(idx == 1 || idx == 2)
		p->sq_delay[sel] = vrc6_sq_reload_of((sel == 0) ? p->pulse1 : p->pulse2);
}

static inline void vrc6_fini(ines_mapper_t* p_mapper)
{
	ines_host_t* p_host = mapper2host(p_mapper);
	ines_apu_exp_detach(&p_host->apu, p_mapper->p_data);   // 解除 APU 扩展槽, 防悬垂
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

	// 引擎复位并挂到 APU 扩展输入槽(复位时机在 apu_reset 之前, 见 nes.c)
	vrc6_exp_attach(p_mapper);
}

static inline void vrc6_writehigh(ines_mapper_t* p_mapper, ines_word_t addr, ines_byte_t val)
{
	VRC6_data_t* p = mapper2VRC6data(p_mapper);
	ines_host_t*  p_host = mapper2host(p_mapper);
	ines_word_t   A;

	addr = vrc6_map_addr(p, addr);
	A = addr & 0xF003;

	// 扩展音声道 (VRC6 引擎, 经 APU 扩展输入槽混音)
	if(A >= 0x9000 && A <= 0x9002)       // 脉冲1 $9000(音量/占空/DAC) $9001 $9002(E)
	{
		vrc6_audio_reg_write(p_mapper, 0, A & 0x03, val);
		return;
	}
	if(A >= 0xA000 && A <= 0xA002)       // 脉冲2 $A000-$A002
	{
		vrc6_audio_reg_write(p_mapper, 1, A & 0x03, val);
		return;
	}
	if(A >= 0xB000 && A <= 0xB002)       // 锯齿 $B000(音量) $B001 $B002(E)
	{
		vrc6_audio_reg_write(p_mapper, 2, A & 0x03, val);
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
//    - FM: $9010 索引锁存, $9030 数据写; YM2413 简化内核(§5b)经 APU 扩展输入槽发声
//    - IRQ latch 在 $E010, 控制 $F000, acknowledge $F010
//    - WRAM 8K @$6000 (host 默认 SRAM, battery 依 iNES 头)
// ============================================================================
typedef struct _VRC7_data_
{
	ines_byte_t    prg_bank[3];   // $8000/$A000/$C000 三 8K 槽
	ines_byte_t    chr_bank[8];   // 8 x 1KB 页
	ines_byte_t    mirror;        // $E000
	ines_byte_t    fm_index;      // $9010 YM2413 地址锁存
	ines_byte_t    fm_reg[0x40];  // $9030 YM2413 寄存器镜像(引擎即时解析)

	// ---- YM2413 简化内核状态(见 §5b; 随 p_data 存档 blob 自动保存) ----
	ines_dword_t  ym_phase[18];  // 各槽相位累加器(19bit, 每 FM 更新步进)
	ines_int_t     ym_eg[18];     // 包络衰减量 0..127 (0.375dB/格; 127≈静音)
	ines_byte_t    ym_st[18];     // 包络状态: 0攻击 1衰减 2持续 3释放 4停
	double         ym_out[18];    // 槽当前线性输出(±约2000, 含包络)
	double         ym_oprev[18];  // 槽上一拍输出(调制器反馈用)
	ines_byte_t    ym_key[9];     // 通道键状态(上沿=键开, 下沿=键关)
	ines_int_t     ym_rem;        // 距下次 FM 更新剩余 CPU 周期(<=0 立即更新)
	ines_int_t     ym_count;      // 包络全局计数器
	ines_int_t     ym_level;      // 本段输出电平(写 exp.buffer 用)
	ines_byte_t    ym_patch[16][8]; // 音色 8 字节 dump(0=用户, 1..15=内置)
	int            ym_ready;      // 惰性查表初始化标志(不复位, 见 §5b)
	VRC_IRQ_state_t irq;
} VRC7_data_t;

#define mapper2VRC7data(m)       ((VRC7_data_t*)((m)->p_data))

// ============================================================================
// 5b. VRC7 YM2413 简化 FM 内核 (2-op × 9 旋律声道)
//    - 时钟: YM2413 主频 3.579545MHz = 2*CPU 主频, 每次 FM 更新间隔 72 主时钟
//      (=36 CPU 周期, 即 49716Hz 槽周期); 相位/包络按该周期演进。
//      寄存器写前经 ines_apu_flush_run 对齐到精确 CPU 周期。
//    - 相位: 19bit 累加器, 每更新步进 (((fnum*2)*ML)<<blk)>>2 (fnum 9bit,
//      blk 3bit, ML 乘数 0..15), 与真实芯片/emu2413 公式一致。
//    - FM: 调制器线性输出(满幅约 ±2000)作为载波相位索引偏移; FB 自反馈
//      (out1+out0)>>(9-fb) 照抄 emu2413。
//    - 包络: 简化 OPLL EG, 0.375dB/格衰减, 攻击/衰减/持续/释放 + EG 音型位;
//      未实现 KSL 键缩放与 AM/PM(简化核心, 后续可扩展)。
//    - 音色: 15 内建音色(Nuke.YKT VRC7 presets)+ 寄存器 0-7 用户音色。
//    参考 emu2413 (Mitsutaka Okazaki, MIT) 寄存器布局与时钟模型; 响度/深度待实录 A/B。
// ============================================================================
#define VRC7_FM_STEP_CPU   36      // FM 更新间隔 = 72 OPLL 主时钟 / 2
#define VRC7_OP_LIN        2000.0  // 运算器满幅线性值(量纲同 emu to_linear)
#define VRC7_EG_MUTE       127     // 包络静音刻度
#define VRC7_PG_MASK       0x7FFFF
#define VRC7_EXP_GAIN      0.85f   // 整片混音增益经验初值(待 A/B 校准)

static const ines_byte_t vrc7_default_patch[16][8] =
{
	{0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00}, // 0 用户(寄存器 $00-$07)
	{0x03,0x21,0x05,0x06,0xe8,0x81,0x42,0x27}, // 1 Violin
	{0x13,0x41,0x14,0x0d,0xd8,0xf6,0x23,0x12}, // 2 Guitar
	{0x11,0x11,0x08,0x08,0xfa,0xb2,0x20,0x12}, // 3 Piano
	{0x31,0x61,0x0c,0x07,0xa8,0x64,0x61,0x27}, // 4 Flute
	{0x32,0x21,0x1e,0x06,0xe1,0x76,0x01,0x28}, // 5 Clarinet
	{0x02,0x01,0x06,0x00,0xa3,0xe2,0xf4,0xf4}, // 6 Oboe
	{0x21,0x61,0x1d,0x07,0x82,0x81,0x11,0x07}, // 7 Trumpet
	{0x23,0x21,0x22,0x17,0xa2,0x72,0x01,0x17}, // 8 Organ
	{0x35,0x11,0x25,0x00,0x40,0x73,0x72,0x01}, // 9 Horn
	{0xb5,0x01,0x0f,0x0f,0xa8,0xa5,0x51,0x02}, // A Synth Lead
	{0x17,0xc1,0x24,0x07,0xf8,0xf8,0x22,0x12}, // B Harpsichord
	{0x71,0x23,0x11,0x06,0x65,0x74,0x18,0x16}, // C Vibraphone
	{0x01,0x02,0xd3,0x05,0xc9,0x95,0x03,0x02}, // D Synth Bass
	{0x61,0x63,0x0c,0x00,0x94,0xc0,0x33,0xf6}, // E Acoustic Bass
	{0x21,0x72,0x0d,0x00,0xc1,0xd5,0x56,0x06}, // F Electric Guitar
};

static int    vrc7_sin_ready = 0;
static double vrc7_sin_tab[1024];
static double vrc7_lin_tab[256];   // 10^(-n*0.375/20): 衰减格 -> 线性系数

// OPLL 频率倍率表(emu2413 ml_table; 乘进相位公式的最终整数系数)
static const ines_int_t vrc7_ml_table[16] =
	{1,2,4,6,8,10,12,14,16,18,20,20,24,24,30,30};

static inline void vrc7_ensure_tables(void)
{
	int i;
	if(vrc7_sin_ready) return;
	for(i = 0; i < 1024; i++)
		vrc7_sin_tab[i] = sin(6.283185307179586477 * i / 1024.0);
	for(i = 0; i < 256; i++)
		vrc7_lin_tab[i] = pow(10.0, -i * 0.375 / 20.0);
	vrc7_sin_ready = 1;
}

// 槽包络演进(每 FM 更新一次)。eg/st 读写引擎状态。
static inline void vrc7_op_env(VRC7_data_t* p, int s, int keyed, int st,
                               int ar, int dr, int sl, int rr, int egf, int sus)
{
	int evolve = 0;
	int atk    = 0;
	int rate   = 0;
	int shift, mask;

	if(keyed)
	{
		if(st == 0){ evolve = 1; atk = 1; rate = ar; }
		else if(st == 1){ evolve = 1; rate = dr; }
		else if(st == 2){ if(!egf) { evolve = 1; rate = rr; } }  // EG=1 持续保持
		else if(st == 3){ evolve = 1; rate = sus ? 5 : (egf ? rr : 7); }
	}
	else if(st == 3)                    // 载波键关释放
		evolve = 1, rate = sus ? 5 : (egf ? rr : 7);

	if(!evolve || rate <= 0)
		return;

	shift = (rate < 13) ? (13 - rate) : 0;
	mask  = (1 << shift) - 1;
	if(shift > 0 && (p->ym_count & mask) != 0)
		return;

	if(atk)
	{
		if(p->ym_eg[s] > 0)
		{
			int d = p->ym_eg[s] >> 2;
			if(d < 1) d = 1;
			p->ym_eg[s] -= d;
			if(p->ym_eg[s] <= 0)
			{
				p->ym_eg[s] = 0;
				p->ym_st[s] = 1;            // 攻击结束 -> 衰减
			}
		}
	}
	else
	{
		if(p->ym_eg[s] < VRC7_EG_MUTE)
		{
			p->ym_eg[s] += 1;
			if(p->ym_st[s] == 1 && (p->ym_eg[s] >> 3) >= sl)
				p->ym_st[s] = 2;            // 达持续电平
		}
	}
}

// 计算单槽当前值(调制器先于载波调用)。返回线性输出。
static inline double vrc7_op_run(VRC7_data_t* p, int s, int isc, int keyed,
                                 int fnum, int blk, int ml, int tl,
                                 int ar, int dr, int sl, int rr,
                                 int egf, int ws, int fb, int sus, double fm_in)
{
	int   st = p->ym_st[s];
	ines_dword_t inc;
	int   idx, iadd = 0;
	double amp, out;

	// 调制器键关后冻结(相位/包络停走; 载波释放期间保持既有调制)
	if(!isc && !keyed)
		return p->ym_out[s];

	inc = ((((ines_dword_t)(fnum << 1)) * (ines_dword_t)ml) << blk) >> 2;
	p->ym_phase[s] = (p->ym_phase[s] + inc) & VRC7_PG_MASK;

	vrc7_op_env(p, s, keyed, st, ar, dr, sl, rr, egf, sus);

	amp = 0.0;
	if(p->ym_eg[s] < VRC7_EG_MUTE)
	{
		int a = p->ym_eg[s] + tl;
		if(a > VRC7_EG_MUTE) a = VRC7_EG_MUTE;
		amp = vrc7_lin_tab[a];
	}
	if(amp < 0.0001)
	{
		p->ym_oprev[s] = p->ym_out[s];
		p->ym_out[s] = 0.0;
		return 0.0;
	}

	idx = (int)((p->ym_phase[s] >> 9) & 1023);
	if(fb > 0)                          // 调制器自反馈
		iadd += (int)(p->ym_out[s] + p->ym_oprev[s]) >> (9 - fb);
	if(isc)
		iadd += (int)fm_in;             // 调制器输出 -> 载波相位索引

	idx = (idx + iadd) & 1023;
	if(ws)                              // 半波: 仅正半周有声
		out = (idx >= 512) ? 0.0 : vrc7_sin_tab[idx];
	else
		out = vrc7_sin_tab[idx];

	out *= amp * VRC7_OP_LIN;
	p->ym_oprev[s] = p->ym_out[s];
	p->ym_out[s]   = out;
	return out;
}

// 一次 FM 更新: 9 声道(键开/关边沿处理 + M/C 计算), 输出写 ym_level
static inline void vrc7_fm_update(ines_apu_exp_t* exp)
{
	VRC7_data_t* p = (VRC7_data_t*)exp->p_chip;
	const ines_byte_t* r = p->fm_reg;
	double mix = 0.0;
	int ch;

	p->ym_count++;

	for(ch = 0; ch < 9; ch++)
	{
		int sM = ch << 1;
		int sC = sM | 1;
		int keyed = (r[0x20 + ch] & 0x10) ? 1 : 0;
		int sus   = (r[0x20 + ch] & 0x20) ? 1 : 0;
		int fnum  = (int)(r[0x10 + ch] | ((r[0x20 + ch] & 1) << 8));
		int blk   = (r[0x20 + ch] >> 1) & 7;
		int pno   = (r[0x30 + ch] >> 4) & 15;
		int chvol = (r[0x30 + ch] & 15) << 3;    // 音量 v*3dB -> 0.375dB 格
		const ines_byte_t* pb;
		double mout, cout;

		if(!p->ym_key[ch] && keyed)     // 键开: 双槽重触发
		{
			p->ym_st[sM] = 0; p->ym_st[sC] = 0;
			p->ym_eg[sM] = VRC7_EG_MUTE; p->ym_eg[sC] = VRC7_EG_MUTE;
			p->ym_out[sM] = 0; p->ym_out[sC] = 0;
			p->ym_oprev[sM] = 0; p->ym_oprev[sC] = 0;
		}
		else if(p->ym_key[ch] && !keyed)         // 键关: 载波释放, 调制器冻结
			p->ym_st[sC] = 3;

		p->ym_key[ch] = (ines_byte_t)keyed;
		pb = p->ym_patch[pno];

		// 调制器 M: dump 偶字节 0/2/4/6 (ml 查倍率表, fb 取字节3低3位)
		mout = vrc7_op_run(p, sM, 0, keyed, fnum, blk,
			vrc7_ml_table[pb[0] & 15], (pb[2] & 63) << 1,
			pb[4] >> 4, pb[4] & 15, pb[6] >> 4, pb[6] & 15,
			(pb[0] >> 5) & 1, (pb[3] >> 3) & 1, pb[3] & 7, 0, 0.0);
		// 载波 C: dump 奇字节 1/3/5/7, tl=声道音量, 输入调制器输出
		cout = vrc7_op_run(p, sC, 1, keyed, fnum, blk,
			vrc7_ml_table[pb[1] & 15], chvol,
			pb[5] >> 4, pb[5] & 15, pb[7] >> 4, pb[7] & 15,
			(pb[1] >> 5) & 1, (pb[3] >> 4) & 1, 0, sus, mout);

		mix += cout;
	}

	if(mix >  32000.0) mix =  32000.0;
	else if(mix < -32000.0) mix = -32000.0;
	p->ym_level = (ines_int_t)mix;
}

// exp.run: 推进到帧内相对 CPU 周期 to(每 36 CPU 周期一次 FM 更新)
static inline void vrc7_audio_run(ines_apu_exp_t* p_exp, ines_int_t to)
{
	VRC7_data_t* p = (VRC7_data_t*)(p_exp ? p_exp->p_chip : NULL);
	ines_int_t c;
	if(!p || to <= p_exp->cursor) return;
	c = p_exp->cursor;
	while(c < to)
	{
		ines_int_t n;
		if(p->ym_rem <= 0)
		{
			p->ym_rem = VRC7_FM_STEP_CPU;
			vrc7_fm_update(p_exp);
		}
		n = (p->ym_rem < (to - c)) ? p->ym_rem : (to - c);
		vrc6_fill_level(p_exp, 0, c, c + n, p->ym_level);
		c += n;
		p->ym_rem -= n;
	}
}

// exp.reset(复位/重挂时): 清引擎并装载内建音色
static inline void vrc7_audio_reset(ines_apu_exp_t* p_exp)
{
	VRC7_data_t* p = (VRC7_data_t*)(p_exp ? p_exp->p_chip : NULL);
	int i, j;
	if(!p) return;
	vrc7_ensure_tables();
	for(i = 0; i < 18; i++)
	{
		p->ym_phase[i] = 0;
		p->ym_eg[i]    = VRC7_EG_MUTE;
		p->ym_st[i]    = 3;              // 释放/静音
		p->ym_out[i]   = 0.0;
		p->ym_oprev[i] = 0.0;
	}
	for(i = 0; i < 9; i++)
		p->ym_key[i] = 0;
	p->ym_rem   = 0;                     // 首个 run 立即更新
	p->ym_count = 0;
	p->ym_level = 0;
	for(i = 0; i < 16; i++)
		for(j = 0; j < 8; j++)
			p->ym_patch[i][j] = vrc7_default_patch[i][j];
}

// 挂到 APU 扩展输入槽(单声道 FM 混音)
static inline void vrc7_exp_attach(ines_mapper_t* p_mapper)
{
	VRC7_data_t* p = mapper2VRC7data(p_mapper);
	ines_host_t*  p_host = mapper2host(p_mapper);
	vrc7_ensure_tables();
	ines_apu_exp_attach(&p_host->apu, p, 1, VRC7_EXP_GAIN, vrc7_audio_run, vrc7_audio_reset);
}

// FM 寄存器写: 先 flush 对齐到写入周期, 再写镜像; 用户音色字节同步到音色槽0
static inline void vrc7_audio_reg_write(ines_mapper_t* p_mapper, ines_int_t idx, ines_byte_t val)
{
	VRC7_data_t* p = mapper2VRC7data(p_mapper);
	ines_host_t*  p_host = mapper2host(p_mapper);

	ines_apu_flush_run(&p_host->apu);

	// YM2413 地址每 9 个寄存器镜像一次(与 emu2413 一致)
	if((0x19 <= idx && idx <= 0x1f) || (0x29 <= idx && idx <= 0x2f) ||
	   (0x39 <= idx && idx <= 0x3f))
		idx -= 9;

	p->fm_reg[idx & 0x3f] = val;
	if(idx < 8)
		p->ym_patch[0][idx] = val;       // 用户音色 $00-$07
}

static inline void vrc7_fini(ines_mapper_t* p_mapper)
{
	ines_host_t* p_host = mapper2host(p_mapper);
	ines_apu_exp_detach(&p_host->apu, p_mapper->p_data);   // 解除 APU 扩展槽, 防悬垂
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
	for(n = 0; n < 0x40; n++)          // FM 寄存器清零(软复位静音)
		p->fm_reg[n] = 0;

	vrc_irq_reset(&p->irq);
	p->irq.has_irq = 1;

	vrc7_set_prg(p, p_host);

	if(p_host->vrom_1k_num > 0)
	{
		for(n = 0; n < 8; n++)
			p->chr_bank[n] = (ines_byte_t)n;
		vrc7_set_chr(p, p_host);
	}

	// 引擎复位并挂到 APU 扩展输入槽(复位时机在 apu_reset 之前, 见 nes.c)
	vrc7_exp_attach(p_mapper);
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
	if(A == 0x9030)                    // FM 寄存器数据(经引擎写, 对齐写入周期)
	{
		vrc7_audio_reg_write(p_mapper, p->fm_index & 0x3F, val);
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
