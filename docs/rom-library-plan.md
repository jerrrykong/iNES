# ROM 库（ROM Library）设计与双端实现规格

> 需求：把"打开 ROM"从"每次浏览文件夹"扩展为"可维护的 ROM 库"——配置一组路径，自动汇总其中的 ROM，支持搜索、排序、一键载入。
> **配套改动（2026-10-06，用户拍板）**：「文件 → 载入ROM...」**不再弹自定义列表对话框**，改为直接调用**系统文件浏览面板**挑 `.nes`；原 `dlgOpenRom.c` / `iNESOpenRomDialog.{h,m}` **两端都删除**，文件属性列表的能力由本 ROM 库承接（§3 的 8 列迁到 ROM 库，列标题 key 继续复用 `dialog.openrom.col_*`）。
> 状态：**mac 侧已实现并冒烟通过**（§11）；**win32 侧待实现**，本文 §1~§10 即为其实现规格，两端行为必须一致。

---

## 1. 入口与窗口形态

| 项 | 规格 |
|---|---|
| 菜单项 | 「文件」菜单 → `menu.file.rom_library` = `ROM Library...`（中文「ROM库...」） |
| 位置 | 紧跟「Load ROM...」之后、「Unload ROM」之前（两端一致） |
| 快捷键 | 无 |
| 窗口形态 | **非模态**独立窗口，可与主窗口同时存在；重复点菜单只是把已有窗口前置 |
| 载入行为 | 点「载入」后主窗口运行该 ROM，**ROM 库窗口不关闭** |
| 关闭 | 窗口标题栏的关闭按钮；关闭时把视图状态落盘 |

| 姊妹菜单 | 「Load ROM...」(`menu.file.open`) 现在**只**调系统文件浏览面板挑 `.nes`（mac: `NSOpenPanel` + `allowedFileTypes=@[@"nes"]`；win32: `IFileDialog` + `*.nes` 过滤）。它记住的目录仍是 `[rom] last_dir`，**"取消"也会记住面板当前所在目录**（沿用旧对话框的行为）。拖放 / 命令行 / Finder 打开方式三条载入路径不变。 |

mac 对应类：`mac/iNESRomLibrary.m`（`iNESRomLibrary : NSWindowController`）。
win32 建议：`win32/dlgRomLib.c` 里的对话框过程 + 一个非模态窗口包装（`IDD_ROMLIB`）。

## 2. 窗体布局（上中下三段）

```
┌──────────────────────────────────────────────────────────────┐
│ [设置] [刷新]              共 N 个 ROM        搜索: [______]   │  ← 操作区(一行)
├──────────────────────────────────────────────────────────────┤
│ 文件名 │ ROM大小 │ Mapper │ PRG │ CHR │ 镜像 │ 电池 │ Trainer │  ← ROM 列表(可拉伸)
│  ...                                                              │
├──────────────────────────────────────────────────────────────┤
│ /完整/路径/显示/框                                    [载入]  │  ← 底部
└──────────────────────────────────────────────────────────────┘
```

- **操作区**：从左到右 = `设置` 按钮、`刷新` 按钮、计数文字、`搜索:` 标签 + 搜索输入框。
  计数文字是**只读状态提示**，放在按钮与搜索区之间的空白处（右对齐），**不是控件**，因此不计入"操作区"的控件序列。
- **底部**：ROM 完整路径显示框（只读、**头部省略**以保留文件名、可选中复制）+ `载入` 按钮。**没有关闭按钮**。
- `载入` 是窗口默认按钮（回车 = 载入）；未选中行时禁用。
- 双击列表行 = 载入（等价 win32 的 `NM_DBLCLK`）。

### 2.1 尺寸常量（两端尽量一致，便于对照）

| 常量 | 值 | 说明 |
|---|---|---|
| 边距 | 8 | 内容区四周 |
| 控件行高 | 24 | 操作区按钮、底部按钮 |
| 按钮宽 | 88 | 设置 / 刷新 / 载入 |
| 按钮间距 | 6 | |
| 段间距 | 8 | 操作区↔列表、列表↔底部 |
| 搜索标签宽 | 56 | |
| 搜索框宽 | 180 | 随窗口宽度富余程度可拉伸，下限 80 |
| 初始内容区 | 760 × 520 | |
| 最小内容区 | 560 × 340 | |

mac 侧按钮宽度按译文自适应（`INESFitButtons`），但**顺序恒为 设置→刷新**（搜索区）/ **仅载入**（底部），并按 §2.1 的固定间距摆放。

## 3. 列表列定义

这 8 列原本属于已删除的「载入 NES 文件」对话框（`dlgOpenRom` / `iNESOpenRomDialog`），现由 ROM 库**独占**，列序、宽度、右对齐、译文 key 一律沿用旧表，保证与旧版界面观感一致：

| # | 列 | i18n key | 设计宽 | 对齐 |
|---|---|---|---|---|
| 0 | 文件名 | `dialog.openrom.col_name` | 220 | 左 |
| 1 | ROM 大小 | `dialog.openrom.col_rom_size` | 80 | 右 |
| 2 | Mapper | `dialog.openrom.col_mapper` | 60 | 右 |
| 3 | PRG | `dialog.openrom.col_prg` | 70 | 右 |
| 4 | CHR | `dialog.openrom.col_chr` | 70 | 右 |
| 5 | 镜像 | `dialog.openrom.col_mirror` | 60 | 左 |
| 6 | 电池 | `dialog.openrom.col_battery` | 50 | 左 |
| 7 | Trainer | `dialog.openrom.col_trainer` | 60 | 左 |

- **复用 `dialog.openrom.*` 的列标题 key，不要另建 key**：两张表的列语义完全相同，复用可保证两端与各语言译文天然一致。
- 列宽下限 40；宽度按译文表头测量后取 `max(设计宽, 测量宽 + 14)`，因此窄语言下不必加宽窗口（列表可横向滚动）。
- 属性列的取值规则与原 `dlgOpenRom` 一致（见 §4.4）。

## 4. 数据来源与扫描规则

### 4.1 扫描时机（关键：不是每次都扫）

| 触发 | 行为 |
|---|---|
| **首次打开 ROM 库且本地没有缓存文件** | 全量扫描一次 → 写缓存 |
| 之后每次打开 | **只读缓存**，不扫盘 |
| 「设置」里路径列表发生变化（增/删） | 关掉设置对话框后**立刻全量扫描** → 写缓存 |
| 点「刷新」 | **立刻全量扫描** → 写缓存 |

### 4.2 扫描的具体规则

1. 遍历配置里的路径列表，**逐个目录枚举，不递归子目录**（沿用原 `dlgOpenRom` 的单目录语义；见 §8 取舍 1）。
2. 只收扩展名 `.nes`（**不区分大小写**）。
3. 多个路径可能重叠（父目录 + 子目录）→ 按**规范化后的完整路径去重**，同一文件只出现一次。
4. 目录不存在 / 不可访问 → 记一条警告日志并跳过该目录，不影响其它目录。
5. 总量上限 `8192` 个文件（所有路径合计），达到即停止枚举。
6. 基础顺序 = 完整路径升序（保证结果稳定可预期）。

### 4.3 懒加载：分批解析文件头

- 阶段 1（同步、很快）：枚举目录条目，**不打开文件**，填入完整路径、文件名、文件大小；此时属性列显示为空。
- 阶段 2（定时器分片）：逐个打开文件读 **16 字节 iNES 文件头**，补齐 Mapper / 镜像 / 电池 / Trainer / PRG / CHR。
  - 分片参数（沿用原 `dlgOpenRom`）：定时器间隔 **10ms**，单片时长预算 **20ms**，单片最多 **32** 个文件。
  - 解析失败（非 iNES 文件、读不出 16 字节）→ **从列表移除**该行。
- 解析完成后再应用排序并回写缓存（属性未齐时排序不可信；此时点表头只更新箭头，排序推迟到解析结束）。

### 4.4 文件头解析规则

与 `core/rom.c` 的 `ines_rom_load_from_file()`、原 `dlgOpenRom` 的解析函数**逐条一致**（win32 侧删掉 `dlgOpenRom.c` 后，这套解析代码迁到 `dlgRomLib.c`）：

```
tag != "NES\x1a"            -> 非法
PROM_block_num == 0         -> 非法
mapper_num  = (flag1 >> 4) | (flag2 & 0xF0)
mirror_type = (flag1 & 0x08) ? FOUR_SCREEN : ((flag1 & 0x01) ? VERT : HORZ)
has_sram    = flag1 & 0x02
has_trainer = flag1 & 0x04
prg_kb      = PROM_block_num * 16
chr_kb      = VROM_block_num * 8
```

文件大小取自目录条目（文件头里没有这一项）。体积格式化：`<1KB` → `B`、`<1MB` → `KB`、否则 `MB`（两位小数），与原 `dlgOpenRom` 相同。

## 5. 缓存文件（`romlib.dat`）

- 位置：`<数据目录>/romlib.dat`
  - mac：`~/Library/Application Support/iNES/romlib.dat`
  - win32：可执行文件同目录（与其他数据文件一致）
- 编码 **UTF-8 无 BOM**，换行 **LF**；**整文件覆盖写**。
- 格式：**纯文本**，首行是版本标识，其后每行一个 ROM，字段用 **TAB** 分隔，共 8 个字段：

```
ROMLIB1
<完整路径>\t<文件大小>\t<Mapper>\t<镜像>\t<电池0|1>\t<Trainer0|1>\t<PRG_KB>\t<CHR_KB>
```

| 规则 | 说明 |
|---|---|
| 版本行 | `ROMLIB1`；与文件首行不一致 → **丢弃缓存，重新扫描** |
| 字段数 | 必须是 8；不足的行**跳过**（不整体失败） |
| 非法路径 | 含 TAB 或换行的路径**无法安全存取，跳过写入**（下次刷新会重新扫到） |
| 读缓存为空 | 视为"没有缓存" → 触发一次全量扫描 |
| 用途 | 只是"省掉重复扫盘"，**不是权威数据**；缓存里的文件可能已被删除，载入时再校验一次 |

## 6. 配置文件（`config.ini` 的 `[romlib]` 段）

| Key | 类型 | 默认 | 含义 |
|---|---|---|---|
| `count` | int | 0 | 路径条数（上限 256） |
| `path0` … `pathN-1` | str | - | 路径列表，**按列表顺序** |
| `sort_column` | int | -1 | 排序列序号（0~7），-1 = 未排序（保持基础顺序） |
| `sort_asc` | int | 1 | 1 = 升序，0 = 降序 |
| `selected` | str | 空 | 上次选中的 ROM **完整路径**（不是行号，避免排序/过滤后错位） |
| `scroll` | int | 0 | 列表**首行序号**（滚动位置） |
| `frame_x` / `frame_y` | int | 0 | 窗口位置（Cocoa 坐标，左下为原点） |
| `frame_w` / `frame_h` | int | 0 | 窗口大小；小于最小内容区时视为无效 |

- 路径条数变少时，把不再使用的 `pathN` 槽位**写成空串**（该 Key 无删除接口）。
- `pathN` 的**值是纯路径**，不含任何分隔符，因此不受 `CONFIG_VAL_LEN`(1024) 限制。
- 写入时机：路径变化立即写；排序、选中、滚动、窗口移动/缩放走**延迟合并保存**（约 0.5s），窗口关闭前再强制存一次。
- 恢复时机：窗口创建时读回；`frame` 恢复只接受**与某块屏幕可见区域相交**的位置，避免把窗口丢到屏幕外。

## 7. 交互细则

### 7.1 搜索

- 搜索框输入即生效（逐字符过滤，不需要回车）。
- 匹配对象：**ROM 文件名**（不含目录），`包含` 关系，**不区分大小写与变音符号**。
- 过滤**不改变排序**：先按排序规则排好，再取子集。
- 过滤期间保留选中项（若该 ROM 仍在结果中）；否则清空选中并禁用「载入」。
- **搜索关键字不写配置**（下次打开仍是全量列表，避免"打开后什么都没看见"的困惑）。

### 7.2 排序

- 点击表头：同列重复点击在升/降之间切换，换列默认升序（`NSTableView` / `LVS_SORTASCENDING` 的既有行为）。
- 排序依据是**属性数值**，不是列上的显示文本（所以 ROM 大小按字节数排，不是按 "1.20 MB" 的字符串排）。
- 主键相同时再按**完整路径**升序，保证结果稳定。
- 扫描（分批解析）期间点表头：只更新箭头，排序推迟到解析结束。

### 7.3 选中与载入

1. 选中行 → 底部路径框显示该 ROM 的**完整路径**，「载入」变为可用。
2. 点「载入」或双击该行：
   - 再次校验文件是否仍存在；不存在 → 提示 `dialog.romlib.file_missing`，**不关窗口**；
   - 未选中行 → 提示 `dialog.romlib.select_rom_first`；
   - 交给上层加载（mac：`iNESAppController` 的 block，走与「载入ROM...」完全相同的分支——**联网中先弹确认框**，确认后通知对端，再请求模拟线程加载）；
   - **载入成功后键盘焦点交回主窗口**（主窗口 `makeKeyAndOrderFront` + 第一响应者复位为视频视图），以便立刻用键盘操作游戏；**ROM 库窗口保持打开**（只是失去焦点）。

### 7.4 状态提示（计数文字，5 种状态，按优先级）

| 状态 | 文案 key | 触发条件 |
|---|---|---|
| 扫描中 | `dialog.romlib.scanning_format` = `Scanning %ld/%ld ...` | 分批解析进行中 |
| 未配置路径 | `dialog.romlib.no_path` | 路径列表为空 |
| 没有可用 ROM | `dialog.romlib.no_supported` | 路径非空但一个 ROM 都没有 |
| 搜索无结果 | `dialog.romlib.no_match` | 有 ROM，但过滤后为空 |
| 正常 | `dialog.romlib.total_format` = `%lu ROM(s)` | 显示**过滤后**的条数 |

### 7.5 路径设置对话框（模态）

| 项 | 规格 |
|---|---|
| 标题 | `dialog.romlib.paths_title` = `ROM Library Paths` |
| 内容 | **单列**列表（`dialog.romlib.col_path` = `Path`），每行一个路径，**无表头**；过长路径中间省略，悬停显示完整值 |
| 底部按钮 | 左侧 `删除`(`dialog.romlib.remove`) / `添加`(`dialog.romlib.add`)，右侧 `关闭`(`dialog.romlib.close`，兼 Esc 快捷键)；**没有确定/取消**；改动即时反映在列表里，**关闭对话框时**才告知调用方"有变化" |
| 按钮位置 | 底部一行：`删除` `添加` 靠左依次排列，`关闭` 贴右边距 |
| 尺寸 | 初始 560 × 340，最小 400 × 240 |
| 删除 | 删除当前选中的行；未选中行时按钮禁用；删除后选中后继行（便于连续删除） |
| 添加 | 弹出**系统文件夹选择面板**（标题 `dialog.romlib.select_dir_title`，只能选目录、单选、可新建目录）；确认后**追加到列表末尾并选中新行** |
| 重复检查 | 追加前比较**规范化路径**（去首尾空白与末尾 `/`、`~` 与符号链接展开）；已存在 → 提示 `dialog.romlib.dir_exists` 并放弃，**列表里不会出现两个相同路径** |
| 生效 | 对话框关闭后，若有变化 → 写配置 + **立刻全量刷新**列表并回写缓存 |

## 8. 已定的取舍（两端都按此执行）

1. **不递归子目录**：只收配置路径**本身**这一层。要支持递归属于后续增强，两端必须同时改。
2. **计数文字放在操作区**：需求只枚举了"设置/刷新/搜索"三个控件，计数是只读提示，不新增控件。
3. **路径设置对话框没有确定/取消**：需求只要求"删除/添加"。代价是"改了一半"也会生效——这与"改完关窗即刷新"的语义一致。
4. **搜索只看文件名、不持久化**：见 §7.1。
5. **选中项存完整路径而不是行号**：排序/过滤都会改变行号，存路径才不会错位。
6. **缓存是纯文本**：便于跨平台共用同一份文件、便于手工排查；代价是体积（1788 个 ROM ≈ 170KB）。
7. **单次扫描上限 8192**：防止把整个机械硬盘目录树塞进界面。
8. **「载入ROM...」改用系统文件浏览面板**（2026-10-06）：浏览、类型筛选、路径输入、取消/重做全部交给操作系统；代价是**丢掉了"选文件夹 + 看文件属性"的列表视图**——那部分能力由 ROM 库（§2、§3）承接，两者互补：临时挑一个 ROM 用系统面板，管理 ROM 库用 ROM 库窗口。

## 9. i18n

新 key（英文真源在 `comm/i18n_en.c`，改英文后要用 `tools/gen_i18n_template` 重新导出 `lang/en.ini`）：

| key | en |
|---|---|
| `menu.file.rom_library` | `ROM Library...` |
| `dialog.romlib.title` | `ROM Library` |
| `dialog.romlib.settings` | `Settings` |
| `dialog.romlib.refresh` | `Refresh` |
| `dialog.romlib.search` | `Search:` |
| `dialog.romlib.load` | `Load` |
| `dialog.romlib.paths_title` | `ROM Library Paths` |
| `dialog.romlib.col_path` | `Path` |
| `dialog.romlib.add` | `Add` |
| `dialog.romlib.close` | `Close` |
| `dialog.romlib.remove` | `Remove` |
| `dialog.romlib.select_dir_title` | `Select the folder to add to the library` |
| `dialog.romlib.dir_exists` | `This path is already in the library.` |
| `dialog.romlib.no_path` | `No path configured; click Settings to add one.` |
| `dialog.romlib.no_supported` | `No supported NES files found` |
| `dialog.romlib.no_match` | `No ROM matches the search.` |
| `dialog.romlib.total_format` | `%lu ROM(s)` |
| `dialog.romlib.scanning_format` | `Scanning %ld/%ld ...` |
| `dialog.romlib.select_rom_first` | `Please select a ROM first.` |
| `dialog.romlib.file_missing` | `The file no longer exists; please refresh the library first.` |

- 省略号在语言文件里统一写 ASCII `...`，mac 渲染时替换成 `…`（沿用 `comm/i18n` 既有规则）。
- 译文状态：`en`（模板）/ `zh-CN` / `zh-TW` / `ja` / `fr` **已补齐**；`ar` / `th` 暂缺 → 运行时自动回退英文。
- 列表的 8 个列标题**复用 `dialog.openrom.*`**，不新增。
- `dialog.openrom.select_file_title` = `Select the NES file to load`：**新增**，用于系统文件面板的标题。
- `dialog.openrom.*` 里其余 key（`title` / `load` / `cancel` / `folder` / `no_folder` / `no_nes_file` / `parsing_*` / `total_format` / `select_dir_title` / `dir_invalid` / `file_missing`）随旧对话框一起**不再被任何代码引用**，但**仍保留在内置表与语言文件里**：i18n 的规则是"key 只增不改"，删除会让各语言文件出现"多余 key"报错。**仍在用的**只有 8 个列标题 + `yes` / `no` + 4 个镜像名。
- win32 侧新增的菜单项要按既有约定在 `win32/i18n_ui.c` 的 ID→key 表里补映射。

## 10. win32 实现指引

| mac | win32 建议 |
|---|---|
| `iNESRomLibrary`（`NSWindowController`） | `win32/dlgRomLib.c`：`IDD_ROMLIB` 模板 + **非模态**窗口（`CreateWindow` + `ShowWindow(SW_SHOW)`，不要 `DialogBox`）；载入成功后 `SetForegroundWindow(hMainWnd)` + `SetFocus(hMainWnd)` 把焦点交回主窗口 |
| `iNESRomLibraryPaths` | `win32/dlgRomLibPaths.c`：`IDD_ROMLIBPATHS`，**模态**（`DialogBox`）；底部按钮 = `删除` `添加` 靠左 + `关闭`（`IDCANCEL`，Esc 生效）靠右 |
| `NSOpenPanel` | `IFileDialog`（FOS_PICKFOLDERS）或 `SHBrowseForFolder`（`BIF_RETURNONLYFSDIRS \| BIF_NEWFOLDERSTYLE`）；`BROWSEINFO.lpszTitle` 用 `dialog.romlib.select_dir_title`；短路径要用 `GetLongPathName` 还原后再比较/保存 |
| `NSTableView` + `sortDescriptorPrototype` | `LVM_SORTITEMS` + `LVS_SORTASCENDING/LVS_SORTDESCENDING`；**注意本工程无 manifest → comctl32 v5，`HDF_SORTUP/HDF_SORTDOWN` 不会画箭头**（与原 `dlgOpenRom` 同样的既有限制，需自行处理） |
| `NSScrollView` 文档坐标 | `LVM_GETTOPINDEX` / `LVM_ENSUREVISIBLE` |
| `config.ini` | `GetPrivateProfileInt/String` + `WritePrivateProfileString`，**键名与 §6 完全一致** |
| `romlib.dat` | 同一份格式（§5）；以二进制方式写，路径含非 ASCII 时注意 UTF-8 转换 |
| 菜单 | 在 `win32/iNES.rc` 的「文件」菜单加一项，ID 建议 `IDM_ROM_LIBRARY`，位置在 `IDM_OPEN` 之后 |
| **删除** `win32/dlgOpenRom.c` | 用户已拍板：`IDM_OPEN`（Load ROM...）不再弹它，改用系统文件浏览；`dlgOpenRom.c` 及其在 `CMakeLists.txt` 的条目、`iNES.rc` 里的 `IDD_OPENROM` 资源一并删除。§4.4 的文件头解析代码**迁到 `dlgRomLib.c`** 复用 |
| `IDM_OPEN` 的新实现 | `IFileDialog`（`FOS_FILEMUSTEXIST \| FOS_PATHMUSTEXIST \| FOS_FORCEFILESYSTEM`），文件类型过滤 `"*.nes"`（大小写不敏感由系统保证），`FILEOK` 结果取 `IShellItem::GetDisplayName(SIGDN_FILESYSPATH)`；标题用 `dialog.openrom.select_file_title`。目录仍写 `[rom] last_dir`（**取消时也写**当前目录）。拖放 / 命令行 / `ShellExecute` 三条路径不受影响 |

实现完成后请在本节打勾并补充与 mac 的差异点。

- [ ] win32 侧实现
- [ ] 两端配置文件键名逐项核对
- [ ] 两端缓存文件互读验证（mac 写的 `romlib.dat` 能否被 win32 直接读出列表）

## 11. mac 侧实施记录（2026-10-06）

新增文件：

| 文件 | 内容 |
|---|---|
| `mac/iNESRomLibrary.h/.m` | ROM 库窗口（`iNESRomLibrary`） |
| `mac/iNESRomLibraryPaths.h/.m` | 路径设置对话框（`iNESRomLibraryPathsDialog`） |

改动文件：`CMakeLists.txt`（`INES_MAC_OBJC_SOURCES` 增 2 个 `.m`）、`mac/iNESApp.h/.m`（菜单项 + `showRomLibrary:` + 退出时关闭）、`comm/i18n_en.c`、`lang/{en,zh-CN,zh-TW,ja,fr}.ini`。

同日第二轮优化：载入成功后 `makeKeyAndOrderFront` + 第一响应者复位为视频视图（焦点回主窗口）；路径对话框加 `dialog.romlib.close`（靠右 + Esc）。

同日第三轮：「载入ROM...」改用 `NSOpenPanel`（`canChooseFiles=YES` / `canChooseDirectories=NO` / 单选 / `allowedFileTypes=@[@"nes"]` / 标题 `dialog.openrom.select_file_title` / `directoryURL` 取 `romInitialDir`），新增 `-rememberOpenDir:` 统一写 `[rom] last_dir`（取消也写），**删除 `mac/iNESOpenRomDialog.h/.m`** 与 CMake 条目、`iNESApp.m` 的 import，文档 §3 的 8 列说明改为"由 ROM 库独占"。冒烟：面板标题正确显示「请选择要载入的 NES 文件」，方向键+回车选中 `ZZZ_UNK_Super Contra 7.nes` 后主窗口进入"运行中"，`[rom] last_dir` 记录为 `bin/ROM`。

冒烟结果（`~/Downloads/NES/任天堂FC全集`，1788 个 ROM，日志与界面双向确认）：

| 检查项 | 结果 |
|---|---|
| 菜单打开非模态窗口 | ✅ 与主窗口并存 |
| 首次全量扫描 + 写缓存 | ✅ `romlib.dat` 1787 行，配置写入 `frame_*` |
| 二次打开只读缓存 | ✅ 日志 `1788 ROM(s) loaded from cache`，无 `enumerated` |
| 搜索过滤 | ✅ 输入 `Mario` → 26 行 |
| 选中行 | ✅ 底部显示完整路径、「载入」可用 |
| 载入 | ✅ 主窗口标题变为运行该 ROM，**ROM 库窗口仍在** |
| 载入后焦点转移 | ✅ `AXMain` 从 ROM 库窗口切到主窗口（键盘可直接操作游戏） |
| 路径对话框「关闭」按钮 | ✅ 靠右排列、点击后结束模态并按新路径刷新；兼 Esc |
| 排序恢复 | ✅ 配置 `sort_column=1` → 首行是最小文件（24592 B）、末行是最大（3145744 B） |
| 选中项恢复 | ✅ 重开后自动选中上次的 ROM |
| 设置→删除路径 | ✅ 列表立刻清空、状态变"未配置路径"、配置 `count=0`、缓存清空 |
| 设置→添加 | ⚠️ 面板正常弹出（标题译文正确），面板内的"前往文件夹/打开"属系统 UI，AX 自动化未跑通，需人工点一次确认 |
| 重复路径提示 | ⚠️ 同上（逻辑与代码评审通过，未跑通 UI） |
