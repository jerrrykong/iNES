# Mapper 开发规范

本文档说明如何为 iNES 新增或修复一个卡带 Mapper。所有 Mapper 实现都放在 `core/mapper/` 下，一个编号一个文件。

## 1. Mapper 是什么

NES 卡带上的"存储器管理芯片"。CPU 只能直接寻址 `$8000-$FFFF`（32KB PRG）和 PPU 的 `$0000-$1FFF`（8KB CHR），Mapper 通过**切换 bank** 把大容量 ROM 映射到这些窗口，并提供：

- PRG bank 切换（`ines_set_prom_bank_*`）
- CHR bank 切换（`ines_set_vrom_bank_*` / `ines_set_vram_bank_n`）
- 名称表镜像控制（`ines_ppu_set_mirror_type`）
- 扫描线计数器 IRQ（`hsync` + `ines_cpu_IRQ`）
- 额外的卡带 RAM、扩展音源、EEPROM 等（按需）

## 2. 接口：函数指针表

`ines_mapper_t`（`core/mapper.h`）就是 Mapper 的全部接口：

| 成员 | 调用时机 / 用途 | 必须实现 |
|---|---|---|
| `p_data` / `data_len` | 由 `INIT_MAPPER_DATA_ST` 分配的私有数据 | 需要状态时才用 |
| `custom_sram` | 使用自定义 SRAM 时置位 | 否 |
| `fini` | 释放私有数据 | 分配了 `p_data` 时必须 |
| `reset` | 上电 / 软件复位，**必须把 bank 恢复到初始状态** | 是 |
| `hsync` | 每条扫描线（0-239 可见区）调用，用于 IRQ 计数、CHR 切换特效 | 否 |
| `vsync` | 每帧结束 | 否 |
| `readlow` | CPU 读 `$6000-$7FFF`（SRAM 区） | 否 |
| `writelow` | CPU 写 `$6000-$7FFF` | 否 |
| `writehigh` | CPU 写 `$8000+`（Mapper 寄存器） | 绝大多数需要 |
| `PPU_latch` | MMC5 类：PPU 取图时锁存 | 仅 MMC5 |
| `PPU_latch_FDFE` | MMC2 类：`$FD/$FE` 触发的 CHR 切换 | 仅 MMC2/MMC4 |
| `savestate` | 即时存档 | 否 |

调用端统一用宏包装（`ines_mapper_writehigh` 等），内部会自动判空，因此**只挂接需要的回调**即可。

## 3. 文件与命名规范

| 项目 | 规范 | 示例 |
|---|---|---|
| 文件 | `core/mapper/<编号>.c`，编号即 Mapper 号 | `core/mapper/4.c` |
| 入口函数 | `ines_bool_t mapper<N>_create(ines_mapper_t* p_mapper)` | `mapper4_create` |
| 私有结构体 | `_<NAME>_data_` + `typedef ... <NAME>_data_t`，命名用芯片名 | `MMC3_data_t` |
| 数据访问宏 | `mapper2<NAME>data(mapper)` | `mapper2MMC3data(p_mapper)` |
| 静态辅助函数 | `mapper<N>_<动作>` 或 `<NAME>_<动作>` | `mapper4_reset`、`MMC3_set_cpu_bank` |
| 内部静态函数 | 一律 `static`，禁止污染全局符号 | —— |

文件头部固定为 4 个 include：

```c
#include "../../comm/idef.h"
#include "../../comm/log.h"
#include "../nes.h"
#include "../mapper.h"
```

## 4. 私有数据

需要用状态（寄存器、计数器）时，定义结构体并在 `create` 里用宏分配：

```c
struct _MMC3_data_ { ... };
typedef struct _MMC3_data_  MMC3_data_t;

#define mapper2MMC3data(mapper)  (MMC3_data_t*)((mapper)->p_data)

ines_bool_t mapper4_create(ines_mapper_t* p_mapper)
{
    INIT_MAPPER_DATA_ST(p_mapper, MMC3_data_t);   // malloc + memset 0
    p_mapper->reset     = mapper4_reset;
    p_mapper->writehigh = mapper4_writehigh;
    p_mapper->hsync     = mapper4_hsync;
    p_mapper->fini      = mapper4_fini;
    return ines_true;
}

void mapper4_fini(ines_mapper_t* p_mapper)
{
    ines_free(p_mapper->p_data);
    p_mapper->p_data = NULL;
}
```

规则：

- `INIT_MAPPER_DATA_ST` 已把内存清零，**不要**再手动 `memset`
- 有 `p_data` 就必须提供 `fini`
- 无状态 Mapper（如 NROM）可以不分配 `p_data`

## 5. `create` 的返回值即"是否实现"

这是本项目的关键约定：

| 返回值 | 含义 |
|---|---|
| `ines_true` | 该 Mapper 已实现 |
| `ines_false` | 占位桩（未实现），宿主加载 ROM 时会报"不支持的映射模式" |

因此**实现完成时务必把 `return ines_false;` 改成 `return ines_true;`**，并在 `core/mapper_creator.c` 的注册表对应行加上 `/* implemented */` 注释。

## 6. 常用 Host API

Mapper 只应通过这些 API 操作硬件，不要直接改 PPU/CPU 内部字段（读 `ppu.reg_ctrl_2` 之类是例外，需谨慎）。

### bank 切换（`core/nes.h`）

```c
ines_set_prom_bank_4(host, b4, b5, b6, b7);   // $8000/$A000/$C000/$E000，单位 8KB
ines_set_prom_bank_5(host, b3, b4, b5, b6, b7);
ines_set_prom_bank_n(host, n, bn);            // n=3~7 对应 $6000+n*0x2000

ines_set_vrom_bank_8(host, b0..b7);           // PPU $0000-$1FFF，单位 1KB
ines_set_vrom_bank_n(host, n, bn);            // n=0~7
ines_set_vram_bank_n(host, n, bn);            // 无 VROM（CHR-RAM）时用

ines_set_sram_bank_n(host, n, bn);            // SRAM $6000-$7FFF
```

约定：

- bank 号需对页数取模：`p_host->prom_8k_num`、`p_host->vrom_1k_num` 是当前 ROM 的页数；需要手动屏蔽时用 `prom_8k_mask` / `vrom_1k_mask` 或 `addr_mask()`
- **有 VROM 用 `ines_set_vrom_bank_*`；没有 VROM（`vrom_1k_num == 0`）时用 `ines_set_vram_bank_n`**，二者都要处理（见 `mapper/4.c` 的 `MMC3_set_ppu_bank`）

### 镜像（`core/ppu.h`）

```c
ines_ppu_set_mirror_type(&p_host->ppu, MIRROR_HORZ);   // VERT / HORZ / SINGLE_SCREEN / FOUR_SCREEN
ines_ppu_set_mirror(&p_host->ppu, n0, n1, n2, n3);     // 四屏/自定义
```

注意：四屏卡带（`rom.mirror_type == MIRROR_FOUR_SCREEN`）不应被 Mapper 改写镜像。

### IRQ（`core/cpu.h`）

```c
ines_cpu_IRQ(&p_host->cpu, MMC_IRQ_MASK, ines_true);    // 拉高（请求中断）
ines_cpu_IRQ(&p_host->cpu, MMC_IRQ_MASK, ines_false);   // 清除
```

### 取宿主指针

```c
ines_host_t* p_host = mapper2host(p_mapper);
```

## 7. 从桩到实现的步骤

1. **复制桩模板**（见 §8）为 `core/mapper/<N>.c`
2. **查证硬件资料**：该编号的寄存器定义、bank 粒度、是否有 IRQ/WRAM
3. **定义私有结构体**：寄存器、bank 号、计数器
4. **实现 `reset`**：设置初始 bank（通常是最后一页固定在 `$C000/$E000`，第 0 页在 `$8000`）、初始镜像、清 IRQ
5. **实现 `writehigh`**：按地址 `switch`，解析寄存器并刷新 bank/镜像
6. **按需实现** `hsync`（IRQ 计数/CHR 特效）、`readlow`/`writelow`（SRAM/扩展寄存器）、`PPU_latch*`
7. **把 `create` 改为 `return ines_true;`**
8. **在 `core/mapper_creator.c`** 的 `extern` 区（已由模板生成）与注册表对应项加 `/* implemented */`
9. **构建并实机验证**：至少跑通 2-3 个使用该 Mapper 的游戏

## 8. 模板

### 8.1 最简单的实现（无状态，以 Mapper 0 / NROM 为例）

```c
#include "../../comm/idef.h"
#include "../../comm/log.h"
#include "../nes.h"
#include "../mapper.h"

static void mapper0_reset(ines_mapper_t* p_mapper)
{
    ines_host_t*  p_host = mapper2host(p_mapper);

    if(p_host->rom.PROM_block_num > 1)
        ines_set_prom_bank_4(p_host, 0, 1, 2, 3);
    else
        ines_set_prom_bank_4(p_host, 0, 1, 0, 1);

    if(p_host->rom.VROM_block_num > 0)
        ines_set_vrom_bank_8(p_host, 0, 1, 2, 3, 4, 5, 6, 7);
}

ines_bool_t  mapper0_create(ines_mapper_t* p_mapper)
{
    p_mapper->reset = mapper0_reset;
    return ines_true;
}
```

### 8.2 带私有数据与 IRQ 的实现骨架（推荐抄 `core/mapper/4.c`）

```c
#include "../../comm/idef.h"
#include "../../comm/log.h"
#include "../nes.h"
#include "../mapper.h"

struct _XXX_data_;
typedef struct _XXX_data_  XXX_data_t;

struct _XXX_data_ {
    ines_byte_t   reg[8];
    ines_byte_t   irq_enabled;
    ines_byte_t   irq_counter;
    ines_byte_t   irq_latch;
    ines_word_t   prg0, prg1;
    ines_word_t   chr01, chr23, chr4, chr5, chr6, chr7;
};

#define mapper2XXXdata(mapper)  (XXX_data_t*)((mapper)->p_data)

static void XXX_set_cpu_bank(XXX_data_t* p, ines_host_t* p_host) { ... }
static void XXX_set_ppu_bank(XXX_data_t* p, ines_host_t* p_host) { ... }

static void mapperN_reset(ines_mapper_t* p_mapper) { ... }
static void mapperN_writehigh(ines_mapper_t* p_mapper, ines_word_t addr, ines_byte_t val)
{
    switch(addr & 0xe001) {           // 按硬件实际地址解码
    case 0x8000: ... break;
    case 0xa000: ... break;
    case 0xc000: p->irq_latch = val; break;
    case 0xe000: p->irq_enabled = 0;
                 ines_cpu_IRQ(&p_host->cpu, MMC_IRQ_MASK, ines_false); break;
    }
}

static void mapperN_hsync(ines_mapper_t* p_mapper, ines_int_t line)
{
    if(line < 0 || line > 239) return;                 // 只在可见区计数
    if(!(p_host->ppu.reg_ctrl_2 & (PPU_ENABLE_BG|PPU_ENABLE_SPR))) return;  // 关屏不计数
    ...
    ines_cpu_IRQ(&p_host->cpu, MMC_IRQ_MASK, ines_true);
}

void mapperN_fini(ines_mapper_t* p_mapper)
{
    ines_free(p_mapper->p_data);
    p_mapper->p_data = NULL;
}

ines_bool_t  mapperN_create(ines_mapper_t* p_mapper)
{
    INIT_MAPPER_DATA_ST(p_mapper, XXX_data_t);
    p_mapper->reset     = mapperN_reset;
    p_mapper->writehigh = mapperN_writehigh;
    p_mapper->hsync     = mapperN_hsync;
    p_mapper->fini      = mapperN_fini;
    return ines_true;
}
```

## 9. 编码与风格要求（Mapper 尤其注意）

- 4 空格或 Tab 缩进均可，**与所在文件保持一致**；新文件建议沿用 `core/mapper/4.c` 的 Tab 风格
- 日志统一用 `INES_LOG(LOG_DBG, MOD_MMC, ISTR("..."))`，模块选 `MOD_MMC`；字符串一律包 `ISTR()`
- 寄存器解析用 `switch (addr & mask)`，不要写长串 `if/else`
- 所有辅助函数 `static`
- 不要使用浮点数、不要 `malloc` 除私有数据以外的内存；需要缓冲就放进结构体
- 不要调用阻塞/IO 函数；`FILE*` 只允许在 `savestate` 里使用

## 10. 调试技巧

- 打开日志：`ines_set_log_level(LOG_DBG)`（GUI：菜单"工具 → 日志 → DEBUG"）
- 关注这些典型故障：
  - 画面花屏/错乱 → CHR bank 号未取模，或未处理 `vrom_1k_num == 0` 的 VRAM 分支
  - 游戏卡在标题 → PRG 初始 bank 不对（`$C000/$E000` 通常要固定到最后一页）
  - 滚动抖动/闪屏 → 镜像方式错误
  - 画面撕裂、定时错乱 → IRQ 计数时序（`hsync` 的边界与"关屏不计数"判断）
  - 存档失效 → 未挂 `readlow/writelow` 或未设置 `custom_sram`

## 11. 提交前检查表

- [ ] 文件位于 `core/mapper/<N>.c`，只包含一个 `mapper<N>_create` 入口
- [ ] 所有内部符号为 `static`
- [ ] `create` 返回 `ines_true`
- [ ] 分配了 `p_data` 就必须有 `fini` 并在其中 `ines_free`
- [ ] `reset` 能把所有 bank、镜像、IRQ 状态复位
- [ ] bank 号已对 `prom_8k_num` / `vrom_1k_num` 取模
- [ ] 同时处理"有 VROM"与"无 VROM(VRAM)"两种情形
- [ ] 四屏卡带不去改写镜像
- [ ] `core/mapper_creator.c` 对应项已加 `/* implemented */`
- [ ] Win32 与 x64 两种配置都能编译通过，无新增警告
- [ ] 至少 2 个使用该 Mapper 的游戏可正常运行
- [ ] 源码为 UTF-8 无 BOM + LF（见 `docs/coding-style.md`）

## 12. 范例：Konami VRC 家族（21/22/23/25/24/26/85，共享 `vrc.h`）

VRC2/VRC4/VRC6/VRC7 走线变体极多，但核心逻辑只有三套（VRC2/4 一套、VRC6 一套、VRC7 一套）。
本项目把它们放进共享头 `core/mapper/vrc.h`（静态 inline），7 个 `mapper<N>.c` 各自只做"配置 + 挂回调"，
是"一个头文件驱动多个编号"的现成范例，后续遇到同族多编号芯片可直接照搬：

```c
// mapper21.c 主体结构（每个编号文件只有 create 是导出符号）
#include "../../comm/idef.h"
#include "../../comm/log.h"
#include "../nes.h"
#include "../mapper.h"
#include "vrc.h"

ines_bool_t mapper21_create(ines_mapper_t* p_mapper)
{
    INIT_MAPPER_DATA_ST(p_mapper, VRC24_data_t);
    VRC24_data_t* p = mapper2VRC24data(p_mapper);
    p->is_vrc2   = 0;      // 芯片族配置：VRC2a=1（CHR 2KB 粒度、无 IRQ）
    p->reg_mask1 = 0x42;    // 引脚错位掩码 → 寄存器偏移 bit0
    p->reg_mask2 = 0x84;    // 引脚错位掩码 → 寄存器偏移 bit1
    p_mapper->fini      = vrc24_fini;
    p_mapper->reset     = vrc24_reset;
    p_mapper->writehigh = vrc24_writehigh;
    p_mapper->hsync     = vrc24_hsync;   // 无 IRQ 硬件的编号(VRC2a)不挂 hsync
    return ines_true;
}
```

要点：

- **差异参数化，而不是复制代码**：族内差异被折叠成几个配置位——
  `reg_mask1/reg_mask2`（地址线错位）、`is_vrc2`（CHR 粒度/有无 IRQ）、`is_vrc6b`（A0/A1 交换）。
- **CPU 周期驱动型 IRQ**：VRC 计数器按 CPU 周期（或 341 dots/线）递增，而宿主只在每个 `hsync`
  回调一次。`vrc.h` 的 `vrc_irq_tick()` 用 `host->cpu.total_cycles` 的真实增量做**批处理推进**，
  把差值折算成剩余量（`rem`），只引入"线内触发时刻"的量化误差、不漂移。实现周期性计数器时优先考虑这种"增量对账"写法。
- **扩展音源 = 状态捕获**：VRC6（3 路 PSG）与 VRC7（YM2413 FM）的音频寄存器在 `writehigh` 里原样存进
  `p_data`（如 `fm_reg[0x40]`），但 APU 尚无扩展声道混音接口，因此**不合成声音**。接入混音后这些状态可直接复用。
- **`custom_sram` 语义（重要）**：`mapper.c` 在 `reset` 里对 `custom_sram == 0` 的 mapper 统一挂 8K 默认 RAM
  到 `$6000-$7FFF`（`ines_set_sram_bank_n`）。因此：
  - 卡带**带 WRAM/电池存档**且无自定义逻辑 → 保持 `custom_sram = 0`（默认 RAM 即 WRAM，可存档）；
  - 卡带**没有 WRAM**，又不想让 `$6000` 出现多余 RAM → 设 `custom_sram = 1` 且**不挂** `readlow/writelow`
    （此时 `$6000` 读回 `addr>>8`、写被丢弃，等效无 RAM）；
  - mapper 需要**自己维护** `$6000`（EEPROM 等）→ 设 `custom_sram = 1` 并实现 `readlow/writelow`。
- 音频寄存器等"写后即忘"的状态若没有消耗方，也要按芯片布局保留一份镜像，便于将来扩展且省去改寄存器解码。
