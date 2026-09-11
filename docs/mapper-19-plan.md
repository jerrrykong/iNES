# Mapper 019（Namco 163 / Namcot 106）实现方案

本文是 mapper 19 的实现设计文档，兼作后续阶段（CHR-RAM 变体、ROM nametable）的实施依据。
规格来源：NESdev wiki 的 *INES Mapper 019*（oldid=24233）与 *Namco 163 audio*（oldid=23403）。
mapper 210（Namco 175/340）的规格与裁剪版对照见 `core/mapper/210.c` 文件头，不另立文档。

- 阶段状态：**P1、P2、P3 均已完成**
  - P1：页面映射 / IRQ / WRAM 写保护 / 12 窗口 CHR 与 CIRAM 当 CHR / 电池存档
  - P2：Namco 163 扩展音发声；附带交付 mapper 210（Namco 175+340，见 `core/mapper/210.c` 文件头）
  - P3：ROM nametable（NT 窗口指向 CHR 页）、CHR-RAM 变体边界核对、NT 窗口的存档往返

---

## 1. 硬件规格

### 1.1 CPU 地址空间（19 个寄存器，每个占 `$800`）

| 地址范围 | 读 | 写 |
|---|---|---|
| `$4800-$4FFF` | 内部 128B RAM 数据口 | 同上（地址由 `$F800` 指定） |
| `$5000-$57FF` | IRQ 计数器低 8 位 | 直接设置计数器低 8 位（非重载值） |
| `$5800-$5FFF` | 高 7 位 + bit7 使能 | 设置高 7 位与 bit7 使能 |
| `$6000-$7FFF` | 8KB 外部 WRAM | 同上（受 `$F800` 的 2KB×4 窗口写保护约束） |
| `$8000-$DFFF` | — | 12 个 1KB PPU 窗口的页值 |
| `$E000-$E7FF` | — | `AMPP PPPP`：P = `$8000-$9FFF` 的 8KB PRG 页；M = 1 关闭扩展音；A = pin22 |
| `$E800-$EFFF` | — | `HLPP PPPP`：P = `$A000-$BFFF` 的 8KB PRG 页；H/L = 1 时对应 pattern 区的 `$E0-$FF` 页改用 CHR-ROM |
| `$F000-$F7FF` | — | `CDPP PPPP`：P = `$C000-$DFFF` 的 8KB PRG 页；C/D = pin44 输出 |
| `$F800-$FFFF` | — | `KKKK DCBA`：内部 RAM 地址端口 + 外部 RAM 写保护（见 §1.4） |

`$E000-$FFFF` 固定映射最后一个 8KB PRG 页。

### 1.2 12 个 1KB PPU 窗口

`$8000-$DFFF` 的 12 个寄存器依次对应 PPU `$0000/$0400/.../$2C00`，即**同时覆盖 pattern 区与 nametable 区**：

| 页值 | pattern 区（`$0000-$1FFF`） | nametable 区（`$2000-$2FFF`） |
|---|---|---|
| `$00-$DF` | CHR-ROM 的 1KB 页 | CHR-ROM 的 1KB 页（ROM nametable，只读） |
| `$E0-$FF` | 内部 2KB NT RAM（偶 = A 页、奇 = B 页），需 `$E800.6/.7 = 0`；否则落到 CHR-ROM 末 `$20` 页 | 恒为内部 NT RAM（偶 = A 页、奇 = B 页） |

CHR 内存配置（wiki 四种组合）与本实现对应关系：

| 配置 | `$00-$DF` | `$E0-$FF` | 本实现 |
|---|---|---|---|
| 仅 CHR-ROM | CHR-ROM | 内部 NT RAM | `ines_set_vrom_bank_n` / `ines_set_ciram_pattern_bank_n` |
| 仅 CHR-RAM | CHR-RAM | 内部 NT RAM | `ines_set_vram_bank_n` / `ines_set_ciram_pattern_bank_n` |
| 同一页值混合 | 按上述分别判断 | | 同上（逐窗口判断） |

nametable 区的 `$00-$DF` 走 `ines_set_nt_chr_bank_n()`（有 CHR-ROM 时指向 ROM 页、纯 CHR-RAM 卡带指向
pattern RAM），这正是 wiki 所说"nametable arrangement 任意、最多 226 个源 nametable"的来源
（224 个 CHR 页 + 2 个 CIRAM 页）。NT 窗口**不受 `$E800.6/.7` 门控**（wiki 表中该区恒可用）。

### 1.3 IRQ

- 15 位 **CPU 周期上计数器**，与 PPU 渲染状态无关（vblank 期间同样计数）。
- 计到 `$7FFF` 触发 IRQ 并**停止计数**。
- 写 `$5000` 或 `$5800` 都会确认（清除）IRQ。
- 读 `$5000` 返回低 8 位，读 `$5800` 返回 bit7 = 使能、bit6-0 = 计数器高 7 位。

### 1.4 `$F800-$FFFF`：地址端口与 WRAM 写保护复用

```
bit7      = 内部 RAM 地址自增（到 $7F 停止，不回绕）
bit6-0    = 内部 RAM 地址
bit7-4    = 必须等于 b0100 才允许外部 RAM 写入；否则外部 RAM 全部只读
bit3-0    = DCBA，依次对应 $6000/$6800/$7000/$7800 四个 2KB 窗口的写保护位（1 = 保护）
```

注意该寄存器**一次写入同时作用于两处**：用自增方式写地址（`$80|addr`）会顺带把外部 RAM 置为只读，
因此有 WRAM 的游戏在写外部 RAM 前必须写 `$40`（或 `$40|protect`）。

### 1.5 外部 WRAM 与内部 128B RAM

- 8KB 外部 WRAM：`$6000-$7FFF`。
- 128B 内部 RAM：`$4800` 数据口 + `$F800` 地址端口访问，芯片内部另有一份 128B。
- 卡带带电池时**两者都由电池保持**；部分游戏（如 Battle Fleet、Dokuganryuu、Famista '90、Hydlide 3、
  Kaijuu Monogatari、Mindseeker）没有外部 WRAM，仅靠内部 128B 存档。
- 无电池时内部 RAM 上电状态未定义，本实现统一清零（可复现）。

### 1.6 扩展音（P2 依据）

内部 128B RAM 中每通道 8 字节，通道 8 = `$78`、通道 1 = `$40`（倒序）：

| 偏移 | 含义 |
|---|---|
| `+0` / `+2` / `+4(b0-1)` | 频率低/中/高，共 18 位 |
| `+1` / `+3` / `+5` | 相位低/中/高，共 24 位（可读写 = 可手动定位波形） |
| `+4(b2-7)` | 波长 L：`length = 256 - (L<<2)`（单位 4bit 采样） |
| `+6` | 波形地址（单位 4bit 采样，`A>>1` 为字节地址） |
| `+7(b0-3)` | 线性音量 |
| `+7(b4-6)` | 启用通道数 `C`：启用数 = `C+1`，**仅 `$7F` 有效** |

更新算法（每次通道更新）：

```
sample(x) = (chip_ram[x/2] >> ((x & 1) * 4)) & 0x0F     // 一字节两个 4bit 采样，低 nibble 在前
phase     = (w[+5] << 16) + (w[+3] << 8) + w[+1]        // 硬件：相位累加器就存在这三个字节里
freq      = ((w[+4] & 3) << 16) + (w[+2] << 8) + w[+0]
length    = 256 - (w[+4] & 0xFC)
phase     = (phase + freq) % (length << 16)             // 本实现写回引擎私有 chan_phase[]
out       = (sample(((phase >> 16) + w[+6]) & 0xFF) - 8) * (w[+7] & 0x0F)
```

- 输出保持到该通道下次更新；`-8` 为直流偏置（音量 8 的采样不随音量变化）。
- **相位不回写 `chip_ram`**（本实现的刻意偏离）：硬件每次更新都会把相位写回 `+1/+3/+5`，
  但 submapper 1/2（无扩展音）的游戏把这 128B 整片当存档 RAM 用，回写会逐帧改坏它们的存档。
  因此相位放在引擎私有 `chan_phase[8]` 中，并在 CPU 写 `$79/$7B/$7D` 时同步过去
  （程序"手动定位波形"仍有效）；CPU **读**这三个地址只能拿到自己最后写入的值。
- **时序**：每 15 CPU 周期更新**一个**通道，在启用通道间轮转；启用 C 个时每通道间隔 `C*15` 周期。
  NTSC 更新率 119.318 kHz(C=1) / 59.659 k(2) / 29.830 k(4) / 14.915 k(8)；频率 `f = n*p/(15*65536*l*c)`。
- **混音**：wiki 推荐直接把各通道输出求和后除以启用通道数（≥6 通道会略偏响，属官方认可的近似）。
  本实现采用该方案，不模拟 TDM 串行输出的开关噪声。
- **音量实测（相对 APU 最响方波的 dB）与 submapper**：

| submapper | 含义 | 实测 dB | 本实现增益 |
|---|---|---|---|
| 0 | 未指定 | — | 同 submapper 3 |
| 1 | 废弃：128B 电池 + 无扩展音 | — | 引擎常开（无法区分变体，见 P2"已知取舍"） |
| 2 | 无扩展音 | — | 引擎常开（同上） |
| 3 | 11.0-13.0 dB | 12.7-13.0（Final Lap / 三国志II / 女神转生II） | ≈ 0.70 |
| 4 | 16.0-17.0 dB | 16.0-16.9（Rolling Thunder） | ≈ 1.14 |
| 5 | 18.0-19.5 dB | 17.3-19.5（King of Kings / Mappy Kids / Erika / 幽游 / 三国志） | ≈ 1.53 |

增益换算过程（**P2 落地时修正了初稿的错误换算**）：

1. 幅值常数取 `N163_AMP_UNIT = 133`，使单通道 vol=15 全幅方波的峰峰值 `225 × 133 ≈ 29900`
   与 VRC6 的 `15 × 2000 = 30000` 同量纲。
2. 现有 VRC6 引擎增益为 `0.45` → VRC6 峰值 ≈ `30000/32767 × 0.45 ≈ 0.412`（`fout` 标度）；
   APU 最响方波（pulse 单通道 vol=15）为 `mix_pulse_table[15] ≈ 0.149`，即 VRC6 比 APU 方波高约 **+8.9 dB**。
3. 于是 N163 要高出 `D` dB 时：`gain = 0.45 × 10^((D - 8.9) / 20)`，得到上表 0.70 / 1.14 / 1.53。
   （初稿给出的 1.8 / 3.0 / 3.9 是把 `fout` 标度算错了一档，会让 N163 单通道峰值达到 1.6-3.5 而严重削顶。）
4. 本模拟器不解析 NES 2.0 submapper，`N163_EXP_GAIN` 取实录区间中段 `D ≈ 16 dB → 1.00f`，
   即比 submapper 3 的卡带略响、比 submapper 5 的卡带略轻；需要逐 ROM 校准时改这一个常量即可。

- Namco 129 的 `$7C` 只编码波长（高 2 位频率恒 0），但 **129 从未有游戏使用扩展音**（Star Wars 是 submapper 2），故不实现该变体。

---

## 2. 与本模拟器宿主的衔接

### 2.1 宿主接口扩展

| 位置 | 改动 |
|---|---|
| `core/ppu.c` + `core/nes.c/h` | 新增 `ines_set_ciram_pattern_bank_n(host, n, page)`：把 PPU 窗口 `n` 指向内部 NT RAM 页，`pattern_type[n] = 2` |
| `core/ppu.c` `ines_ppu_write` | 写 `$2007` 到 pattern 区时，`pattern_type != 1` 即可写（0 = VRAM、2 = 内部 NT RAM 都可写，1 = VROM 只读） |
| `core/ppu.c` 存档 | `PTRW[n] = 2` 时按 `VNTM21KNUM` 保存页号；读档时校验 `BANK[n] < NES_MAX_NTRAM_BANKS`，非法类型返回 `-1` |

`pattern_type` 取值：`0` = VRAM(`pattern_table`)、`1` = VROM、`2` = 卡带内部 NT RAM（CIRAM 当 CHR）。

### 2.2 存档与电池布局

`p_data`（`Namco163_data_t`）随存档自动保存，因此 128B 内部 RAM、12 个窗口页值、IRQ 状态都在即时存档内。

电池（`.sav`）使用 SRAM 块布局：

| SRAM 块 | 偏移 | 内容 |
|---|---|---|
| 0 | `$0000-$1F7F` | 空闲 |
| 0 | `$1F80-$1FFF` | 128B 内部 RAM 镜像（`N163_CHIP_RAM_SRAM_OFFSET`） |
| 1 | `$2000-$3FFF` | 8KB 外部 WRAM |

- 内部 RAM 镜像只在 `rom.has_sram`（iNES 电池位）时启用，并在 reset 时从 SRAM 回读。
- 内部 RAM 每次写入都同步到镜像；**脏标记按约 1 秒节流**（`N163_SAVE_THROTTLE_CYCLES`），
  避免扩展音游戏逐帧改写通道寄存器导致 GUI 每帧写盘（GUI 每帧调用 `ines_host_save_sram`）。

### 2.3 外部 WRAM 写保护

mapper 通过 `custom_sram = 1` 接管 `$6000-$7FFF`：

```c
ines_set_sram_bank_n(p_host, 3, N163_WRAM_SRAM_BANK);          // mem_bank[3] = SRAM 块 1，SRAM_used[1] = 1
p_host->cpu.bank_writeable[3] = NES_BANK_WRITE_PROTECTED;      // 写落到 mapper19_writelow，读仍直读 mem_bank[3]
```

这样做的原因：

1. 写保护是 **2KB×4 窗口**粒度，宿主的 `NES_BANK_WRITE_ABLE` 只有 8KB 粒度，必须自己处理写。
2. 用 `NES_BANK_WRITE_PROTECTED`（值 2）而不是清零：`NES_BANK_CAN_WRITE` 为假（写走 mapper），
   而 `NES_BANK_CAN_READ` 为真（读直读 `mem_bank[3]`，无需 `readlow`）；
   且 CPU 存档按"`bank_writeable[3] != 0` → 存 SRAM 块号"编码，读档时 `mem_bank[3]` 能正确还原
   （若用 0，则会被当作 PROM 指针而存出非法值导致读档失败）。

### 2.4 IRQ 实现

- 在 `hsync` 中按 `cpu.total_cycles` 的真实增量对账（本模拟器的 `ines_mapper_hsync` 在可见区与 vblank 区都会调用，
  满足"自由计数器"需求），触发时刻量化到扫描线边界。
- 计到 `$7FFF` 停止计数；使能位为 1 时拉 IRQ。
- 与 MMC3 系不同：**不按可见扫描线门控计数**。

### 2.5 扩展音与 APU 输入槽

| 位置 | 改动 |
|---|---|
| `core/mapper/19.c` | `Namco163_exp_attach()` 把引擎挂到 `p_host->apu.exp`（`channels = 1`、`gain = N163_EXP_GAIN`）；`mapper19_fini()` 用 `ines_apu_exp_detach()` 解除 |
| `core/nes.c` | 无需改动：`ines_mapper_reset()` 早于 `ines_apu_reset()`，在 mapper reset 里挂接即可（APU 复位会回调引擎的 reset） |
| `core/apu.c` | `ines_apu_load_state()` 的 `memset` 会清掉扩展槽的芯片指针与回调（读档后扩展音永久失效），改为摘出/还原 |
| `core/apu.h` | 修正扩展槽缓冲"幅值恒为非负"的旧注释（N163 输出有符号） |

写入路径：`$4800` 数据口每次写、以及 `$E000.6` 静音位**真变化**时调用 `ines_apu_flush_run()`，
使寄存器变更落在准确的 CPU 周期上（与 `vrc.h` 的 VRC6/VRC7 做法一致）。

### 2.6 nametable 窗口与 ROM nametable

| 位置 | 改动 |
|---|---|
| `core/ppu.h` | `ines_ppu_t` 新增 `nt_type[4]`：0 = 内部 CIRAM（可写）、1 = 卡带 CHR-ROM 页（只读）、2 = 卡带 CHR-RAM 页（可写） |
| `core/ppu.c` `ines_ppu_set_mirror` | 4 个 nametable 窗口一律回到内部 CIRAM，同时把 `nt_type[0..3]` 复位为 0 |
| `core/ppu.c` 写 `$2007` | nametable 区改为 `nt_type[(addr>>10)&3] != 1` 才写——ROM nametable 必须只读，否则会把 CHR-ROM 当 RAM 写坏 |
| `core/ppu.c` 存档 | 新增 `NTT` 字节（原 `Reserved1`）记录 4 个窗口各 2 bit 的类型；`BANK[8..11]` 的数值按类型解释（CIRAM 页 / VROM 页 / VRAM 页）。状态结构尺寸不变，旧存档仍可读（`NTT == 0` → 全部 CIRAM） |
| `core/nes.c/h` | 新增 `ines_set_nt_chr_bank_n(host, n, bn)`：`n = 0-3` 对应 PPU 窗口 8-11；有 CHR-ROM 时指向 ROM 页（`bn &= vrom_1k_mask`，非 2 的幂容量再回卷，防越界读），纯 CHR-RAM 卡带指向 pattern RAM 并标记 `pattern_table_used` |

**为什么不用"把 ROM 页拷进 CIRAM 再映射"的做法**：4 个 NT 窗口可能同时需要 4 个不同的 ROM 页，
而 CIRAM 只有 2 页（还要留给"CIRAM 当 CHR"），影子拷贝必然冲突，且会丢掉"只读"语义。

---

## 3. 分阶段计划

### P1（已完成）

| 项 | 位置 |
|---|---|
| 12 个 CHR/NT 窗口、CIRAM 当 CHR、`$E800.6/.7` 门控、无 VROM 退化 CHR-RAM | `core/mapper/19.c` |
| PRG 三槽 + `$E000` 固定末页、`$E000.6` 关闭扩展音标志 | 同上 |
| IRQ 计数器（读写 `/ $7FFF` 停止 / 写即确认） | 同上 |
| 内部 128B RAM 数据口与 `$F800` 地址端口（自增到 `$7F` 停止） | 同上 |
| 外部 WRAM 写保护（2KB×4 精确粒度）与读写接管 | 同上 |
| 电池：内部 RAM 镜像 + 外部 WRAM | 同上 + SRAM 布局（§2.2） |
| PPU：`pattern_type = 2`（可写 pattern + 存档）与 `ines_set_ciram_pattern_bank_n` | `core/ppu.c`、`core/nes.c/h` |
| 注册表标注 + 文档 | `core/mapper_creator.c`、`docs/mapper-list.md`、`docs/api.md` |

**P1 时的取舍（P3 已完善）**：NT 窗口写入 `$00-$DF`（ROM nametable）时曾按内部 NT RAM 页近似并打一条
`LOG_WAR`；现由 §2.6 的 `nt_type` + `ines_set_nt_chr_bank_n()` 完整实现，无近似、无告警。

### P2（扩展音发声）——已完成

1. 引擎直接实现在 `core/mapper/19.c` 内（只有 mapper 19 用得到，未像 `vrc.h` 那样独立成头文件）。
   频率/波长/波形地址/音量等寄存器仍以 `chip_ram` 为权威存储；**相位累加器改为引擎私有
   `chan_phase[8]`、不回写 `chip_ram`**（理由与代价见 §1.6 与 P2"已知取舍"）。
2. 挂载：`Namco163_exp_attach()` → `ines_apu_exp_attach(&p_host->apu, p, 1, N163_EXP_GAIN, run, reset)`，
   `channels = 1`（Σ/C 归一已在引擎内部完成）；在 `mapper19_reset()` 末尾挂接（时机早于 `ines_apu_reset`），
   `mapper19_fini()` 里 `ines_apu_exp_detach()` 解除，避免悬垂指针。
3. 状态（都在 `Namco163_data_t` 内，随即时存档自动保存）：
   `chan_phase[8]`（24 位相位累加器，初值取自 `chip_ram` 的 `+1/+3/+5`，兼容电池回读）、
   `tick_rem`（距下次通道更新的剩余 CPU 周期，跨帧延续）、`next_ch`（轮转游标，槽号 7 = 通道 8）、
   `chan_out[8]`（各通道当前输出）、`level`（整片电平）。
   `Namco163_sync_phase_from_ram()` 在 CPU 写 `$79/$7B/$7D` 时把写入同步到 `chan_phase[]`。
4. `Namco163_audio_run(exp, to)` 按"通道更新时刻"分段填充缓冲，`to` 为帧内相对 CPU 周期：
   区间不足一个更新周期时只扣减 `tick_rem` 并填当前 `level`；跨过更新点时先填旧电平、再
   `Namco163_update_one()` 更新一个通道，下一段用新电平（阶梯波，不做插值）。
5. `Namco163_fill()` 用 `ines_cpu_cycles_to_samples()` 换算采样 bin 并 clamp 到 `MAX_SAMPLE_PER_FRAME`
   （与 VRC6 引擎一致）。
6. `Namco163_update_one()`：`c = ((chip_ram[0x7f] >> 4) & 7) + 1`，**只在最高的 c 个槽内轮转**
   （`c = 1` 时只更新通道 8，与 wiki "When C=0, only channel 8 enabled" 一致）；
   槽号越界（启用数变小）时归位到最高槽；更新后调用 `Namco163_calc_level()` 重算
   `level = Σ(启用槽输出) × N163_AMP_UNIT / c`。
   **不维护增量 `sum`**：`c` 变化时直接对启用槽求和，天然正确且每个更新周期只多 8 次加法。
7. 寄存器写入前 `ines_apu_flush_run(&p_host->apu)`：`$4800` 数据口写（每次）、`$E000.6` **只在静音位真变化时**
   （避免频繁切 PRG 页时无谓 flush）；写 `$7F`（音量/启用通道数）时立即重算 `level`，使变更落在准确 CPU 周期。
8. 幅值/增益：`N163_AMP_UNIT = 133`、`N163_EXP_GAIN = 1.00f`，推导见 §1.6。两个常量都标注了"待实录 A/B 校准"。
9. 静音语义：`$E000.6 = 1` 只静输出，芯片照常推进（"disables sound" 理解为静音）。
10. 附带修复：`ines_apu_load_state()` 原来 `memset` 整个 APU 结构，会把扩展音源槽的芯片指针与回调一起清掉，
    导致**读档后 VRC6/VRC7/N163 扩展音永久失效**；现改为先摘出挂接信息、`memset` 后还原，
    并把 `exp.cursor` 对齐到还原后的 `last_cycles`。

**P2 已知取舍**：本模拟器不解析 NES 2.0 submapper，因此

- 扩展音**默认常开**（`$40-$7F` 一律按通道寄存器解释）。无扩展音的变形（submapper 1/2）若把这片 RAM
  当普通存档 RAM 写，会发出噪声；这类游戏可依赖 `$E000.6` 静音，必要时再按头部特征做门控。
- **相位刻意不回写 `chip_ram`**：这是上一条的连锁取舍。硬件把相位累加器存在 `$79/$7B/$7D` 并每次更新
  改写它；若照做，submapper 1/2 的游戏（内部 128B RAM 是它们的唯一存档区）每帧会有 24 字节存档被改坏，
  属数据丢失级问题（受影响的有 Battle Fleet、Dokuganryuu、Famista '90、Hydlide 3、Kaijuu Monogatari、
  Mindseeker 等）。改为引擎私有相位后：存档数据完全不被触碰，程序写相位仍有效，
  代价仅是 CPU 读 `$79/$7B/$7D` 拿到的是自己最后写入的值而非实时相位。
  将来若解析 NES 2.0 submapper，可对 submapper 3/4/5 打开回写以完全对齐硬件。
- 音量取实录区间中段，逐 ROM 需校准时改 `N163_EXP_GAIN` 一个常量。

### P2 附带交付：mapper 210（Namco 175 / Namco 340）

规格与裁剪版对照、变体判定与取舍全部写在 `core/mapper/210.c` 文件头。要点：
`custom_sram` 保持默认 `0`（`$6000-$7FFF` 由宿主统一挂 8KB PRG RAM，**不**按电池位区分 175/340，
理由见下）；`$E000.6/.7` 的镜像选择统一按 340 处理（175 硬连 H/V，其商用卡带都会把高位写成与硬连一致的值）；
`$C000-$C7FF` 的 PRG RAM 使能位不按位门控。无私有状态，bank 与镜像都是立即生效、由宿主存档覆盖。

不区分 175/340 的原因（wiki 建议"按 iNES 电池位猜"，本实现不采纳）：

1. 该判据只能覆盖带电池的 175（Family Circuit '91 / Splatterworld），对 175 无 WRAM 的
   Chibi Maruko-chan / Famista '91 / Heisei Tensai Bakabon 会误判成 340，天生不完整。
2. "不挂 RAM"的代价大于收益：`bank_writeable[3] == 0` 时 `$6000` 的读**仍**走 `mem_bank[3]`
   （读的不是开总线值，而是宿主 CPU 内 `dead_mem` 哑缓冲）；且 CPU 即时存档对 `bank_writeable[n] == 0`
   的块按 `PROM28KNUM(mem_bank[n])` 编码，`dead_mem` 不在 `rom.pPROMs` 内 → 存出垃圾页号、读档校验失败。
3. 多给 8KB RAM 对 340 游戏无可见影响（它们不会去读自己没有的 RAM），而 175 的隐藏自检需要 RAM 存在，
   统一挂载反而更接近商用卡带的行为；无电池卡带不会被写盘（`has_sram == 0`）。

### P3（外围完善）——已完成

1. **ROM nametable**：新增 `ines_set_nt_chr_bank_n()`（§2.6），NT 窗口的 `$00-$DF` 直接指向 1KB CHR 页；
   `Namco163_update_chr()` 先整组按 CIRAM 刷新、再把选 CHR 页的窗口逐个改写。
   NT 区的 `$E0-$FF` 恒为 CIRAM，**不受 `$E800.6/.7` 门控**（与 wiki 表格一致）。
2. **CHR-RAM 卡带变体**：按 wiki 逐条核对后确认 P1 行为正确——无 CHR-ROM 时 `$00-$DF` 落到 pattern RAM
   （`ines_set_vram_bank_n`），NT 窗口同样落到 pattern RAM（`nt_type = 2`，可写）。
3. **存档**：NT 窗口的类型与目标页号随即时存档往返（`NTT` + `BANK[8..11]`），读档校验越界返回 `-1`。

---

## 4. 未模拟 / 不实现的项

| 项 | 原因 |
|---|---|
| `$E000.7`(pin22)、`$F000.6/.7`(pin44) | 板级调试/输出引脚，无游戏依赖 |
| TDM 串行输出的开关噪声（8 通道 14.9 kHz 等） | 采用 wiki 推荐的求和/归一近似；采样率 44.1 kHz 下量化该噪声会产生严重混叠 |
| Namco 129 的波长编码变体 | 129 从未有游戏使用扩展音 |
| 相位累加器的回写（`$79/$7B/$7D` 的读返回实时相位） | 见 §1.6 与 P2"已知取舍"：回写会改坏 submapper 1/2 游戏的内部 RAM 存档 |
| DMC 播放期间自增读内部 RAM 的丢字节 bug | 本模拟器无真实 DMC DMA 读时序 |
| 内部 RAM 上电随机内容 | 统一清零，保证可复现 |

---

## 5. 验证清单

> **状态（2026-09-12）**：Mapper 19 已由用户实机验证，运行无问题。下列条目保留为后续回归与深入验证项；
> 最后一条（mapper 210）因暂时没有可用 ROM，尚未验证。

- [ ] 扩展音的 20 个游戏（三国志II、King of Kings、Rolling Thunder、幽游白书、Erika 等）能进游戏且音乐正常。
- [ ] 音量主观校准：对 Rolling Thunder（约 +16.9 dB）与 Sangokushi II（约 +12.9 dB）A/B，
      确认 `N163_EXP_GAIN = 1.00f` 是否需要按 §1.6 的公式改。
- [ ] 存档类游戏（三国志II / King of Kings / 女神转生II / Juvei Quest）存档后重启能读回：
      外部 WRAM 走 SRAM 块 1，纯 128B 存档游戏（Battle Fleet / Hydlide 3 等）走 SRAM 块 0 尾部。
- [ ] 纯 128B 存档游戏（无扩展音变体）连续运行/反复存档读档，存档数据不被引擎改动（验证相位不回写）。
- [ ] 直接读写 IRQ 寄存器（`$5000`/`$5800`）的游戏分屏无抖动。
- [ ] 即时存档/读档往返正常（含 `pattern_type = 2` 窗口；读档后扩展音仍在发声）。
- [ ] CHR-RAM 变体 ROM（CIRAM 当 CHR、pattern 区 `$E0-$FF`）能显示正确图案。
- [ ] ROM nametable：NT 窗口写 `$00-$DF` 的游戏画面正常，且**不会**把 CHR-ROM 写坏
      （`$2007` 写向 ROM nametable 被忽略）；把窗口切回 `$E0-$FF` 后 CIRAM 内容仍正确。
- [ ] 存档往返：NT 窗口处于 ROM nametable / CIRAM 两种状态分别存档读档，窗口指向与画面均还原。
- [ ] mapper 210：Dream Master / Splatterhouse Wanpaku Graffiti（340，四种镜像切换）、
      Chibi Maruko-chan / Famista '91（175 无 WRAM）、Family Circuit '91（175 带 WRAM 存档）。
