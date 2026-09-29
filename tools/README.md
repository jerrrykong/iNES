# 工具索引（tools/）

本目录放**辅助脚本与小工具**，**不进入主构建目标**：CMake 不收集本目录，`*.c` 需手动编译（见 `docs/build.md` §7）。

> **为什么这些脚本在这里**：`build/` 被 `.gitignore` 第 2 行 `build/**` 忽略，早先的手工脚本（`dump_frame.ps1`、`smoke.ps1` 等）
> 放在 `build/` 下**从未入库**，重建构建目录就会丢失。2026-09-29 已统一迁入本目录。

## 1. 快速索引

| 工具 | 用途 | 备注 |
|---|---|---|
| **`smoke.ps1`** | **最常用**：按文件名通配在 `D:\NES` 递归找 ROM → 无头跑 → 抓 3 帧 PNG | 内部调 `dump_frame.ps1` |
| **`dump_frame.ps1`** | 无头加载 ROM，在指定时刻抓帧存 PNG（同时存 `.bin` 原始索引） | **必须 32 位 PowerShell** |
| **`dump_frame_keys.ps1`** | 抓帧 + **按键注入**（菜单 / 多合一卡带验证必备） | 同上 |
| `scan_mapper.ps1` | 扫 `D:\NES` 列出指定 mapper 号的 ROM（header/PRG/CHR） | 改脚本内 `$ids` |
| `render_chr.ps1` | 把 ROM 的 CHR 区渲染成 tile 图（BMP） | 读 `%TEMP%\inesdbg\rom.nes` |
| `render_mb.ps1` | 从 `iNES.log` 解析 `mem_bank` 渲染 PPU 取址图 | 需 DBG 级日志 |
| `render_nt.ps1` | 从日志渲染 nametable | 同上 |
| `convert_encoding.ps1` | 校验/转换源码编码与换行（UTF-8 无 BOM + LF） | 先 `-WhatIf` 预演 |
| `nes_scan.py` | 生成 ROM 清单 `D:\NES\nes_roms.csv` | python；CRC 与 `core/rom.c:calc_crc32` 一致 |
| `gen_i18n_template.c` | 内置英文表 → `lang/en.ini` 模板 | 手动编译，见 `docs/build.md` §7 |
| `i18n_check.c` | 校验各语言 ini 与内置表一致（key/占位符/编码/LF） | 提交前必跑 |

## 2. 常用用法

### 2.1 冒烟抓帧（最常用）

```powershell
powershell -ExecutionPolicy Bypass -File tools\smoke.ps1 -pattern '<文件名通配>' -tag <输出目录名>
# 例：tools\smoke.ps1 -pattern '7-in-1*.nes' -tag m44
# → 复制到 C:\Temp\<tag>\rom.nes，抓 frame_1..3.png
```

- **`-pattern` 要给完整文件名或足够长的通配**（传 `caltron` 会 `no rom matched`）。
- 文件名含 `[` `]` 时用 `*` 绕开（`-Filter` 里方括号是字面量，但通配展开会出问题）。

### 2.2 直接抓帧（可指定时刻与日志级别）

```powershell
$ps32 = 'C:\Windows\SysWOW64\WindowsPowerShell\v1.0\powershell.exe'
& $ps32 -ExecutionPolicy Bypass -Command "& 'd:\proc\krh\iNES\tools\dump_frame.ps1' -outDir C:\Temp\m44 -waits @(1500,2500,4000)"
```

- `-logLevel 2` 才出 DBG/INF（默认 10=NTY）。`-cpuTrace 1` 开 CPU 轨迹。
- `-noStop` 跳过 `ines_stop`（进程退出顺带杀线程），用于抓"卡死"现场。
- **必须 32 位 PowerShell**：`inescore.dll` 是 32 位。

### 2.3 按键注入（菜单 / 多合一卡带）

```powershell
& $ps32 -ExecutionPolicy Bypass -File tools\dump_frame_keys.ps1 -rom <rom.nes> -keys DOWN,DOWN,DOWN,DOWN,DOWN,DOWN,START
```

- 按键名：`A B SELECT START UP DOWN LEFT RIGHT`（位定义见 `core/joypad.h`）。
- 流程：等 `-bootMs` 抓 `menu.png` → 依次按键（`-holdMs` 按住 / `-gapMs` 松开间隔）→ 抓 `shot1.png`、`shot2.png`。
- **为什么必须有它**：菜单本身只跑在 bank 0，不进游戏就永远不会写 bank 寄存器，日志里一条记录都没有。

### 2.4 编码校验（提交前）

```powershell
powershell -ExecutionPolicy Bypass -File tools\convert_encoding.ps1 -WhatIf   # 预演
powershell -ExecutionPolicy Bypass -File tools\convert_encoding.ps1           # 执行
```

### 2.5 ROM 清单

```powershell
$c = Import-Csv 'D:\NES\nes_roms.csv' -Encoding UTF8
$c | Where-Object { $_.mapper -eq '44' } | Select-Object name, prg_kb, chr_kb, path
```

查"某 mapper 有哪些 ROM"直接过滤 CSV，**不要扫盘**。

## 3. 环境坑（踩过的）

- **`inescore.dll` 是 32 位** → 所有 P/Invoke 调用必须走 `C:\Windows\SysWOW64\WindowsPowerShell\v1.0\powershell.exe`。
- **日志位置**：核心按 `GetModuleFileName` 写 **exe 同目录**；经 32 位 PowerShell 宿主加载 dll 时该路径不可写 → **回退 `%TEMP%\iNES.log`**。
- **长任务别用 `| Select-Object -Last N`**（会缓冲住输出），改读日志文件。
- **PowerShell 的 `[byte] -shl 8` 会被截断成 byte**，位移前必须先转 `[int]`（解析 ROM header 时极易踩）。
- `Remove-Item` 在本机被包装，管道调用会失败 → 用 `[System.IO.File]::Delete()`。
- 改构建前先关 `iNES.exe`，否则 `LNK1104`；`LNK1207`（PDB 格式不兼容）→ 删 `bin\*.pdb` 重建。

## 4. 外部工具（不在本目录）

| 工具 | 位置 | 用途 |
|---|---|---|
| **Mapper 号诊断 skill** | `C:\Users\jerry\.codebuddy\skills\nes-mapper-diagnose` | ROM 能跑但图形错乱 / 怀疑 header 误标时鉴定真实 mapper 号 |
| IMA 笔记 API | skill `ima-skills` 的 `ima_api.cjs` | 取开发资料（如 mapper 硬件说明） |

### 4.1 Mapper 号诊断（纯 node、零依赖）

```bash
node scripts/nes_probe.js <rom.nes>          # header / 容量 / bank 数 / 6502 递归反汇编 / 写窗口分类 / AND 冲突逃逸表
node scripts/patch_mapper.js <rom.nes> 3     # 只改 header mapper 位 → <原名>.m3.nes（不覆盖原文件）
```

判定顺序：**容量是否与该 mapper 期望矛盾** → 写落在哪个窗口 → bank 号在数据线还是地址线 →
是否需要别的寄存器先开使能 → bank 位位置（`&3` vs `bit3-2`）。

判定后**改 ROM header 验证，不要为误标 ROM 放宽 mapper 实现** —— 实现里的限制往往是真实硬件约束。
已知案例：`D:\NES\nesrom-master\阿拉丁3.nes` header 标 41，实际是 **3（CNROM）**。

### 4.2 IMA 笔记 API（还原要点，无脚本时可按此重建）

```
POST https://ima.qq.com/openapi/<apiPath>
Headers: ima-openapi-clientid / ima-openapi-apikey / ima-openapi-ctx: {}
凭证：   %USERPROFILE%\.config\ima\client_id 与 api_key
流程：   wiki/v1/search_knowledge（带 knowledge_base_id）→ 取 media_id
         → wiki/v1/get_media_info → data.notebook_ext_info.notebook_id
         → note/v1/get_doc_content（target_content_format=0）
```

优先用 `node ima_api.cjs`（skill 自带）；手搓 POST 时**必须自行 UTF-8 解码响应**再落盘，
否则 PowerShell 重定向会把 UTF-8 转成 UTF-16/GBK 导致中文乱码。
