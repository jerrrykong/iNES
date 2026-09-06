# iNES 项目架构

## 1. 总体分层

```
+---------------------------------------------------------------+
|  前端层 (frontend)                                             |
|   win32/    : Win32 GUI (iNES.exe) —— 直接链接核心，同进程调用   |
|   Android/  : JNI 绑定（Android.mk）—— 通过 inescore 共享库      |
+---------------------------------------------------------------+
|  封装层 (binding)                                              |
|   libinescore.c : inescore.dll 的 C API —— 线程 + 帧循环 + 缓冲  |
+---------------------------------------------------------------+
|  主机层 (host)   core/nes.c, nes.h                             |
|   ines_host_t —— 聚合 rom/mapper/cpu/ppu/apu/joypad，负责总线、  |
|   bank 切换、帧驱动、SRAM 存档、即时存档 (save state)             |
+---------------------------------------------------------------+
|  设备层 (devices)                                              |
|   core/cpu.c    6502 CPU                                       |
|   core/ppu.c    PPU（渲染 + 名称表镜像）                         |
|   core/apu.c    APU（音频合成）                                 |
|   core/joypad.c 手柄输入                                        |
|   core/rom.c    iNES 文件解析                                   |
|   core/mapper.c + mapper/*.c   卡带 Mapper（bank 切换/IRQ）      |
+---------------------------------------------------------------+
|  公共层 (comm)                                                 |
|   idef.h（类型/字符串/内存宏） log.c（日志） buf.c（缓冲）        |
|   net.c（联网对战） platform.c（平台适配） thread.c（线程/互斥）   |
+---------------------------------------------------------------+
```

## 2. 模块职责

| 模块 | 主要文件 | 职责 |
|---|---|---|
| host | `core/nes.c` `core/nes.h` | 顶层聚合与调度：`ines_host_doframe()` 驱动一帧；提供 `ines_host_read/write` 总线；提供 bank 切换 API 给 Mapper 使用 |
| rom | `core/rom.c` `core/rom.h` | 解析 iNES 文件头（16 字节）、trainer、PROM/VROM 数据；维护 mapper 号、镜像类型、SRAM 标志 |
| mapper | `core/mapper.c` `core/mapper.h` `core/mapper_creator.c` `core/mapper/*.c` | Mapper 抽象（函数指针表）+ 按 ID 创建具体 Mapper；各编号一个 `.c` 文件 |
| cpu | `core/cpu.c` `core/cpu.h` | 6502 指令执行、中断（IRQ/NMI）、周期计数 |
| ppu | `core/ppu.c` `core/ppu.h` | 扫描线渲染、精灵、名称表镜像、PPU 寄存器 |
| apu | `core/apu.c` `core/apu.h` | 2A03 音频合成，输出 8bit/44100Hz/单声道 |
| joypad | `core/joypad.c` `core/joypad.h` | 两个手柄的按键位与移位读取 |
| comm | `comm/*.c` | 日志、缓冲、网络、平台、线程等与模拟无关的基础设施 |
| 绑定 | `libinescore.c` | 把核心封装为跨语言可用的 DLL：内部起线程跑帧循环，对外提供取画面/取音频/设输入等简单接口 |

## 3. 核心数据结构

### `ines_host_t`（`core/nes.h`）

模拟器的"主板"，所有设备都以**值成员**的形式内嵌其中：

```c
struct _ines_host_ {
    ines_rom_t      rom;
    ines_mapper_t   mapper;
    ines_cpu_t      cpu;
    ines_ppu_t      ppu;
    ines_apu_t      apu;
    ines_joypad_t   joypad;
    ines_setting_t  setting;
    ines_byte_t     SRAM[NES_MAX_SRAM_SIZE];      // 64KB
    ines_byte_t     SRAM_used[NES_MAX_SRAM_BANKS];
    ines_byte_t     SRAM_write_flag;
    ...
    ines_word_t     prom_8k_num, vrom_1k_num;     // 页数量（Mapper 常用）
    ines_word_t     prom_8k_mask, vrom_1k_mask;   // 页掩码（取模用）
};
```

由于是值成员，模块之间用 `offsetof` 反查宿主，这是本项目的重要约定：

```c
#define cpu2host(p)     ((ines_host_t*)((char*)(p) - offsetof(ines_host_t, cpu)))
#define ppu2host(p)     ((ines_host_t*)((char*)(p) - offsetof(ines_host_t, ppu)))
#define apu2host(p)     ((ines_host_t*)((char*)(p) - offsetof(ines_host_t, apu)))
#define mapper2host(p)  ((ines_host_t*)((char*)(p) - offsetof(ines_host_t, mapper)))
```

### `ines_mapper_t`（`core/mapper.h`）

Mapper 是一个函数指针表 + 一块私有数据：

```c
struct _ines_mapper_ {
    ines_byte_t*  p_data;      // 私有数据（由 INIT_MAPPER_DATA_ST 分配）
    ines_size_t   data_len;
    ines_int_t    custom_sram;
    void (*fini)(ines_mapper_t*);
    void (*reset)(ines_mapper_t*);
    void (*hsync)(ines_mapper_t*, ines_int_t);
    void (*vsync)(ines_mapper_t*);
    ines_byte_t (*readlow)(ines_mapper_t*, ines_word_t);
    void (*writelow)(ines_mapper_t*, ines_word_t, ines_byte_t);
    void (*writehigh)(ines_mapper_t*, ines_word_t, ines_byte_t);
    ines_byte_t (*PPU_latch)(ines_mapper_t*, ines_word_t, ines_int_t);  // MMC5
    void (*PPU_latch_FDFE)(ines_mapper_t*, ines_word_t);                // MMC2
    int  (*savestate)(ines_mapper_t*, FILE*, ines_bool_t);
};
```

## 4. 数据流

### 4.1 帧循环（`ines_host_doframe`）

```
doframe:
  ines_ppu_start_frame()
  for each scanline:
      ines_ppu_render_line()        -> 256x1 像素写入前端缓冲
      mapper->hsync(scanline)       -> Mapper 可在此产生 IRQ（如 MMC3 计数）
      CPU 执行该行的周期数
  ines_ppu_end_frame()
  ines_apu_render_frame()           -> 生成本帧音频采样
```

### 4.2 总线访问

- CPU 读 `$0000-$1FFF`：内部 RAM（不经过 mapper）
- CPU 读 `$2000+`：`ines_host_read()` 分发到 PPU / APU / SRAM / Mapper
- CPU 写 `$6000-$7FFF`：SRAM 或 Mapper 的 `writelow`
- CPU 写 `$8000+`：`ines_mapper_writehigh()` → Mapper 寄存器（bank 切换）
- PPU 读 `$0000-$1FFF`：VROM/VRAM bank（由 Mapper 通过 `ines_set_vrom_bank_*` 设置）

### 4.3 两种前端的集成方式

**Win32 GUI（`win32/iNES.c`，同进程直接使用核心）**

```
定时器/消息循环 -> 计算帧时间
    -> ines_joypad_update_bits(&host.joypad, key1, key2)
    -> ines_host_doframe(&host, screen_buf)        // 得到一帧 256x240 8bit 索引色
    -> ines_apu_setoutbuffer() / ines_apu_getoutlen()  // 取音频写 waveOut 缓冲
    -> GDI 贴图到窗口
```

**inescore DLL（`libinescore.c`，跨语言/跨进程使用）**

```
ines_start() -> 创建线程 nes_proc()
nes_proc 循环：等待到下一帧时间 -> 取输入 -> ines_host_doframe()
              -> 写入双缓冲 g_render_buffer[] 与 g_audio_buffer
前端调用 ines_is_vedio_ready() / ines_get_vedio_data() / ines_get_audio_data() 取走
```

输入/输出各有一把互斥锁（`g_mutex_input` / `g_mutex_output`），SRAM 每 5 秒自动存盘一次。

## 5. 线程与时序模型

| 场景 | 线程 | 说明 |
|---|---|---|
| Win32 GUI | 主线程（UI + 模拟） | 模拟在窗口消息/定时器里推进，靠 `timeGetTime` 与音频缓冲长度调节帧时间 |
| inescore DLL | 前端线程 + `nes_proc` 模拟线程 | 模拟线程用高精度计时（`QueryPerformanceCounter` / `gettimeofday`）做帧同步，音视频通过双缓冲 + 互斥量交给前端 |

音频同步策略（DLL 内）：比较"已输出音频字节对应的时间"与"期望帧时间"，超前则拉长下一帧（+25%），落后则缩短（-25%），实现简单的自适应同步。

## 6. 目录与文件命名

| 目录 | 命名规则 |
|---|---|
| `core/` | 设备模块：`cpu.c/h` `ppu.c/h` `apu.c/h` `rom.c/h` `joypad.c/h` `nes.c/h` `mapper.c/h` `mapper_creator.c/h` |
| `core/mapper/` | 一个编号一个文件：`0.c` … `255.c`；入口函数 `mapperN_create()`；私有数据结构 `XXX_data_t` |
| `comm/` | 基础设施：`idef.h` `log.*` `buf.*` `net.*` `platform.*` `thread.*` |
| `win32/` | GUI：`iNES.c`（主窗口/主循环）、`w*.c`（调试视图窗口）、`dlgNetPlay.c`（联网对话框）、`stdafx.*`（预编译头） |
| `docs/` | 本文档目录 |
| `tools/` | 一次性/辅助脚本（如编码转换） |

## 7. 常量与配置

| 常量 | 值 | 位置 |
|---|---|---|
| 分辨率 | 256 x 240，8bit 索引色（最多 64 色） | `core/nes.h` |
| 音频 | 44100 Hz / 8bit / 单声道 | `core/nes.h` |
| SRAM | 最大 64KB，按 8KB 分 8 个 bank | `core/nes.h` |
| PROM 页 | 8KB（`$8000-$FFFF` 共 4 页） | `ines_set_prom_bank_4` |
| VROM 页 | 1KB（PPU `$0000-$1FFF` 共 8 页） | `ines_set_vrom_bank_8` |
| 镜像方式 | `MIRROR_SINGLE_SCREEN/VERT/HORZ/FOUR_SCREEN` | `core/ppu.h` |
| IRQ 掩码 | `MMC_IRQ_MASK 0x02`（Mapper IRQ 线） | `core/cpu.h` |
| 手柄按键 | `JOYPAD_KEY_A/B/SELECT/START/UP/DOWN/LEFT/RIGHT` | `core/joypad.h` |
