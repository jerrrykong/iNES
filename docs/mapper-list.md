# 已实现 Mapper 清单

统计口径：扫描 `core/mapper/*.c`（256 个文件）并结合 `core/mapper_creator.c` 注册表的 `/* implemented */` 标注。

## 1. 总览

| 项目 | 数量 |
|---|---|
| Mapper 文件总数 | 256（`0.c` ~ `255.c`） |
| 注册表标注 `implemented` | 39 |
| 另有实质代码但未标注 | 1（Mapper **163**） |
| 占位桩（未实现） | 216 |

> 判定依据：桩文件统一为 **39 行**，只有 `reset` / `writehigh` 两个空函数且 `create` 返回 `ines_false`；真实实现则行数显著更多、带私有数据或 IRQ，且返回 `ines_true`。

## 2. 已实现 Mapper 明细

| ID | 行数 | 内部数据名 | IRQ | hsync | 注册表标注 | 说明 / 业界常见名称（供参考） |
|---|---|---|---|---|---|---|
| 0 | 37 | — | | | ✅ | NROM：无 bank 切换，最基础 |
| 1 | 363 | `MMC1` | | | ✅ | MMC1（SxROM）：串行写入寄存器，支持 PRG/CHR 切换与镜像 |
| 2 | 39 | — | | | ✅ | UxROM：仅 PRG bank 切换，CHR 为 RAM |
| 3 | 47 | — | | | ✅ | CNROM：仅 CHR bank 切换 |
| 4 | 392 | `MMC3` | ✅ | ✅ | ✅ | MMC3（TxROM）：最经典，含扫描线计数 IRQ |
| 5 | 455 | `MMC5` | ✅ | ✅ | ✅ | MMC5（ExROM）：最复杂，扩展 RAM、扩展图形模式、`PPU_latch` |
| 6 | 141 | `MMC6` | ✅ | ✅ | ✅ | MMC6（HKROM）：MMC3 变种，含 1KB 内置 WRAM |
| 7 | 45 | — | | | ✅ | AxROM：32KB PRG 切换 + 单屏镜像 |
| 8 | 41 | — | | | ✅ | FFE F3xxx |
| 9 | 170 | `MMC2` | | | ✅ | MMC2（PxROM）：`$FD/$FE` 触发 CHR 切换，用 `PPU_latch_FDFE` |
| 10 | 172 | `MMC4` | | | ✅ | MMC4（FxROM）：MMC2 的 PRG 版 |
| 11 | 41 | — | | | ✅ | Color Dreams |
| 12 | 322 | `MMC3_v1` | ✅ | ✅ | ✅ | MMC3 变体（另一种 IRQ/寄存器行为） |
| 13 | 41 | — | | | ✅ | CPROM：CHR-RAM bank 切换 |
| 15 | 103 | — | | | ✅ | 100-in-1 类多卡带 |
| 16 | 240 | `Mapper16` | ✅ | ✅ | ✅ | Bandai FCG，**注册表注明 "no EEPROM"**（串行 EEPROM 未实现） |
| 17 | 594 | `Mapper17_data_t` | ✅ | ✅ | ✅ | **Front Fareast Super Magic Card**（iNES Mapper 017 专用）：4M/2M/锁存三种 PRG 模式、1KB CHR-RAM（镜像 CHR 数据拷进 pattern RAM）、32KB WRAM 切页、16 位 IRQ 计数器（M2/PA12 可选）、`$5000-$5FFF` scratch RAM、trainer 装载与硬复位入口（`$7000`） |
| 18 | 227 | `MMC18` | ✅ | ✅ | ✅ | Jaleco SS88006 |
| 19 | 662 | `Namco163_data_t` | ✅ | ✅ | ✅ | **Namco 163（Namcot 106）**：12 窗口 CHR/NT、CIRAM 当 CHR、ROM nametable、8KB WRAM + 2KB×4 写保护、15 位 CPU 周期 IRQ、8 通道波表扩展音（经 APU 扩展槽）；[方案与增益标定](mapper-19-plan.md) |
| 21 | 31 | `VRC24_data_t` | ✅ | ✅ | ✅ | Konami **VRC4a/c**（VRC 系，逻辑在 `vrc.h` 共享） |
| 22 | 31 | `VRC24_data_t` | | | ✅ | Konami **VRC2a**：CHR 2KB 粒度、无 IRQ、无 WRAM |
| 23 | 31 | `VRC24_data_t` | ✅ | ✅ | ✅ | Konami **VRC2b/VRC4f** |
| 24 | 28 | `VRC6_data_t` | ✅ | ✅ | ✅ | Konami **VRC6a**：3 路扩展音已由 VRC6 引擎发声（经 APU 扩展输入槽） |
| 25 | 30 | `VRC24_data_t` | ✅ | ✅ | ✅ | Konami **VRC2c/VRC4b/d/e** |
| 26 | 28 | `VRC6_data_t` | ✅ | ✅ | ✅ | Konami **VRC6b**：A0/A1 交换、带 8K WRAM；3 路扩展音已发声 |
| 32 | 212 | `G101_data_t` | | | ✅ | **Irem G-101**（52-pin DIP）：8KB PRG 双窗口（$8000/$C000 由 PRG 模式位交换角色，$C000/$E000 固定倒数第二/最后一页）、8×1KB CHR（$B000-$B007，掩码 $F007）、H/V 镜像；无 IRQ、无 WRAM。《Major League》(J) 硬线单屏 + `$9000` 失效，按 `rom.crc32_p == 0xC0FED437` 走特例分支 |
| 33 | 193 | `TC0190_data_t` | | | ✅ | **Taito TC0190**：寄存器掩码 `$A003`（A0-A1 选组内寄存器、A13 选组、A14 未解码）；PRG 8KB 双窗口（`$C000`/`$E000` 固定倒数第二/最后一页）；CHR 为 2×2KB（寄存器值以 2KB 为单位、不丢 LSB）+ 4×1KB；镜像在 `$8000` bit6；**无 IRQ** |
| 34 | 250 | `NINA34_data_t` | | | ✅ | **BNROM / NINA-001、NINA-002**（两块板共用一个编号，按 CHR 容量区分）：`vrom_1k_num <= 8` → **BNROM**（上电 $8000-$BFFF = 0 号 32KB bank 的低 16KB、$C000-$FFFF = PRG 最后 16KB，之后 `$8000+` 写入 = 32KB PRG bank，8KB CHR 不分页；**仅 PRG ≤ 128KB 复现 AND 型总线冲突**，超出该容量的大容量板（1024KB《泰坦尼克号》）直接锁存写入值）；`> 8` → **NINA**（`$7FFD` = PRG bank、`$7FFE`/`$7FFF` = 两个 4KB CHR 窗口，寄存器**叠在 8KB PRG-RAM 上**：写既进寄存器也进 RAM、读回 RAM 值）。两者均无 IRQ、无扩展音，镜像由硬件固定（沿用卡带头） |
| 41 | 212 | `CALTRON41_data_t` | | | ✅ | **Caltron 6-in-1**（离散逻辑多合一卡带，容纳 4 个未改动的 CNROM / NROM 游戏）：外层寄存器在 `$6000-$67FF`，**bank 号取自写入地址而非数据线**（A5 = 镜像 0V/1H、A4-A3 = 外层 32KB CHR、A2-A0 = 32KB PRG bank @ `$8000-$FFFF`）；内层 8KB CHR 写 `$8000+`（取数据线 bit1-0，**仅 PRG bank 为 4..7 时有效** —— bit2 兼作该使能），该写落在 PRG-ROM 区故复现 AND 型总线冲突。CHR 为两级：外层 32KB × 内层 8KB（共 128KB）；**无 PRG-RAM**（`$6000` 是寄存器，置 `custom_sram = 1` 不挂默认 RAM）、无 IRQ、无扩展音；上电与按住 reset 时两个寄存器清零 |
| 43 | 304 | `TONYI_data_t` | ✅ | ✅ | ✅ | **TONY-I / YS-612**（《超级马力欧兄弟 2》日版从 Famicom Disk System 改成 ROM 卡带的盗版转接板；两块板只差 IRQ 控制寄存器的地址：TONY-I 在 `$4122`、YS-612 在 `$8122`，掩码同为 `$71FF`）：PRG 共 80KB，iNES 映像按「两块 32KB 芯片 → 2KB 芯片**重复四遍** → 8KB 芯片」排列。`$6000-$7FFF` 固定 #2、`$8000-$9FFF` 固定 #1、`$A000-$BFFF` 固定 #0、`$C000-$DFFF` 可切换、`$E000-$FFFF` 是那块 8KB 芯片；`$5000-$5FFF` 是 2KB 芯片重复一次填满 4KB。CHR 8KB **不分页**。寄存器：`$4022` 的 bit2-0 选 `$C000` 的 bank，但硬件译码**非恒等** —— 写入值 0..7 对应实际页 **4,3,4,4,4,7,5,6**；IRQ 控制寄存器 bit0 = 1 允许计数、0 = 应答 + 关闭 + 计数器清零，使能后 **12 位计数器随每个 M2（CPU）周期递增、溢出触发**。`$6000` 是 PRG 而不是 SRAM（`custom_sram = 1`）；不控制镜像。实现说明：`$5000-$5FFF` 落在 CPU bank 2，而宿主 PRG 映射接口只覆盖 bank 3~7（`ines_set_prom_bank_n` 有 `assert(3 <= n && n <= 7)`），故这 4KB 由 `readlow` 按「2KB 芯片 + (addr & $7FF)」提供；IRQ 是 CPU 周期驱动，在 hsync 里按 `cpu.total_cycles` 的真实增量批处理推进 |
| 44 | 407 | `SB7_data_t` | ✅ | ✅ | ✅ | **Super Big 7-in-1**（以 MMC3 为基础的多合一卡）：寄存器窗口与行为**完全等同 MMC3**（`$8000-$FFFF`，掩码 `$E001`，含扫描线计数器 IRQ），唯一区别在 `$A001` —— bit7 使能 / bit6 写保护同 MMC3，**bit2-0 = 块选择（选 7 等同选 6）**。块 0-5 各 128KB PRG+CHR，块 6/7 各 256KB（整卡 1MB+1MB）；**MMC3 选出的所有页（含两个固定页）都要过 `(页号 AND and) OR or` 映射到当前块内**（PRG 以 8KB 页、CHR 以 1KB 页计，所以块 6 的固定页是 126/127、块 0 的是 14/15）；上电与复位选中块 0 |
| 45 | 485 | `GA23C_data_t` | ✅ | ✅ | ✅ | **GA23C 多合一**（MMC3 内核 + 外层 bank 寄存器）：MMC3 部分与 MMC3 完全一致（`$8000-$FFFF`，掩码 `$E001`，含扫描线计数器 IRQ；`$A001` 就是普通的 PRG-RAM 保护，不像 44 那样承载块选择）；外层是 `$6000` 上的四个 bank 寄存器，**按写入次序轮流填入**（第 1 次写 -> #0、第 2 次 -> #1 … 第 5 次又回到 #0），写 `$6001` 则四个寄存器清零并解除锁定（#3.bit6 置 1 后 `$6000` 写入失效直到解锁）。MMC3 选出的页号一律过 `((页号 AND and) OR or)`：PRG 以 8KB 页计（`and = (~#3) & 0x3F`、`or = #1 | ((#2 & $C0) << 2)`），CHR 以 1KB 页计（`and = (1 << ((#2 & $0F) - 7)) - 1`、`or = #0 | ((#2 & $F0) << 4)`）；**两个固定页同样是 MMC3 原始输出 A13-A18 = 111110/111111（0x3E/0x3F）再过这道变换**，而不是"整卡的最后两页"。外层寄存器**叠在 WRAM 上**且不受 MMC3 的 WRAM 位控制：写同时进 RAM 与寄存器，读回 RAM 值 |
| 46 | 200 | `RUMBLE46_data_t` | | | ✅ | **Rumble Station 15-in-1**（NES-on-a-Chip 多合一，收录一批已授权的 Color Dreams 游戏）：**两级选页**。外层 `$6000-$7FFF` 锁存写入值 `[CCCC PPPP]` —— bit7-4 选 64KB CHR bank、bit3-0 选 64KB PRG bank，上电为 0；内层 `$8000-$FFFF` 锁存写入值 `[.CCC ...P]` —— 是 Color Dreams（11）的**缩减子集**，bit6-4 选 64KB bank 内的 8KB CHR、bit0 选 64KB bank 内的低 / 高 32KB PRG。合成后 **32KB PRG bank = (外层 PPPP << 1) \| 内层 P**（最多 32 个 = 1MB）、**8KB CHR bank = (外层 CCCC << 3) \| 内层 CCC**（最多 128 个 = 1MB）。`$6000-$7FFF` 是寄存器因此**没有 PRG-RAM**（`custom_sram = 1`）；无 IRQ、不控制镜像。**不做** AND 型总线冲突 —— 它是 NES-on-a-Chip 而非真 ROM 芯片，同门类的 11 也不做，做错会把 bank 号按 ROM 内容截掉而黑屏 |
| 48 | 265 | `TC0690_data_t` | ✅ | ✅ | ✅ | **Taito TC0690**（033 的超集）：寄存器掩码 `$E003`（A0-A1 选组内、A13/A14 选组）；PRG/CHR 布局同 TC0190；镜像单独在 `$E000` bit6；IRQ 与 MMC3 同构（`$C000` reload **取反 XOR $FF**、`$C001` 重载、`$C002` 使能、`$C003` 应答关闭）。**已知取舍：资料称比 MMC3 晚约 4 个 CPU 周期，当前无周期级回调，与 MMC3 同时刻置位** |
| 57 | 203 | `GK57_data_t` | | | ✅ | **GK 47-in-1 / SuperGK 6-in-1**（多合一卡，128KB PRG + 128KB CHR）：寄存器掩码 `$8800`（只按 A11 分成 `$8000` / `$8800` 两组）。`$8000` = `[CH.. ..AA]` —— C = CHR Mode（0 = CNROM 模式 / 1 = NROM 模式）、H = CHR A16、AA = **CNROM 模式**下的 CHR A13-A14；`$8800` = `[PPPO MBbb]` —— PPP = PRG Reg、O = PRG Mode、M = 镜像（0 垂直 / 1 水平）、B = CHR A15、bb = **NROM 模式**下的 CHR A13-A14。CHR 是**整块 8KB 一起切换**：bank = `(H<<3) \| (B<<2) \| (C ? bb : AA)`。PRG 两种模式，`PPP` **在两种模式下都是 16KB 页号**：**Mode 0** = 16KB bank（= PPP）同时出现在 `$8000` 与 `$C000` 两个窗口（16KB 游戏靠这个镜像跑起来）；**Mode 1** = 整个 32KB 窗口 = 32KB bank #`(PPP >> 1)`（只取高 2 位）。无 IRQ、无 PRG-RAM。实测 `6in1_SuperGK-L02A.nes` / `6in1.nes` / `54in1.nes` 三个 ROM 的菜单与所选游戏均正常：SuperGK 第 1 项进游戏写 `$8800 = #$22`（Mode 0）确认了 Mode 0；第 4 项 INT'L LEAGUE（= Major League Baseball）走 Mode 1，写 `$8800 = #$D4`（PPP = 6）—— 128KB 只有 4 个 32KB bank，PPP = 6 只有当作 16KB 页号才合法，据此定为 `PPP >> 1` 后花屏消失 |
| 58 | 213 | 无 | | | ✅ | **简单 NROM/CNROM 型多合一**（GK-192 的 118-in-1、HKX5268 的 68-in-1 等，128KB PRG + 64KB CHR）：**Address Latch** —— bank 号由**写入地址**译码，写入的数据只用来选镜像（与 41 / 225 / 255 同族，切勿按数据线锁存器去实现）。`$8000-$FFFF` 写：A6 = PRG Mode（0 = NROM-256，整个 32KB 窗口 = 32KB bank `#((A2..A1)>>1)`；1 = NROM-128，16KB bank `#(A2..A0)` 镜像到 `$8000` 与 `$C000`）、A5..A3 = 8KB CHR bank（整块 8KB 一起切）、数据 D1 = 镜像（1 垂直 / 0 水平）。无 IRQ、无 PRG-RAM、无状态寄存器（每次写都重设三个窗口），上电 = 16KB bank 0 镜像。iNES Mapper 213 是本编号的重复。实现参照 VirtuaNES 0.97 `Mapper058.cpp`。实测 `118-in-1 (GK-192 board) [!].nes` 与 `68in1_HKX5268.nes` 菜单均正常显示 |
| 60 | 131 | `M60_data_t` | | | ✅ | **Reset-based NROM-128 4-in-1 多合一**（kevtris `Reset Based Four in One`，64KB PRG + 32KB CHR）：卡带里是四个各自独立的 NROM-128 游戏，每块 16KB PRG + 8KB CHR。**没有任何 bank 寄存器** —— 当前块由片内计数器决定，**只能靠软复位递增**（上电停在块 0，此后每复位 +1；Disch 推测该计数器 2 位宽，实现里对块数取模）。16KB 块 #N 镜像到 `$8000` 与 `$C000`（8KB 页 2N、2N+1），CHR 是 8KB 块 #N 整块切换；块数 = min(PRG 的 16KB 块数, CHR 的 8KB 块数)。无 IRQ、无 PRG-RAM、不控制镜像。注意 **FCEUX 等把 T3H53 放在 iNES 060、把本卡带挤走**，因此标着 60 却不是 reset-based 多合一的 ROM 实属 mapper 59。实测：真 ROM 的复位序列为 0→1→2→3→0（每复位一次递增 1），四块画面各不相同；另用合成 ROM 交叉验证（块 N 的 CHR tile k = 纯色 `(2N+k)%4`，程序把整屏填成 tile #N），九次复位得到屏幕主色 0,3,2,1,0,3,2,1,0，与"PRG 与 CHR 同步按块切换"吻合（若只切 CHR 应为 0,1,2,3；若只切 PRG 应为 0,2,0,2） |
| 85 | 25 | `VRC7_data_t` | ✅ | ✅ | ✅ | Konami **VRC7**：FM(YM2413) 简化内核已接入（vrc.h §5b，单声道经 APU 扩展输入槽） |
| 163 | 178 | `MMC163` | | ✅ | ❌ | 有完整实现（含 `reset/writehigh/readlow/writelow/hsync/fini`），但注册表未标注 `implemented` |
| 210 | 142 | —（无私有状态） | | | ✅ | **Namco 175 / Namco 340**（Namco 163 的降本版，同一个 iNES 号）：8 窗口 1KB CHR、3 槽 8KB PRG、340 可选 H/V/单屏镜像；175/340 变体不区分（详见 `core/mapper/210.c` 文件头） |
| 225 | 248 | `K1010_data_t` | | | ✅ | **ET-4310(60pin) / K-1010(72pin) 多合一板**（52 Games、58-in-1、64-in-1 等）：bank 号由**写入地址**译码（A14 高位 + A13 镜像 + A12 页大小 + A11-A6 PRG + A5-A0 CHR），PRG 16KB/32KB 两种模式、整块 8KB CHR、`$5800-$5FFF` 4×4bit 附加 RAM；无 IRQ |
| 255 | 250 | `BMC110_data_t` | | | ✅ | **110-in-1 多合一板**：与 225 同构（资料原文即注明"看起来是 225 的重复"），位域命名不同（B/M/Z）但换算一致；**已知实现分歧**：只有 fceumm 把 CHR 页号最低 2 位改成取写入值，本实现按 Nestopia/Mesen/puNES 取地址译码，见 `core/mapper/255.c` 文件头 |

> **实机验证状态（2026-09-20）**：**19**（Namco 163）已由用户实机验证，游戏运行无问题；
> **17**（Super Magic Card）与 **210**（Namco 175/340）暂无可用 ROM，尚未实机验证（仅通过编译与静态检查）。
> **32**（Irem G-101）与 **33**（Taito TC0190）已由用户用 `D:\NES\任天堂FC全集` 中的 ROM 实机验证通过。
> **225 / 255**（多合一卡带）尚未实机验证，但已用 `tools/dump_frame.ps1` 无头抓帧确认菜单画面正常：
> 225 = `52 Games (U) [p]`、`58-in-1 [p]`、`64-in-1 (J) [p]`、`72-IN-1`；255 = `110-in-1 (Unl) [p]`、`115IN1`。

> **Mapper 33 / 48 混标问题**：大量 mapper **048**（Taito TC0690，比 033 多一套 IRQ、镜像处理不同）的卡带
> 在流传的 ROM 里被错误标注为 033（`Bakushou!! Jinsei Gekijou 2/3`、`Captain Saver`、`Don Doko Don 2`、
> `Flintstones - The Rescue of Dino & Hoppy` 等）。当前 33 的实现不含 IRQ，这类卡带会缺少中断。
> **Mapper 48 已于 2026-09-25 实现**，并在 `core/rom.c` 的 ROM 加载层加了 **Mapper ID 修正表**：
> 按 PROM CRC32 把已确认的 4 个 ROM 从 33 改为 48（`0x1394E1A2` Bakushou 3、`0x49C84B4E` Don Doko Don 2、
> `0x202DF297` Captain Saver、`0x547E6CC1` Flintstones）。
> 资料点名但 ROM 缺失的 `Bubble Bobble 2 (J)`、`Jetsons (J)` 待补录；其余标注为 33 的 ROM 归属未定，保持原样。
> 注意：修正后存档头里的 `mapperid` 也随之变为 48，修正前存的档会被判为不匹配而拒绝加载。
> `D:\NES\任天堂FC全集` 中标注为 33 的 12 个 ROM 全部命中此风险，真正的 033 游戏是
> `Akira`、`Bakushou!! Jinsei Gekijou`、`Don Doko Don`、`Insector X`。
> Mapper 32 的《Major League》(J) 硬线单屏（CIRAM A10 接 +5V）且 `$9000` 寄存器失效，
> iNES 头无法表达（NES 2.0 用 submapper 区分），实现里按 `rom.crc32_p == 0xC0FED437` 走硬线分支。
> 详细验证项见 [mapper-19-plan.md](mapper-19-plan.md) §5。

> **VRC 家族共享实现**：21/22/23/25（VRC2/VRC4）、24/26（VRC6）、85（VRC7）的核心逻辑
> 集中在 `core/mapper/vrc.h`（约 1200 行）：
> - 引脚错位由 `reg_mask1/reg_mask2` 统一对齐（每个编号一套掩码）
> - VRC4/6/7 共用同一个 IRQ 计数器状态机（latch/使能/ack/模式）
> - VRC6 扩展音：APU **扩展音源输入槽**（`ines_apu_exp_t`，见 `apu.h`），VRC6 引擎（vrc.h §4b，
>   周期精确方波/锯齿）挂槽、随 `run_until` 惰性推进、`render_frame` 混音
> - VRC7 FM(YM2413)：vrc.h §5b 简化 FM 内核（2-op × 9 旋律声道、15 内建音色 + 用户音色、
>   19bit 相位/FB 反馈/简化 OPLL EG），同样经扩展槽发声；FM 寄存器写前先
>   `ines_apu_flush_run` 对齐（时钟模型：YM2413 主频 = 2×CPU，FM 更新每 36 CPU 周期一次）
> - 两芯片混音幅值均为经验标定（`VRC6_EXP_GAIN` / `VRC7_EXP_GAIN`），待与实录 A/B

未实现但值得注意的是 **14**：它的桩里已有基本的 bank 设置骨架，可以直接作为新实现的起点。

## 3. 特性矩阵（已实现部分）

| 特性 | 使用的 Mapper |
|---|---|
| 扫描线 IRQ（`hsync` + `ines_cpu_IRQ`） | 4, 5, 6, 12, 16, 17, 18, 19 |
| `hsync`（无 IRQ，用于 CHR 切换特效） | 163 |
| PRG + CHR 全切换 | 1, 4, 5, 6, 12, 16, 17, 18, 19, 163, 210, 225, 255 |
| 仅 PRG 切换 | 2, 7, 11, 15 |
| 仅 CHR 切换 | 3, 13 |
| 无切换 | 0 |
| `PPU_latch`（MMC5 图形扩展） | 5 |
| `PPU_latch_FDFE`（`$FD/$FE` 锁存） | 9, 10, 17 |
| 内部 NT RAM 当作 CHR（`pattern_type = 2`） | 19 |
| nametable 窗口指向 CHR 页（ROM nametable，`ines_set_nt_chr_bank_n`） | 19 |
| nametable 窗口指向 pattern RAM 页（CHR-RAM nametable，`ines_set_nt_pattern_bank_n`） | 17 |
| 自定义 SRAM（`custom_sram = 1`，含写保护） | 17, 19 |
| `$4020-$5FFF` 附加 RAM（`readlow` / `writelow`） | 17, 225, 255 |
| 自由镜像排布（`ines_ppu_set_mirror`，含单屏选择） | 1, 6, 7, 16, 17, 18, 19, 21-26, 210 |
| 扩展音（APU 扩展输入槽） | 19, 24, 26, 85 |
| 私有数据 + `fini` | 1, 4, 5, 6, 9, 10, 12, 16, 17, 18, 19, 163, 225, 255 |
| 镜像自带 trainer（复位入口覆盖，`host.reset_entry`） | 17 |

## 4. 桩文件（占位实现）

桩的统一形态（`core/mapper/14.c` 为例）：

```c
static void mapper14_reset(ines_mapper_t* p_mapper)
{
    ines_host_t*  p_host = mapper2host(p_mapper);
    if(p_host->prom_8k_num >= 4)
        ines_set_prom_bank_4(p_host, 0, 1, 2, 3);
    else
        ines_set_prom_bank_4(p_host, 0, 1, 0, 1);
    if(p_host->vrom_1k_num > 0)
        ines_set_vrom_bank_8(p_host, 0, 1, 2, 3, 4, 5, 6, 7);
}

static void mapper14_writehigh(ines_mapper_t* p_mapper, ines_word_t addr, ines_byte_t val)
{
    ines_host_t*  p_host = mapper2host(p_mapper);
    (void)p_host;
}

ines_bool_t  mapper14_create(ines_mapper_t* p_mapper)
{
    p_mapper->reset     = mapper14_reset;
    p_mapper->writehigh = mapper14_writehigh;
    return ines_false;          // <- 未实现
}
```

这些文件由 `core/mapper/auto_gen.lua` 依据 `core/mapper/templ.c.tpl` 批量生成，因此结构完全一致。加载这类 ROM 时宿主会返回 `NES_ERR_UNSUPPORT_MAPPER_ID`（10002）。

## 5. 新增实现的建议顺序

按"常见游戏覆盖面"排序，可作为后续补全 Mapper 的参考优先级（业界常见编号，非本仓库现状）：

1. **23 / 24 / 26**（MMC3 变体，VRC 系）—— 与已实现的 4/12 结构接近，改造成本低
2. **21 / 22 / 25**（VRC2/VRC4）—— 大量日厂游戏
3. **69 / 71 / 73 / 75 / 79**（常见亚洲产 Mapper）
4. **32 / 33 / 34 / 48 / 87 / 90 / 94 / 95 / 118 / 119 / 180**
5. 其余按需求驱动

具体寄存器定义请以公开硬件文档为准，实现规范见 [mapper-guide.md](mapper-guide.md)。

## 6. 维护提示

- 新增/修复后，记得同步：
  - `core/mapper_creator.c` 注册表项的 `/* implemented */` 注释
  - 本文档的"已实现 Mapper 明细"表
- `core/mapper/*.c` 由 `file(GLOB ... CONFIGURE_DEPENDS)` 收集，**新增文件无需改 CMakeLists.txt**，重新配置即可
