# iNES

NES（FC）模拟器，由两部分组成：

- **iNES** — Win32 图形界面模拟器（窗口、菜单、调试视图、联网对战）
- **inescore** — 独立的模拟核心共享库，对外暴露 C API（`libinescore.c`），可被其它前端复用

项目原本使用 Visual C++ 2008 工程（`project/win32/iNES.vcproj`、`project/win32/iNesCore_DLL.vcproj`）构建，现已迁移为 **CMake** 构建，零第三方依赖。

---

## 目录结构

| 目录 / 文件 | 说明 |
|---|---|
| `core/` | 模拟核心：`apu` `cpu` `ppu` `nes` `rom` `joypad` `mapper`，以及 `core/mapper/*.c` 中的 256 个 mapper 实现 |
| `comm/` | 公共基础库：`buf` `log` `net` `platform`（`thread.c` 仅核心库使用） |
| `win32/` | Win32 GUI 前端：`iNES.c` 主窗口，w* 系列为内存/调色板/图案表/卷轴等调试视图，`dlgNetPlay.c` 联网对战 |
| `libinescore.c` | 核心共享库的 C API 导出层 |
| `Android/` | Android JNI 构建文件（`jni/Android.mk`） |
| `bin/` | 构建产物输出目录（已被 `.gitignore` 忽略） |
| `tools/` | 辅助脚本，见「源码编码与换行」 |
| `CMakeLists.txt` | 构建脚本（由两个 vcproj 移植而来） |
| `ReadMe.txt` | VC++ 2008 向导生成的原始说明（历史留存） |

---

## 环境要求

| 组件 | 要求 | 备注 |
|---|---|---|
| CMake | ≥ 3.16 | 已在 **4.4.3** 上验证 |
| 编译器 | MSVC（VS2022 Build Tools，MSVC **14.44**）+ Windows 10 SDK | GUI 目标仅支持 Windows |
| 操作系统 | Windows 10/11 | 纯 Windows 32 位 GUI；核心库理论上可移植到 UNIX（未充分验证） |

无需任何第三方库（Lua 依赖已移除，见「已知事项」）。

---

## 构建

### 快速开始（推荐）

```powershell
# 配置（默认 Win32 / x86，与原 VC 工程一致）
cmake -S . -B build

# 构建 Release
cmake --build build --config Release
```

也可以用 Visual Studio 打开 `build/iNES.sln`，选择 `Release | Win32` 后生成。

### 其它常用命令

```powershell
# Debug 构建（可执行文件带 _d 后缀）
cmake --build build --config Debug

# 只构建某一个目标
cmake --build build --config Release --target iNES
cmake --build build --config Release --target inescore

# x64 构建（评估为安全，见「Win64 支持」）
cmake -S . -B build-x64 -A x64
cmake --build build-x64 --config Release

# 清理后重建
cmake --build build --config Release --clean-first
```

若使用 Ninja 这类**单配置生成器**，必须在配置时指定构建类型，且目标架构取决于命令行环境：

```powershell
cmake -S . -B build-ninja -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build-ninja
# 要生成 32 位程序，请在 “x86 Native Tools Command Prompt for VS 2022” 中执行
```

### 构建选项

| 选项 | 默认值 | 说明 |
|---|---|---|
| `INES_BUILD_CORE` | `ON` | 构建核心共享库 `inescore` |
| `INES_BUILD_GUI` | `ON` | 构建 Win32 GUI `iNES`（非 Windows 平台自动跳过） |
| `INES_OUTPUT_DIR` | `<仓库>/bin` | 可执行文件与共享库的输出目录 |
| `CMAKE_GENERATOR_PLATFORM` | `Win32` | 目标平台，可用 `cmake -A x64` 覆盖 |

示例：只构建核心库

```powershell
cmake -S . -B build -DINES_BUILD_GUI=OFF
cmake --build build --config Release
```

### 构建产物

| 配置 | 产物 |
|---|---|
| Release | `bin/iNES.exe`、`bin/inescore.dll` |
| Debug | `bin/iNES_d.exe`、`bin/inescore.dll` |

> 注意：Debug 与 Release 的 `inescore.dll` **同名，会互相覆盖**（沿用原工程行为）；`iNES` 通过 `DEBUG_POSTFIX=_d` 区分。

---

## 目标与原 VC 工程的对应关系

| 原工程 | CMake 目标 | 类型 | 字符集 | 主要依赖 |
|---|---|---|---|---|
| `iNES.vcproj` | `iNES` | Win32 GUI 可执行文件 | Unicode（`UNICODE`/`_UNICODE`） | `winmm` `ws2_32` `comdlg32` `comctl32` `shell32` |
| `iNesCore_DLL.vcproj` | `inescore` | 共享库 | 多字节（MBCS） | `ws2_32`（UNIX 下为 `pthread` + `m`） |

由于两个目标字符集不同，`core/` 与 `comm/` 会分别编译进两个目标（与原工程一致）。

---

## 文档

完整文档位于 [`docs/`](docs/README.md)：

| 文档 | 内容 |
|---|---|
| [docs/architecture.md](docs/architecture.md) | 项目架构：分层、模块职责、数据流、线程与时序 |
| [docs/api.md](docs/api.md) | 核心 API 参考（host/rom/cpu/ppu/apu/joypad/mapper/公共库/DLL 导出） |
| [docs/coding-style.md](docs/coding-style.md) | 编码规范 |
| [docs/mapper-guide.md](docs/mapper-guide.md) | **Mapper 开发规范**（接口、模板、IRQ、检查表） |
| [docs/mapper-list.md](docs/mapper-list.md) | **已实现 Mapper 清单**（18 个已实现，238 个占位桩） |
| [docs/build.md](docs/build.md) | 构建与工程约定 |

## 源码编码与换行

全部源码（`.c` `.h` `.rc` `.lua` `.mk` `.txt` `.md`）已统一为 **UTF-8 无 BOM + LF**：

- MSVC 编译时统一加 `/utf-8`，避免中文注释触发 C4819 或被按本地代码页误解释
- `.gitattributes` 中设置了 `* text=auto eol=lf`，防止 Git 检出时转回 CRLF
- `win32/iNES.rc` 例外：为让资源编译器正确识别中文，它是 **UTF-8 with BOM**，并使用 `#pragma code_page(65001)`
- `win32/targetver.h` 必须保持**纯 ASCII**：它会被 `rc.exe` 包含，非 ASCII 注释会扰乱资源编译器的预处理器（曾导致 `RC1022: expected '#endif'`）

需要重新批量转换时使用：

```powershell
powershell -ExecutionPolicy Bypass -File tools\convert_encoding.ps1 -WhatIf   # 预演
powershell -ExecutionPolicy Bypass -File tools\convert_encoding.ps1          # 执行
```

脚本会按「纯 ASCII / 已是 UTF-8 / GBK(936) / UTF-16(BOM)」自动判定并转换，只重写确有变化的文件。

---

## Win64 支持

评估结论：**安全，可编译 x64**。依据：

- `core/`、`comm/` 中无内联汇编、无 `_M_IX86` 分支、无 `sizeof(int/long/指针)` 大小假设
- 无指针与 32 位整型互存（`core` 中未使用 `void *`，GUI 层未用 `SetWindowLong(GWL_USERDATA)`）
- `comm/net.c` 使用 `typedef SOCKET socket_t`，在 Win64 下为 64 位 `UINT_PTR`，正确
- Windows 采用 LLP64 数据模型，`long` 在 x64 下仍为 32 位，与 Win32 行为一致
- 唯一的平台分支是 `core/cpu.c` 的 `__GNUC__` computed-goto，MSVC 下不启用

构建后建议重点关注 MSVC 的 `C4267 / C4311 / C4312 / C4244`（大小与指针截断）警告。

---

## 已知事项

- **Lua 依赖已移除**：原工程在 `win32/stdafx.h` 保留了 `#include <lua.h>`、`stdafx.c` 保留了 `#pragma comment(lib, "libluastatic5.1*.lib")`，但代码中从未调用任何 Lua API，属于历史遗留，现已删除。仓库里保留的两个 `.lua` 是离线工具脚本，不参与构建与运行：
  - `core/mapper/auto_gen.lua`：从 `templ.c.tpl` 批量生成 mapper 代码
  - `bin/get_wave.lua`：解析运行日志提取 APU 波形采样
- **旧 PDB 冲突**：`bin/` 下若残留 VC2008 时代的 `*.pdb`，新版 MSVC 会报 `LNK1207: PDB 格式不兼容`，删除这些 `.pdb` 后重新构建即可。
- **`core/mapper/16.c`** 历史上是「4 字节损坏头 + UTF-16BE 前段 + ASCII 后段」的混合编码文件，已修复为纯 ASCII UTF-8；如需再次处理同类文件请人工确认（转换脚本不会自动处理无标准 BOM 的混合编码）。

---

## 运行

构建完成后直接运行 `bin/iNES.exe`，通过「文件 → 载入ROM」打开 `.nes` 文件。

| 快捷键 | 功能 |
|---|---|
| `Ctrl+O` / `Ctrl+U` | 载入 / 卸载 ROM |
| `Ctrl+F1` / `F1` | 重新上电 / 软件复位 |
| `P` / `Space` | 暂停 / 单帧执行 |
| `F5`-`F8` | 窗口缩放 x1-x4 |
| `F9` | 静音 |
| `F12` | 截图 |
| `Ctrl+0`-`9` / `Ctrl+Shift+0`-`9` | 即时存档 / 载入存档 |

（快捷键以 `win32/iNES.rc` 中的菜单定义为准。）
