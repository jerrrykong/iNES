/**
 * @file    19.c
 * @brief   iNES Mapper 019 - Namco 163(别名 Namcot 106)
 *
 * 硬件要点(依据 NESdev wiki "INES Mapper 019" oldid=24233、"Namco 163 audio" oldid=23403)：
 *
 * CPU 地址空间(19 个寄存器，每个占 $800 字节)：
 *   $4800-$4FFF  内部 128B RAM 数据口(读/写)，地址由 $F800 指定
 *   $5000-$57FF  IRQ 计数器低 8 位(读/写，直接访问计数器本身，非重载值)
 *   $5800-$5FFF  IRQ 计数器高 7 位 + bit7 使能(读/写)
 *   $6000-$7FFF  8KB 外部 WRAM(有电池则掉电保存，2KB×4 窗口写保护由 $F800 控制)
 *   $8000-$DFFF  CHR/NT 选择：12 个 1KB 窗口，依次对应 PPU $0000/$0400/.../$2C00
 *                  页 $00-$DF -> 1KB CHR-ROM 页
 *                  页 $E0-$FF -> 内部 2KB NT RAM 页(偶=A、奇=B)；pattern 区需 $E800.6/.7 = 0，
 *                                NT 区恒可用
 *   $E000-$E7FF  AMPP PPPP：P = $8000-$9FFF 的 8KB PRG 页；M = 1 关闭扩展音；A = pin22(不模拟)
 *   $E800-$EFFF  HLPP PPPP：P = $A000-$BFFF 的 8KB PRG 页；
 *                  H = 1 时 $0000-$0FFF 的 $E0-$FF 页改选 CHR-ROM 的末 $20 页
 *                  L = 1 时 $1000-$1FFF 同理
 *   $F000-$F7FF  CDPP PPPP：P = $C000-$DFFF 的 8KB PRG 页；C/D = pin44 输出(不模拟)
 *   $F800-$FFFF  KKKK DCBA：同时是内部 RAM 地址端口与外部 RAM 写保护寄存器
 *                  KKKK 必须等于 b0100 才允许外部 RAM 写入(否则全部只读)
 *                  DCBA = $6000/$6800/$7000/$7800 四个 2KB 窗口的写保护位(1 = 保护)
 *                  AAAAAAA(b0-6) = 内部 RAM 地址，bit7 = 数据口访问后地址自增(到 $7F 停止)
 *   $E000-$FFFF  最后一个 8KB PRG 页固定映射 $E000-$FFFF
 *
 * IRQ：15 位 CPU 周期上计数器，计到 $7FFF 触发 IRQ 并停止计数；写 $5000 或 $5800 清除 IRQ。
 *
 * 扩展音(Namco 163 audio)：$4800-$4FFF 访问的 128B RAM 里含 8 个波表通道寄存器，
 *   每通道 8 字节，通道 8 = $78、通道 1 = $40(倒序)：
 *     +0 低 8 位频率  +1 低 8 位相位  +2 中 8 位频率  +3 中 8 位相位
 *     +4 高 2 位频率(b0-1) + 波长(b2-7)  +5 高 8 位相位
 *     +6 波形地址(单位 4bit 采样)        +7 音量(b0-3) + 启用通道数(b4-6，仅 $7F 有效)
 *   P1 只把这片 RAM 当作可读写、可电池保存的 RAM；P2 起由本文件的扩展音引擎发声
 *   (每 15 个 CPU 周期更新一个通道，在启用的通道间轮转；Σ(通道输出)/启用通道数 混音)。
 *
 * 已决事项(见 docs/mapper-19-plan.md)：
 *   1. 8KB 外部 WRAM 由本 mapper 自己处理写(2KB 窗口粒度精确保护)，
 *      映射到 SRAM 块 1；bank_writeable[3] 置 NES_BANK_WRITE_PROTECTED 使写落到 writelow，
 *      同时该值可被存档正确还原(mem_bank[3] 指向 SRAM)。
 *   2. 128B 内部 RAM 的电池镜像放 SRAM 块 0 尾部($1F80-$1FFF)，避免与 8KB WRAM 重叠。
 *   3. ROM nametable 已完整支持：NT 窗口($2000-$2FFF)的 $00-$DF 页直接指向 CHR 的 1KB 页
 *      (经 ines_set_nt_chr_bank_n()，只读；纯 CHR-RAM 卡带则指向 pattern RAM)。
 *   4. pin22 / pin44 输出、Namco 129 变体(无游戏使用扩展音)不模拟。
 *   5. 扩展音默认开启(本模拟器不解析 NES 2.0 submapper)：无扩展音的变形(submapper 1/2)
 *      若把 $40-$7F 当普通存档 RAM 写入，会发出噪声，需要时可用 $E000.6 静音。
 *   6. **相位累加器不回写 chip_ram**(硬件上它就存在 $79/$7B/$7D，引擎每次更新都会改写)：
 *      submapper 1/2 的游戏(无扩展音)把内部 128B RAM 整片当存档 RAM 用，若回写会把它们的
 *      存档数据(每通道 +1/+3/+5 共 24 字节)逐帧改坏。故本实现把相位放在私有 chan_phase[]，
 *      并在 CPU 写 $79/$7B/$7D 时同步(程序"手动定位波形"仍有效)；代价是 CPU **读** $79/$7B/$7D
 *      只能拿到自己最后写入的值(硬件会返回实时相位)。若将来解析 NES 2.0 submapper，
 *      可以对 submapper 3/4/5 打开回写以完全对齐硬件。
 */

#include "../../comm/idef.h"
#include "../../comm/log.h"
#include "../nes.h"
#include "../mapper.h"
#include "../apu.h"   // APU 扩展音源输入槽 (ines_apu_exp_t / attach API)


#define N163_CHIP_RAM_SIZE         0x80        // ASIC 内部 RAM 字节数
#define N163_CHIP_ADDR_MASK        0x7f        // $F800 低 7 位为内部 RAM 地址
#define N163_WRAM_BASE             0x6000      // 外部 WRAM 起始
#define N163_WRAM_SIZE             0x2000      // 外部 WRAM 大小
#define N163_WRAM_WINDOW_SIZE      0x0800      // 写保护窗口大小(2KB)
#define N163_WRAM_WINDOW_NUM       4           // 写保护窗口数量
#define N163_CHR_WINDOW_NUM        12          // 12 个 1KB PPU 窗口
#define N163_CHR_CIRAM_PAGE        0xe0        // 页值 >= $E0 表示选内部 NT RAM
#define N163_WRAM_SRAM_BANK        1           // 8KB 外部 WRAM 使用的 SRAM 块号
#define N163_CHIP_RAM_SRAM_OFFSET  0x1f80      // 128B 内部 RAM 电池镜像所在 SRAM 偏移(块 0 尾部)
#define N163_SAVE_THROTTLE_CYCLES   1789773    // 内部 RAM 脏标记节流(约 1 秒 @NTSC CPU 时钟)

/* ---- 扩展音(Namco 163 audio)发声引擎参数 ---- */
#define N163_CH_REG_BASE           0x40        // 通道 1 的寄存器基址(通道 8 = $78)
#define N163_CH_COUNT              8           // 波表通道数
#define N163_CH_REG_SIZE           8           // 每通道 8 字节寄存器
#define N163_CH_VOL_ADDR           0x7f        // 音量 + 启用通道数寄存器(仅 $7F 的 bit4-6 有效)
#define N163_CH_ENABLE_SHIFT       4           // $7F.bit4-6 = 启用通道数 - 1
#define N163_CH_ENABLE_MASK        0x07
#define N163_CH_UPDATE_CYCLES      15          // 芯片每 15 个 CPU 周期更新并输出一个通道
#define N163_AMP_UNIT              133         // 幅值常数：单通道 vol=15 方波峰峰值 ≈ 225*133 ≈ 29900(与 VRC6 同量纲)
#define N163_EXP_GAIN              1.00f       // 整片混音增益：约 +16dB(相对 APU 最响方波)，取实录区间 12.7-19.5dB 中段

/** Namco 163 私有数据 */
typedef struct _Namco163_data_ Namco163_data_t;

struct _Namco163_data_
{
	ines_byte_t  chip_ram[N163_CHIP_RAM_SIZE]; ///< ASIC 内部 128B RAM($4800 数据口)
	ines_byte_t  chip_addr;                    ///< 内部 RAM 地址($F800 低 7 位)
	ines_bool_t  chip_auto_inc;                ///< $F800 bit7：数据口访问后地址自增
	ines_byte_t  chr_bank[N163_CHR_WINDOW_NUM];///< $8000-$DFFF 写入的 12 个 1KB 窗口页值
	ines_byte_t  prg_bank[3];                  ///< $8000/$A000/$C000 三个可切 8KB PRG 槽
	ines_byte_t  wram_protect;                 ///< $F800 低 4 位：外部 RAM 2KB 窗口写保护位(1=保护)
	ines_bool_t  wram_writable;                ///< $F800 高 4 位 == b0100 才允许外部 RAM 写入
	ines_bool_t  chr_ram_off_lo;               ///< $E800.6：1 = $0000-$0FFF 的 $E0-$FF 页用 CHR-ROM
	ines_bool_t  chr_ram_off_hi;               ///< $E800.7：1 = $1000-$1FFF 的 $E0-$FF 页用 CHR-ROM
	ines_bool_t  sound_disabled;               ///< $E000.6：1 = 关闭扩展音(P2 发声引擎使用)
	ines_word_t  irq_counter;                  ///< 15 位 CPU 周期上计数器
	ines_bool_t  irq_enabled;                  ///< $5800.7
	ines_int64_t irq_synced_cycles;            ///< hsync 增量对账基准(CPU 周期)
	ines_int64_t chip_ram_dirty_cycles;        ///< 上次标记内部 RAM 脏的 CPU 周期(节流用)

	/* 扩展音发声引擎(P2) */
	ines_dword_t chan_phase[N163_CH_COUNT];    ///< 各通道 24 位相位累加器(引擎私有，不回写 chip_ram)
	ines_int_t   chan_out[N163_CH_COUNT];      ///< 各通道当前输出 ((采样-8)*音量)，保持到下次更新
	ines_int_t   level;                        ///< 当前整片输出电平(Σ启用通道输出/启用数 × 幅值常数)
	ines_int_t   tick_rem;                     ///< 距下次通道更新的剩余 CPU 周期
	ines_int_t   next_ch;                      ///< 下一个要更新的通道槽 0-7(槽 7 = 通道 8)
};

#define mapper2Namco163data(p)  ((Namco163_data_t*)((p)->p_data))


/**
 * 把 128B 内部 RAM 的改动同步到电池镜像(SRAM 块 0 尾部)。
 * @param p       Namco 163 私有数据
 * @param p_host  宿主
 * @param addr    内部 RAM 地址
 * @param val     写入值
 * @note 只有卡带带电池(rom.has_sram)才持久化；脏标记按约 1 秒节流，
 *       避免扩展音游戏逐帧改写通道寄存器时反复写盘。
 */
static void Namco163_sync_chip_ram_save(Namco163_data_t* p, ines_host_t* p_host, ines_byte_t addr, ines_byte_t val)
{
	ines_int64_t now;

	if(!p_host->rom.has_sram)
		return;

	p_host->SRAM[N163_CHIP_RAM_SRAM_OFFSET + addr] = val;

	if(p_host->SRAM_write_flag == 1)
		return;

	now = p_host->cpu.total_cycles;
	if((now - p->chip_ram_dirty_cycles) < N163_SAVE_THROTTLE_CYCLES && p->chip_ram_dirty_cycles != 0)
		return;

	p->chip_ram_dirty_cycles = now;
	p_host->SRAM_write_flag = 1;
}


/**
 * 判断外部 WRAM 的某地址当前是否允许写入。
 * @param p     Namco 163 私有数据
 * @param addr  $6000-$7FFF 内的地址
 * @return ines_true 可写
 */
static ines_bool_t Namco163_wram_write_enable(Namco163_data_t* p, ines_word_t addr)
{
	ines_int_t win;

	if(!p->wram_writable)
		return ines_false;                       // $40-$4E 之外：外部 RAM 全部只读

	win = (ines_int_t)((addr - N163_WRAM_BASE) / N163_WRAM_WINDOW_SIZE);
	return ((p->wram_protect >> win) & 1) ? ines_false : ines_true;
}


/**
 * 设置一个 1KB PPU 窗口的目标内存。
 * @param p_host   宿主
 * @param n        窗口号 0-7(PPU pattern 区)
 * @param b        窗口页值
 * @param ciram_ok 该窗口是否允许选内部 NT RAM($E800.6/.7 门控)
 * @note 无 CHR-ROM 的卡带(CHR-RAM)退化为使用 pattern RAM，与 wiki "CHR Memory Configurations" 一致。
 */
static void Namco163_set_chr_window(ines_host_t* p_host, ines_int_t n, ines_byte_t b, ines_bool_t ciram_ok)
{
	if(b >= N163_CHR_CIRAM_PAGE && ciram_ok)
	{
		// 内部 2KB NT RAM 当 CHR：偶页值 = A 页、奇页值 = B 页
		ines_set_ciram_pattern_bank_n(p_host, (ines_word_t)n, (ines_word_t)(b & 1));
	}
	else if(p_host->vrom_1k_num > 0)
	{
		ines_set_vrom_bank_n(p_host, (ines_word_t)n, (ines_word_t)b);
	}
	else
	{
		ines_set_vram_bank_n(p_host, (ines_word_t)n, (ines_word_t)(b & 0x1f));
	}
}


/**
 * 依据 12 个窗口页值刷新 PPU 的内存窗口映射。
 * @param p       Namco 163 私有数据
 * @param p_host  宿主
 */
static void Namco163_update_chr(Namco163_data_t* p, ines_host_t* p_host)
{
	ines_int_t  n;
	ines_byte_t nt[4];

	// pattern 区 $0000-$0FFF：$E800.6 = 0 时 $E0-$FF 选内部 NT RAM
	for(n = 0; n < 4; n++)
	{
		Namco163_set_chr_window(p_host, n, p->chr_bank[n], (ines_bool_t)(!p->chr_ram_off_lo));
	}
	// pattern 区 $1000-$1FFF：$E800.7 门控
	for(n = 4; n < 8; n++)
	{
		Namco163_set_chr_window(p_host, n, p->chr_bank[n], (ines_bool_t)(!p->chr_ram_off_hi));
	}
	// nametable 区 $2000-$2FFF：$E0-$FF 恒选内部 NT RAM(与 $E800 门控无关，偶=A/奇=B)；
	// $00-$DF 选 1KB CHR 页(ROM nametable，只读)，即芯片"最多 226 个源 nametable"的来源。
	// 先整组按 CIRAM 刷新(顺带把 4 个窗口的 nt_type 复位)，再把选 ROM/CHR-RAM 页的窗口单独改写。
	for(n = 8; n < N163_CHR_WINDOW_NUM; n++)
	{
		nt[n - 8] = (ines_byte_t)((p->chr_bank[n] >= N163_CHR_CIRAM_PAGE) ? (p->chr_bank[n] & 1) : 0);
	}
	ines_ppu_set_mirror(&p_host->ppu, nt[0], nt[1], nt[2], nt[3]);

	for(n = 8; n < N163_CHR_WINDOW_NUM; n++)
	{
		if(p->chr_bank[n] < N163_CHR_CIRAM_PAGE)
			ines_set_nt_chr_bank_n(p_host, (ines_word_t)(n - 8), (ines_word_t)p->chr_bank[n]);
	}
}


/* ====================================================================
 * 扩展音发声(Namco 163 audio)
 * 芯片每 15 个 CPU 周期更新并输出一个通道，在 "启用的通道" 间轮转
 * ($7F.bit4-6 = C 时启用通道 8 起的 C+1 个)；每个通道的输出保持到它下次被更新。
 * 整片输出 = Σ(启用通道输出) / 启用通道数，再乘以幅值常数。
 * ==================================================================== */

/**
 * 计算整片输出电平。
 * @param p Namco 163 私有数据
 * @return 整片电平(Σ启用通道输出 / 启用通道数 × N163_AMP_UNIT)
 * @note 每次通道更新后重算，通道数变化时天然正确，无需维护增量累加。
 */
static ines_int_t Namco163_calc_level(Namco163_data_t* p)
{
	ines_int_t c = ((ines_int_t)((p->chip_ram[N163_CH_VOL_ADDR] >> N163_CH_ENABLE_SHIFT) & N163_CH_ENABLE_MASK)) + 1;
	ines_int_t i;
	ines_int_t sum = 0;

	for(i = 0; i < c; i++)
	{
		sum += p->chan_out[N163_CH_COUNT - 1 - i];    // 启用的是最高的 c 个槽(通道 8 起)
	}

	return (sum * N163_AMP_UNIT) / c;
}


/**
 * 更新一个波表通道：相位步进 + 取一个 4bit 采样作为输出。
 * @param p    Namco 163 私有数据
 * @param slot 通道槽号 0-7(槽 0 = 通道 1，槽 7 = 通道 8)
 * @note 相位累加器保存在引擎私有的 `chan_phase[]`，**不回写** chip_ram，原因见文件头"已决事项"第 6 条。
 *       采样值减 8 是 ASIC 的直流中心偏置，因此输出有符号。
 */
static void Namco163_update_channel(Namco163_data_t* p, ines_int_t slot)
{
	ines_byte_t* w  = p->chip_ram + N163_CH_REG_BASE + slot * N163_CH_REG_SIZE;
	ines_int_t   freq;
	ines_int_t   length;
	ines_dword_t phase;
	ines_int_t   idx;
	ines_int_t   sample;

	// +0/+2/+4.b0-1 = 18 位频率；+4.b2-7 = 波长(256-L)×256；+1/+3/+5 = 24 位相位(引擎私有)
	freq   = ((ines_int_t)(w[4] & 0x03) << 16) | ((ines_int_t)w[2] << 8) | (ines_int_t)w[0];
	length = 256 - (ines_int_t)(w[4] & 0xfc);
	phase  = p->chan_phase[slot];

	// 相位按波长回卷(24 位高 8 位有效)
	phase = (phase + (ines_dword_t)freq) % (ines_dword_t)(length << 16);
	p->chan_phase[slot] = phase;

	// 波形地址单位是 4bit 采样：一字节两个采样，低 nibble 在前
	idx    = ((ines_int_t)(phase >> 16) + (ines_int_t)w[6]) & 0xff;
	sample = (ines_int_t)((p->chip_ram[idx >> 1] >> ((idx & 1) * 4)) & 0x0f);

	p->chan_out[slot] = (sample - 8) * (ines_int_t)(w[7] & 0x0f);
}


/**
 * CPU 写内部 RAM 后同步引擎相位。
 * @param p   Namco 163 私有数据
 * @param addr 内部 RAM 地址 $00-$7F
 * @note 硬件上相位累加器就存在 $79/$7B/$7D(每个通道的 +1/+3/+5)，程序写这三个字节即为
 *       "手动定位波形"。本实现把相位放在私有数组里，因此必须在这里把 CPU 的写入同步过去。
 */
static void Namco163_sync_phase_from_ram(Namco163_data_t* p, ines_int_t addr)
{
	ines_int_t slot;
	ines_byte_t* w;

	if(addr < N163_CH_REG_BASE || addr >= N163_CHIP_RAM_SIZE)
		return;

	slot = (addr - N163_CH_REG_BASE) / N163_CH_REG_SIZE;
	if(slot >= N163_CH_COUNT)
		return;

	w = p->chip_ram + N163_CH_REG_BASE + slot * N163_CH_REG_SIZE;
	p->chan_phase[slot] = ((ines_dword_t)w[5] << 16) | ((ines_dword_t)w[3] << 8) | (ines_dword_t)w[1];
}


/**
 * 轮转更新一个通道并刷新整片电平。
 * @param p Namco 163 私有数据
 * @note 启用通道数 C 变化后游标可能落到禁用槽，这里把它归位到最高槽，只在启用范围内轮转。
 */
static void Namco163_update_one(Namco163_data_t* p)
{
	ines_int_t c    = ((ines_int_t)((p->chip_ram[N163_CH_VOL_ADDR] >> N163_CH_ENABLE_SHIFT) & N163_CH_ENABLE_MASK)) + 1;
	ines_int_t slot = p->next_ch;

	if(slot > N163_CH_COUNT - 1 || slot < N163_CH_COUNT - c)
		slot = N163_CH_COUNT - 1;

	Namco163_update_channel(p, slot);

	slot--;
	if(slot < N163_CH_COUNT - c)
		slot = N163_CH_COUNT - 1;
	p->next_ch = slot;

	p->level = Namco163_calc_level(p);
}


/**
 * 把 [from,to) 区间的每个输出样本填成 level。
 * @param p_exp 扩展音源槽
 * @param from  起始 CPU 周期(本帧内相对值)
 * @param to    结束 CPU 周期(本帧内相对值)
 * @param level 该区间的输出电平
 */
static void Namco163_fill(ines_apu_exp_t* p_exp, ines_int_t from, ines_int_t to, ines_int_t level)
{
	ines_int_t o = (ines_int_t)ines_cpu_cycles_to_samples((ines_int64_t)from);
	ines_int_t b = (ines_int_t)ines_cpu_cycles_to_samples((ines_int64_t)to);

	if(b > MAX_SAMPLE_PER_FRAME)
		b = MAX_SAMPLE_PER_FRAME;

	while(o < b)
	{
		p_exp->buffer[0][o++] = level;
	}
}


/**
 * 扩展音引擎推进：按 "通道更新时刻" 分段填充 [0,to) 区间的样本。
 * @param p_exp 扩展音源槽
 * @param to    目标 CPU 周期(本帧内相对值)
 * @note 输出是分段常数(阶梯波)，这里不做插值；抗混叠交给 APU 末端的低通。
 */
static void Namco163_audio_run(ines_apu_exp_t* p_exp, ines_int_t to)
{
	Namco163_data_t* p = (Namco163_data_t*)(p_exp ? p_exp->p_chip : NULL);
	ines_int_t       c;
	ines_int_t       lv;

	if(p == NULL || to <= p_exp->cursor)
		return;

	c = p_exp->cursor;
	while(c < to)
	{
		ines_int_t seg;

		if(p->tick_rem <= 0)
			p->tick_rem = N163_CH_UPDATE_CYCLES;

		lv  = p->sound_disabled ? 0 : p->level;
		seg = c + p->tick_rem;
		if(seg > to)
		{
			Namco163_fill(p_exp, c, to, lv);
			p->tick_rem -= (to - c);
			c = to;
			break;
		}
		Namco163_fill(p_exp, c, seg, lv);
		c = seg;
		p->tick_rem = N163_CH_UPDATE_CYCLES;
		Namco163_update_one(p);                       // 更新后 level 变化，下一段用新电平
	}
}


/**
 * 扩展音引擎复位(挂接时与 APU 复位时都会被调用)。
 * @param p_exp 扩展音源槽
 */
static void Namco163_audio_reset(ines_apu_exp_t* p_exp)
{
	Namco163_data_t* p = (Namco163_data_t*)(p_exp ? p_exp->p_chip : NULL);
	ines_int_t       n;

	if(p == NULL)
		return;

	for(n = 0; n < N163_CH_COUNT; n++)
	{
		// 相位初值取自内部 RAM(可能是电池回读内容)，与硬件"相位存在 RAM 里"一致
		Namco163_sync_phase_from_ram(p, N163_CH_REG_BASE + n * N163_CH_REG_SIZE);
		p->chan_out[n] = 0;
	}
	p->tick_rem = N163_CH_UPDATE_CYCLES;
	p->next_ch  = N163_CH_COUNT - 1;                  // 从通道 8(最高槽)开始轮转
	p->level    = 0;
}


/**
 * 把扩展音引擎挂到 APU 的扩展音源槽。
 * @param p_mapper mapper 实例
 * @note 复位时机在 ines_apu_reset 之前，与 VRC6/VRC7 的做法一致(见 core/nes.c)。
 */
static void Namco163_exp_attach(ines_mapper_t* p_mapper)
{
	ines_host_t*     p_host = mapper2host(p_mapper);
	Namco163_data_t* p      = mapper2Namco163data(p_mapper);

	ines_apu_exp_attach(&p_host->apu, p, 1, N163_EXP_GAIN, Namco163_audio_run, Namco163_audio_reset);
}


/**
 * 软件复位。
 * @param p_mapper mapper 实例
 */
static void mapper19_reset(ines_mapper_t* p_mapper)
{
	ines_host_t*     p_host = mapper2host(p_mapper);
	Namco163_data_t* p      = mapper2Namco163data(p_mapper);
	ines_int_t       n;
	ines_byte_t      n0 = 0, n1 = 0, n2 = 0, n3 = 0;

	// ------------------------------------------------------------------
	// 外部 8KB WRAM：映射到 SRAM 块 1，写操作交给本 mapper(2KB 窗口粒度写保护)。
	// bank_writeable[3] 用 NES_BANK_WRITE_PROTECTED 而非 0：
	//   写 -> NES_BANK_CAN_WRITE 为假 -> 落到 mapper19_writelow
	//   读 -> NES_BANK_CAN_READ 为真 -> 直接读 mem_bank[3]
	//   存档 -> WR[3] != 0 时按 SRAM 块号保存/还原，mem_bank[3] 可正确恢复
	// ------------------------------------------------------------------
	ines_set_sram_bank_n(p_host, 3, N163_WRAM_SRAM_BANK);
	p_host->cpu.bank_writeable[3] = NES_BANK_WRITE_PROTECTED;
	p->wram_writable = ines_false;
	p->wram_protect  = (ines_byte_t)((1 << N163_WRAM_WINDOW_NUM) - 1);

	// 128B 内部 RAM 的电池镜像：SRAM 块 0 尾部
	if(p_host->rom.has_sram)
	{
		p_host->SRAM_used[0] = 1;
		memcpy(p->chip_ram, p_host->SRAM + N163_CHIP_RAM_SRAM_OFFSET, sizeof(p->chip_ram));
	}

	// ------------------------------------------------------------------
	// IRQ：15 位 CPU 周期上计数器
	// ------------------------------------------------------------------
	p->irq_counter       = 0;
	p->irq_enabled       = ines_false;
	p->irq_synced_cycles = p_host->cpu.total_cycles;
	ines_cpu_IRQ(&p_host->cpu, MMC_IRQ_MASK, ines_false);

	// ------------------------------------------------------------------
	// PRG：$8000/$A000/$C000 三个可切槽 + $E000 固定最后一个 8KB 页
	// ------------------------------------------------------------------
	p->prg_bank[0] = 0;
	p->prg_bank[1] = (ines_byte_t)((p_host->prom_8k_num > 1) ? 1 : 0);
	p->prg_bank[2] = (ines_byte_t)((p_host->prom_8k_num > 2) ? 2 : 0);
	ines_set_prom_bank_n(p_host, 4, p->prg_bank[0]);
	ines_set_prom_bank_n(p_host, 5, p->prg_bank[1]);
	ines_set_prom_bank_n(p_host, 6, p->prg_bank[2]);
	ines_set_prom_bank_n(p_host, 7, (ines_word_t)(p_host->prom_8k_num - 1));

	// ------------------------------------------------------------------
	// PPU 12 个窗口：pattern 区默认 CHR-ROM 线性 0-7；
	// NT 区默认按 iNES 头声明的镜像方式指向内部 NT RAM($E0/$E1 两页)
	// ------------------------------------------------------------------
	for(n = 0; n < 8; n++)
	{
		p->chr_bank[n] = (ines_byte_t)n;
	}

	switch(p_host->rom.mirror_type)
	{
	case MIRROR_VERT:        n0 = 0; n1 = 1; n2 = 0; n3 = 1; break;
	case MIRROR_HORZ:        n0 = 0; n1 = 0; n2 = 1; n3 = 1; break;
	case MIRROR_FOUR_SCREEN: n0 = 0; n1 = 1; n2 = 2; n3 = 3; break;
	default:                 n0 = 0; n1 = 0; n2 = 0; n3 = 0; break;
	}
	p->chr_bank[8]  = (ines_byte_t)(N163_CHR_CIRAM_PAGE | n0);
	p->chr_bank[9]  = (ines_byte_t)(N163_CHR_CIRAM_PAGE | n1);
	p->chr_bank[10] = (ines_byte_t)(N163_CHR_CIRAM_PAGE | n2);
	p->chr_bank[11] = (ines_byte_t)(N163_CHR_CIRAM_PAGE | n3);

	p->chr_ram_off_lo   = ines_false;
	p->chr_ram_off_hi   = ines_false;
	p->sound_disabled   = ines_false;
	p->chip_addr        = 0;
	p->chip_auto_inc    = ines_false;

	Namco163_update_chr(p, p_host);

	// 扩展音：把引擎挂到 APU 扩展音源槽(会顺带复位引擎状态)
	Namco163_exp_attach(p_mapper);
}


/**
 * $4020-$5FFF 读：内部 RAM 数据口与 IRQ 计数器。
 * @param p_mapper mapper 实例
 * @param addr     CPU 地址
 * @return 读到的字节(未命中返回开总线值)
 * @note $6000-$7FFF 的读不经此函数(由 mem_bank[3] 直接提供)。
 */
static ines_byte_t mapper19_readlow(ines_mapper_t* p_mapper, ines_word_t addr)
{
	Namco163_data_t* p = mapper2Namco163data(p_mapper);
	ines_byte_t      b;

	if(addr < 0x4800)
		return (ines_byte_t)(addr >> 8);                  // $4020-$47FF：无卡带逻辑

	if(addr < 0x5000)                                     // $4800-$4FFF：内部 RAM 数据口
	{
		b = p->chip_ram[p->chip_addr];
		if(p->chip_auto_inc && p->chip_addr < N163_CHIP_ADDR_MASK)
			p->chip_addr++;                               // 自增到 $7F 停止，不回绕
		return b;
	}

	if(addr < 0x5800)                                     // $5000-$57FF：IRQ 计数器低 8 位
		return (ines_byte_t)(p->irq_counter & 0x00ff);

	if(addr < 0x6000)                                     // $5800-$5FFF：高 7 位 + 使能位
	{
		b = (ines_byte_t)((p->irq_counter >> 8) & 0x7f);
		if(p->irq_enabled)
			b |= 0x80;
		return b;
	}

	return (ines_byte_t)(addr >> 8);                      // 开总线
}


/**
 * $4020-$5FFF 写：内部 RAM 数据口、IRQ 计数器、外部 WRAM(带写保护)。
 * @param p_mapper mapper 实例
 * @param addr     CPU 地址
 * @param val      写入值
 */
static void mapper19_writelow(ines_mapper_t* p_mapper, ines_word_t addr, ines_byte_t val)
{
	ines_host_t*     p_host = mapper2host(p_mapper);
	Namco163_data_t* p      = mapper2Namco163data(p_mapper);

	if(addr < 0x4800)
		return;                                           // $4020-$47FF：无卡带逻辑

	if(addr < 0x5000)                                     // $4800-$4FFF：内部 RAM 数据口
	{
		// $40-$7F 是本片的通道寄存器：先把引擎推到当前时刻，再改寄存器，
		// 这样音量/频率变更落在准确的 CPU 周期上(与 VRC6/VRC7 的做法一致)。
		ines_apu_flush_run(&p_host->apu);
		p->chip_ram[p->chip_addr] = val;
		Namco163_sync_phase_from_ram(p, p->chip_addr);     // 程序写 $79/$7B/$7D = 手动定位波形
		if(p->chip_addr == N163_CH_VOL_ADDR)
			p->level = Namco163_calc_level(p);             // 启用通道数($7F.bit4-6)变化即时生效
		Namco163_sync_chip_ram_save(p, p_host, p->chip_addr, val);
		if(p->chip_auto_inc && p->chip_addr < N163_CHIP_ADDR_MASK)
			p->chip_addr++;                               // 自增到 $7F 停止，不回绕
		return;
	}

	if(addr < 0x6000)                                     // $5000-$5FFF：IRQ 计数器(直接访问)
	{
		if(addr < 0x5800)
		{
			p->irq_counter = (ines_word_t)((p->irq_counter & 0x7f00) | val);
		}
		else
		{
			p->irq_counter = (ines_word_t)((p->irq_counter & 0x00ff) | ((ines_word_t)(val & 0x7f) << 8));
			p->irq_enabled = (val & 0x80) ? ines_true : ines_false;
		}
		ines_cpu_IRQ(&p_host->cpu, MMC_IRQ_MASK, ines_false);   // 写任一寄存器都确认(清除)IRQ
		return;
	}

	// $6000-$7FFF：8KB 外部 WRAM(2KB×4 窗口写保护)
	if(addr < N163_WRAM_BASE + N163_WRAM_SIZE)
	{
		if(Namco163_wram_write_enable(p, addr))
		{
			p_host->SRAM[(((ines_dword_t)N163_WRAM_SRAM_BANK) << 13) + (addr & 0x1fff)] = val;
			p_host->SRAM_write_flag = 1;                  // 标记存档已修改
		}
	}
}


/**
 * $8000-$FFFF 写：CHR/NT 窗口选择、PRG 页、CHR-RAM 使能、内部 RAM 地址端口与 WRAM 写保护。
 * @param p_mapper mapper 实例
 * @param addr     CPU 地址
 * @param val      写入值
 */
static void mapper19_writehigh(ines_mapper_t* p_mapper, ines_word_t addr, ines_byte_t val)
{
	ines_host_t*     p_host = mapper2host(p_mapper);
	Namco163_data_t* p      = mapper2Namco163data(p_mapper);

	if(addr < 0xe000)                                     // $8000-$DFFF：12 个 1KB PPU 窗口
	{
		p->chr_bank[(addr - 0x8000) >> 11] = val;
		Namco163_update_chr(p, p_host);
		return;
	}

	if(addr < 0xe800)                                     // $E000-$E7FF：AMPP PPPP
	{
		ines_bool_t mute = (val & 0x40) ? ines_true : ines_false;    // bit7 = pin22，不模拟

		if(p->sound_disabled != mute)
			ines_apu_flush_run(&p_host->apu);             // 静音切换落在准确的 CPU 周期

		p->prg_bank[0]    = (ines_byte_t)(val & 0x3f);
		p->sound_disabled = mute;
		ines_set_prom_bank_n(p_host, 4, p->prg_bank[0]);
		return;
	}

	if(addr < 0xf000)                                     // $E800-$EFFF：HLPP PPPP
	{
		p->prg_bank[1]     = (ines_byte_t)(val & 0x3f);
		p->chr_ram_off_lo  = (val & 0x40) ? ines_true : ines_false;
		p->chr_ram_off_hi  = (val & 0x80) ? ines_true : ines_false;
		ines_set_prom_bank_n(p_host, 5, p->prg_bank[1]);
		Namco163_update_chr(p, p_host);
		return;
	}

	if(addr < 0xf800)                                     // $F000-$F7FF：CDPP PPPP
	{
		p->prg_bank[2] = (ines_byte_t)(val & 0x3f);
		// bit7/bit6 = pin44 输出(PPU $0000-$0FFF / $2000-$3FFF 区段选中)，本模拟器不模拟
		ines_set_prom_bank_n(p_host, 6, p->prg_bank[2]);
		return;
	}

	// $F800-$FFFF：内部 RAM 地址端口 + 外部 RAM 写保护(同一寄存器)
	p->chip_addr     = (ines_byte_t)(val & N163_CHIP_ADDR_MASK);
	p->chip_auto_inc = (val & 0x80) ? ines_true : ines_false;

	if((val & 0xf0) == 0x40)
	{
		p->wram_writable = ines_true;
		p->wram_protect  = (ines_byte_t)(val & 0x0f);
	}
	else
	{
		// 原文：$40-$4E 之外的任何取值都会让外部 RAM 全部只读
		p->wram_writable = ines_false;
		p->wram_protect  = (ines_byte_t)((1 << N163_WRAM_WINDOW_NUM) - 1);
	}
}


/**
 * 扫描线回调：推进 IRQ 计数器。
 * @param p_mapper mapper 实例
 * @param scanline 当前扫描线(hsync 在可见区与 vblank 区都会被调用)
 * @note N163 的计数器是与 CPU 周期同步的自由计数器，与 PPU 渲染状态无关，
 *       因此每条扫描线都按真实 CPU 周期增量对账；触发时刻量化到扫描线边界。
 */
static void mapper19_hsync(ines_mapper_t* p_mapper, ines_int_t scanline)
{
	ines_host_t*     p_host = mapper2host(p_mapper);
	Namco163_data_t* p      = mapper2Namco163data(p_mapper);
	ines_int64_t     now    = p_host->cpu.total_cycles;
	ines_int64_t     delta;
	ines_int64_t     target;

	(void)scanline;

	// 复位时 ines_cpu_reset 会把 total_cycles 归零，此处做下溢保护
	delta = (now >= p->irq_synced_cycles) ? (now - p->irq_synced_cycles) : 0;
	p->irq_synced_cycles = now;

	if(p->irq_counter >= 0x7fff || delta == 0)
		return;

	target = (ines_int64_t)p->irq_counter + delta;
	if(target >= 0x7fff)
	{
		p->irq_counter = 0x7fff;                          // 计到 $7FFF 停止计数
		if(p->irq_enabled)
			ines_cpu_IRQ(&p_host->cpu, MMC_IRQ_MASK, ines_true);
	}
	else
	{
		p->irq_counter = (ines_word_t)target;
	}
}


/**
 * 释放私有数据。
 * @param p_mapper mapper 实例
 */
static void mapper19_fini(ines_mapper_t* p_mapper)
{
	ines_host_t* p_host = mapper2host(p_mapper);

	INES_LOG(LOG_DBG, MOD_MMC, ISTR("mapper19_fini.\n"));
	ines_apu_exp_detach(&p_host->apu, p_mapper->p_data);   // 解除 APU 扩展槽, 防悬垂
	ines_free(p_mapper->p_data);
	p_mapper->p_data = NULL;
}


/**
 * 创建 mapper 19(Namco 163)。
 * @param p_mapper mapper 实例
 * @return 恒为 ines_true(已实现)
 */
ines_bool_t mapper19_create(ines_mapper_t* p_mapper)
{
	// 外部 WRAM 由本 mapper 自己处理读写(2KB 窗口粒度写保护)
	p_mapper->custom_sram = 1;
	INIT_MAPPER_DATA_ST(p_mapper, Namco163_data_t);

	p_mapper->reset     = mapper19_reset;
	p_mapper->hsync     = mapper19_hsync;
	p_mapper->readlow   = mapper19_readlow;
	p_mapper->writelow  = mapper19_writelow;
	p_mapper->writehigh = mapper19_writehigh;
	p_mapper->fini      = mapper19_fini;

	return ines_true;
}
