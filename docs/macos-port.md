# macOS 支持方案

本文是 iNES 的 macOS 移植设计与实施记录。Windows 侧行为保持不变，`comm/` 的改动均为新增平台分支。

## 1. 目标与范围

| 项目 | 结论 |
|---|---|
| 前端技术 | 原生 Cocoa + Objective-C(`.m`，非 ObjC++)，不引入第三方库 |
| 核心复用 | `core/` + `comm/` 直接编译进 `iNES.app`（同进程），与 `win32/` 的策略一致 |
| 音频格式 | APU 的 8bit unsigned 转 **SInt16**，AudioQueue 输出 |
| 数据目录 | `~/Library/Application Support/iNES/` |
| 平台宏 | 引入 `INES_POSIX` 统一描述 Linux / Android / macOS |

## 2. 现状盘点

| 模块 | 平台相关程度 | 结论 |
|---|---|---|
| `core/` | 仅 `core/cpu.c` 一处 `__GNUC__` 分支（computed goto，Clang 可编译） | 可移植，改动≈0 |
| `comm/idef.h` | Win32 / linux 两分支 | 必改（缺 macOS：类型、`ISTR`、`PRI64`、`_t*` 宏） |
| `comm/thread.{h,c}` | Win32 / linux 两分支 | 走 pthread，需处理取消语义 |
| `comm/net.c/.h` | Win32 Winsock / POSIX socket | 走 POSIX 分支，补 `<sys/ioctl.h>` |
| `comm/log.c` | 相对路径日志 + `gettimeofday` | 必改（路径 + `struct timeval` 头文件） |
| `comm/platform.c` | 无 | 原样可用，新增数据目录查询 |
| `libinescore.c` | `get_cur_time_us` / `do_sleep` | 缺 mac 分支 |
| `win32/` | 约 7300 行纯 Win32 API | 不可复用，需重写 |

## 3. 平台层改造

### 3.1 平台宏归一化

`comm/idef.h` 顶部做一次归一化，源码统一判断 `INES_POSIX`：

```c
#if defined(linux) || defined(__linux__) || defined(__APPLE__) || defined(__ANDROID__)
	#ifndef INES_POSIX
		#define INES_POSIX 1
	#endif
#endif
```

这样 CMake 与 `Android.mk` 都无需额外定义，`linux` 这个误导性名字不再出现在源码分支里。

### 3.2 逐文件改动

| 文件 | 改动 |
|---|---|
| `comm/idef.h` | 新增 `INES_POSIX` 分支：`ines_cstr_t=const char*`、`ISTR(s)=s`、`PRI64="ll"`、`_tstat/_stat/_trename/_tfopen/_fputts/_vftprintf/_istspace/_tcslen/_tcserror` 宏；补 `<sys/time.h>`（macOS 的 `struct timeval` 不随 `<time.h>` 提供）与 `<strings.h>`（`strcasecmp`）；新增 `ines_get_data_dir()` 声明 |
| `comm/thread.h` | pthread 分支扩展到 `INES_POSIX` |
| `comm/thread.c` | ① pthread 分支扩展到 `INES_POSIX`；② macOS 下跳过 `PTHREAD_CANCEL_ASYNCHRONOUS`（Darwin 不支持可靠异步取消）；③ `ines_thread_terminate` 在 macOS 下直接返回失败并记日志——核心停止路径用的是协作式 `g_stop_flag` + `ines_thread_wait`，不依赖强制终止 |
| `comm/net.c` | `#elif defined(linux)` → `INES_POSIX`；补 `<sys/ioctl.h>`（`FIONBIO`）；`__t2a` 的 POSIX 分支返回 `(char*)ts` |
| `comm/net.h` | 无改动（`net_saddr_t`/`net_port_t` 复用 `idef.h` 类型） |
| `comm/log.c` | `#elif defined(linux)` → `INES_POSIX`；`__APPLE__` 下设日志目录为数据目录，避免从 Finder 启动时 CWD=`/` 不可写。**Windows 分支保持原样** |
| `comm/log.h` | `#ifdef linux` → `INES_POSIX`（printf 格式检查属性） |
| `comm/platform.c` | 新增 `ines_get_data_dir()`：Windows 返回 exe 同目录，macOS 返回 `~/Library/Application Support/iNES`（自动创建），其它 POSIX 返回 `~/.local/share/iNES` |
| `libinescore.c` | `get_cur_time_us` 增加 `INES_POSIX` 分支（`clock_gettime(CLOCK_MONOTONIC)`）；`do_sleep` 用 `nanosleep`（`usleep` 自 macOS 10.13 起 deprecated） |

### 3.3 数据目录

| 用途 | Windows | macOS |
|---|---|---|
| `config.ini` | exe 同目录 | `~/Library/Application Support/iNES/config.ini` |
| `iNES.log` | exe 同目录 | 同上 |
| `save/*.sav` | exe 同目录 | 同上 `save/` |
| `state/*.stN` | exe 同目录 | 同上 `state/` |
| `snapshot/*.bmp` | exe 同目录 | 同上 `snapshot/` |

不放在 `.app` 内部：`/Applications` 下不可写。

### 3.4 线程模型

| 方案 | 评价 |
|---|---|
| 主线程跑模拟（对照 `win32` 的 `OnIdle`） | 改动小，但 `NSTimer` 抖动大且阻塞 UI 事件 |
| **模拟独立线程 + 主线程 UI（采用）** | 复用 `libinescore.c` 已验证的帧循环/双缓冲/音频自适应同步；AppKit 交互流畅 |

- 输入与命令走 `ines_mutex_t` 保护的共享结构（对齐 `libinescore.c` 的 `g_mutex_input`）。
- 画面走 `screen_buffer[2]` 双缓冲 + 输出互斥量。
- 时序与音频同步直接移植 `win32/iNES.c` 的 `wvPlayingNum` 判定：在飞缓冲数低于目标时把帧时长减 1ms，高于时加 1ms。

## 4. mac 前端（`mac/`）

### 4.1 文件职责

| 文件 | 对应 win32 | 职责 |
|---|---|---|
| `mac/main.m` | `_tWinMain` | `NSApplication` 启动 |
| `mac/iNESApp.h/.m` | `iNES.c` 主逻辑 | 应用状态、帧循环、菜单动作、存档/读档、截图、路径、窗口 |
| `mac/iNESVideo.h/.m` | `OnPaint` + `StretchDIBits` | 自定义 `NSView`，索引色→BGRA→`CGImage` 渲染与缩放 |
| `mac/iNESAudio.h/.m` | `waveOut` 部分 | AudioQueue 输出、缓冲池、8bit unsigned→SInt16 |
| `mac/iNESPalette.h/.c` | `rgbQuard` | 64 色调色板 + BGRA 查表 |
| `mac/iNESOsd.h/.c` | `code_ascii_5x7` + `DrawTextToBitmap` | OSD 文字写入索引色缓冲 |
| `mac/iNESConfig.h/.c` | `Get/SetPrivateProfile*` | INI 读写（保留同名接口） |
| `mac/Info.plist` | `iNES.rc` 资源段 | bundle 元信息 |

### 4.2 视频

- 核心输出 256×240 8bit 索引色，DIB 自底向上 → 转换时按行倒序翻转。
- 调色板 → 256 项 BGRA 查表；`kCGBitmapByteOrder32Little | kCGImageAlphaNoneSkipFirst`，内存字节序 B,G,R,X。
- `CGBitmapContext`（256×240）承载转换结果，`drawRect:` 用 `CGBitmapContextCreateImage` 取图后按等比缩放绘制，`interpolationQuality = None` 保持像素风。
- 后续如需滤镜/扫描线，把 `presentIndexedPixels:` 换成 Metal 实现即可，不影响其它模块。

### 4.3 音频

- ASBD：44100Hz / 1ch / `kAudioFormatLinearPCM` / `kLinearPCMFormatFlagIsSignedInteger | kLinearPCMFormatFlagIsPacked` / 16bit。
- 缓冲池模型与 `waveOut` 一一对应：预分配 `MAX_BUF_NUM` 个 `AudioQueueBuffer`，每帧把该帧音频写进一个空闲缓冲后 `AudioQueueEnqueueBuffer`，播放完成由 `AudioQueueOutputCallback` 回收（等价 `MM_WOM_DONE`）。
- 格式转换：APU 输出 8bit unsigned（0x80 为静音）→ `SInt16 = (v - 128) << 8`。
- 启动时用 `audio_cache_num` 个静音缓冲做预填充，建立初始延迟。

### 4.4 输入

| NES 键 | macOS |
|---|---|
| A / 连发A | `X` / `S`（按 `charactersIgnoringModifiers` 判断，规避键盘布局差异） |
| B / 连发B | `Z` / `A` |
| SELECT | Shift（左右均可） |
| START | 回车 |
| 方向 | 方向键 |
| 焦点 | `NSApp.isActive && window.isKeyWindow`，失焦清空按键集合 |

按键集合在 `keyDown:` / `keyUp:` / `flagsChanged:` 维护，连发频率沿用 `key_flash_freq`。

### 4.5 菜单与快捷键

对齐 `win32/iNES.rc`，菜单项文本与快捷键保持一致；不可用项（联网对战、调试视图）在 M1 置灰。

| 功能 | 快捷键 |
|---|---|
| 载入 ROM / 卸载 ROM | Cmd+O / Cmd+U |
| 重新上电 / 软件复位 | Cmd+F1 / F1 |
| 暂停 / 单帧执行 | P / Space |
| 全屏 | F12 / Ctrl+Cmd+F |
| 缩放 x1~x4 | F5~F8 |
| 静音 | F9 |
| 即时存档 0~9 / 读档 0~9 | Cmd+0~9 / Cmd+Shift+0~9 |
| 截图 | F11 |
| 日志级别、CPU TRACE | 工具菜单 |

### 4.6 配置

自写 INI 读写，保留 `GetConfigStr / GetConfigInt / SetConfigStr / SetConfigInt` 同名接口与 `config.ini` 的 section/key 布局，便于用户在平台间迁移配置。

### 4.7 图标

- 源稿: `mac/icon/iNES-icon.svg`，红白机手柄抽象化（深红底 + 米白手柄面板 + 十字键 + 两枚圆钮）。
- 输出: `mac/icon/iNES.icns`，由 `mac/icon/make-icns.sh` 生成，覆盖 16/32/64/128/256/512/1024 全尺寸并保留 alpha。
- 接入: `CMakeLists.txt` 将 `.icns` 设为 `MACOSX_PACKAGE_LOCATION Resources`，`mac/Info.plist` 声明 `CFBundleIconFile = iNES.icns`。

## 5. 构建

- `CMakeLists.txt`：`CMAKE_GENERATOR_PLATFORM` 必须包进 `if(WIN32)`（现状是无条件设置，macOS 上直接配置失败）。
- `if(APPLE) enable_language(OBJC)`；新增选项 `INES_BUILD_MAC_GUI`（默认 ON，仅 `APPLE` 生效）。
- `add_executable(iNES MACOSX_BUNDLE ...)`，输出 `bin/iNES.app`，链接 Cocoa / AudioToolbox / CoreAudio / QuartzCore。
- `CMAKE_OSX_DEPLOYMENT_TARGET` 默认 11.0；Universal 二进制通过 `-DCMAKE_OSX_ARCHITECTURES="arm64;x86_64"` 开启。
- 不参与 mac 构建：`win32/*.rc`、`win32/Resource.h`、`win32/iNES.ico`。

## 6. 里程碑

| 阶段 | 交付 |
|---|---|
| M1 可玩 | 构建、平台层、主窗口、视频、音频、键盘、菜单（打开/卸载/复位/暂停/单帧/缩放/静音/音量/截图/日志级别/CPU TRACE）、即时存档读档、`config.ini`、拖放 ROM |
| M2 完整 | 6 个调试视图、联网对战、最近文件、手柄、全屏增强 |
| M3 打磨 | Metal 渲染、Universal2、签名与公证 |

## 7. 风险

| # | 风险 | 应对 |
|---|---|---|
| 1 | CoreAudio 8bit LinearPCM 为 signed，APU 输出 unsigned | 转 SInt16 |
| 2 | Darwin 取消语义与 glibc 不同 | 协作式停止，macOS 下 `ines_thread_terminate` 返回失败 |
| 3 | 无 `GetPrivateProfile*` | 自写 INI 模块 |
| 4 | 相对路径在 `/Applications` 下不可写 | 统一到数据目录 |
| 5 | `NSTimer` 抖动 | 模拟线程 + 缓冲池自适应同步 |
| 6 | CMake 无条件设置 `Win32` | 加 `if(WIN32)` 守卫 |

## 8. 实施记录

### 8.1 M1 完成情况

| 项 | 状态 |
|---|---|
| CMake：`if(WIN32)` 守卫、`enable_language(OBJC)`、`INES_BUILD_MAC_GUI`、`MACOSX_BUNDLE` | 完成 |
| 平台层：`INES_POSIX` 归一化、数据目录、线程/网络/日志 | 完成 |
| `mac/` 前端：`main.m`、`iNESApp.{h,m}`、`iNESVideo.{h,m}`、`iNESAudio.{h,m}`、`iNESPalette.{h,c}`、`iNESOsd.{h,c}`、`iNESConfig.{h,c}`、`Info.plist` | 完成 |
| 主窗口 / 视频 / 音频 / 键盘 / 菜单 / 缩放 / 比例 / 静音 / 音量 | 完成 |
| 载入 ROM（对话框 / 命令行 / Finder 打开方式 / 拖放）、卸载、重新上电、软件复位 | 完成 |
| 暂停、单帧执行、截图（BMP）、OSD（CPU 占用率） | 完成 |
| 即时存档 0~9 / 读档 0~9（含覆盖确认、标题显示存档时间） | 完成 |
| 最近文件（最多 10 条）、`config.ini` 持久化 | 完成 |
| 全屏（`toggleFullScreen:`，F12） | 完成 |
| 应用图标（`mac/icon/iNES-icon.svg` → `iNES.icns`，Big Sur 风格） | 完成 |

### 8.2 与设计文档的差异（实现时确定）

| 项 | 说明 |
|---|---|
| ARC | 视图/音频模块使用 `weak` 属性与 block 捕获，必须开启 ARC；`-fobjc-arc` 以**源文件属性**只加在 4 个 `.m` 上——加在目标上会被 clang 以 `invalid argument '-fobjc-arc' not allowed with 'C'` 拒绝（core/comm 是 `.c`） |
| 命令执行位置 | 载入/卸载/复位/存档/读档全部在**模拟线程**内完成：`host` 是值成员且被 `ines_host_doframe` 频繁访问，跨线程操作会与帧循环竞态。主线程只把请求写入 `s_ctl`（互斥量保护），模拟线程每帧开头取出执行；主线程需要的状态（`status`/`rom_title`/`rom_crc32`）由模拟线程回写同一结构 |
| 画面共享 | 模拟线程绘制 `s_screen_back` 后拷贝到 `screen_front`（`s_mutex_output` 保护）再提交视图；`screen_front` 仅被主线程截图读取 |
| 最近文件 | 提前到 M1 实现（`histories/file0..9` 与 win32 同 key，可跨平台迁移） |
| 窗口坐标 | 以 Cocoa 坐标（左下角原点）写入 `display/x`、`display/y`；读取时校验与某块屏幕可见区域相交，否则居中，避免 Windows 侧遗留坐标把窗口丢到屏幕外 |
| 截图 | 写 8bit BMP（文件头 + 256 项调色板 + 自底向上像素），与 win32 输出格式一致，便于对比 |
| OSD | 用 `DrawTextToBitmap` 直接画在索引色缓冲上（颜色索引 32，同 win32），绘制完再提交视图；`工具 > 显示 OSD` 可关闭 |
| 快捷键 | 全屏用 F12（未额外绑定 Ctrl+Cmd+F，避免与系统快捷键冲突）；其余同 4.5 节 |
| `iNESVideo.h` | 补充 `- (void)resetKeys;` 声明（窗口失焦时由控制器清空按键，避免"卡键"） |

### 8.3 验证

```
cmake -S . -B project/build && cmake --build project/build            # Release
# iNES.app 与 libinescore.dylib 均构建成功；mac 前端零警告
./bin/iNES.app/Contents/MacOS/iNES bin/ROM/4人麻将.nes
```

- 启动：数据目录与 `save/ state/ snapshot/` 自动创建，`config.ini` 首次运行按默认值建立。
- 音频：`audio: started, 44100Hz mono SInt16, 10 buffers.`
- 载入：`Load ROM ... OK`（mapper 0 / 4人麻将），10 秒运行后日志时间戳 ≈ 1.7×10⁷ 周期（≈ 60fps 满速）。
- 退出：`osascript -e 'quit app "iNES"'` 干净退出，模拟线程保存 SRAM 后停止，无崩溃。
- 最近文件与窗口位置已写回 `config.ini`。

遗留（非本次引入）：`core/rom.c:141` 的 `-Wformat`、`comm/net.c:341` 的 `-Wpointer-sign` 在 clang 下各有一条警告，属既有代码，本次未改动。

### 8.4 后续（M2）

调试视图（图形/卷轴/调色板/内存/寄存器）、联网对战、手柄支持、全屏增强；`ines_thread_terminate` 在 macOS 下按设计返回失败，停止路径统一走协作式标志 + `ines_thread_wait()`。
