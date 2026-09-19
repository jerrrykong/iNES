# Win 侧交接文档：联网协议版本控制 + 联机读档（状态同步）

- **交接方**：macOS 侧（本轮改动的实现方；mac 目标已编译、已启动冒烟、已过双进程自测）
- **接收方**：负责 `win32/` 的 CodeBuddy（Windows + MSVC）
- **日期**：2026-09-19
- **必读设计文档**（本文件只讲"要做什么"，设计取舍在里面）：
  - `docs/netplay-protocol-version-plan.md` —— 版本规则（§2）、握手流程（§5）、缓存帧数（§6）、版本变更表（§8）、拍板结论（§9）、实现状态（§10）
  - `docs/netplay-state-sync-plan.md` —— 同步流程（§4~§6）、失败矩阵（§7）、存档格式兼容（§8）、实现要点与拍板（§12）

---

## 0. 一句话结论

共用层 `comm/` 已经把 **协议版本号 + 版本校验 + 缓冲帧数强制一致 + 联机读档状态同步** 全部实现；`win32/` 的调用点也已同步改完。但**交接方本机没有 MSVC，win32 一行都没编译过**。

Win 侧的任务按顺序是：
1. **编译通过**（P0，见 §4）
2. **取消两个缓冲帧数下拉框**，与 mac 端 UI 保持一致（P1，见 §5.1 / §5.2，**已拍板，照做即可**）
3. **与 mac 端（`NET_VER=2`）真机联调**（P2，见 §6）

> **2026-09-19 拍板（用户）**：**缓冲帧的设计两端保持一致** —— mac 与 win32 **都不提供缓冲帧数下拉框**，唯一来源是 `config.ini` 的 `[netplay] cache_num`（无配置 = 4，钳位 1~5），UI 只作只读显示，发布后不可改；修改入口后续放"设置"。

---

## 1. 拍板规则（动联网代码前必须遵守）

| # | 规则 | 说明 |
|---|------|------|
| 1 | **版本号单一整数，不区分主次** | 不兼容修改 → `NET_VER` +1；兼容修改 → 不变。判定一句话：**"旧端按原逻辑还能不能正常对战"**，拿不准就按不兼容 +1。当前 `NET_VER = 2` |
| 2 | **版本不同 → 拒绝连接，提示升级** | 含「联网对战…」手工建服务器/直连；握手失败时 `START_RSP.fno` 回传服务端版本，提示可带"本机 x / 对端 y" |
| 3 | **房间列表显示版本号，异版本灰显不可选** | beacon 的 `pad` 区拿 4 字节放 `net_ver`（`LAN_VER` 不动，属兼容扩展）。旧端不携带 → 显示"旧版（需升级）" |
| 4 | **缓冲帧数必须两端一致，由主机下发，客户端禁止回落本地默认**；**两端 UI 也保持一致：都不提供下拉框** | 值来自主机 `config.ini` 的 `[netplay] cache_num`，无配置则 `NP_CACHE_DEFAULT`(4)，钳位 `NP_CACHE_MIN(1)~NP_CACHE_MAX(5)`；读到 0/越界 → **拒绝连接**。含手工主从模式。UI 只作只读显示（mac 已取消下拉框；win32 待改，见 §5.1 / §5.2） |
| 5 | **读档仅主机可发起；存档双方均可** | 从机读档菜单灰显（`win32/iNES.c:2707`） |
| 6 | **载入失败 → 硬件复位，绝不中断连接** | 同步**已发起**后任一方失败（含 sign 不一致、格式版本不匹配）→ 主机在解冻后首帧提交 `NET_CTRL_CODE_HARDRESET`(=**1**，不是 2)，走 ctrl + 延迟线双方同帧复位。例外：**本地校验失败（还没发包）→ 不复位、继续对战**；**TCP 链路真断/超时 → 才结束联网** |

---

## 2. 本轮改动清单

### 2.1 共用层 `comm/`（Win/Mac 各编译一份，已定稿）

| 文件 | 改动 |
|------|------|
| `comm/net.h` | `NET_VER` 由 `0x0101` 改为 **2**（L112）；新增 `NET_CMD_STATE_REQ/RSP/DATA/DONE/GO = 0x20~0x24`（L29-33）与 5 个 `pack(1)` 包结构（L69-97）；`START_RSP` 失败时 `fno` 语义 = 服务端版本（L21-22）；声明 `net_send_all()`（L144） |
| `comm/net.c` | 新增 `static net_wait_write()`（L477，select 等可写）与 `net_send_all()`（L495，循环 send + 5s 超时）。**非阻塞 socket 上大包必须走它** |
| `comm/npsession.h` | `NP_SYNC_NONE/BUSY/OK/RESET/FAILED`（L143-147）、`NP_SYNC_CHUNK 512` / `NP_SYNC_CHUNKS 8` / `NP_SYNC_MAX_SIZE 512KB` / `NP_SYNC_TIMEOUT 5` / `NP_STATE_VER 1`；回调类型 `np_state_load_fn` / `np_state_sign_fn`；API `np_sync_set_handler/begin/poll/state/clear/cancel` |
| `comm/npsession.c` | ① 握手版本不符 → `rsp.code=1` 且 `rsp.fno = NET_VER`；② 客户端**强制**采用服务端 `cache_num`（`START_RSP.fno` 高 32 位），缺失/越界 → 拒绝（提示"对端版本过旧…"），**删除了原来的本地默认回落分支**；③ 同步状态机 `SYNC_IDLE/WAIT_RSP/SEND/WAIT_DONE/RECV_DATA/WAIT_GO` + `np_crc32()`；④ `np_frame_begin()` 优先处理 `cmd >= NET_CMD_STATE_REQ`（DATA 未收全时 `break` 保留，**不能落进 `default` 被丢弃**）；⑤ `np_reset()` 释放同步缓冲（`np_end()` 会走到） |
| `comm/lan.h` / `comm/lan.c` | beacon `pad[10]` → `ines_dword_t net_ver; ines_byte_t pad[6];`；`lan_room_t` 增 `net_ver`；`lan_advertise()` 填 `htonl(NET_VER)`；`lan_room_update()` 解析并写日志 `... ver=%u` |

> **Win 侧注意**：`comm/lan.c` 的广播在 Win 侧是"按接口子网定向广播"（`lan_iface_list()` + `GetAdaptersAddresses`），本轮合并时已保留该实现，不要退回 `sendto(255.255.255.255)`。

### 2.2 `win32/`（已改，**未编译**）

| 文件 | 位置 | 改动 |
|------|------|------|
| `win32/iNES.c` | L167 | 新增全局 `sync_ctrl_req`（待提交的硬复位控制码） |
| | L245-248 | 前向声明 `OnMenuSyncState` / `OnIdleSyncState` / `ines_state_load_mem` / `ines_state_sign` |
| | L349 | `WinMain` 内 `np_sync_set_handler(ines_state_load_mem, ines_state_sign, NULL);` |
| | L1187-1191 | `OnIdle` 联网分支：`OnIdleSyncState()` 返回非 0 则 return；`np_sync_state()==NP_SYNC_BUSY` 则**跳过本帧** |
| | L1375-1379 | 输入采集后：`if(sync_ctrl_req){ ctrl_key_state = sync_ctrl_req; sync_ctrl_req = 0; }` |
| | L2318-2332 | `OnMenuLoadState()` 联网时改走 `OnMenuSyncState(index)`（本机不立即载入） |
| | L2361 | `static ines_byte_t s_sync_buf[NP_SYNC_MAX_SIZE];`（512KB，静态存储，不要挪进栈） |
| | L2364 | `ines_crc32_mem()`（IEEE 802.3，表惰性初始化） |
| | L2401 | `ines_state_load_mem()`：写 `state\.sync.tmp` → `ines_load_state()` → 删除临时文件 |
| | L2452 | `ines_state_sign()`：RAM + name_table + sp_RAM + frame_count + reg_A/X/Y/P/SP/PC 的 CRC32 |
| | L2484 | `OnMenuSyncState()`：非主机拒绝、同步中拒绝、`GetSaveStateTime()` 为空/过大/读失败**仅提示**（不复位）、`np_sync_begin()` |
| | L2567 | `OnIdleSyncState()`：BUSY→`np_sync_poll()`；OK→`UpdateAllViews()`；RESET→仅主机置 `sync_ctrl_req = NET_CTRL_CODE_HARDRESET`；FAILED→`np_end(); is_net_play=0` |
| | L2705-2708 | `UpdateMenuLoadState()`：`is_net_play && (!np_is_server() \|\| np_sync_state()!=NP_SYNC_NONE)` → 灰显 |
| `win32/dlgNetPlay.c` | L20、L192-201 | `extern GetConfigInt`；手工模式 `cache_num` 从 `config.ini [netplay] cache_num` 读取并钳位（**下拉框已不再生效**，见 §5.1） |
| `win32/dlgLanMatch.c` | L41-42、L98-106 | `extern GetConfigInt/SetConfigInt`；房主 `cache_num` 从配置读取并钳位 |
| | L628-652 | 房间列表新增「版本」列（68）与「缓冲」列（56）：昵称 120 / ROM 180 / 版本 68 / 缓冲 56 |
| | L345-360 | 版本列文本：`2` / `N（需升级）` / `旧版（需升级）`；缓冲列 `N 帧` / `--` |
| | L426-430 | 加入时校验 `net_ver != NET_VER` → 提示"协议版本不一致，请升级到相同版本后再联机。" |
| | L699-733 | `OnItemChanged` 异版本禁用「加入」；`OnCustomDraw` 异版本/异 ROM 灰显（`COLOR_GRAYTEXT`） |
| | L473-487 / L616-623 / L767 | 缓冲帧数下拉框（改值重新发布 / 初始化 / `CBN_SELCHANGE`）—— **已拍板删除，改只读显示，见 §5.2** |

### 2.3 `mac/`（已改、已编译、已自测，供对照）

`mac/iNESApp.m`（`app_crc32` / `app_state_load_mem` / `app_state_sign` / `syncStateOnThread:` / `pollStateSyncOnThread:` / 模拟线程冻结 / `validateMenuItem`）、`mac/iNESLanLobby.m`（`hostCacheNum` 读配置、版本列、`rowJoinable:` 灰显）、`mac/iNESNetPlayDialog.m`（手工模式读配置）。

---

## 3. P0-0：先拿到代码

交接方本机的这些改动**尚未 commit**（`git status`：`comm/*`、`mac/*`、`win32/*` 为 modified，`docs/*` 为 untracked）。

Win 侧开工前二选一：
- **A（推荐）**：交接方 `git add -A && git commit && git push`，Win 侧 `git pull`
- **B**：`git diff > win32-netplay.patch` + `git status --porcelain` 里的 3 个新文档一起打包传给 Win 侧，`git apply`

---

## 4. P0：编译通过

### 4.1 构建命令

```bat
cmake -S . -B build
cmake --build build --config Release
:: 产物 bin\iNES.exe（Debug 为 bin\iNES_d.exe）
```

注意：VS 生成器**不带 `--config` 默认编 Debug**；要 Release 必须显式 `--config Release`。字符集：`iNES` = Unicode，`inescore` = MBCS，`core/`+`comm/` 会被编译两次。

### 4.2 风险点表（按出现概率排序）

| 症状 | 原因 / 处理 |
|------|-------------|
| `error C2065: 'NP_SYNC_MAX_SIZE' 未声明` 等 | `win32/iNES.c` 需 `#include "npsession.h"`（已含则检查 `net.h` 包含链） |
| `error LNK2019: 无法解析的外部符号 _GetConfigInt` | `dlgNetPlay.c` / `dlgLanMatch.c` 的 `extern ines_int_t GetConfigInt(ines_cstr_t, ines_cstr_t, ines_int_t)` 必须与 `win32/iNES.c:2216` 的定义**签名完全一致**（含 `ines_int_t`，不要写成 `int`） |
| `error C4013: 'net_send_all' 未定义` | `comm/net.c` 未加入编译 —— CMake 已含，确认没用旧的 VC 工程文件 |
| `warning C4101 未引用的局部变量` / `C4189` | `/W3` 下新增代码里的临时变量（如 `ines_int_t st`）确实用到了；若故意不用保留 `(void)x;` |
| `error C2664: ListView_InsertColumn` 参数 | 第 4 列已加，确认 `LVCOLUMN` 复用时 `mask` 仍带 `LVCF_SUBITEM`（`dlgLanMatch.c:630-652`） |
| `RC1022 unexpected end of file` / 资源中文乱码 | **编码规范**：全部源码 UTF-8 无 BOM + LF；例外 `win32/iNES.rc`（**必须带 BOM**）、`win32/targetver.h`（纯 ASCII）。新增中文字符串一律包 `ISTR()` |
| 全角括号/顿号显示成问号 | 同上的编码问题；MSVC 已加 `/utf-8`（`CMakeLists.txt:326`） |
| `s_sync_buf` 512KB 报栈溢出 / 编译极慢 | 它是 `static` 全局存储（`iNES.c:2361`），**不要**改成局部变量 |
| `ines_snprintf` 的 `%u` 输出异常 | 已用 `(unsigned)` 强转（`dlgLanMatch.c:346`）；`ines_dword_t` 与 `NET_VER`（int 宏）比较处也需 cast，见 L345/L426/L699 |
| `select()` 编译告警 | `net_wait_write()` 传 `sock + 1`；Windows 忽略 `nfds`，无害（POSIX 必须 +1，别改） |
| `net_send_all` 在 Win32 上死等 5 秒才失败 | 非阻塞 socket 满时 `send()` 返回 `SOCKET_ERROR`/`WSAEWOULDBLOCK`，循环里靠 `net_check_write()` + `net_wait_write()` 推进；若实测长时间无进展，检查 `net_set_async()` 在 Win32 分支确实设了 `FIONBIO` |

---

## 5. P1：界面/交互要修的 4 项

> **§5.1 与 §5.2 已由用户拍板（2026-09-19）：两端缓冲帧 UI 保持一致 —— 都不提供下拉框，只作只读显示。** 按下面步骤实施即可，不要再保留下拉框。

### 5.1 【已拍板·必改】取消「联网对战…」对话框的缓冲帧数下拉框

- 位置：`win32/iNES.rc:248`（`COMBOBOX IDC_CMB_CACHE,67,65,114,89`）+ `win32/dlgNetPlay.c:84/101-107/117`
- 现状：下拉框照常初始化并显示"4"，但 `dlgNetPlay_OnStartConnect()`（L192-195）已改成从 `config.ini [netplay] cache_num` 取值 → **用户改下拉框完全无效**，属于误导。mac 端早已取消该下拉框。

**实施步骤**

1. `win32/iNES.rc`：把 `IDD_NETPLAY` 里的 `COMBOBOX IDC_CMB_CACHE,...` 改成只读文本控件
   ```
   LTEXT   "",IDC_CMB_CACHE,67,65,114,20
   ```
   （沿用原 ID 可少改一处 `Resource.h`；若原处还有"缓冲帧数"标签，一并改成说明性文案，如"缓冲帧数：由 config.ini 的 [netplay] cache_num 决定"）
2. `win32/dlgNetPlay.c`：
   - 删除 L101-107 的 `CB_ADDSTRING` ×5 与 `CB_SETCURSEL`；
   - 删除 L84、L117 对 `IDC_CMB_CACHE` 的 `EnableWindow`（单选"客户端"时不再置灰这个控件）；
   - 改为在 `dlgNetPlay_OnInitDialog()` 里填只读文本：
     ```c
     ines_char_t  text[128];
     ines_snprintf(text, count_of(text), ISTR("缓冲 %d 帧（config.ini [netplay] cache_num）"),
                   (int)GetConfigInt(ISTR("netplay"), ISTR("cache_num"), NP_CACHE_DEFAULT));
     SetDlgItemText(hDlg, IDC_CMB_CACHE, text);
     ```
   - L192-201 的配置读取**保持不变**（钳位逻辑也不要删）。
3. 约束核对：**生效值必须来自配置**；客户端传多少都无效（以服务端下发为准）。

### 5.2 【已拍板·必改】取消局域网面板的缓冲帧数下拉框（与 mac 一致）

- 位置：`win32/iNES.rc:281`（`IDC_LANMATCH_CMB_CACHE,52,242,54,80`）、`win32/dlgLanMatch.c:473-487`（`dlgLanMatch_OnCacheChange`：改值 → 立即以新值重新发布）、L616-623（初始化）、L767（`CBN_SELCHANGE` 分支）
- 现状：`s_cache_num` 初值来自配置，但改下拉框只改**内存变量**并重新广播，**不落盘**（下次开还是旧值）。mac 端已取消，只显示"缓冲 N 帧，发布后固定"。

**实施步骤**

1. `win32/iNES.rc`：`IDD_LANMATCH` 的 `COMBOBOX IDC_LANMATCH_CMB_CACHE,52,242,54,80` →
   ```
   LTEXT   "",IDC_LANMATCH_CMB_CACHE,52,242,200,10
   ```
2. `win32/dlgLanMatch.c`：
   - 删除 L616-623 的 `CB_ADDSTRING` 循环与 `CB_SETCURSEL`；
   - 删除 `dlgLanMatch_OnCacheChange()`（L473-487）与 L767 的 `CBN_SELCHANGE` 分支；
   - 在 `dlgLanMatch_OnInitDialog()` 里填只读文本：
     ```c
     ines_snprintf(text, count_of(text), ISTR("缓冲 %d 帧（发布后固定，可在 config.ini 的 [netplay] cache_num 调整）"), s_cache_num);
     SetDlgItemText(hDlg, IDC_LANMATCH_CMB_CACHE, text);
     ```
   - L98-106 从配置读 `s_cache_num` 并钳位**保持不变**；beacon 广播（`dlgLanMatch_OnTimer` L514-515）继续带 `s_cache_num`。
3. 提示文案建议与 mac 对齐（mac `iNESLanLobby.m`）：
   `已发布到局域网（缓冲 %d 帧，发布后固定；可在 config.ini 的 [netplay] cache_num 调整）。加入他人房间后，本机作为副手柄（客户机）。`
   —— 对应 `iNES.rc:278` 的 `IDC_LANMATCH_LAB_HINT`（现文案为"已发布到局域网。加入他人房间后，本机作为副手柄（客户机）。"）。
4. 约束：发布后**不可改**（握手时才用这个值）；加入方不能用本机值。

### 5.3 【必查】局域网面板新增「版本」列，列宽需目视确认

- `dlgLanMatch.c:633-651`：120 + 180 + 68 + 56 = **424**；`iNES.rc:279` 的列表控件宽 **427** → 理论上刚好，但 Win32 列表有边框/滚动条，请实际看一眼**是否出现横向滚动条**，必要时把 ROM 列收到 176（mac 端用的 110/176/68/56）。
- 顺便确认：异版本行确实是灰字（comctl32 **v5**，无 manifest → `LVS_EX_DOUBLEBUFFER` 不生效属正常，不影响 `NM_CUSTOMDRAW` 灰显）。

### 5.4 【后续】`config.ini` 的 `cache_num` 没有配置入口

- `win32/iNES.rc:126` 有菜单项 `MENUITEM "选项(&O)...", IDM_OPTIONS`，但 `win32/iNES.c` 里**没有任何 `case IDM_OPTIONS`** —— 这是个死菜单（`Resource.h:50` 有 ID）。
- 用户已明确"缓冲帧数从配置读取（**后续再加上配置修改功能**）"。落点建议就是复用 `IDM_OPTIONS` 做一个「选项」对话框，至少放一个 1~5 的缓冲帧数选择，写 `[netplay] cache_num`。
- 此项**本轮不做**，但请在 Win 侧记录，别让 `IDM_OPTIONS` 继续当死菜单。

---

## 6. P2：真机验证清单（判据写死了，照着跑）

准备：两端都放同一个 ROM（crc32 必须一致）。mac 端 `NET_VER=2`；验证"版本不一致"需要一个 `NET_VER=1` 的旧版 exe（临时把 `comm/net.h` 的 `NET_VER` 改成 1 编一份即可）。

| # | 场景 | 判据 |
|---|------|------|
| V1 | 手工建服务器（「联网对战…」）：两端版本一致 | 正常握手；两端日志 `netplay: server/client(manual), cache_num=N`，**N 相同且 = 服务端 config 值** |
| V2 | 手工建服务器：一端 `NET_VER=1` | 拒绝连接，提示含"版本不一致 / 请升级"，不进入对战 |
| V3 | 局域网面板：mac 发布、Win 浏览 | Win 列表「版本」列显示 `2`；双方互相可见（若仍出现"Win 看不到 macOS 房间"，属**已知的另一个问题**，见 MEMORY：Windows 多网卡广播 / macOS 本地网络权限，与本轮无关） |
| V4 | 局域网面板：房间是旧版（不携带 `net_ver`） | 显示"旧版（需升级）"、灰显、「加入」禁用；点选提示"协议版本不一致，请升级到相同版本后再联机。" |
| V5 | 缓冲帧数：主机 `config.ini [netplay] cache_num=2`，客户端不配 | 双方实际 `cache_num` 都是 **2**（客户端不得回落到 4）；列表「缓冲」列显示 `2 帧` |
| V6 | 联机读档（**主机**发起） | 主机点「读档」→ 双方短暂冻结（画面停住，通常 <0.5s）→ 双方载入同一份状态 → 继续对战，无 desync；从机侧 `state\.sync.tmp` 用完即删，不覆盖从机本地存档槽位 |
| V7 | 联机读档（**从机**发起） | 菜单灰显，点了没反应（mac 已验证；Win 请确认 `UpdateMenuLoadState` 的灰显生效） |
| V8 | 读档失败（把从机的 `ines_state_load_mem` 临时改成 `return -1`） | 两端都不退出联网；主机提交硬复位 → **双方同帧复位**、游戏继续（弹出"存档载入失败，已复位重开。"）。**连接必须还在**（标题仍显示"联网对战中"） |
| V9 | 本地校验失败（主机选一个空槽位读档） | 只弹提示"该槽位没有可用存档。"，**不冻结、不复位**，对战继续 |
| V10 | 存档（双方各自「存档」） | 不受影响，仍可存（本轮未加限制） |
| V11 | 同步进行中直接关闭 ROM / 退出 | `np_end()` → `np_reset()` 会释放同步缓冲；不应崩溃、不应泄漏 |

---

## 7. 关键契约（动 win32 联网代码前必读）

1. **冻结期必须继续调用 `np_frame_begin()` 和 `np_sync_poll()`** —— 停掉就收不到包，必然超时。前端只在 `NP_SYNC_BUSY` 时跳过 `doframe`（`iNES.c:1187-1191`）。
2. **从机绝不能自行复位** —— 帧号会错位。只有主机提交 `NET_CTRL_CODE_HARDRESET`（=**1**，走 `NET_CMD_FRAME.ctrl` + 延迟线），双方才在同一逻辑帧复位。
3. **`net_is_server()` = "监听 socket 是否存在"**，而 `np_frame_input()` 用它决定主/副手柄路由 → **对战中绝不能关 listen socket**，只能由 `np_end()` / `net_close()` 关。
4. **socket 是非阻塞的**：`net_send()` 只 send 一次且不检查返回值；**任何大块数据必须走 `net_send_all()`**。接收缓冲仅 **1024B** → 单片 512B（含 3B 头 = 515B）是硬上限，别调大。
5. **`comm/net.c` 不是线程安全的**。Win 端是单线程（`OnIdle` + 模态对话框的 `WM_TIMER`），天然没问题；但别把联网调用挪到新线程。
6. **存档字节流可跨端**：`ines_save_state/load_state` 是 `FILE*` 字节流，结构全定长 + `pack(1)`、无指针/无 double → 小端平台字节级一致。同步时从机**不落盘自己的槽位**（临时文件 `state\.sync.tmp`，载入后删除）。
7. **编码**：源码 UTF-8 无 BOM + LF；`win32/iNES.rc` 保留 BOM、`win32/targetver.h` 纯 ASCII。新增中文串用 `ISTR()`；`np_poll()` 出来的 msg 是 **UTF-8**（`dlgNetPlay_SetInfo` / `lanmatch_utf8_to_tchar` 已做转换）。
8. **本轮没有新增源文件**，CMakeLists 不用改（若 Win 侧新增对话框 `.c`，记得手动加进 `CMakeLists.txt:144` 的 `INES_WIN32_SOURCES`）。

---

## 8. 接口速查（Win 侧唯一需要对接的会话层 API）

```c
// 启动注册一次（iNES.c:349）
void np_sync_set_handler(np_state_load_fn load, np_state_sign_fn sign, void* user);

int  np_sync_begin(const void* buf, int len);  // 仅主机；内部拷一份，可立即复用 buf
void np_sync_poll(void);                       // 主机每帧推进；从机不用调
int  np_sync_state(void);                      // NP_SYNC_NONE/BUSY/OK/RESET/FAILED
void np_sync_clear(void);                      // 结果只消费一次
void np_sync_cancel(void);                     // 取消，不发包

int  np_is_server(void);
int  np_cache_num(void);
```

`OnIdle` 里的接线顺序（`win32/iNES.c`）：

```
if (is_net_play) {
    if (OnIdleSyncState()) return;              // 处理同步结果；FAILED 时已结束联网
    if (np_sync_state() == NP_SYNC_BUSY) return;// 冻结：跳过本帧，但包照收
    if (!np_input_ready()) return;              // 网络卡
}
... 跑帧 ...
// 输入采集后（L1375）
if (sync_ctrl_req) { ctrl_key_state = sync_ctrl_req; sync_ctrl_req = 0; }
```

---

## 9. 版本纪律（下次动协议时怎么做）

1. 先判断"旧端按原逻辑还能不能正常对战"：能 → **不改 `NET_VER`**；不能 → **+1**；拿不准 → **+1**。
2. 改 `comm/net.h` 的 `NET_VER` 后，**必须**同步 `docs/netplay-protocol-version-plan.md` 的 §8 版本变更表（写清"版本号 / 变更内容 / 是否兼容 / 日期"）。
3. 改公共 API 同步 `docs/api.md`；新增/修改发现层字段同步 `docs/lan-quick-match-plan.md`。
4. 发布 checklist（详见方案文档）：两端 `NET_VER` 一致 → 房间列表版本显示正确 → 异版本拒绝 → 缓冲帧数一致 → 联机读档可用。

---

## 10. 附录 A：双进程自测工具（Win 侧可直接编来验证 `comm/` 层）

不依赖 core，只链 `comm/npsession.c` + `comm/net.c` + `comm/log.c`。mac 上已用它跑通"成功"与"从机载入失败"两条路径。Win 侧把下面内容存成 `npsync.c`（放 `/tmp` 或临时目录，**不要**提交进仓库），按注释编译：

```c
// 双进程自测: 握手 + 状态同步(联机读档)
//   用法: npsync s <port>
//         npsync c <ip> <port> [load_fail]
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "npsession.h"
#include "net.h"

// comm/log.c 需要的桩(不引入 core / 前端)
ines_cstr_t ines_get_data_dir(ines_str_t szPath, ines_size_t szLen)
{
    if ((szPath != NULL) && (szLen > 0))
        ines_strncpy(szPath, "./", szLen - 1);
    return szPath;
}

#define CRC    0x12345678u
#define LEN    70000          // > 单片上限, 走多片发送

static ines_byte_t  g_buf[LEN];
static int          g_load_fail = 0;
static int          g_loaded    = 0;
static int          g_len       = 0;

static int load_fn(const void* buf, int len, void* user)
{
    (void)user;
    if (g_load_fail) return -1;
    if ((buf == NULL) || (len != LEN)) return -1;
    if (memcmp(buf, g_buf, LEN) != 0) { printf("  !! content mismatch\n"); return -1; }
    g_loaded = 1; g_len = len;
    return 0;
}

static int sign_fn(ines_dword_t* out, void* user)
{
    (void)user;
    *out = 0xabcdef01u;
    return 0;
}

static int wait_poll(const char* who)
{
    int  i, rc;
    char msg[256];

    for (i = 0; i < 800; i++)
    {
        msg[0] = 0;
        rc = np_poll(msg, sizeof(msg));
        if (rc == NP_POLL_OK)    { printf("%s: playing (%s)\n", who, msg); return 0; }
        if (rc == NP_POLL_FAILED){ printf("%s: handshake failed: %s\n", who, msg); return -1; }
        /* Windows: Sleep(10); */
        usleep(10 * 1000);
    }
    printf("%s: handshake timeout\n", who);
    return -1;
}

int main(int argc, char** argv)
{
    int  i, rc, is_server;

    if (argc < 3) { printf("usage: npsync s <port> | npsync c <ip> <port> [load_fail]\n"); return 1; }

    is_server = (argv[1][0] == 's');

    for (i = 0; i < LEN; i++)
        g_buf[i] = (ines_byte_t)((i * 7 + 3) & 0xff);

    np_sync_set_handler(load_fn, sign_fn, NULL);

    if (is_server)
    {
        if (0 != np_begin(1, NULL, atoi(argv[2]), CRC, 4)) { printf("server: np_begin failed\n"); return 1; }
        if (0 != wait_poll("server")) return 1;
        if (0 != np_sync_begin(g_buf, LEN)) { printf("server: np_sync_begin failed\n"); return 1; }
        printf("server: sync begin (%d bytes)\n", LEN);

        for (i = 0; i < 4000; i++)
        {
            np_frame_begin();
            np_sync_poll();
            rc = np_sync_state();
            if (rc == NP_SYNC_OK)    { printf("server: SYNC OK (loaded=%d)\n", g_loaded); break; }
            if (rc == NP_SYNC_RESET) { printf("server: SYNC RESET\n"); break; }
            if (rc == NP_SYNC_FAILED){ printf("server: SYNC FAILED\n"); break; }
            /* Windows: Sleep(1); */
            usleep(1000);
        }
        if (i >= 4000) printf("server: sync timeout\n");
    }
    else
    {
        g_load_fail = (argc > 4) ? atoi(argv[4]) : 0;

        if (0 != np_begin(0, argv[2], atoi(argv[3]), CRC, 0)) { printf("client: np_begin failed\n"); return 1; }
        if (0 != wait_poll("client")) return 1;
        printf("client: load_fail=%d\n", g_load_fail);

        for (i = 0; i < 4000; i++)
        {
            np_frame_begin();
            rc = np_sync_state();
            if (rc == NP_SYNC_OK)    { printf("client: SYNC OK (loaded=%d, len=%d)\n", g_loaded, g_len); break; }
            if (rc == NP_SYNC_RESET) { printf("client: SYNC RESET (loaded=%d)\n", g_loaded); break; }
            if (rc == NP_SYNC_FAILED){ printf("client: SYNC FAILED\n"); break; }
            /* Windows: Sleep(1); */
            usleep(1000);
        }
        if (i >= 4000) printf("client: sync timeout\n");
    }

    np_end();
    return 0;
}
```

**Windows 编译**（在 x86 Native Tools 命令行，仓库根目录执行）：

```bat
cl /nologo /W3 /utf-8 /DWIN32 /D_WINDOWS /D_CRT_SECURE_NO_WARNINGS /DUNICODE /D_UNICODE ^
   /I. /Icore /Icomm /Fe:npsync.exe npsync.c comm\npsession.c comm\net.c comm\log.c ws2_32.lib
```

**预期输出**

| 用例 | server | client |
|------|--------|--------|
| 成功 | `server: playing` → `sync begin (70000 bytes)` → `SYNC OK (loaded=1)` | `client: playing` → `SYNC OK (loaded=1, len=70000)` |
| 从机载入失败（`npsync c 127.0.0.1 8891 1`） | `SYNC RESET` | `SYNC RESET (loaded=0)` |

> 两个都必须是 RESET 且不退出 —— 这验证了"失败不中断连接"。

---

## 11. 验收 Checklist（Win 侧完成后逐条打勾）

- [ ] `cmake --build build --config Release` 零 error（warning 按项目现状处理）
- [ ] §5.1 「联网对战…」的 `IDC_CMB_CACHE` 已改为只读文本（下拉框已删）
- [ ] §5.2 局域网面板的 `IDC_LANMATCH_CMB_CACHE` 已改为只读文本（下拉框已删），提示文案与 mac 对齐
- [ ] 两端 UI 一致：mac 与 win32 **都不再有缓冲帧数下拉框**，唯一来源是 `config.ini [netplay] cache_num`
- [ ] §5.3 房间列表 4 列无横向滚动条、异版本灰显可见
- [ ] §6 的 V1~V11 全部跑过（V6/V8 为重点）
- [ ] 与 mac 端（相同 ROM、`NET_VER=2`）跨端对战 + 跨端联机读档成功
- [ ] 手工改 `config.ini [netplay] cache_num` 生效，客户端强制采用
- [ ] `docs/netplay-protocol-version-plan.md` §10 / `docs/netplay-state-sync-plan.md` §12 的"实现状态"表把 win32 一栏从"已改未编译"改为"已验证"
- [ ] （可选）`IDM_OPTIONS` 死菜单记进待办

---

## 12. 已知遗留（不在本轮范围）

1. **mac 看不到 Windows 发布的房间**（反向正常）—— Windows 多网卡广播 / macOS 本地网络权限的嫌疑最大，与本轮改动无关，见 `MEMORY.md` 与 `docs/lan-quick-match-plan.md`。
2. **未测的失败分支**：传输超时（5s）、sign 被篡改、**主机侧**载入失败注入（mac 侧只注入过从机失败）。
3. **`cache_num` 的配置 UI**（用户已明确"后续加"）。
4. win32 `wSPmemory.c:442` 光标自增无 clamp（与本轮无关，待修）。
