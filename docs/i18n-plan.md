# iNES 国际化（i18n）方案

> 状态：**mac 侧 M1+M2+M3（精简）已完成并通过冒烟测试；win32 侧待实施**，交接清单见 §16。
> 范围：`win32/`（Win32 GUI）+ `mac/`（Cocoa）两端界面文本；英语为原生语言，简体中文/日语/法语通过外部语言文件提供，后续可加语言。
> 相关文档：`docs/coding-style.md`（编码/命名）、`docs/build.md`（§7 工具构建）、`docs/api.md`（§10 i18n 公共 API）

### 已拍板决策（2026-09-19 / 09-20）

| # | 议题 | 结论 |
| --- | --- | --- |
| ① | 英文真源 | **内置英文编译期表**（方案 A），模板由工具导出（§5） |
| ② | 语言菜单位置 | 按各平台习惯：两端都放**「工具」菜单第 2 项**，位置见 §9 |
| ③ | 调试/查看窗口翻译范围 | **只翻纯 UI 元素**（窗口标题、按钮、表头）；硬件/CPU 术语保留英文（§10.1） |
| ④ | mac 是否改 Auto Layout | **本次不改**，仍用手工 frame「测量后定位」；改造留待成本/收益评估后再定（§14） |
| ⑤ | 交付范围 | **M1 + M2 + M3（精简版）一次做完**，含 `zh-CN.ini` / `ja.ini` / `fr.ini` 完整文件（§13） |
| ⑥ | 简体中文文案来源 | **直接沿用现有界面中文文案**，仅做少量术语统一（§11.1） |
| ⑦ | 首次运行的语言选择 | **自动匹配最合适的语言，不弹任何提示**（§1.1、§3.2） |
| ⑧ | 日语译文语体 | **常体**（不使用 `です・ます`）（§11.2） |
| ⑨ | 两端分工 | mac 侧先做完并测试，再交接 win32 侧实施（§16） |

---

## 1. 目标与非目标

### 1.1 目标

1. 全部界面文本（主窗口标题、菜单、对话框控件、按钮、提示框、状态栏/状态文本）走 i18n，运行时可切换。
2. **英语为原生语言**：英文是 UI 文本的真源，界面按"英文"设计与排版；其它语言是翻译层。
3. 新增"语言"菜单项，列出可用语言并标出当前语言，切换即时生效。
4. 首次运行按**操作系统语言自动匹配**，**不弹任何提示**（决策 ⑦）；无匹配 → 英语；用户选择写入 `config.ini` 后优先使用。
5. 支持**自定义语言**：外部配置文件（UTF-8 INI），无需改代码、无需重编译即可加语言。
6. 基于模板产出**日语/法语**完整翻译文件；后续按模板加语言即可。
7. **控件按译文长度自适应**：按钮/标签/窗口尺寸随文本变长自动调整（两端各自实现）。

### 1.2 非目标

- **日志不翻译**：`INES_LOG` 输出是开发者信息，保持现状（现有中文日志不动，新增日志按现有风格写）。
- **核心层不翻译**：`core/` 内部无 UI 文本；`ines_host_*` 的错误码由前端翻译成用户提示。
- ROM 标题、文件名、IP/端口、版本号等数据文本不翻译。
- 不做 RTL（阿拉伯语/希伯来语）布局镜像。

---

## 2. 现状盘点

| 端 | UI 文本来源 | 规模（估算） | 布局方式 | 字符类型 |
| --- | --- | --- | --- | --- |
| win32 | `iNES.rc`：菜单（约 70 项）、对话框（4 个，约 30 个控件）、`STRINGTABLE`（19 条）；`.c` 里 `ISTR("...")` 的 `MessageBox`/标题（约 90 处，含注释） | 约 250~350 条 | RC 固定坐标（DLU） | `TCHAR` = `wchar_t`（Unicode 构建） |
| mac | `iNESApp.m` 的 `buildMenuBar`（约 60 项）、4 个对话框/窗口（`iNESOpenRomDialog` / `iNESLanLobby` / `iNESNetPlayDialog` / `iNESDebug*`）、`showAlert` 提示 | 约 250~350 条 | **全部手工 frame**（`NSMakeRect` + `initWithFrame:`，无 Auto Layout、无 `autoresizingMask`） | UTF-8 `char` |

要点：

- 两端**字符宽度不同**（win32 UTF-16 / mac UTF-8），共享层必须屏蔽差异 → i18n 返回 `ines_cstr_t`（`ISTR` 体系内的字符串类型），key 用 ASCII `const char*`。
- win32 菜单里有 `&` 助记符与 `\tCtrl+O` 快捷键后缀；mac 没有。这两者**不能**写进语言文件（否则 mac 界面会出现 `&`），必须由平台侧追加（§7.1）。
- win32 的 `STRINGTABLE` 用 `%1`（`FormatMessage` 风格），与 mac 的 `%s/%d` 不兼容 → 统一为 **printf 风格**（§4.4）。
- `comm/` 已有 INI 读写：`mac/iNESConfig.h`（`GetConfigStr/SetConfigStr`）、win32 `WritePrivateProfileString`；语言文件沿用同样的 INI 风格，翻译人员零学习成本。
- 数据目录：`comm/platform.c` 的 `ines_get_data_dir()`（win32 = exe 同目录，mac = `~/Library/Application Support/iNES`）。

---

## 3. 总体设计

### 3.1 模块位置：新增 `comm/i18n.{c,h}`

- 纯 C、零第三方依赖，被 `iNES`(win32) 与 `iNES.app`(mac) 两个目标各自编译（与 `comm/log.c` 同样的做法）。
- 不碰 UI 框架：系统语言探测由**前端**传入（`ines_i18n_init(preferred)`），comm 只做"标签归一化 + 匹配"。
- 只在 **UI 线程**调用；模拟线程不使用（切换语言时也不会有并发读取）。

### 3.2 数据流

```
启动
 ├─ 读 config.ini [ui] language        （用户已选 -> 直接用）
 ├─ 否则 前端探测系统语言 -> ines_i18n_init("zh-CN")
 │        ├─ 扫描 lang 目录 -> 语言清单(内置 en + 外部文件)
 │        └─ 匹配: zh-CN -> zh -> en(兜底)   [无匹配 -> en]
 └─ 首次运行把结果写回 config.ini

运行时切换(菜单)
 └─ ines_i18n_set_language("ja")
      ├─ 前端: SetConfigStr("[ui] language")
      ├─ 前端: 重建/刷新菜单 + 窗口标题 + 已打开对话框控件文本(§7)
      └─ 前端: 触发自适应布局(§8)
```

### 3.3 语言 ID 与匹配规则

- 使用 **BCP-47 短标签**：`en` / `zh-CN` / `zh-TW` / `ja` / `fr` / `de` …
- 文件名即语言 ID（大小写不敏感）：`en.ini`、`zh-CN.ini`、`zh-TW.ini`、`ja.ini`、`fr.ini`。
- 匹配顺序（对系统语言 `zh-Hans-CN`）：
  1. 完整匹配 `zh-Hans-CN`
  2. 去掉脚本子标签 `zh-CN`
  3. 只留主语言 `zh`
  4. 都无 → `en`
- 繁体同理：系统语言 `zh-Hant-TW` → `zh-Hant-TW` → **`zh-TW`** → `zh-Hant` → `zh` → `en`；`zh-TW` 直接命中。
  （`zh-Hant-HK` 之类未提供的地区变体会回落到 `en`，需要时按 `zh-TW.ini` 复制一份 `zh-HK.ini` 即可，无需改代码。）
- 内置语言 `en` 恒存在（编译期表），不依赖文件。

### 3.4 语言文件搜索顺序（先命中先用，逐文件覆盖）

1. **用户目录**：`<数据目录>/lang/*.ini`（win32 = exe 同目录 `lang\`；mac = `~/Library/Application Support/iNES/lang/`）
   —— 用户自定义/改翻译放这里，升级不会被覆盖。
2. **程序目录**：win32 = exe 同目录 `lang\`（与上同目录时可合并）；mac = `iNES.app/Contents/Resources/lang/`。

> mac 侧：`Contents/Resources/lang` 由 CMake 复制 `lang/*.ini` 进 bundle；用户目录优先级更高，便于翻译人员直接编辑调试。
>
> **目录在启动时自动创建**（`iNES_i18n_user_lang_dir()`），因此即便原本不存在，翻译人员也可以直接把新的 `*.ini` 丢进去。

### 3.5 动态加载（运行时增删语言，无需重启）

- 语言清单**不是**启动时一次性固定：`ines_i18n_rescan()` 会重新扫描 §3.4 的全部目录、重新登记 `[meta]`，并按当前语言的文件内容**重载文本**（缺的 key 自动回退英文；当前语言文件被删则回落 `en` 并 `LOG_WAR`）。
- 用法：运行时新增 / 改写 / 删除 `lang/*.ini` 后调用一次。返回可用语言数（含内置 `en`）。
- mac 侧已接入：`self.languageMenu.delegate = self`，`menuNeedsUpdate:` → `updateLanguageMenu` 先 `iNES_i18n_rescan()` 再重建条目，
  **每次打开「语言」菜单都是最新清单**，新语言文件不必重启、也不必重新构建 bundle。
- win32 侧待办（§16）：语言菜单同样应在 `WM_INITMENUPOPUP` 时 `ines_i18n_rescan()` + 重建 `IDM_LANGUAGE_BASE + i` 条目。
- 注意：`rescan` 与 `set_language` 一样会让已取出的 `ines_i18n_text()` 指针失效，禁止缓存该指针。

---

## 4. 语言文件格式（UTF-8 无 BOM + LF，与项目规范一致）

### 4.1 结构

```ini
; ============================================================
; iNES 语言文件 / Language file
; 编码: UTF-8 无 BOM, 换行 LF
; 用法: 复制本模板, 改 [meta] 的 id/name, 逐条翻译右侧值。
;       key(等号左侧) 与注释禁止改动; 值内的 %s/%d 占位符必须保留且顺序不变。
; ============================================================

[meta]
id       = en
name     = English          ; 以本语言书写, 显示在"语言"菜单
version  = 1                ; 对应模板版本, 与程序内置英文表比对用

[app]
title             = iNES
title_format      = %s - %s          ; 1=ROM 标题, 2=状态文本([status])
title_net_format  = %s - %s (%s)     ; 1=ROM 标题, 2=状态, 3=对战标识(net_play_tag)

; 状态(标题栏后缀)
[status]
off               = Not running
running           = Running
pause             = Paused
frame_step        = Frame step
net_play_tag      = Net Play         ; 联网时拼进 title_net_format 的第 3 个 %s

[menu]
file             = File
file.open        = Load ROM...
file.close       = Unload ROM
file.net_play    = Net Play
file.lan_match   = LAN Quick Match
file.recent      = Recent Files
file.exit        = Exit
control          = Control
control.hard_reset      = Hard Reset
control.soft_reset      = Soft Reset
control.pause           = Pause
control.frame_step      = Frame Step
control.full_screen     = Full Screen
control.zoom            = Zoom
control.aspect          = Aspect Ratio
control.aspect_original = Original
control.mute            = Mute
control.volume          = Volume
control.save_state      = Save State
control.load_state      = Load State
control.snapshot        = Screenshot
tools            = Tools
tools.options    = Options...
tools.osd        = Show OSD
tools.log        = Log
tools.debug_view = Debug Views
help             = Help
help.about       = About iNES
language         = Language                 ; 语言菜单(新增)

; 提示信息
[msg]
load_rom_first          = Please load a ROM first.
net_play_host_only      = Only the host can reset during net play.
net_play_no_pause       = Cannot pause during net play.
net_play_quit_confirm   = A net play game is running. End it now?
peer_left               = The peer has left; continuing in single-player mode.
err_bad_format          = Invalid file format.
err_unsupported_mapper  = Unsupported mapper #%d.
err_illegal_instruction = Illegal instruction %s.

; 对话框: 网络对战
[dialog.netplay]
title        = Net Play
run_as       = Run as
server       = Server
client       = Client
address      = Address
port         = Port
cache_frames = Cache frames
ok           = Start
cancel       = Cancel

; 调试窗口标题
[view]
pattern_table = Pattern Table Viewer
name_table    = Name Table Viewer
palette       = Palette Viewer
memory        = Program Memory Viewer
vmemory       = Pattern Memory Viewer
spmemory      = Sprite Memory Viewer
register      = Register Viewer
```

### 4.2 规范

| 项 | 规则 |
| --- | --- |
| 编码/换行 | UTF-8 **无 BOM**、**LF**（与 `docs/coding-style.md` §11 一致，工具校验） |
| 注释 | `;` 或 `#` 起头；`;;` 为"给翻译人员的说明"（可选保留） |
| 分节 | `[section]` 分组；完整 key = `section.name`（如 `menu.file.open`） |
| 键值分隔 | 第一个 `=`；两侧空白 trim；值可用双引号包裹以保留首尾空格 |
| 转义 | `\n` 换行、`\t` 制表、`\\` 反斜杠；不支持续行（一条一行，便于 diff） |
| 占位符 | 仅 printf 风格：`%s` / `%d` / `%u` / `%02X` 等，**禁止** `%1`（FormatMessage）与 `%1$s`（MSVC 不支持） |
| 助记符 | 语言文件里**不写** `&` 与 `\tCtrl+O`（由 win32 侧按命令 ID 追加，mac 侧忽略） |
| 省略号 | 统一写三个半角点 `...`（ASCII，便于编辑与工具校验）；mac 侧显示时自动替换为 `…`（U+2026，HIG），win32 原样使用 |
| 缺失处理 | 缺 key → 回退内置英文 → 仍缺 → 显示 key 本身并 `LOG_WAR`（绝不显示空白） |
| 多余 key | 忽略，仅 DEBUG 日志提示（允许翻译文件滞后于模板） |

### 4.3 `[meta]`

- `id`：语言 ID（BCP-47），必须与文件名一致，否则告警并以文件名为准。
- `name`：**用该语言自身书写**的名称（`English` / `简体中文` / `日本語` / `Français`），直接显示在"语言"菜单 —— 这样用户看不懂当前语言时也能找到入口。
- `version`：模板版本；低于内置版本时提示"翻译可能过期"（不阻止加载）。

### 4.4 占位符与语序

统一 printf 风格，调用点自己格式化：

```c
ines_snprintf(buf, len, ines_i18n_text(ISTR_KEY("msg.err_unsupported_mapper")), mapper_id);
```

翻译**不得调整占位符顺序**（C 运行时无位置参数可移植方案）；如确需换语序，改英文原文使其适配。工具 `tools/i18n_check` 会校验各语言文件的占位符序列与模板完全一致。

---

## 5. 内置英文与模板（已定：方案 A）

### 5.1 方案 A（采用）：内置英文表 + 由表导出模板

- `comm/i18n_en.c`：编译期内置英文表（`key -> English`），**英文真源在代码里**。
- `tools/gen_i18n_template.c`（纯 C 小工具，随项目构建）：从内置表导出 `lang/en.ini` 模板（含分组、注释骨架）。
- 其它语言文件基于 `lang/en.ini` 复制翻译；程序运行时加载覆盖内置表。
- 优点：无任何外部文件时界面仍是完整英文（发布版更健壮，mac bundle 不必强制打包 lang）；英文与代码同仓同审。
- 缺点：改英文要重编译（英文改动本就低频）；内置表与模板需工具保持一致（提交前跑校验）。

### 5.2 备选方案 B（未采用）：英文也是外部文件

- `lang/en.ini` 是唯一真源，随程序分发；缺失时降级显示 key（或极小内置兜底集）。
- 优点：英文改动无需重编译；翻译人员只面对文件。
- 缺点：文件丢失/未随包 → 界面变 key，用户可见事故。

### 5.3 结论与落地约束

采用 A（健壮性优先）。落地时遵守：

1. **内置表是英文唯一真源**，改英文必须改 `comm/i18n_en.c` 并重跑 `tools/gen_i18n_template` 重新导出 `lang/en.ini`。
2. 提交前跑 `tools/i18n_check`，保证「内置表 ↔ `en.ini` ↔ 各语言文件」三者 key 集一致（不一致 → 非 0 退出）。
3. `lang/en.ini` 纳入版本库，作为翻译模板；**不允许**手工编辑后与内置表失配（工具会报）。
4. 运行时仍会加载 `lang/en.ini`（若存在），可与内置表互相覆盖 —— 便于在不重编译的情况下微调英文，但正式改动务必回写内置表。

---

## 6. API 草案（`comm/i18n.h`）

```c
#ifndef __INES_I18N_H__
#define __INES_I18N_H__

#include "idef.h"

#ifdef __cplusplus
extern "C" {
#endif

#define INES_I18N_ID_MAX     16      /* 语言 ID 缓冲(en / zh-CN) */
#define INES_I18N_NAME_MAX   64      /* 语言显示名(UTF-8) */

typedef struct _ines_i18n_lang_
{
    char  id[INES_I18N_ID_MAX];      /* en / zh-CN / ja / fr */
    char  name[INES_I18N_NAME_MAX];  /* English / 简体中文 / 日本語 */
    int   builtin;                   /* 1 = 内置(不可被覆盖) */
} ines_i18n_lang_t;

/* 初始化: preferred 为前端探测到的系统语言(可 NULL); 扫描 lang 目录并建立语言清单。
   返回 0 成功。可重复调用(切换语言前的重新扫描)。 */
int  ines_i18n_init(const char* preferred);
void ines_i18n_fini(void);

/* 枚举可用语言(含内置 en), 返回数量。langs 可为 NULL(仅取数量)。 */
int  ines_i18n_enum(ines_i18n_lang_t* langs, int max_count);

/* 把系统语言标签归一到可用语言 ID: "zh-Hans-CN" -> "zh-CN" -> "zh" -> "en"。
   返回静态缓冲, 永不返回 NULL。 */
const char* ines_i18n_match(const char* lang_tag);

/* 切换语言, 成功返回 0; 未知 ID 返回 -1(不改变当前语言)。 */
int  ines_i18n_set_language(const char* id);
const char* ines_i18n_language(void);          /* 当前语言 ID */

/* 取文本。key 为 ASCII("menu.file.open"); 缺失时回退英文, 再缺返回 key。
   返回进程内静态串(切换语言后失效), 不要缓存指针跨语言切换使用。 */
ines_cstr_t ines_i18n_text(const char* key);

/* 带格式: ines_i18n_text_fmt(buf, len, "msg.err_unsupported_mapper", 4);
   内部即 ines_snprintf(buf, len, ines_i18n_text(key), ...) */
ines_str_t  ines_i18n_text_fmt(ines_str_t buf, ines_size_t len, const char* key, ...);

#ifdef __cplusplus
};
#endif

#endif
```

要点：

- **key 用 `const char*`**（ASCII 字面量，不包 `ISTR`）；**返回值用 `ines_cstr_t`**（win32 下是 `const wchar_t*`，POSIX 下是 `const char*`），调用点直接 `MessageBox(hWnd, ines_i18n_text("msg.x"), ...)` / `[NSString stringWithUTF8String:...]` 无需改写法。
- win32：加载时把 UTF-8 值一次性转成 UTF-16（`MultiByteToWideChar(CP_UTF8)`），运行时零转换开销。
- 内部存储：哈希表或有序数组二分（约 400 条，二分足够，避免引入动态哈希复杂度）。

---

## 7. 两端接入

### 7.1 win32

1. **RC 文本改英文**：`iNES.rc` 的菜单、对话框、STRINGTABLE 全部改为**英文纯文本**（不含 `&` 与 `\tCtrl+O`），作为"原生语言"基准布局。
2. **助记符/快捷键由代码追加**：新增一张平台表（`iNES.c`）：

   ```c
   typedef struct { UINT id; const char* key; char mnemonic; const char* accel; } APP_MENU_TEXT;
   /* { IDM_OPEN, "menu.file.open", 'O', "\tCtrl+O" }, ... */
   ```
   应用时拼成 `Load ROM(&O)...\tCtrl+O`（非英语时 `&` 仍在，但助记符字母由表决定，避免翻译后冲突/丢失）。
3. **菜单应用**：`WM_INITMENUPOPUP` 或语言切换时遍历 ID 调 `SetMenuItemInfo(MIIM_STRING)`；子菜单标题同理。
4. **对话框**：每个 `WM_INITDIALOG` 里调用

   ```c
   i18n_apply_dialog(hDlg, APP_DLG_NETPLAY_ITEMS, count_of(...));  /* 文本 + 自适应 */
   ```
   表结构 `{ 控件ID, i18n key, 自适应标志 }`。
5. **提示框**：`ISTR("请先载入一个ROM。")` → `ines_i18n_text("msg.load_rom_first")`。
6. **工具窗口标题**：`LoadString(IDS_WND_*_TITLE)` → `ines_i18n_text("view.register")`（`IDS_*` 可保留为英文兜底，逐步弃用）。

### 7.2 mac

1. **菜单**：`buildMenuBar` 里所有 `@"载入ROM…"` → `APP_I18N(@"menu.file.open")`；其中

   ```objc
   static NSString* APP_I18N(NSString* key)   /* mac/i18n 桥接, 定义在 iNESApp.m */
   {
       return [NSString stringWithUTF8String:ines_i18n_text([key UTF8String])];
   }
   ```
2. **切换语言**：重新执行 `buildMenuBar` + 刷新窗口标题 + 刷新已打开的对话框（关闭重开或遍历更新）。
3. **对话框/调试窗口**：控件创建时 `APP_I18N(...)`，随后按 §8.2 测量重排。
4. **快捷键**：mac 菜单用 `keyEquivalent`，与翻译无关，不动。
5. **省略号**：`APP_I18N()` 内部把文本结尾的 `...` 替换为 `…`（U+2026），菜单项与按钮统一（符合 HIG）；语言文件仍写 ASCII 的 `...`。

---

## 8. 自适应尺寸（重点）

### 8.1 win32

新增 `win32/i18n_fit.c`（或并入 `iNES.c`）提供：

```c
typedef struct
{
    int         id;        /* 控件 ID */
    const char* key;       /* i18n key */
    unsigned    flags;     /* FIT_GROW_W(可横向拉伸) | FIT_ANCHOR_RIGHT(右对齐) | FIT_WRAP(可换行) */
} APP_FIT_ITEM;

void i18n_apply_dialog(HWND hDlg, const APP_FIT_ITEM* items, int count);
```

算法：

1. 取对话框字体（`WM_GETFONT` / `DS_SETFONT` 的 `MS Shell Dlg`）→ `GetTextExtentPoint32` 量文本宽高。
2. 需要的宽度 = 文本宽 + 控件装饰（按钮 `2*SM_CXEDGE + 6`，单选/复选 `+ SM_CXMENUCHECK`，分组框 `+ 2*边距`）。
3. 若 `need > current` → 加宽；`FIT_ANCHOR_RIGHT` 的控件保持右边缘不动（左移）；`FIT_WRAP` 的静态文本用 `DT_CALCRECT` 算换行后的高度并加高。
4. 累计"最右溢出量" → 若超过对话框客户区，用 `SetWindowPos` 加宽对话框（保持左上不动），并让 `FIT_GROW_W` 的控件（EDIT / LIST / 整行静态文本）跟随变宽；底部按钮行整体右移/下移。
5. 主菜单自适应由系统处理；**主窗口尺寸**不变（客户区随画面缩放，与文本无关）。

### 8.2 mac（手工 frame 布局，无 Auto Layout）

现状是"先定 frame 再设文本"，改成**先设文本 → 测量 → 定位**：

```objc
/* 通用: 设文本并按内容收紧, 返回实际尺寸 */
static NSSize app_fit(id control, NSString* text)
{
    if([control respondsToSelector:@selector(setTitle:)])       [control setTitle:text];
    else                                                        [control setStringValue:text];
    if([control respondsToSelector:@selector(sizeToFit)])       [control sizeToFit];
    return [control frame].size;
}
```

- 按钮：`sizeToFit` 后按 `contentInsets`/最小宽度（如 ≥ 72pt）与右边界对齐布局。
- 标签（`NSTextField`）：`sizeToFit` 后限制最大宽度，超出则设 `preferredMaxLayoutWidth`-等价行为（手工计算行数：用 `NSString boundingRectWithSize:options:attributes:`）并加高。
- 容器/窗口：按"所有控件最大右边界 + 边距"重算 `contentSize`，再 `setFrame:display:`。
- 每个对话框的 `layoutXXX` 函数改为"测量—定位"两趟（先测量全部，再算位置），保持现有手工布局风格，不引入 Auto Layout。

验收：法文（通常比英文长 20~30%）、日文（较短但字体度量不同）下按钮不截断、控件不重叠、窗口不裁切。

---

## 9. 语言选择菜单（位置已定）

两端现有菜单：`文件 / 控制 / 工具 / 帮助`（win32 另含 `IDC_POPUP`，mac 另含 Apple 菜单）。都没有"视图/显示"菜单，"工具"里第一个功能项就是"选项…"。

| 端 | 位置（已定） | 形态 |
| --- | --- | --- |
| win32 | **「工具(&T)」→ 第 2 项**，即紧跟 `选项(&O)...` 之后、原分隔线之前：`POPUP "语言(&L)"` | 子菜单列出 `ines_i18n_enum()` 结果，当前项 `MFT_RADIOCHECK`，命令 ID = `IDM_LANG_BASE + index` |
| mac | **「工具」→ 第 2 项**，紧跟 `选项…` 之后、原分隔线之前：「语言」子菜单（**不加 `…`**，它是展开子菜单而非弹对话框） | 同样枚举，当前项 `state = NSControlStateValueOn` |

平台习惯依据：

- **Windows**：无独立"设置"窗口的桌面程序，语言通常挂在"工具/视图"菜单下（如 MPC-HC、PotPlayer 的 Language 子菜单）；本项目"工具"是唯一的设置类入口，"选项…"之后放"语言"最易发现。
- **macOS**：HIG 建议语言放"设置…"（⌘,），但本项目 mac 的"选项…"目前是 `showUnimplemented`（**未实现**），放进去等于不可用；退而放"工具"菜单，与 win32 保持同一位置，两端代码与文档一致。若日后"选项…"窗口落地，可在其中再加一个语言下拉作为第二入口（**本次不做**，避免两处状态同步成本）。

其它约定：

- 子菜单项文本直接取各语言文件 `[meta] name`（English / 简体中文 / 日本語 / Français）—— 用户看不懂当前界面语言时也能凭本语言名找到入口。
- 切换后：写 `config.ini` `[ui] language = ja` → `ines_i18n_set_language()` → 刷新菜单/窗口/对话框（§7）→ 自适应（§8）。
- 已打开的对话框**（包括调试视图窗口）即时刷新标题与按钮**；窗口内部的标签/表头按 §10.1 的边界处理。

---

## 10. Key 清单与规模（分期）

| 期 | 内容 | 条数（估） |
| --- | --- | --- |
| **M1** | 框架（`comm/i18n`）+ 语言菜单 + 主窗口标题/状态 + 主菜单（文件/控制/工具/帮助，约 60 项）+ 通用提示（`msg.*`，约 40 条） | 约 150 |
| **M2** | 4 个对话框（关于/网络对战/载入 ROM/局域网快速对战）+ 载入 ROM 列表列名 + 局域网房间表列名 | 约 90 |
| **M3（精简）** | 7 个调试/查看窗口的**纯 UI 元素**：窗口标题、按钮、表头、通用标签（地址/数据/值…）；**不翻**内部硬件术语（§10.1） | 约 40 |

合计约 **280 条**（原估 440 条，因 M3 只翻纯 UI 元素而大幅收敛）。

命名：`section.name`，section ∈ `app / status / menu / msg / dialog.<name> / view.<name> / debug.<name>`。

### 10.1 翻译边界（按决策 ③）

**翻译（纯 UI 元素）**

- 窗口/对话框标题、按钮（确定、取消、关闭、浏览、应用…）
- 菜单及其子菜单
- 表头与列标题、分组框标题、复选框/单选按钮标签
- 状态文本、提示/确认/错误消息
- 通用名词：地址、数据、值、行、列、断点、运行、暂停、单步、复位、查看…

**不翻译（术语与数据）**

- 寄存器名与标志位：`A` `X` `Y` `PC` `SP` `P` / `N V U B D I Z C`（寄存器查看器 46 项标签整体保留英文）
- 指令助记符（`LDA` `JMP` `STA`…）与反汇编输出
- 硬件/子系统缩写：`CPU` `PPU` `APU` `DMA` `NMI` `IRQ` `VBlank` `CHR` `PRG` `SRAM` `VRAM` `Mapper` `Sprite` `Name Table` `Pattern Table`、端口号、十六进制与数值
- 文件路径、ROM 名、昵称、IP

→ 这些条目在 `lang/*.ini` 里**不出现**（不是"值等于英文"的冗余条目），避免翻译人员误翻；程序侧直接写字面量。模板导出时同样跳过。

**例外：寄存器查看器的"说明"列**（46 条，如 `累加器` / `音量/包络/占空比`）。它不是术语本身而是**功能说明短语**，因此做成**代码内多语字面量**：`s_defs[].note_zh`（简）+ `note_zh_tw`（繁）+ `note_en`（英），按当前语言选一套（`zh-TW/HK/Hant` → 繁体；其它 `zh*` → 简体；其余 → 英文），**不进 `lang/*.ini`**。

- 理由：若进语言文件就要给 ja/fr 各翻 46 条，而说明列是**固定 26 字符列**的等宽网格（`IDBG_RV_NOTE_COLS`，两端一致），法语译文普遍超出会被裁切；按 §10.1 的"硬件/术语保持英文"原则，非中文语言统一用英文说明即可。
- 英文说明**控制在 ≤26 字符**，不得超出说明列宽度。
- 实现两端同构（参考 win32 `wReg_InitTexts()`）：定义表是 UTF-8 常量，**建窗口/切语言时各转换一次**并缓存（`mac` 的 `s_noteText[]` / win32 的 `s_noteText[]`），绘制热路径不再做编码转换。
- win32 侧待办见 §16.2 第 6 项。

---

## 11. 语言文件产出（zh-CN / zh-TW / ja / fr / th / ar）

### 11.1 简体中文：沿用现有界面文案（决策 ⑥）

- `lang/zh-CN.ini` 初版**直接沿用现有界面中文文案**逐条搬运（win32 RC 与 mac 源码里现在的说法），不另起炉灶 —— 老用户看到的措辞与现在一致。
- 仅做少量**术语统一**（两端/同义不一致时取其一）：
  - 缓冲帧数（Cache frames）
  - 局域网快速对战（两端文案一致）
  - 卷轴查看 / 图形查看 / 图案内存查看 / 精灵内存查看（沿用现名）
- 顺手修掉现有文案里的笔误（如 win32 RC 的 `卷轴查看(&N),,,` → `...`）。
- 之后要改中文措辞只动 `lang/zh-CN.ini`，无需改代码。

### 11.1b 繁体中文（台湾）：`lang/zh-TW.ini`

- 语言 ID **`zh-TW`**，`[meta] name = 繁體中文`（菜单里自然排在 `简体中文` 之后）。
- 以 `lang/zh-CN.ini` 为底本转写，**不逐字直转**，按台湾习惯调整术语：
  内存→記憶體、线程→執行緒、端口→連接埠、地址→位址、服务器→伺服器、客户机→用戶端、
  协议→協定、网络→網路、局域网→區域網路、连接→連線、支持→支援、覆盖→覆寫、
  字节→位元組、文件→檔案、文件夹→資料夾、复位→重設、重新上电→重新上電、
  调试→偵錯、视图→檢視、精灵→精靈、手柄→手把、截图→擷圖、全屏→全螢幕、
  存档/读档→存檔/讀檔、日志→日誌、帮助→說明、文件(菜单)→檔案。
- 标点沿用底本风格：中文全角标点；省略号仍写 ASCII `...`（mac 渲染时替换为 `…`）。
- 术语与数据（ROM / CHR / PRG / Mapper / OAM / DMA / CPU …）保持英文。
- 寄存器查看器的说明列另有**繁体字面量**（`note_zh_tw`，见 §10.1 例外条款），
  语言 ID 含 `TW` / `HK` / `Hant` 时取用。
- 增删条目后用 `tools/i18n_check lang/zh-TW.ini` 校验（key 集、占位符、编码、换行）。

### 11.2 日语 / 法语

**日语一律常体**（决策 ⑧）：不使用 `です・ます`，提示语统一为体言止め / `〜する` / `〜か？`（例：`先に ROM を読み込め。`、`セーブ %d が存在しない、または現在の ROM と一致しない。`）。法语用 `vous`。

1. 由 `tools/gen_i18n_template` 导出 `lang/en.ini`（M1 完成时产出完整模板）。
2. 复制为 `lang/ja.ini` / `lang/fr.ini`，填 `[meta]`：

   ```ini
   [meta]
   id      = ja
   name    = 日本語
   version = 1
   ```
3. 逐条翻译右侧值（保留 `%s/%d`、不写 `&`/`\t`）。示例（`ja.ini` / `fr.ini`）：

   | key | en | ja | fr |
   | --- | --- | --- | --- |
   | `menu.file.open` | Load ROM... | ROMを読み込む... | Charger une ROM... |
   | `menu.control.pause` | Pause | 一時停止 | Pause |
   | `msg.load_rom_first` | Please load a ROM first. | 先に ROM を読み込め。 | Veuillez d'abord charger une ROM. |
   | `dialog.netplay.cache_frames` | Cache frames | バッファフレーム数 | Images en mémoire tampon |
   | `status.running` | Running | 動作中 | En cours |

4. 放到 `<数据目录>/lang/`（或 mac app 的 `Resources/lang/`）→ 重启即出现在"语言"菜单，**无需改代码**。
5. `tools/i18n_check` 校验：key 集一致、占位符一致、UTF-8 无 BOM、LF。

---

## 12. 工具与校验

- `tools/gen_i18n_template.c`：内置英文表 → `lang/en.ini`（含分组注释骨架）。
- `tools/i18n_check.c`：比对 `内置表 / en.ini / 各语言 ini`；报告缺失、多余、占位符不一致、编码与换行问题；非 0 退出（可挂到提交前自检）。
- 二者为纯 C 小工具，随仓库提供，构建方式写入 `docs/build.md`（不进入主目标）。

---

## 13. 实施计划与验收

| 期 | 任务 | 验收标准 |
| --- | --- | --- |
| **M1** | `comm/i18n.{c,h}` + `comm/i18n_en.c`（内置英文表，约 150 条）+ `CMakeLists.txt` 登记 + 语言菜单（两端）+ 主菜单/标题/状态/通用提示接入 + `tools/gen_i18n_template` + `lang/zh-CN.ini` | ① 系统语言为中文 → 首次启动中文；英文系统 → 英文；无匹配（如德语）→ 英文 ② 语言菜单可切换，菜单/标题/提示即时变化 ③ 无 lang 文件时界面为完整英文 ④ win32/mac 均编译无新增警告 |
| **M2** | 4 个对话框（关于/网络对战/载入 ROM/局域网快速对战）+ 列表列名接入；两端自适应布局例程；`lang/ja.ini`、`lang/fr.ini`；`tools/i18n_check` | ① 英/中/日/法四语下按钮无截断、控件无重叠、窗口无裁切 ② `i18n_check` 全绿 ③ 5 ROM 启动冒烟、网络对战/局域网面板可用 |
| **M3（精简）** | 7 个调试/查看窗口的**纯 UI 元素**（窗口标题、按钮、表头、通用标签），按 §10.1 边界执行；`zh-CN/ja/fr` 补齐相应条目 | ① 7 个窗口标题与按钮随语言切换 ② 寄存器名/助记符/硬件术语保持英文不变 ③ 调试器功能不变（600 帧哈希、断点行为不受影响） |

**本次一次性完成 M1 + M2 + M3（精简）**（决策 ⑤），按上表顺序推进、分三段验收；每段结束同步一次文档与 `CMakeLists.txt`。

> 进度：**mac 侧 M1+M2+M3 已全部完成并通过冒烟测试**（记录见 §17）；**win32 侧待实施**，交接清单见 §16。

每次改动同步：`docs/api.md`（新增 `ines_i18n_*` 公共 API）、`docs/build.md`（工具构建）、`CMakeLists.txt`（新源文件）。

---

## 14. 风险与取舍

| 风险 | 说明与对策 |
| --- | --- |
| win32 本机无法编译验证 | `win32/` 改动只能由你（Win 侧）编译；M1 我会先把 mac 侧跑通，win32 侧严格按同一 key 表与同构代码改，减少来回 |
| 字体与字形缺失 | win32 `MS Shell Dlg` 在非对应语言系统上依赖字体回退（日文汉字在中文系统可能回退到宋体，字形可接受但度量变化）→ 自适应按实际字体测量，不写死宽度 |
| 译文长度极端 | 德语/法语常比英文长 30%+；对话框已按"溢出即加宽"处理，需限制单条文本长度（工具可警告超长值，如 > 60 字符） |
| 语序与占位符 | 禁用位置参数，工具校验占位符序列一致；翻译只能按原语序 |
| 双重真源漂移 | 内置英文表与 `en.ini` 可能不一致 → `i18n_check` 提交前必跑 |
| 运行时切换遗漏 | 文本分散在多个窗口 → 提供"未翻译扫描"辅助：DEBUG 下把仍为英文的项记日志（仅 M1 期间使用） |
| 语言文件被用户改坏 | 解析失败只跳过该文件并 `LOG_WAR`，不影响启动 |
| 线程安全 | i18n 仅 UI 线程使用；切换语言时不会处于模拟线程读取路径（模拟线程不取 UI 文本） |
| mac 手工 frame 布局的维护成本（决策 ④） | **本次不引入 Auto Layout**：4 个对话框 + 7 个调试窗口改"测量后定位"是可控的一次性成本；若日后界面重排频繁（窗口可缩放、控件动态增减），再评估整体迁移 Auto Layout 的收益。本次实现需把布局逻辑收敛成"测量—定位"两趟的独立函数，避免把尺寸计算散落在创建代码里，为将来迁移留口子 |

---

## 15. 决策记录

| # | 议题 | 结论 | 影响章节 |
| --- | --- | --- | --- |
| ① | 英文真源 | **内置英文编译期表**（方案 A）+ 工具导出模板 | §5 |
| ② | 语言菜单位置 | 两端均为**「工具」菜单第 2 项**（紧跟「选项…」之后） | §9 |
| ③ | 调试窗口翻译范围 | **只翻纯 UI 元素**（标题/按钮/表头/通用标签），硬件与 CPU 术语保留英文 | §10.1 |
| ④ | mac 是否改 Auto Layout | **本次不改**，仍用手工 frame「测量后定位」；布局收敛成独立函数，日后可评估迁移 | §8.2、§14 |
| ⑤ | 交付范围 | **M1 + M2 + M3（精简）一次做完**，含 `zh-CN.ini` / `ja.ini` / `fr.ini` | §13 |
| ⑥ | 简体中文文案来源 | **沿用现有界面中文文案**，仅做少量术语统一与笔误修正 | §11.1 |
| ⑦ | 首次运行的语言选择 | **自动匹配，不提示**：`系统语言 → zh-Hans-CN/zh-CN/zh → en`，只在日志记 INFO | §1.1、§3.2 |
| ⑧ | 日语语体 | **常体**（不用 `です・ます`）；法语用 `vous` | §11.2 |
| ⑨ | 两端分工 | mac 侧先做完并冒烟，再交接 win32 侧按同一 key 表实施 | §16 |

---

## 16. Win32 待完成工作（交接清单）

> mac 侧（M1+M2+M3 精简）已全部完成并通过冒烟测试。win32 侧按本章实施，key 表与行为必须与 mac 完全一致。
> 前置：本机（mac）**无 MSVC / `rc.exe`，win32 改动无法编译验证**，只能靠"同构改写 + 逐条比对 key"来降低返工。

### 16.1 已完成、win32 可直接复用的部分

| 项 | 位置 | 说明 |
| --- | --- | --- |
| 公共 i18n 层 | `comm/i18n.{c,h}`、`comm/i18n_en.c` | 纯 C，已在 `CMakeLists.txt` 登记（`inescore` 目标也用得到）；win32 下 `ines_cstr_t` = UTF-16 |
| 语言文件 | `lang/{en,zh-CN,ja,fr}.ini` | 已随 CMake 拷贝到程序目录 `lang/`（win32 见 CMake 的 `lang` 拷贝段，新加语言文件无需改脚本） |
| key 真源 | `comm/i18n_en.c` | 约 168 条；**改英文只改这里**，再跑 `gen_i18n_template` 覆盖 `lang/en.ini` |
| 校验工具 | `tools/gen_i18n_template.c`、`tools/i18n_check.c` | 构建方式见 `docs/build.md` §7；提交前必跑 |
| 系统语言探测 | 未做 | win32 需自行取（`GetUserDefaultUILanguage` / `GetLocaleInfoEx` → BCP-47 标签），再交给 `ines_i18n_init()` |

### 16.2 win32 侧待办（按序）

1. **初始化**
   - `main()` 里：`ines_i18n_add_lang_dir("<程序目录>\\lang")`（数据目录 `lang/` 由公共层自动优先）→ 取系统语言标签 → `ines_i18n_init(tag)`。
   - 读 `config.ini` 的 `[ui] language`：有值且可用则 `ines_i18n_set_language()`（**不存在/不可用时不写回**，保持自动匹配）。
   - 切语言后同 mac：`SetConfigStr(ISTR("ui"), ISTR("language"), id)`。
2. **RC 全部改英文**：`win32/iNES.rc` 的菜单/对话框/字符串改为 `en.ini` 对应英文；**助记符 `&` 与快捷键 `\t` 不进语言文件**（决策 §4.2），由 win32 侧维护一张 `ID → 助记符/快捷键` 表，在 `i18n_apply_dialog` 时追加。
3. **菜单与标题接入**：主菜单文本、`WM_SETTEXT` 类标题、`msg.*` 通用提示走 `ines_i18n_text()`。
4. **自适应尺寸**（§8.1）：
   - 对话框：`GetTextExtentPoint32` 测量 → `SetWindowPos` 加宽/下移；按钮最小宽度约 78px（与 mac 一致）。
   - 列表列名：`ListView_SetColumnWidth` 按表头文本宽度。
   - 7 个调试窗口复用同一例程（纯 UI 元素，§10.1 边界）。
5. **语言菜单**：「工具」菜单**第 2 项**（紧跟「选项…」，之前插一个 `IDM_LANGUAGE_BASE + i`），`ines_i18n_enum()` 枚举、当前语言打勾；切换后**重画所有已打开窗口**。
   与 mac 一样支持动态加载（§3.5）：在 `WM_INITMENUPOPUP` 里先 `ines_i18n_rescan()` 再重建条目，运行时新增的 `lang\*.ini` 不必重启。
6. **寄存器查看器「说明」列补英文与繁体**：`win32/wRegister.c` 的 `s_defs[]` 增 `note_zh_tw` + `note_en` 两列（照抄 `mac/iNESRegisterView.m` 的 46 条×2，**列序必须与结构字段序 `note_zh, note_zh_tw, note_en` 一致**），`wReg_InitTexts()` 按当前语言选一套写入 `s_noteText[]`（`TW/HK/Hant` → 繁体，其它 `zh*` → 简体，其余 → 英文）；切语言时重新调用一次并重绘（win32 目前无语言切换，接入 i18n 后顺带生效）。英文说明**不得超过 26 字符**（`WREG_NOTE_COLS`，与 mac 一致）。两端表格必须逐行对齐，否则说明会张冠李戴。
7. **语言文件**：`lang/*.ini` 已含 `en / zh-CN / zh-TW / ja / fr / th / ar`；CMake 用 `file(GLOB)` 自动打包，新增语言只需丢一个 ini 进 `lang/`，无需改脚本（win32 拷到 exe 同目录 `lang\`，mac 进 `Resources/lang`）。
   - **阿拉伯语只有译文、未做 RTL**：界面仍按 LTR 排版（顺序、对齐、菜单方向都不镜像）。将来要做 RTL 属于布局层工作（`mac/iNESUiLayout`、`win32` 自适应尺寸），不必改语言文件。
7. **验收**（对齐 §13）：
   - 英/中/日/法四语下：按钮无截断、控件无重叠、窗口无裁切。
   - 系统语言 zh / ja / fr / 德语（无匹配 → 英文）四种首次启动。
   - 语言切换即时生效（含已打开的调试窗口/对话框）。
   - `i18n_check` 全绿；MSVC 无新增警告。

### 16.3 win32 实施时的注意点

- UNICODE 下日志格式串：key / 语言 id 是 `char*`，必须用 `%S`（`comm/i18n.c` 已用 `I18N_FMT_S` / `I18N_FMT_KEY` 宏，照抄即可）。
- `ines_i18n_text()` 返回进程内静态串，**切换语言后旧指针失效**，不要缓存；每次用时取。
- 语言文件省略号统一写 ASCII `...`；win32 不需要 mac 那个 `…` 替换逻辑。
- 布局逻辑收敛成"测量—定位"两趟的独立函数（决策 ④ 同样适用于 win32），别把尺寸计算散落在创建代码里。

---

## 17. mac 实施记录（2026-09-20）

已完成：

- `comm/i18n.{c,h}` + `comm/i18n_en.c`（168 条英文）+ `CMakeLists.txt` 登记；语言文件随 bundle 拷贝到 `Resources/lang/`。
- `mac/iNESi18n.{h,m}`：`L10N` / `L10NF` 宏 + 系统语言探测（`[NSLocale preferredLanguages]`）+ 配置读写；首次运行只记日志、不提示。
- `mac/iNESUiLayout.{h,m}`：`INESFitLabel` / `INESFitButtons` / `INESFitWindowHeight` / `INESEnsureContentWidth`（测量—定位两趟）。
- 主菜单/窗口标题/状态、4 个对话框（关于/网络对战/载入 ROM/局域网）、7 个调试窗口的纯 UI 元素、寄存器查看器全部接入；工具菜单第 2 项为「语言」。
- **寄存器查看器「说明」列双语化**（2026-09-22）：`s_defs[]` 增 `note_zh` / `note_en` 两列，按语言选一套；文本缓存 `s_noteText[]` 与 `idbg_rv_init_texts()` 参考 win32 `wReg_InitTexts()`（建视图 / 切语言各转换一次，绘制热路径不转换）；视图监听 `INESLanguageDidChangeNotification` 重刷并重绘。
  实测（探针打日志后已移除）：`lang=en → A=Accumulator / PC=Program counter`；切 zh-CN → `A=累加器 / PC=程序计数器`；切 ja → 回落英文（`A=Accumulator`），符合 §10.1「术语/硬件说明非中文语言保持英文」。
- **繁体中文 `lang/zh-TW.ini`**（2026-09-22）：168 条，以 zh-CN 为底本按台湾用语转写（§11.1b）；系统语言 `zh-Hant-TW` 经匹配规则第 2 轮落到 `zh-TW`。寄存器说明列同步增加 `note_zh_tw` 一列（三套字面量按语言选一）。
  实测：菜单 `檔案 / 載入 ROM…`、对话框标题 `載入 NES 檔案`、主窗口 `iNES - 90tank - 執行中`、寄存器窗口 `暫存器檢視器`；说明列 zh-TW → `累加器 / 堆疊指標（頁 1）/ 手把 2 按鍵（讀取）`，zh-CN → `累加器 / 栈指针(页 1) / 手柄 2 按键(读)`，en → `Accumulator / Stack pointer (page 1) / Joypad 2 buttons (read)`。`i18n_check` 对 4 个语言文件全部 OK。
- `tools/gen_i18n_template.c` / `tools/i18n_check.c`；`lang/{en,zh-CN,ja,fr}.ini`。

实施中修掉的两个问题（避免 win32 重踩）：

1. **`en.ini` 与内置英文表漂移** → 用 `gen_i18n_template` 重新生成覆盖（`i18n_check` 提交前必跑）。
2. **mac 的 `L10NF` 不能用 `[NSString initWithFormat:arguments:]`**：NSString 的 `%s` 按"系统编码"解释字节，UTF-8 的中日文参数会变空串（实测标题变成 `iNES - 90tank - `）。改为先在 C 层 `ines_vsnprintf` 拼好再转 NSString（`mac/iNESi18n.m`）。win32 不受影响（本就走 C 层），但若在哪用了 `StringCchPrintf` 之外的托管格式化，需同样注意。

冒烟结果（mac，系统语言 zh-CN）：四语切换即时生效（标题 `iNES - 90tank - {En cours / 動作中 / 运行中}`、已打开的"寄存器查看器"标题同步刷新）；法语「载入 ROM」对话框 720×552、按钮 `Charger(95)`/`Annuler(93)` 无截断；日语「网络对战」按钮 `開始(76)`/`キャンセル(108)` 无截断；无新增编译警告。
