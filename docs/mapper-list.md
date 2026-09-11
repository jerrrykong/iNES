# 已实现 Mapper 清单

统计口径：扫描 `core/mapper/*.c`（256 个文件）并结合 `core/mapper_creator.c` 注册表的 `/* implemented */` 标注。

## 1. 总览

| 项目 | 数量 |
|---|---|
| Mapper 文件总数 | 256（`0.c` ~ `255.c`） |
| 注册表标注 `implemented` | 26 |
| 另有实质代码但未标注 | 1（Mapper **163**） |
| 占位桩（未实现） | 229 |

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
| 18 | 227 | `MMC18` | ✅ | ✅ | ✅ | Jaleco SS88006 |
| 19 | 662 | `Namco163_data_t` | ✅ | ✅ | ✅ | **Namco 163（Namcot 106）**：12 窗口 CHR/NT、CIRAM 当 CHR、ROM nametable、8KB WRAM + 2KB×4 写保护、15 位 CPU 周期 IRQ、8 通道波表扩展音（经 APU 扩展槽）；[方案与增益标定](mapper-19-plan.md) |
| 21 | 31 | `VRC24_data_t` | ✅ | ✅ | ✅ | Konami **VRC4a/c**（VRC 系，逻辑在 `vrc.h` 共享） |
| 22 | 31 | `VRC24_data_t` | | | ✅ | Konami **VRC2a**：CHR 2KB 粒度、无 IRQ、无 WRAM |
| 23 | 31 | `VRC24_data_t` | ✅ | ✅ | ✅ | Konami **VRC2b/VRC4f** |
| 24 | 28 | `VRC6_data_t` | ✅ | ✅ | ✅ | Konami **VRC6a**：3 路扩展音已由 VRC6 引擎发声（经 APU 扩展输入槽） |
| 25 | 30 | `VRC24_data_t` | ✅ | ✅ | ✅ | Konami **VRC2c/VRC4b/d/e** |
| 26 | 28 | `VRC6_data_t` | ✅ | ✅ | ✅ | Konami **VRC6b**：A0/A1 交换、带 8K WRAM；3 路扩展音已发声 |
| 85 | 25 | `VRC7_data_t` | ✅ | ✅ | ✅ | Konami **VRC7**：FM(YM2413) 简化内核已接入（vrc.h §5b，单声道经 APU 扩展输入槽） |
| 163 | 178 | `MMC163` | | ✅ | ❌ | 有完整实现（含 `reset/writehigh/readlow/writelow/hsync/fini`），但注册表未标注 `implemented` |
| 210 | 142 | —（无私有状态） | | | ✅ | **Namco 175 / Namco 340**（Namco 163 的降本版，同一个 iNES 号）：8 窗口 1KB CHR、3 槽 8KB PRG、340 可选 H/V/单屏镜像；175/340 变体不区分（详见 `core/mapper/210.c` 文件头） |

> **实机验证状态（2026-09-12）**：**19**（Namco 163）已由用户实机验证，游戏运行无问题；
> **210**（Namco 175/340）暂无可用 ROM，尚未实机验证（仅通过编译与静态检查）。
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

未实现但值得注意的是 **14**、**17**：它们的桩里已有基本的 bank 设置骨架，可以直接作为新实现的起点。

## 3. 特性矩阵（已实现部分）

| 特性 | 使用的 Mapper |
|---|---|
| 扫描线 IRQ（`hsync` + `ines_cpu_IRQ`） | 4, 5, 6, 12, 16, 18, 19 |
| `hsync`（无 IRQ，用于 CHR 切换特效） | 163 |
| PRG + CHR 全切换 | 1, 4, 5, 6, 12, 16, 18, 19, 163, 210 |
| 仅 PRG 切换 | 2, 7, 11, 15 |
| 仅 CHR 切换 | 3, 13 |
| 无切换 | 0 |
| `PPU_latch`（MMC5 图形扩展） | 5 |
| `PPU_latch_FDFE`（`$FD/$FE` 锁存） | 9, 10 |
| 内部 NT RAM 当作 CHR（`pattern_type = 2`） | 19 |
| nametable 窗口指向 CHR 页（ROM nametable，`ines_set_nt_chr_bank_n`） | 19 |
| 自定义 SRAM（`custom_sram = 1`，含写保护） | 19 |
| 自由镜像排布（`ines_ppu_set_mirror`，含单屏选择） | 1, 6, 7, 16, 18, 19, 21-26, 210 |
| 扩展音（APU 扩展输入槽） | 19, 24, 26, 85 |
| 私有数据 + `fini` | 1, 4, 5, 6, 9, 10, 12, 16, 18, 19, 163 |

## 4. 桩文件（占位实现）

桩的统一形态（`core/mapper/17.c` 为例）：

```c
static void mapper17_reset(ines_mapper_t* p_mapper)
{
    ines_host_t*  p_host = mapper2host(p_mapper);
    if(p_host->prom_8k_num >= 4)
        ines_set_prom_bank_4(p_host, 0, 1, 2, 3);
    else
        ines_set_prom_bank_4(p_host, 0, 1, 0, 1);
    if(p_host->vrom_1k_num > 0)
        ines_set_vrom_bank_8(p_host, 0, 1, 2, 3, 4, 5, 6, 7);
}

static void mapper17_writehigh(ines_mapper_t* p_mapper, ines_word_t addr, ines_byte_t val)
{
    ines_host_t*  p_host = mapper2host(p_mapper);
    (void)p_host;
}

ines_bool_t  mapper17_create(ines_mapper_t* p_mapper)
{
    p_mapper->reset     = mapper17_reset;
    p_mapper->writehigh = mapper17_writehigh;
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
