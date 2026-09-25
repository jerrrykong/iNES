# 构建说明

完整构建步骤见仓库根目录的 [README.md](../README.md)，本文补充工程侧的约定与常见问题。

## 1. 目标与源文件

| CMake 目标 | 类型 | 源文件来源 |
|---|---|---|
| `inescore` | 共享库 | `core/apu.c cpu.c joypad.c mapper.c mapper_creator.c nes.c ppu.c rom.c`、`core/mapper/*.c`、`comm/buf.c log.c net.c platform.c thread.c`、`libinescore.c` |
| `iNES` | Win32 GUI 可执行文件 | 上述 `core` + `comm`（不含 `thread.c`）+ `win32/*.c` + `win32/iNES.rc` |

- `core/mapper/*.c` 用 `file(GLOB ... CONFIGURE_DEPENDS)` 收集：**新增 mapper 文件后无需改 CMakeLists.txt**，重新配置即可纳入构建
- 新增其它源文件（如新的 `comm/xxx.c`）**需要**手动加入 `CMakeLists.txt` 的对应列表

## 2. 关键编译设置

| 设置 | 值 | 原因 |
|---|---|---|
| `CMAKE_GENERATOR_PLATFORM` | `Win32`（可用 `-A x64` 覆盖） | 原 VC 工程为 32 位；必须以**缓存变量**在 `project()` 之前设置，否则不生效 |
| `CMAKE_BUILD_TYPE` | `Release`（单配置生成器默认值，显式传 `Debug` 可覆盖） | 不指定会得到"空构建类型"：既无 `-O3` 也无 `-DNDEBUG`，`assert` 生效、帧率明显下降 |
| 字符集 | `iNES`：`UNICODE/_UNICODE`；`inescore`：多字节 | 与原工程一致，因此 `core/comm` 会分别编译进两个目标 |
| MSVC 选项 | `/W3 /utf-8` | 与 VC 工程告警级别一致；源码为 UTF-8，需显式指定源/执行字符集 |
| 预定义宏 | `WIN32` `_WIN32` `_WINDOWS` `_CRT_SECURE_NO_WARNINGS` `UNICODE` `_UNICODE`，Debug 附加 `_DEBUG` | 对齐 vcproj；`_WIN32` 同时供 `rc.exe` 使用 |
| 链接库 | `winmm` `ws2_32` `comdlg32` `comctl32` `shell32`（`inescore` 仅 `ws2_32`） | 对应源码中的 `#pragma comment(lib, ...)` 与 API 使用 |

## 3. 输出目录

统一输出到 `<仓库>/bin`（可用 `-DINES_OUTPUT_DIR` 修改）：

| 配置 | 32 位（Win32）产物 | 64 位（x64）产物 |
|---|---|---|
| Release | `iNES.exe`、`inescore.dll` | `iNES64.exe`、`inescore64.dll` |
| Debug | `iNES_d.exe`、`inescore.dll`（同名，会覆盖 Release 的 dll） | `iNES64_d.exe`、`inescore64.dll`（同名，会覆盖 Release 的 dll） |

64 位产物名由 `CMakeLists.txt` 的 `INES_BITS_SUFFIX`（`CMAKE_SIZEOF_VOID_P == 8` 时为 `64`）决定，
追加在 `OUTPUT_NAME` 之后、`DEBUG_POSTFIX` 之前，因此 32/64 位产物不会再互相覆盖（仅 Windows 生效）。

## 4. 编码与换行的工程约定

| 文件 | 编码 | 换行 | 说明 |
|---|---|---|---|
| 所有 `.c` `.h` `.lua` `.mk` `.txt` `.md` | UTF-8 **无 BOM** | LF | 由 `tools/convert_encoding.ps1` 统一 |
| `win32/iNES.rc` | UTF-8 **带 BOM** | LF | rc.exe 需要 BOM 才能正确识别中文；同时保留 `#pragma code_page(65001)` |
| `win32/targetver.h` | 纯 ASCII | LF | 被 `rc.exe` 包含，非 ASCII 注释会扰乱其预处理器（曾导致 `RC1022`），文件中已写明原因 |
| 二进制（`.ico` `.nes` `.xlsx`） | 不转换 | 不转换 | `.gitattributes` 中已声明为 `binary` |

`.gitattributes` 设置 `* text=auto eol=lf`，防止 Git 检出时把 LF 转回 CRLF。

批量转换/校验：

```powershell
powershell -ExecutionPolicy Bypass -File tools\convert_encoding.ps1 -WhatIf   # 预演
powershell -ExecutionPolicy Bypass -File tools\convert_encoding.ps1          # 执行
```

脚本已排除 `bin` `doc` `build` `cmake-build-*` `out` `.git` `.codebuddy` 目录。

## 5. 常见构建问题

| 现象 | 原因 | 处理 |
|---|---|---|
| `LNK1207: PDB 格式不兼容` | `bin/` 残留 VC2008 时代的旧格式 `.pdb` | 删除 `bin\*.pdb` 后重新构建 |
| `RC1022: expected '#endif'` | 被 rc 包含的头文件（如 `targetver.h`）含非 ASCII 注释 | 保持这些头文件为纯 ASCII |
| `RC2104: undefined keyword or key name` | rc 文件编码/内容被破坏 | 恢复 `win32/iNES.rc` 为正确的 UTF-8 |
| 平台仍是 x64 | `CMAKE_GENERATOR_PLATFORM` 未生效或被缓存 | 删除构建目录重新配置；或显式 `-A Win32` |
| `C4819` 中文告警 | 缺 `/utf-8` | 已在 CMake 中统一添加，勿删除 |
| 帧率明显偏低（`assert` 反而生效） | 单配置生成器下 `CMAKE_BUILD_TYPE` 为空 | 现已默认 `Release`；旧的空类型构建目录删除后重新配置 |

## 6. 应用图标

Windows 与 macOS 共用一份源稿 `mac/icon/iNES-icon.svg`：

| 平台 | 产物 | 生成方式 |
|---|---|---|
| Windows | `win32/iNES.ico`、`win32/small.ico` | `powershell -ExecutionPolicy Bypass -File mac\icon\make-ico.ps1` |
| macOS | `mac/icon/iNES.icns` | 同上加 `-UpdateIcns`；`mac/icon/make-icns.sh`（qlmanage 路线）会垫白底，详见其头部说明 |

`.ico` 用 32bpp DIB（BGRA + alpha、自下而上）而不是 PNG 条目：`rc.exe` 与各版本 Windows 的资源加载器对 DIB 支持最广。重新生成图标后重新构建 `iNES` 目标即可 —— `.ico` 时间戳变化会触发 `iNES.rc` 重编译。

## 7. i18n 辅助工具（`tools/`，不进入主构建目标）

两个纯 C 小工具，直接 `clang`/`cl` 单文件编译即可：

```bash
# 导出英文模板: comm/i18n_en.c(真源) -> lang/en.ini
clang -DINES_POSIX -o /tmp/gen_i18n_template tools/gen_i18n_template.c
/tmp/gen_i18n_template lang/en.ini

# 校验各语言文件(key 集/占位符/编码/换行)
clang -DINES_POSIX -o /tmp/i18n_check tools/i18n_check.c
/tmp/i18n_check lang/zh-CN.ini lang/ja.ini lang/fr.ini   # 全 OK 时退出码 0
```

Windows（MSVC）同理：`cl /DINES_POSIX /Fe:i18n_check.exe tools\i18n_check.c`（`/utf-8` 视需要添加）。

约定：**改英文必须改 `comm/i18n_en.c` 并重跑 `gen_i18n_template` 覆盖 `lang/en.ini`**；提交前跑 `i18n_check`。

## 8. Win64

评估为安全（无内联汇编、无指针/整型互存、`socket_t` 已按平台定义、LLP64 下 `long` 仍为 32 位）。构建后请关注 `C4267 / C4311 / C4312 / C4244` 警告。

```powershell
cmake -S . -B build-x64 -A x64
cmake --build build-x64 --config Release
```
