# 寄存器查看窗口 设计方案（macOS / win32）

> 状态: **mac 端已实现（2026-09-15）；win32 端已实现（2026-09-18，`win32/wRegister.c`）**，决策见 §10。
>
> 实现偏差: 实际 46 项（CPU 8 / PPU 12 / APU 23 / I/O 3），§4 原记 51/24 为多计;
> 位格每个占 3 列（`[x]`），故总宽 97 列（≈710px）而非 88 列/648px。
> 范围: macOS 前端 `mac/iNESRegisterView.m` 与 win32 前端 `win32/wRegister.c`（`IDM_VIEW_REG` = 32867）。
>
> win32 实现的两点差异（由线程模型决定，非功能差异）:
>   1. win32 的 `OnIdle()` 在主线程直接 `ines_host_doframe()`，host 与窗口同线程 ——
>      **不需要**快照与写队列：取值直读 host 字段，改值直接应用（写端口仍走
>      `ines_ppu_writelow` / `ines_apu_write` / `ines_host_write`）;
>   2. 重绘沿用 wMemory.c 的 50ms `WM_TIMER` + `UpdateAllViews()` 节奏；暂停且无变化、
>      无 Tips 时不重绘，变化高亮回落与 Tips 过期各补一拍整幅重绘。

## 0. 结论摘要

新增第 7 个调试视图 `IDBG_VIEW_REGISTER`（窗口标题「寄存器查看器」），自绘等宽网格，一行一个寄存器：
**名称 / 地址 / 值(hex) / 位格 / 说明**，覆盖 CPU + PPU + APU + I/O 共 **51 项**；可写项支持
「十六进制整值编辑」与「位格逐位编辑」两种改法（**单击只选中，不立即改值**；位格选中后输入 `0/1` 才改写）。数据经现有调试快照链路（帧末采集 / 50ms 刷新 /
写队列帧首应用），不触碰 host 线程独占约束。

---

## 1. 现状（改动依据）

| 项 | 现状 |
|---|---|
| `win32` | 已完成：`win32/wRegister.c` + `wRegister.h`（`IDM_VIEW_REG` = 32867 接上，`UpdateAllViews()` 加 `wReg_SetUpdate()`）；标题串 `IDS_WND_REG_TITLE`(115) |
| `mac/iNESDebug.h` | 6 个视图（`IDBG_VIEW_COUNT 6`），第 20 行注释「win32 的"寄存器"菜单项为空实现, 这里同样不提供」 |
| `mac/iNESApp.m:790-807` | 「调试视图」子菜单 6 项走 `showDebugView:`（tag = 视图号），另有单独一项「寄存器…」走 `showUnimplemented:` |
| 快照 | `ines_dbg_snapshot_t` 目前只有内存/调色板/精灵 + `reg_ctrl_1`，**没有 CPU/APU 寄存器** |
| 写队列 | `ines_dbg_write_t {space, addr, val}`，只能表达"某空间某地址写一个字节"，**无法表达寄存器写**（尤其 16 位 PC / PPU T / V） |
| 视图基类 | `iNESMemoryView` 已有可复用的等宽网格、翻转坐标、legacy 滚动条、字体度量（`charW` 保留小数）、滚轮/键盘编辑骨架 |

---

## 2. 改动文件清单

| 文件 | 改动 |
|---|---|
| `mac/iNESDebug.h` | 新增 `IDBG_VIEW_REGISTER = 6`（`COUNT → 7`）；新增 `ines_dbg_regs_t` 并挂进快照；新增寄存器写 API；更新第 20 行过时注释 |
| `mac/iNESDebug.m` | `ines_dbg_capture()` 补采寄存器；`ines_dbg_apply_writes()` 分支处理寄存器写；`ines_dbg_view_title()` 加「寄存器查看器」；`showView:` 创建 `iNESRegisterView`；`tick` 加一次 `refreshView:` |
| `mac/iNESRegisterView.h/.m` | **新文件**：寄存器定义表 + 自绘视图（约 700~850 行） |
| `mac/iNESApp.m` | 菜单 `titles` 数组加 `@"寄存器…"`（tag = 6），删除原走 `showUnimplemented:` 的单独一项 |
| `CMakeLists.txt` | `INES_MAC_OBJC_SOURCES` 加 `mac/iNESRegisterView.m`（ARC 由该变量的 `COMPILE_OPTIONS` 自动生效） |
| `docs/macos-port.md` | §8.5 调试视图清单补一项（若有清单） |

---

## 3. 数据层设计

### 3.1 快照扩展（模拟线程写入，主线程只读）

```c
typedef struct _ines_dbg_regs_
{
    /* ---- CPU（内部寄存器，直接拷字段）---- */
    ines_byte_t   a, x, y, p, sp;
    ines_word_t   pc;
    ines_byte_t   int_pending;      // bit0 NMI / bit1 MMC / bit2 APU
    ines_int64_t  total_cycles;
    ines_byte_t   jammed;

    /* ---- PPU ---- */
    ines_byte_t   ctrl1;            // $2000
    ines_byte_t   ctrl2;            // $2001
    ines_byte_t   status;           // $2002（拷字段，绝不调 ines_ppu_readlow）
    ines_byte_t   oam_addr;         // $2003
    ines_byte_t   oam_data;         // sp_RAM[oam_addr]（$2004 当前值）
    ines_word_t   t, v;             // $2005/$2006 的内部地址寄存器
    ines_byte_t   fine_x;           // $2005 第一次写入的低 3 位
    ines_byte_t   toggle_2005_2006;
    ines_byte_t   read2007_buffer;
    ines_int_t    scanline;         // current_line
    ines_byte_t   in_vblank;

    /* ---- APU（各声道端口上一次写入值）---- */
    ines_byte_t   pulse1[4];        // $4000-$4003
    ines_byte_t   pulse2[4];        // $4004-$4007
    ines_byte_t   triangle[4];      // $4008-$400B
    ines_byte_t   noise[4];         // $400C-$400F
    ines_byte_t   dmc[4];           // $4010-$4013
    ines_byte_t   ctrl_4015;        // apu.reg_ctrl（写入值，非读回状态）
    ines_byte_t   frame_4017;       // apu.reg_frame_mode
    ines_byte_t   apu_status;       // 派生状态位（不调 ines_apu_read，避免清 IRQ）
    ines_byte_t   apu_irq_flag;     // apu.irq_flag / dmc.irq_flag 合成

    /* ---- I/O ---- */
    ines_byte_t   dma_high;         // $4014 上次写入值
    ines_byte_t   joy_strobe;       // $4016 上次写入值（strobe）
} ines_dbg_regs_t;
```

> **关键约束**：`ines_apu_read($4015)` 会清 IRQ flag、`ines_ppu_readlow($2002)` 会清 VBlank 与 toggle
> —— 采集一律**直拷结构体字段**，不走任何 `read` 路径。

### 3.2 写队列扩展

现有三元组不够用（16 位寄存器会产生中间态），改为带类型标签：

```c
#define IDBG_WRITE_MEMORY   0
#define IDBG_WRITE_REGISTER 1

typedef struct _ines_dbg_write_
{
    ines_int_t   kind;    // 0 = 内存(原语义) / 1 = 寄存器
    ines_int_t   space;   // kind=0 时有效
    ines_int_t   addr;    // kind=0: 地址; kind=1: 寄存器 ID
    ines_int_t   val;     // kind=1 时为完整值(8 位或 16 位，一次投递、原子应用)
} ines_dbg_write_t;

int ines_dbg_post_reg_write(ines_int_t regId, ines_int_t val);
```

`ines_dbg_apply_writes()` 新增寄存器分支（模拟线程、帧首执行，与内存写在同一次调用里）。

### 3.3 写入路径表（模拟线程侧，独占 host，安全）

| 目标 | 写入方式 | 说明 |
|---|---|---|
| CPU A/X/Y/SP/P/PC | 直接改 `host.cpu.reg_*` | 无端口语义 |
| $2000 / $2001 / $2003 | `ines_ppu_writelow()` | **必须走端口**：$2000 会同步 `index_t` 的 NT 位 |
| $2004 OAMDATA | `ines_ppu_writelow(0x2004)` | 会自增 OAMADDR（真实语义） |
| $2007 PPUDATA | `ines_ppu_writelow(0x2007)` | 写 VRAM + 地址自增 |
| PPU T / V / fine_x | 直接改 `index_t` / `index_v` / `index_x` | 不经过 $2005/$2006 双写，避免连带副作用；界面标注「内部状态」 |
| $4000-$4013 / $4015 / $4017 | `ines_apu_write()` | 会重载 length counter / 清 DMC IRQ / 置 `reg_written` —— 这才是"可写"的真实语义 |
| $4014 OAMDMA | `ines_host_write(0x4014, val)` | **会立即触发 256 字节 DMA + 514 周期**，界面需二次确认 |
| $4016 JOYPAD | `ines_host_write(0x4016, val)` | strobe，会重置手柄移位寄存器 |

---

## 4. 窗口内容（51 项）

图例：`W` = 可写，`R` = 只读，`WO` = 只写（显示值为上次写入值）。

### 组 1：CPU（8 项）

| 名称 | 地址 | 位宽 | 属性 | 来源 / 写路径 |
|---|---|---|---|---|
| A | `-` | 8 | W | `cpu.reg_A` |
| X | `-` | 8 | W | `cpu.reg_X` |
| Y | `-` | 8 | W | `cpu.reg_Y` |
| P | `-` | 8 | W | `cpu.reg_P`（位名 N V - B D I Z C）；**B/R 位允许修改**（R 恒 1、B 仅入栈有效，说明列标注） |
| SP | `-` | 8 | W | `cpu.reg_SP` |
| PC | `-` | 16 | W | `cpu.reg_PC` |
| IRQ PEND | `-` | 8 | W | `cpu.INT_pending`（NMI / MMC / APU） |
| CYCLES | `-` | 64 | R | `cpu.total_cycles`（十进制） |

### 组 2：PPU（12 项）

| 名称 | 地址 | 位宽 | 属性 | 来源 / 写路径 |
|---|---|---|---|---|
| PPUCTRL | `$2000` | 8 | W | `reg_ctrl_1` → 端口 |
| PPUMASK | `$2001` | 8 | W | `reg_ctrl_2` → 端口 |
| PPUSTATUS | `$2002` | 8 | **R** | `reg_status`（不清 VBlank） |
| OAMADDR | `$2003` | 8 | W | `reg_spr_addr` → 端口 |
| OAMDATA | `$2004` | 8 | W | `sp_RAM[oam_addr]` → 端口（自增） |
| PPUSCROLL | `$2005` | 派生 | **R** | 显示 `X=nn FX=n Y=nn FY=n`（由 T / fine_x 反算） |
| PPUADDR T | `$2006T` | 16 | W | `index_t` 直写 |
| PPUADDR V | `$2006V` | 16 | W | `index_v` 直写 |
| PPUDATA | `$2007` | 8 | WO | `read_2007_buffer`（写触发 VRAM 写 + 地址自增） |
| SCANLINE | `-` | 派生 | **R** | `current_line` |
| VBLANK | `-` | 1 | **R** | `in_vblank` |
| TOGGLE | `-` | 1 | **R** | `toggle_2005_2006`（W1 / W2 指示） |

### 组 3：APU（23 项）

| 名称 | 地址 | 属性 | 位名（bit7 → bit0） |
|---|---|---|---|
| P1VOL / P1SWP / P1TLO / P1THI | `$4000-$4003` | W | duty / loop / const / vol；E / period / neg / shift；timer-lo；len / timer-hi |
| P2VOL / P2SWP / P2TLO / P2THI | `$4004-$4007` | W | 同上 |
| TRLIN / TR— / TRTLO / TRTHI | `$4008-$400B` | W（$4009 保留位） | ctrl / reload；—；timer-lo；len / timer-hi |
| NSVOL / NS— / NSFRQ / NSLEN | `$400C-$400F` | W（$400D 保留位） | loop / const / vol；—；mode / period；len |
| DMFREQ / DMDAC / DMADDR / DMLEN | `$4010-$4013` | W | irq / loop / rate；dac；addr；len |
| APUCTRL | `$4015` | W | — — — DMC NOI TRI P2 P1 |
| APUSTAT | `$4015` | **R**（派生） | frame-IRQ / dmc-IRQ / 5 个 length≠0 |
| FRAMECTR | `$4017` | W | mode / IRQ-disable / — |

### 组 4：I/O（3 项）

| 名称 | 地址 | 属性 | 说明 |
|---|---|---|---|
| OAMDMA | `$4014` | WO | 写即触发 DMA（危险操作，二次确认） |
| JOYPAD1 | `$4016` | W | strobe；读为手柄状态（本窗口不读） |
| JOYPAD2 | `$4017` | R | 读为手柄 2 状态 |

> 注：`$4017` 读写不对称是核心既有行为（`ines_host_read` → joypad、`ines_host_write` → APU 帧计数器），
> 窗口依此分列两行并在说明列标注。

---

## 5. 布局

字体沿用 `idbg_memory_font()`（Courier New 12pt，`charW ≈ 7.2`、行高 14），坐标左上原点
`isFlipped = YES`，与 `iNESMemoryView` 完全同构。

```
NAME       ADDR    VALUE  L   BIT7 ......... BIT0        说明
------------------------------------------------------------------------
  CPU                                                          [可折叠]
A           -        72          [ ][ ][ ][ ][ ][ ][ ][ ]  累加器
P           -        24          [N][V][R][B][D][I][Z][C]  状态寄存器
PC          -      E192  L       [7][6][5][4][3][2][1][0]  程序计数器
                     H   [F][E][D][C][B][A][9][8]
  PPU
PPUCTRL   $2000      B0          [N][M][S][B][s][I][n][n]  NMI/图样/尺寸
PPUADDR.V $2006V   225F  L       [X][X][X][X][X][X][X][X]  当前 VRAM 地址
                     H   [0][Y][Y][Y][N][N][Y][Y]
------------------------------------------------------------------------
```

列宽（字符数，固定）：`NAME 10 | ADDR 7 | VALUE 6 | 字节标记 1 | 位格 24(=8×3) | 说明 26`，共 74 列。

> **说明列是多语的**：定义表存 `note_zh`（简）/ `note_zh_tw`（繁）/ `note_en`（英）三套字面量，
> 按界面语言选一套（`zh-TW/HK/Hant` → 繁体，其它 `zh*` → 简体，其余 → 英文），
> **不进语言文件**（属术语类描述，见 `docs/i18n-plan.md` §10.1）。
> 因此**英文说明必须 ≤ 26 字符**，否则会被说明列裁切。文本在建视图与切换语言时
> 各转换并缓存一次（与 win32 `wReg_InitTexts()` 同构），绘制热路径不再做编码转换。

- **位格每行只画 8 位**：16 位寄存器拆成**两个显示行** —— 首行 `L` 画低字节（bit7..0），
  次行 `H` 画高字节（bit15..8）；值列仍是 16 位整值显示与编辑（4 个 nibble）。8 位寄存器
  只有一行、无 `L/H` 标记。
- 位格保留位名字母；**字色/字重表达位值**：值 1 = 常色 + 粗体，值 0 = 浅灰 + 常规
  （双重冗余，不依赖单一颜色，深色模式 / 色弱同样可读；Courier New Bold 与常规体等宽，
  不破坏网格）。只读位在灰底之上同样区分明暗。
- 位格单字符缩写；**单击只移动光标、不改写值**
- 值列右对齐；64 位项不占位格，值放说明列
- **四周留空半个字符**（`padX = charW/2`、`padY = charH/2`），头部与正文整体内缩，
  命中测试同步偏移
- 窗口初始尺寸：`suggestedContentSize` = `74 列 × charW + 2×padX + 滚动条宽 ≈ 554 × 574`，
  带纵向滚动（显示行数 ≈ 4 组标题 + 46 项 + 4 个 16 位项的次行 = 54 行）
- 未载入 ROM：整块灰底 `IDBG_OFF_GRAY`，与现有 3 个内存视图一致

### 5.1 选中态 / 可写性配色（值列与位格都可选中，视觉必须区分）

**规则：选中与可写是两个正交维度** —— 任何值列、任何位格都能被选中（便于查看与复制），
但只有「可写」才用反白表达「可改」，只读一律灰底。

| 状态 | 值列 / 位格外观 | 说明 |
|---|---|---|
| 未选中 · 可写 | 正常：黑字白底（深色模式白字深底），位格 `[]` 描边常色 | 默认态 |
| **选中 · 可写** | **反白**：填充为选中色（`selectedTextBackgroundColor` 或 `controlAccentColor`），文字反色为 `selectedTextColor`；当前 nibble / 当前位再加**更亮的焦点框** | 明确表达「这里能改」 |
| 未选中 · 只读 | **灰底**：填充 `IDBG_OFF_GRAY`（≈ `#F0F0F0`）；**值列字色与可写值一致**（黑/深色 + 变化红），位格字母灰 | 底色一眼看出不可改，值本身仍易读 |
| **选中 · 只读** | **灰底保留** + 只加焦点描边（1px accent 虚线/实线框），**绝不反白** | 能选中、能复制，但视觉上明确「改不了」 |
| 行内单个只读位（可写寄存器中的保留位，如 `$4009` / `$400D` 保留位、P 的某些位） | 该位单独灰底，同行其它可写位保持常色 | 位级粒度区分 |
| 编辑中（仅可写） | 反白 + 当前 nibble 闪烁/实心光标 | 见 §6 细节 1 |

要点：

1. 灰底用与「未载入 ROM」一致的 `IDBG_OFF_GRAY`，全项目统一。
2. 焦点框与填充分离：填充表达**可写性**，描边表达**选中位置**；因此选中只读项时不会误判为可编辑。
3. 组标题行（CPU/PPU/APU/IO）永不反白；折叠态用三角标记 `▸ / ▾`。
4. 深色模式（mac 10.14+）下用系统动态色 `selectedTextBackgroundColor` / `disabledControlTextColor`，
   不硬编码，跟随 `iNESMemoryView` 现有取色方式。

---

## 6. 交互设计

| 操作 | 行为 |
|---|---|
| **单击值列** | 选中该行（只读项**同样可选中**，仅灰底 + 焦点框）；**可写项**才进入值列编辑态（8 位 2 nibble / 16 位 4 nibble，当前 nibble 反白光标） |
| **0-9 / A-F** | 可写项：写入当前 nibble → 自动右移；最后一位输完**自动提交**并保持在原行。**只读项：忽略 + Tips「该寄存器只读」** |
| **Enter** | 提交编辑缓冲（投递一次写请求）；只读项无编辑缓冲，忽略 |
| **Esc** | 取消值列编辑，恢复快照值；位格态则退出编辑态（见细节 1） |
| **← / →** | 值列态：在 nibble 间移动；位格态：在位间移动（`→` 往低位、`←` 往高位，越界停在两端） |
| **↑ / ↓** | 上/下一行（**可落在只读行**，只选中、灰底；保持当前列：值列仍在值列、位格仍在位格，位号不变；自动跳过折叠组内的行与组标题） |
| **Tab / Shift-Tab** | 下一个 / 上一个**可写**行（保持当前列；只读行被跳过，因为无法编辑） |
| **单击位格** | **只选中**：当前行切到该行、光标落在该位 —— **可写位与只读位都可选中**（只读位灰底 + 焦点框），**不改写值** |
| **0 / 1（位格态）** | 可写位：把该位置 0 / 1 并**立即提交**一次写请求，光标自动左移一位（便于从高位往低位连续输入）。**只读位：忽略 + Tips「该位只读」** |
| **其它按键（位格态）** | **忽略**，并弹出短暂 Tips「只能输入 0 / 1」（0.8s 淡出，自绘或 `NSHelpManager` 风格小气泡） |
| **空格** | 值列态：切换到位格态（光标落在最高可写位）；位格态：取反当前位并提交 |
| **双击组标题** | 折叠 / 展开该组（默认全展开） |
| **滚轮 / PageUp / PageDown / Home / End** | 滚动（复用 `iNESMemoryView` 的 `scrollWheel:` 逻辑） |
| **右键菜单**（P2） | 复制值 / 复制整表到剪贴板 |

关键体验细节：

1. **编辑冻结**：正在编辑的行停止刷新（否则 50ms 刷新会冲掉输入），行首显示 `*` 标记。
   - **值列态**：多行输入（nibble 逐个敲），最后一位输完自动提交；`Enter` 提前提交、`Esc` 取消并恢复快照值。
   - **位格态**：单比特输入，敲 `0`/`1` 即改即提交，**无需 `Enter`**；`Esc` 仅退出编辑态（已提交的位不回滚，
     说明列提示「按位修改为立即生效」）。
   - **只读项不进入编辑态**：选中只读值列/只读位时 `_editActive = NO`，行首不显示 `*`，也不冻结刷新；
     任何输入一律忽略并弹 Tips，避免用户误以为在输入。
2. **变化高亮**：与上次快照比较，值变化的行/位用红色绘制，保持 0.5s 后回落 —— 运行时一眼看出哪些寄存器正被程序改写。
3. **运行态提示**：窗口底部提示「暂停后修改更可靠」；运行时修改会在下一帧被程序覆盖属正常现象。
4. **危险操作确认**：`$4014` OAMDMA 写前弹 `NSAlert` 一次（按住 Shift 可跳过）。
5. **编辑提交走 delegate** → `ines_dbg_post_reg_write()` → 帧首 `ines_dbg_apply_writes()` 应用，UI 线程绝不碰 host。

---

## 7. 写入语义与风险

| 风险 | 处理 |
|---|---|
| `ines_apu_write()` 内部会 `run_until(CUR_CPU_CYCLES)` | 在帧首与内存写同一时机调用，等价于"CPU 在帧边界写端口"，安全；**实现前先核对 `CUR_CPU_CYCLES` 宏定义** |
| `$2002` / `$4015` 读有副作用 | 快照直拷字段，窗口只读且不触发端口读 |
| 改 PPU T/V 不经过 $2006 双写 | 属"调试器改内部状态"，界面标注，不连带改另一个寄存器 |
| 改 PC / SP / P 后程序继续跑会立刻覆盖 | 明确提示，属调试器预期行为 |
| 快照是帧末值 | 一帧内被多次改写的端口（如 $2005/$2006 toggle）只显示末值，说明列标注 |

---

## 8. 实现骨架

`mac/iNESRegisterView.h/.m`：

```objc
// 寄存器定义表（静态 const，纯数据，便于与将来的 win32 实现对齐）
typedef struct {
    int          group;          // IDBG_GRP_CPU / PPU / APU / IO
    const char*  name;           // "PPUCTRL"
    const char*  addr_text;      // "$2000" / "  -"
    int          reg_id;         // 投递写请求用
    int          width;          // 8 / 16 / 64
    unsigned int writable_mask;  // 可写位掩码（64 位项恒 0）
    const char*  bit_letters[16];// 位缩写（高位 → 低位）
    const char*  bit_names[16];  // 位全称（选中时显示在说明列）
} idbg_reg_def_t;

@interface iNESRegisterView : NSView
@property (nonatomic, assign) const ines_dbg_snapshot_t* snapshot;
@property (nonatomic, weak)   id<iNESRegisterViewDelegate> delegate;
+ (NSSize)suggestedContentSize;
- (BOOL)hasContent;
@end
```

内部状态：`_startLine`、`_curRow`、`_cursorColumn`（`IDBG_COL_VALUE` / `IDBG_COL_BIT`）、`_editActive`、
`_editValue`、`_editNibble`、`_bitIndex`（0 = 最低位）、`_tipsText` + `_tipsExpire`（0.8s 淡出）、
`_lastValues[]`、`_lastChangeTime[]`、`_folded[4]`；绘制 / 滚动 / 字体逻辑与 `iNESMemoryView` 保持一致。

命中测试：按 `x` 落在哪一列决定光标列（值列 / 位格列），单击统一走 `selectRow:column:bit:`
（**只读项也允许选中**），**不触发任何写请求**；写请求只在 `0/1`（位格态）、十六进制满位或 `Enter`（值列态）、
空格时投递，且投递前先判 `writable_mask`：不可写则丢弃并弹 Tips。

绘制分层（保证选中/可写两维度可区分）：

```
1) 行背景（交替行底色 / 组标题）
2) 单元格"可写性"填充：可写 = 无填充(未选中) 或 反白(选中)；只读 = IDBG_OFF_GRAY 恒填充
3) 文本：可写 selectedTextColor / 只读 disabledControlTextColor
4) 焦点描边：accent 色 1px，只标位置，不改变 2) 的填充语义
5) 变化高亮层（红，0.5s）
```

---

## 9. 验证方式（本机可直接做）

1. `cmake --build build -j` 构建 `iNES.app`（mac 目标可编译、可运行）。
   —— 2026-09-15 已跑通：`make -j8` 零错误零告警；载入 `bin/ROM/90tank.nes` 后经「工具 → 调试视图 →
   寄存器查看…」打开窗口，标题「寄存器查看器」，CPU/PPU/APU 各行值、位格字母、说明列渲染正常。
2. `osascript` 点菜单「工具 → 调试视图 → 寄存器…」打开窗口；读窗口 `size` / `position` 校验初始布局
   （无法直接截终端截图，已知限制）。
3. 载入 `bin/ROM/90tank.nes`：对比暂停 / 运行时各寄存器值；改 A / PC / PPUCTRL 后单帧执行，观察行为变化。
4. 写 `/tmp` 小 harness：调用 `ines_dbg_capture()` 后逐字段比对快照与 `host` 字段，确保 51 项映射无一错位
   （core 在 macOS 可编译）。
5. `read_lints` 清零。

---

## 10. 决策记录（已确认，2026-09-15）

| # | 决策项 | 结论 | 落地方式 |
|---|---|---|---|
| 1 | 位格区 | **保留** | 布局按 §5 的 `NAME 10 / ADDR 7 / VALUE 6 / 位格 32 / 说明 30+`，窗口宽 ≈ 648px；右键菜单保留一个「显示位格」开关作为可选收敛 |
| 2 | PPU T / V | **可写** | 直改内部字段 `index_t` / `index_v`（不经过 `$2006` 双写，避免连带把 T 拷到 V）；说明列标注「内部状态」；`fine_x` 同步可写 |
| 3 | CPU P 的 B / R 位 | **允许改** | `P` 行 `writable_mask = 0xFF`；说明列标注「R 恒 1、B 仅入栈有效」，不阻止写入 |
| 4 | `$4014 / $4016 / $4017` | **按推荐方案纳入 I/O 组** | `$4014` OAMDMA 写前弹 `NSAlert` 二次确认（按住 Shift 跳过）；`$4016` 写走 `ines_host_write`；`$4017` 只读（读为手柄 2 状态）、写走 `ines_apu_write` 的帧计数器语义 |
| 5 | 变化高亮时长 | **0.5s 可行** | 按 §6 细节 2 实现，值变化的行/位红色绘制，0.5s 后回落 |

6. **位格交互改为「先选中、后输入」**（2026-09-15 追加）：单击位格只选中（与单击值列一致），
   位格态仅接受 `0/1`（立即提交），其它按键忽略并弹 Tips「只能输入 0 / 1」。详见 §6。
7. **选中与可写解耦**（2026-09-15 追加）：值列与位格**全部可选中**（含只读项）；可写选中态 = **反白**，
   只读 = **灰底**（选中只加焦点描边，绝不反白）；填充表达**可写性**，描边表达**选中位置**，两者正交。详见 §5.1。
8. **四周留空**（2026-09-15 实现后追加）：内容区四周留空半个字符，头部/正文/行底整体内缩，
   命中测试同步偏移；窗口尺寸在原列宽基础上加 `2×padX`。
9. **位格 0/1 表达**（2026-09-15 实现后追加）：两案取舍后采用「保留位名字母 + 颜色/字重表达位值」——
   值 1 = 常色粗体、值 0 = 浅灰常规。否决「直接显示 0/1 + 缩写挪说明列」：说明列已被语义文本占用，
   且会同时丢掉「这位是什么」的即时信息。加粗作第二冗余，深色模式 / 色弱不依赖单一颜色。
10. **16 位寄存器位域拆两行**（2026-09-15 实现后追加）：位格每行只画 8 位，16 位项 = `L`（低字节）
    + `H`（高字节）两个显示行；值列仍按 16 位整值显示与编辑。位格区从 48 列收窄到 24 列，
    总宽 97 → 74 列（≈710px → ≈554px）。
11. **行级脏矩形刷新**（2026-09-15 性能优化）：调试管理器 50ms tick 原先对所有可见视图无条件
    `setNeedsDisplay:YES`，寄存器视图每 tick 全量重绘 ~1700 次文本调用 + 全窗口表面上传，
    导致主窗口卡顿（采样：drawRect 占主线程 ~13%，`CABackingStore` 同步等待占 ~33%）。
    改为 `refreshForTick` 行级判定：只对**值变化的行**（含 $2005 派生说明列与高亮回落补拍）
    `setNeedsDisplayInRect:`；暂停且无变化时零绘制零上传（实测采样归零）。结构性变化
    （滚动/折叠/选中/编辑/Tips 淡出）仍走 `setNeedsDisplayAll` 整幅。配套微优化：等宽字体
    单字符宽恒为 `_charW`，去掉全部 `sizeWithAttributes` 测量；位字母/L-H 标记/位号改用
    预生成单字符缓存表，消除每 tick ~300 个临时字符串。运行态实测：drawRect 样本 174→44，
    同步等待 453→179。

确认后无其他开放问题，可按本文档实现。
