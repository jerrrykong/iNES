# 编码规范

目标：保持与既有代码（VC2008 时代风格、C89 兼容写法）一致，同时便于在 MSVC/GCC 与 32/64 位下构建。

## 1. 语言与依赖

- 纯 C，**不使用 C++ 特性**；头文件用 `#ifdef __cplusplus / extern "C"` 包裹
- 不引入第三方库；只用 C 运行时与系统 API
- 尽量保持 C89 风格的写法（变量声明在块首、无 `//` 之外的新语法依赖），便于老编译器兼容
- Mapper 等模块不得使用浮点、动态分配（私有数据除外）、IO/阻塞调用

## 2. 文件组织

- 头文件必须有 `#ifndef __XXX_H__ / #define / #endif` 保护
- `.c` 首个 include 是对应自身头文件（若有），随后是 `comm/idef.h`
- Mapper 文件固定顺序：

```c
#include "../../comm/idef.h"
#include "../../comm/log.h"
#include "../nes.h"
#include "../mapper.h"
```

- 一个模块一个 `.c/.h` 对；Mapper 例外：一个编号一个文件

## 3. 命名

| 类别 | 规则 | 示例 |
|---|---|---|
| 类型 | `ines_<名>_t`，结构体标签 `_ines_<名>_` | `ines_host_t`、`struct _ines_host_` |
| 函数（外部） | `ines_<模块>_<动作>` | `ines_host_doframe`、`ines_ppu_set_mirror_type` |
| 函数（文件内） | `static`，可不带 `ines_` 前缀 | `MMC3_set_cpu_bank`、`mapper4_reset` |
| 宏 / 常量 | 全大写下划线 | `NES_STATUS_RUNNING`、`MMC_IRQ_MASK` |
| 枚举型常量 | 全大写 | `LOG_DBG`、`MIRROR_HORZ` |
| 变量 | 小写下划线 | `p_host`、`vrom_1k_num` |
| 指针参数 | `p_` 前缀 | `p_mapper`、`p_ppu` |
| Mapper 私有数据 | `<NAME>_data_t` + `mapper2<NAME>data()` | `MMC3_data_t`、`mapper2MMC3data` |

## 4. 类型

- 一律使用 `ines_*` 类型，不直接写 `int` / `char` / `unsigned short`
- 布尔用 `ines_bool_t` + `ines_true` / `ines_false`
- 字符与字符串用 `ines_char_t` / `ines_cstr_t`，字面量必须包 `ISTR()`
- 表示尺寸用 `ines_size_t`，表示 64 位计数用 `ines_int64_t`
- 打印 64 位用 `ISTR(PRI64)ISTR("d")` 组合（Windows `I64d` / Linux `lld`）

## 5. 格式

- 缩进：与所在文件保持一致；`core/`、`win32/` 现有代码使用 **Tab**（新文件建议沿用）
- 花括号：**Allman 风格**（`{` 单独一行），与现有代码一致
- `switch` 的 `case` 与 `switch` 同缩进一级，每个 `case` 显式 `break`
- 函数之间空 1-2 行；逻辑段落之间空一行
- 行宽不强制，但避免超过 120 列

## 6. 注释

- 注释使用**中文**（与现有注释一致），术语、标识符保留英文
- 文件头写清模块职责；Mapper 文件头写清硬件/寄存器要点（参考 `core/mapper/4.c`）
- 解释"为什么"而不是"做什么"；复杂位运算、时序、补丁(patch)必须注释
- 临时禁用的代码用 `#if 0` 块并注明，避免大段 `/* */` 注释代码

## 7. 日志

- 统一 `INES_LOG(级别, 模块, ISTR("..."), ...)`，不要用 `printf`
- 级别选择：调试细节 `LOG_DBG`、关键流程 `LOG_INF`、异常但可恢复 `LOG_WAR`、失败 `LOG_ERR`
- 高频路径（逐帧/逐扫描线）不要打日志，或用 `ines_check_level()` 先判断
- 模块选择：系统 `MOD_SYS`、Mapper `MOD_MMC`、网络 `MOD_NET`，其余按模块
- 格式化串必须包 `ISTR()`，参数中的字符串同理

## 8. 内存与资源

- 成对出现：`ines_host_init/free`、`ines_rom_init/free`、`ines_mapper_init/free`
- 结构体私有数据用 `INIT_MAPPER_DATA_ST`（已清零）或 `ines_alloc_st`，释放用 `ines_free`
- `free` 后置 `NULL`，避免悬垂
- 优先使用宿主已提供的静态缓冲（如 `SRAM[]`），不自行开大数组

## 9. Mapper 专项

- 只用 `ines_set_*_bank_*` 系列改 bank，不直接改 PPU/CPU 内部映射
- bank 号对 `prom_8k_num` / `vrom_1k_num` 取模（或用 `prom_8k_mask` / `vrom_1k_mask`）
- 必须同时处理"有 VROM"与"无 VROM(VRAM)"两种情形
- 四屏卡带不改写镜像
- `create` 返回 `ines_true` 才表示已实现
- 详见 [mapper-guide.md](mapper-guide.md)

## 10. 平台相关

- Windows / POSIX 分支用 `#ifdef WIN32 ... #elif defined(linux) ... #endif`
- 涉及 socket 时用 `socket_t`（Windows 为 64 位 `SOCKET`，POSIX 为 `int`），**不要用 `int` 存句柄**
- 时间用 `ines_int64_t` 微秒；Windows 用 `QueryPerformanceCounter`，POSIX 用 `gettimeofday`
- 不要假设指针宽度为 4 字节，不要把指针存进 `int`/`long`；需要随指针大小变化的整数用 `size_t` / `intptr_t`
- 不要假设 `long` 为 64 位（Windows  LLP64 下仍为 32 位）

## 11. 源文件编码与换行

- **UTF-8 无 BOM + LF**（`win32/iNES.rc` 例外：带 BOM，见 `docs/build.md`）
- 不要提交 GBK/UTF-16 或 CRLF 文件；可用 `tools/convert_encoding.ps1` 校验/转换
- 不要在代码里写平台相关的换行或转义

## 12. 提交前自检

- [ ] 新文件已纳入构建（Mapper 会自动 GLOB，其它源文件需改 `CMakeLists.txt`）
- [ ] Win32 与 x64 均编译通过，无新增警告（`/W3`）
- [ ] 无硬编码路径、无调试残留（如写死的 ROM 路径）
- [ ] 编码为 UTF-8 无 BOM、换行为 LF
- [ ] 修改了公共 API 时同步更新 `docs/api.md`
- [ ] 新增/修复 Mapper 时同步更新 `docs/mapper-list.md` 与 `core/mapper_creator.c` 的 `implemented` 标注
