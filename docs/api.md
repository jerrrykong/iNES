# 核心 API 参考

所有接口均为 C 接口，`extern "C"` 保护，位于 `core/*.h` 与 `comm/*.h`。

## 1. 基础类型（`comm/idef.h`）

| 类型 | 定义 | 说明 |
|---|---|---|
| `ines_byte_t` | `unsigned char` | 8 位无符号 |
| `ines_sbyte_t` | `signed char` | 8 位有符号 |
| `ines_word_t` | `unsigned short` | 16 位地址 |
| `ines_sword_t` | `signed short` | |
| `ines_dword_t` | `unsigned int` | |
| `ines_int_t` | `int` | |
| `ines_size_t` | `size_t` | |
| `ines_int64_t` | `__int64` / `long long` | 平台相关 |
| `ines_bool_t` | `int` | `ines_true(1)` / `ines_false(0)` |
| `ines_char_t` | `TCHAR` / `char` | 字符集相关 |
| `ines_cstr_t` / `ines_str_t` | `const TCHAR*` / `TCHAR*` | |

字符串与 IO 统一走宏，保证 Unicode/多字节两套构建都能编译：

```
ISTR(s)         字符串字面量包装（_T / 原样）
ines_printf / ines_sprintf / ines_snprintf / ines_vsnprintf
ines_strcpy / ines_strncpy / ines_strcmp / ines_strstr / ines_strcasecmp
ines_alloc_st(st) / ines_alloc(sz) / ines_free(p)
count_of(a)     MAKE_BYTES(b0..b7)
INES_MAX_PATH   4096
```

## 2. 常用常量

| 常量 | 值 | 位置 |
|---|---|---|
| `NES_ERR_FILE_FORMAT` / `NES_ERR_UNSUPPORT_MAPPER_ID` / `NES_ERR_ILLEGAL_INSTRUCTION` | 10001 / 10002 / 10003 | `core/nes.h` |
| `NES_STATUS_OFF/RUNNING/PAUSE/FRAME_STEP` | 0/1/2/3 | `core/nes.h` |
| `SCREEN_WIDTH` / `SCREEN_HEIGHT` / `SCREEN_PIXELS` | 256 / 240 / 61440 | `core/nes.h` |
| `NES_AUDIO_SAMPLE_RATE` / `_BITS` / `_CHANNEL` | 44100 / 8 / 1 | `core/nes.h` |
| `NES_MAX_SRAM_SIZE` / `NES_MAX_SRAM_BANKS` | 0x10000 / 8 | `core/nes.h` |
| `MIRROR_SINGLE_SCREEN/VERT/HORZ/FOUR_SCREEN` | 0/1/2/3 | `core/ppu.h` |
| `PPU_ENABLE_BG` / `PPU_ENABLE_SPR` | 0x08 / 0x10 | `core/ppu.h` |
| `MMC_IRQ_MASK` | 0x02 | `core/cpu.h` |
| `JOYPAD_KEY_A/B/SELECT/START/UP/DOWN/LEFT/RIGHT` | 0x01…0x80 | `core/joypad.h` |
| `INES_FILE_TAG` / `INES_FILE_HEADER_SIZE` | `"NES\x1a"` / 16 | `core/rom.h` |

## 3. Host（`core/nes.h`）

```c
void        ines_host_init(ines_host_t* p_host, int is_ntsc);
void        ines_host_free(ines_host_t* p_host);
ines_bool_t ines_host_load_rom(ines_host_t* p_host, ines_cstr_t nes_file, ines_cstr_t ram_file);
void        ines_host_reset(ines_host_t* p_host);
void        ines_host_init_setting(ines_host_t* p_host, ines_int_t is_ntsc);
ines_int_t  ines_host_doframe(ines_host_t* p_host, ines_byte_t* p_screen);   // 渲染一帧
ines_byte_t ines_host_read(ines_host_t* p_host, ines_word_t addr);
void        ines_host_write(ines_host_t* p_host, ines_word_t addr, ines_byte_t val);
void        ines_host_write_sram_raw(ines_host_t* p_host, ines_word_t addr, ines_byte_t val);
void        ines_host_load_sram(ines_host_t* p_host, ines_cstr_t ram_file);
void        ines_host_save_sram(ines_host_t* p_host, ines_cstr_t ram_file);
ines_int_t  ines_host_save_state(ines_host_t* p_host, FILE* fSave);
ines_int_t  ines_host_load_state(ines_host_t* p_host, FILE* fSave);
ines_int_t  ines_save_state(ines_host_t* p_host, FILE* fSave);
ines_int_t  ines_load_state(ines_host_t* p_host, FILE* fSave);
```

### bank 切换（主要给 Mapper 用）

```c
ines_set_prom_bank_4(host, b4, b5, b6, b7);      // $8000/$A000/$C000/$E000，8KB/页
ines_set_prom_bank_5(host, b3, b4, b5, b6, b7);
ines_set_prom_bank_n(host, n, bn);               // n=3~7
ines_set_sram_bank_n(host, n, bn);               // $6000-$7FFF
ines_set_vrom_bank_8(host, b0..b7);              // PPU $0000-$1FFF，1KB/页
ines_set_vrom_bank_n(host, n, bn);               // n=0~7
ines_set_vram_bank_n(host, n, bn);               // CHR-RAM 情形
ines_set_ciram_pattern_bank_n(host, n, page);    // 内部 NT RAM 当作 CHR 页(n=0~7，page=0~1)
ines_set_nt_chr_bank_n(host, n, bn);             // nametable 窗口指向 CHR 页(n=0~3 即 PPU 窗口 8~11)
```

`ines_set_nt_chr_bank_n()` 供 Namco 163 的 ROM nametable 特性使用：有 CHR-ROM 时窗口指向
CHR-ROM 1KB 页（只读，`$2007` 写入被忽略），纯 CHR-RAM 卡带则指向 pattern RAM（可写）。
窗口要回到内部 CIRAM 时调用 `ines_ppu_set_mirror()`（它会把 4 个 nametable 窗口的类型全部复位）。

### 反查宿主

```c
cpu2host(p);  ppu2host(p);  apu2host(p);  mapper2host(p);
```

## 4. ROM（`core/rom.h`）

```c
void        ines_rom_init(ines_rom_t* p_rom);
void        ines_rom_free(ines_rom_t* p_rom);
void        ines_rom_reset(ines_rom_t* p_rom);
ines_bool_t ines_rom_load_from_file(ines_rom_t* p_rom, ines_cstr_t file_name);
```

`ines_rom_t` 关键字段：`mapper_num`、`mirror_type`、`has_sram`、`has_trainer`、`PROM_block_num`、`VROM_block_num`、`crc32_p`。

## 5. CPU（`core/cpu.h`）

```c
void       ines_cpu_init(ines_cpu_t* p_cpu);
void       ines_cpu_free(ines_cpu_t* p_cpu);
void       ines_cpu_reset(ines_cpu_t* p_cpu);
ines_int_t ines_cpu_exec(ines_cpu_t* p_cpu, ines_int_t cycles);
void       ines_cpu_IRQ(ines_cpu_t* p_cpu, ines_byte_t irq_mask, ines_bool_t is_set);
void       ines_cpu_NMI(ines_cpu_t* p_cpu);
ines_int_t ines_cpu_save_state(ines_cpu_t* p_cpu, FILE* fSave);
ines_int_t ines_cpu_load_state(ines_cpu_t* p_cpu, FILE* fSave);
```

## 6. PPU（`core/ppu.h`）

```c
void        ines_ppu_init(ines_ppu_t* p_ppu);
void        ines_ppu_free(ines_ppu_t* p_ppu);
void        ines_ppu_reset(ines_ppu_t* p_ppu);
void        ines_ppu_set_mirror(ines_ppu_t* p_ppu, ines_byte_t n0, n1, n2, n3);
void        ines_ppu_set_mirror_type(ines_ppu_t* p_ppu, ines_byte_t mt);
ines_byte_t ines_ppu_readlow(ines_ppu_t* p_ppu, ines_word_t addr);
void        ines_ppu_writelow(ines_ppu_t* p_ppu, ines_word_t addr, ines_byte_t val);
void        ines_ppu_start_frame(ines_ppu_t* p_ppu);
void        ines_ppu_end_frame(ines_ppu_t* p_ppu);
void        ines_ppu_start_vblank(ines_ppu_t* p_ppu);
void        ines_ppu_end_vblank(ines_ppu_t* p_ppu);
void        ines_ppu_render_line(ines_ppu_t* p_ppu, ines_byte_t* p_line);
ines_int_t  ines_ppu_save_state(ines_ppu_t* p_ppu, FILE* fSave);
ines_int_t  ines_ppu_load_state(ines_ppu_t* p_ppu, FILE* fSave);
```

## 7. APU（`core/apu.h`）

```c
void         ines_apu_init(ines_apu_t* p_apu);
void         ines_apu_free(ines_apu_t* p_apu);
void         ines_apu_reset(ines_apu_t* p_apu);
ines_byte_t  ines_apu_read(ines_apu_t* p_apu, ines_word_t addr);
void         ines_apu_write(ines_apu_t* p_apu, ines_word_t addr, ines_byte_t val);
void         ines_apu_flush_run(ines_apu_t* p_apu);
void         ines_apu_setoutbuffer(ines_apu_t* p_apu, ines_byte_t* buf, ines_dword_t len, ines_int_t volumn);
ines_dword_t ines_apu_getoutlen(ines_apu_t* p_apu);
void         ines_apu_start_frame(ines_apu_t* p_apu);
void         ines_apu_render_frame(ines_apu_t* p_apu, double end_time);
ines_int_t   ines_apu_save_state(ines_apu_t* p_apu, FILE* fSave);
ines_int_t   ines_apu_load_state(ines_apu_t* p_apu, FILE* fSave);

// 扩展音源输入槽（mapper 24/26/85/19 的 VRC6/VRC7/N163 引擎挂在这里）
void         ines_apu_exp_attach(ines_apu_t* p_apu, void* p_chip, ines_int_t channels, float gain,
                                 void (*run)(ines_apu_exp_t*, ines_int_t),
                                 void (*reset)(ines_apu_exp_t*));
void         ines_apu_exp_detach(ines_apu_t* p_apu, void* p_chip);
```

- 扩展音槽**不随存档保存**（芯片状态在 mapper 私有数据里），但 `ines_apu_load_state()` 会保留槽位的
  芯片指针与回调，并把 `exp.cursor` 对齐到还原后的 `last_cycles`；否则读档后扩展音会永久失效。
- `p_chip` 必须与 `ines_apu_exp_detach()` 传入的指针一致（通常填 `p_mapper->p_data`）。

## 8. 手柄（`core/joypad.h`）

```c
void        ines_joypad_init(ines_joypad_t* p_joypad);
void        ines_joypad_reset(ines_joypad_t* p_joypad);
void        ines_joypad_free(ines_joypad_t* p_joypad);
void        ines_joypad_input_brush(ines_joypad_t* p_joypad, ines_byte_t brush);
void        ines_joypad_update_bits(ines_joypad_t* p_joypad, ines_int_t key1, ines_int_t key2);
ines_byte_t ines_joypad_read(ines_joypad_t* p_joypad, ines_byte_t index);
```

按键位：`JOYPAD_KEY_A/B/SELECT/START/UP/DOWN/LEFT/RIGHT`，两个手柄各自一个 `ines_int_t` 位图。

## 9. Mapper（`core/mapper.h` / `mapper_creator.h`）

```c
void        ines_mapper_init(ines_mapper_t* p_mapper);
void        ines_mapper_free(ines_mapper_t* p_mapper);
void        ines_mapper_reset(ines_mapper_t* p_mapper);
ines_bool_t ines_mapper_create(ines_mapper_t* p_mapper, ines_int_t mapper_id);   // 按 ID 创建
ines_int_t  ines_mapper_save_state(ines_mapper_t* p_mapper, FILE* fSave);
ines_int_t  ines_mapper_load_state(ines_mapper_t* p_mapper, FILE* fSave);
ines_word_t addr_mask(ines_word_t num);      // 按大小生成地址掩码
INIT_MAPPER_DATA_ST(mapper, st)              // 分配并清零私有数据
```

调用回调统一用宏（内部判空）：`ines_mapper_hsync` `ines_mapper_vsync` `ines_mapper_readlow` `ines_mapper_writelow` `ines_mapper_writehigh` `ines_mapper_PPU_latch` `ines_mapper_PPU_latch_FDFE`。

详见 [mapper-guide.md](mapper-guide.md)。

## 10. 公共库（`comm/`）

### 日志 `comm/log.h`

```c
INES_LOG(level, module, fmt, ...);        // 推荐；内部先做级别判断
ines_log(level, module, fmt, ...);        // 带互斥
ines_log_r(level, module, fmt, ...);      // 可重入
ines_check_level(level);
ines_get_log_level(); ines_set_log_level(level);
ines_set_log_stamp_func(ines_int64_t (*f)(void));   // 自定义时间戳（通常用 CPU 周期）
ines_get_last_error_log(index, &level);             // 供 UI 显示
```

级别：`LOG_TRA(1) < LOG_DBG(2) < LOG_INF(3) < LOG_WAR(4) < LOG_ERR(5) < LOG_FAU(6) < LOG_NTY(10)`
模块：`MOD_SYS MOD_INES MOD_ROM MOD_CPU MOD_MMC MOD_PPU MOD_APU MOD_IN MOD_SCN MOD_NET`

### 线程 `comm/thread.h`

```c
ines_mutex_init/fini/lock/try_lock/unlock
ines_thread_init(th, func, ud) / ines_thread_start / ines_thread_wait / ines_thread_terminate / ines_thread_fini
ines_thread_set_auto_detach(th)   ines_thread_getcurid()
```

### 国际化 `comm/i18n.h`（两端共用，纯 C）

```c
int          ines_i18n_init(const char* preferred);   // preferred=前端探测到的系统语言(可 NULL)
void         ines_i18n_fini(void);
void         ines_i18n_add_lang_dir(const char* dir); // init 之前追加搜索目录(如 mac bundle 的 Resources/lang)
int          ines_i18n_enum(ines_i18n_lang_t* langs, int max_count);  // 含内置 en
const char*  ines_i18n_match(const char* lang_tag);   // "zh-Hans-CN" -> "zh-CN" -> "zh" -> "en"
int          ines_i18n_set_language(const char* id);
const char*  ines_i18n_language(void);
ines_cstr_t  ines_i18n_text(const char* key);         // key 为 ASCII("menu.file.open")
ines_str_t   ines_i18n_text_fmt(ines_str_t buf, ines_size_t len, const char* key, ...);
```

- 返回 `ines_cstr_t`：win32 为 UTF-16，POSIX 为 UTF-8，前端可直接使用。
- key 缺失 → 回退内置英文 → 仍缺 → 返回 key 本身并 `LOG_WAR`。
- 英文真源为内置编译期表 `comm/i18n_en.c`；外部 `lang/*.ini`（UTF-8 无 BOM + LF）按 key 覆盖。
- 只在 UI 线程调用；语言切换后旧的返回指针失效，不要跨切换缓存。
- 设计见 `docs/i18n-plan.md`。

### 其它

- `comm/buf.h`：循环/动态缓冲（音视频、网络）
- `comm/net.h`：`socket_t`（Windows 为 `SOCKET`，POSIX 为 `int`）+ 连接/监听/收发
- `comm/platform.h`：平台相关适配（时间、睡眠、文件等）

## 11. DLL 导出接口（`libinescore.c` → `inescore.dll`）

```c
int  ines_init_lib(void);                                    // 初始化日志等
void ines_set_loglevel(int level, int cpu_trace);
void ines_log_text(int level, const char* message);          // 外部写入日志（UTF-8）
int  ines_start(int is_ntsc, const char* rom_file, const char* ram_file);
int  ines_stop(void);
int  ines_is_running(void);
int  ines_update_input(int key1, int key2, int reset);
int  ines_is_vedio_ready(void);
int  ines_get_vedio_data(ines_byte_t* buffer, int len);      // 256x240 8bit 索引色
int  ines_get_audio_length(void);
int  ines_get_audio_data(ines_byte_t* buffer, int len);      // 8bit/44100/单声道
int  ines_get_frame_time_us(void);
void ines_set_volumn(int vol);                               // 0~100
int  ines_pause(void);      // 未实现（返回 0）
int  ines_step(void);       // 未实现（返回 0）
int  ines_load(const char* save_file);   // 未实现
int  ines_save(const char* save_file);   // 未实现
```

约定：

- 路径字符串按 **UTF-8** 传入，DLL 内部转换为当前字符集（`utf8_to_t`）
- `ines_start` 内部创建模拟线程，画面/音频通过双缓冲 + 互斥量交给调用方
- 视频缓冲大小固定 `SCREEN_PIXELS`(61440)，音频按 44100 字节/秒产生
