# 局域网快速配对对战 设计方案（macOS / win32）

> 状态: **mac 端阶段 1~3 已完成并自测通过、阶段 4 收尾中（进度与实测见 §13）；
> win32 端已实施完毕、构建 0 警告（记录见 §15），真机跨端验证待用户完成**
>
> 范围: macOS 前端（`mac/iNESLanLobby.*`）与 win32 前端（`win32/dlgLanMatch.*`），
> **两端要能互相发现、互相对战**。原 D1「不同步实现 win32」已作废：本机已有 VS2022 + CMake 构建环境
> （见 `docs/build.md`），且 `comm/lan.c` 本就是跨平台实现、已编入两个目标。
>
> 与现有手动对战的关系: 两者**只共享会话层**（两端共用的 `comm/npsession`，由 `mac/iNESNetPlaySession`
> 下沉而来），发现层完全独立，互不干扰。

---

## 0. 结论摘要

在 mac 端新增第三种进入对战的方式：**文件 > 局域网快速对战…**。

打开面板后本机自动向局域网 **UDP 广播**自己的 ROM 校验码、昵称与对战端口；同时接收他人的广播并列出房间。
**列表里只有 ROM 校验码相同的房间可选**；ROM 不同的房间照常显示（灰字 + 标注"ROM 不同"）但不可选中。

角色规则（已拍板）：

- **先发布者为服务端**，对游戏有控制权，**无需对方确认**；
- **后加入者为客户机**，只能接受"服务端"身份，不接受的人可以自己发布房间让别人来加；
- 配对成功（握手校验通过）后**双方直接硬复位开打**，没有二次确认。

核心实现红利：新模式只负责产出 `(角色, IP, 端口)` 三元组，随后**全部复用**现有 `np_begin / np_poll / np_frame_*`，
帧同步、延迟线、主副手柄路由、软硬复位控制码、ROM 二次校验一行不改。

---

## 1. 产品规则（已确认）

| # | 规则 | 说明 |
|---|---|---|
| R1 | **macOS + win32**（原"仅 macOS"已作废） | 用户 2026-09-19 明确要求两端同步、**且能跨端对战**；会话层因此下沉到 `comm/npsession`（见 §15） |
| R2 | **先发布者 = 服务端**，有控制权 | 帧数基准、端口、由谁受理接入都由服务端决定 |
| R3 | **加入无需服务端确认** | 客户端连上并完成校验即进入对战，双方**同时硬复位**后开始 |
| R4 | 昵称**持久化**（首次生成，可改） | 首次进入局域网模式时自动生成 `Player xxxx`（`xxxx` = 4 位随机数字，例 `Player 4712`）并写入 `config.ini` 的 `[netplay] nickname`；面板内可随时修改，关闭面板即保存 |
| R5 | **后加入者 = 客户机**，不可协商 | 面板上明确提示"加入后本机将作为**副手柄（客户机）**"；想当主机的人自行发布房间 |
| R6 | 仅相同 ROM 可配对 | ROM 不同的房间**可见但不可选中**（UI 层）+ 握手时 `NET_CMD_START_RSP.code=2` 二次校验（协议层）双保险 |
| R7 | 仅限局域网 | 不做 NAT 穿透 / 中继服务器；广播报文不出本网段 |
| R8 | 断线不重连 | 沿用既有约定：缓存耗尽即"网络卡"表现为卡帧等待，不触发重连动作 |
| R9 | **对战中的房间不再广播、不再进入列表** | 开打即 `lan_close()`，房间在 TTL(3s) 后自然消失；报文里因此**不设 busy 位** |
| R10 | 不显示 IP | 列表只有昵称与 ROM 名；IP 仅内部用于 `net_connect()` |

### R2 「服务端控制权」的具体含义

| 项目 | 归属 | 现状 |
|---|---|---|
| 缓冲帧数（1~5，默认 4） | **服务端**下发 | `np_begin(cache_num)` 仅服务端生效；客户端以 `START_RSP.fno` 高 32 位对齐（已实现） |
| TCP 端口 / 受理谁的接入 | **服务端** | 见 F2 的自动端口 |
| 开始时的硬复位 | 双方各自执行 | 沿用 `net_play_start` 现有逻辑，本次不改 |
| 软/硬复位控制码 | 任一方均可提交 | `NET_CTRL_CODE_SOFTRESET / HARDRESET` 现既有行为，本次不改 |
| 踢人 / 单方面结束对战 | **不在本次范围** | 目前双端都没有"结束对战"入口（需重启 ROM），如需要另开议题 |

---

## 2. 现状（改动依据）

| 项 | 现状 | 影响 |
|---|---|---|
| `comm/net.h` | **只有 TCP**：`listen / connect / send / recv / check_recv`，**无 UDP、无本机端口查询** | 广播发现需从零加一层 UDP（F1） |
| `comm/net.c:340-341` | 服务端 `net_is_connected()` 在已有 conn 时直接 `return 1`，**不再 accept** | 天然不会混入第三个连接；但 listen socket 仍占端口（F9） |
| `comm/net.c:122` | 已有 `net_set_async()`（`FIONBIO`）与 `select` 封装 | 非阻塞收 beacon 沿用同一跨平台风格 |
| `comm/thread.h` | mutex / thread / wait 齐全 | **本方案不需要新线程**，见 §3 |
| `comm/npsession.{h,c}` | 已封装 `NP_ST_*` 状态机、`np_begin / np_poll / np_frame_input`（**win32 与 macOS 共用**，2026-09-19 由 `mac/iNESNetPlaySession` 下沉而来） | 新模式只需调用它，会话层零改动 |
| `comm/npsession.h:19-22` | 明确约定"握手期只在主线程、对战期只在模拟线程，`comm/net.c` 非线程安全" | 发现阶段**必须**与对战阶段时间互斥 → 免线程免锁 |
| 校验码来源 | `host.rom.crc32_p`（`win32/iNES.c:644`、`mac/iNESApp.m:1746`） | 与 R6 判据一致 |
| 默认端口 | TCP 8891（`mac/iNESNetPlayDialog.m:19`） | 发现用 **UDP 8892**，对战沿用 8891（被占用时自动改，见 F2） |
| `mac/Info.plist` | **缺 `NSLocalNetworkUsageDescription`** | macOS 15 起访问局域网会弹权限框且缺自定义文案 → F8 必做（现有手动对战同样受益） |
| `CMakeLists.txt:141-156` | `.m` 与 `.c` 两个源列表，`.m` 自动带 `-fobjc-arc` | 新增文件要手工加入对应列表 |
| 菜单 | `mac/iNESApp.m:661` 「联网对战…」、`startNetPlay:`(1363) | 新菜单项紧邻它，校验逻辑（ROM 已载入 / 非对战中）可复用 |

---

## 3. 总体架构

```
┌──────────── 新增：发现层（发现阶段独占，主线程定时器驱动） ────────────┐
│  comm/lan.{h,c}      UDP 8892 广播 beacon / 非阻塞收包 / 房间表管理      │
│  mac/iNESLanLobby.*  UI：昵称输入 + 本机 ROM + 房间列表 + 加入/取消      │
└──────────────────────────┬────────────────────────────────────────────┘
                           │ 产出三元组 (is_server, ip, port)
                           ▼
┌──────────── 现有：会话层（对战阶段独占，一行不改） ────────────────────┐
│  np_begin → np_poll(50ms) → NP_ST_PLAYING → np_frame_begin/input        │
│  START/START_RSP 握手 incl. crc 校验 → 延迟线 → 主副手柄 → 复位控制码    │
└────────────────────────────────────────────────────────────────────────┘
```

**关键成本决策：发现阶段与对战阶段时间互斥**（沿用会话层既有约定）。于是：

- UDP socket 非阻塞，由主线程定时器驱动 —— **不加线程、不加锁**；
- mac：`NSTimer` 挂 `NSRunLoopCommonModes`（模态对话框下必须如此，`iNESOpenRomDialog` / `iNESNetPlayDialog` 均已验证该写法）；
- 用户点"加入"或有人接入后立即：停 beacon → 关发现 socket → 释放 listen 端口 → 交棒 `np_begin()`。

> 备选（独立发现线程 + 双缓冲房间表 + 协作式停止）仅在"对战中仍要持续广播 busy 状态 / 排队观战"时才需要。当前需求无此诉求，**不做**。

---

## 4. 发现机制选型

| 方案 | 依赖 | 跨平台 | 实现量 | 结论 |
|---|---|---|---|---|
| **A. UDP 有限广播 `255.255.255.255:8892`** | 无 | 一份 C 代码两端通用 | ~280 行 | **采用** |
| B. UDP 组播 `239.x.x.x` TTL=1 | 无 | 同上 | +30 行 | 备选（第二批，见 F14） |
| C. Bonjour / mDNS (`NSNetService`) | Apple 私有 | mac 独享；win32 需手写 RFC 6762/6763 或引 SDK | — | **排除**（违反零第三方依赖） |
| D. 子网单播扫描（遍历 /24 逐 IP 探测） | 无 | 可 | ~200 行 | **排除**（脏且慢） |

说明：酒店/校园/企业 AP 若开启 **AP 隔离（client isolation）**，广播与组播同样不可达，此情形下只能用现有手动 IP 模式——这是局域网发现的固有边界，文档与面板空态文案里都要讲清楚。

报文收发节奏：**每 1000ms 广播一次**；接收端 **TTL 3s** 过期剔除；UI **每 500ms** 刷新列表。流量约 128 B/s/实例，可忽略。

---

## 5. 报文格式（定长 128 字节，网络字节序）

```c
#pragma pack(push, 1)
struct _lan_beacon {
    ines_byte_t  magic[8];      // "INESLAN1"
    ines_dword_t ver;           // 发现协议版本(与 NET_VER 相互独立, 初值 1)
    ines_dword_t crc32;         // ROM 校验码(与握手里的一致)
    ines_word_t  tcp_port;      // 实际对战端口(可能 ≠ 8891, 见 F2)
    ines_byte_t  region;        // 0 NTSC / 1 PAL(提示用)
    ines_byte_t  nick_len;      // ≤ 24
    ines_byte_t  rom_len;       // ≤ 56
    ines_byte_t  peer_id[16];   // 进程级随机 id: 主键 + 过滤自身
    ines_byte_t  nick[24];      // 昵称, UTF-8(默认 "Player xxxx")
    ines_byte_t  rom[56];       // ROM 文件名(截断), UTF-8
    ines_byte_t  pad[11];       // 补齐 128(R9: 对战中即停广播, 故不设 busy 位)
};                              // 共 128 B
#pragma pack(pop)
```

要点：

- **IPv4 only**（广播语义），与现有 `sockaddr_in` 一致；
- 文本一律 **UTF-8 字节流**（Win32 Unicode 构建下 `ines_char_t` 是 `wchar_t`，由 `comm/lan.c` 的
  `lan_t2a/lan_a2t` 用 `WideCharToMultiByte(CP_UTF8)` / `MultiByteToWideChar(CP_UTF8)` 转换；**win32 已实现**）；
- 接收时用 `recvfrom` 的**源地址**作为对方 IPv4，**不需要枚举本机网卡**（省掉 `getifaddrs` / `GetAdaptersAddresses` 双分支）。

---

## 6. 详细流程

### 6.1 发布方（服务端）

```
文件 > 局域网快速对战…   (ROM 未载入 / 已在对战中 → validateMenuItem 置灰)
  → 生成 peer_id(随机 16B)、默认昵称 "Player " + arc4random_uniform(9000)+1000
  → lan_open(): socket(AF_INET, SOCK_DGRAM) + SO_BROADCAST
                 Windows: SO_REUSEADDR / POSIX: SO_REUSEPORT (同机多实例共存)
                 bind(0.0.0.0:8892) + 非阻塞
  → net_listen("0.0.0.0", 8891)；若端口被占用则用 port=0 让系统分配，
     再用 net_get_local_port() 取回实际端口写进 beacon(F2)
  → 每 1s 广播 beacon；每 500ms 收包更新房间表、刷新 UI
  → 有人接入 → 握手(net_poll 每帧 50ms)：
       收到 NET_CMD_START → 校验 NET_VER → 校验 crc32 一致 → 回 START_RSP(code=0)
  → 握手通过（不需要任何确认弹窗）→
       停 beacon、关 UDP socket、释放 listen 端口(F9) → 进入 NP_ST_PLAYING
       → 请求模拟线程做一次双方同步硬复位 → 开始对战
```

### 6.2 加入方（客户机）

```
双击/点"加入"一个房间（仅 crc32 相同且非 busy 的行可选）
  → UI 明确提示："加入后本机将作为副手柄（客户机）"（R5，不可协商）
  → 先停自己的 beacon 并关闭自己的 listen(F9) —— 放弃服务端角色
  → net_connect(对端 IPv4, beacon.tcp_port) → np_begin(client)
  → 同一套握手：发送 NET_CMD_START，等待 START_RSP
       code=0 → 直接开始；code=1 版本不匹配 / code=2 ROM 不匹配 → 报错退回面板
```

### 6.3 竞态：双方同时对对方的房间点了"加入"

按 R5，加入方必须**先关闭自己的 listen**再发起 connect。于是：

1. **绝大多数情况**：双方都关闭 listen 后才 connect → 双方都收到 ECONNREFUSED → 提示"对方已开始其他对战，请重试或自行发布房间"，双败但状态一致、可解释。
2. **极小窗口**（对方关闭 listen 之前 connect 就到达并被 accept）：为避免"我不小心变成服务端"，再加一道保险——
   加入方在 pending 期间置位 `s_joining`，**完全不进入服务端分支的 accept 流程**（`np_poll` 里一行判断）。
   连接会停在 backlog 中永不被 accept，对端 5 秒握手超时后自行报错退回。

结论：**后加入者永远不可能成为服务端**（R5 得到严格保证），且不需要分布式锁或退避重传。

---

## 7. 改动文件清单

| 文件 | 改动 |
|---|---|
| `comm/lan.h` / `comm/lan.c` | **新文件**：发现层（UDP 广播 + 非阻塞收包 + 房间表 + TTL/去重） |
| `comm/net.h` / `comm/net.c` | 新增 `net_get_local_port()`（`getsockname`）、`net_close_listen()`；`net_listen` 支持 `port==0` |
| `mac/iNESLanLobby.h` / `.m` | **新文件**：快速对战面板（昵称输入 + ROM 信息 + 房间表 + 500ms 定时器） |
| `mac/iNESApp.m` | 菜单项「局域网快速对战…」+ `startLanQuickMatch:` + `validateMenuItem:` 条件 |
| `CMakeLists.txt` | `INES_MAC_OBJC_SOURCES` 加 `iNESLanLobby.m`；`inescore` 两个编译目标加 `comm/lan.c` |
| `mac/Info.plist` | 新增 `NSLocalNetworkUsageDescription` |
| `docs/macos-port.md` | §8.7 之后补「快速配对」小节（实现时再写） |

**不改动**：`core/`（含 mapper）、`comm/net.c` 的既有 TCP 行为。
（原写"不改动 `win32/`"已于 2026-09-19 作废：win32 端同步实现见 §14/§15，
  会话层 `iNESNetPlaySession` 也已下沉为 `comm/npsession`。）

---

## 8. 功能拆分与成本

单位：h = AI 辅助下的净工时；行数 = 估算值。

### 8.1 必须项（mac MVP）

| # | 功能 | 技术要点 | 行数 | 实现 | 验证 | 风险 |
|---|---|---|---|---|---|---|
| F1 | 发现层 `comm/lan.{h,c}` | `lan_open/close/send/poll`；POSIX `SO_REUSEPORT` vs Win32 `SO_REUSEADDR`；`SO_BROADCAST`；字节序；每 tick 最多处理 32 包防 flood | +280 | 1.5 | 1.0 | 中 |
| F2 | 实际监听端口可查 | `net_listen(port=0)` + `net_get_local_port()` + `net_close_listen()` | ~25 | 0.5 | 0.5 | 低 |
| F3 | 房间表 | `peer_id` 主键；过滤自身广播；TTL 3s 剔除；上限 32 项 | ~150（并 F1） | 0.5 | — | 低 |
| F4 | `iNESLanLobby.{h,m}` | `NSPanel` + `NSTableView`（昵称 / ROM / IP:端口 / 状态）；500ms `NSTimer` 挂 `NSRunLoopCommonModes`；表格结构参考 `iNESOpenRomDialog.m` | +450 | 2.0 | 1.0 | 中 |
| F5 | 菜单接入 | 新菜单项 + `validateMenuItem:`（ROM 已载入 && 非对战中）+ 复用 `startNetPlay:` 的前置校验 | ~60 | 0.5 | 0.5 | 低 |
| F6 | ROM 不同不可选 | `tableView:shouldSelectRow:` 返回 NO（crc 不同 / busy）；**灰字 + 副标注"ROM 不同"**（不反白、不换背景色，保持现有视觉） | +40（并 F4） | 0.5 | 0.5 | 低 |
| F7 | 角色指派与自动开始 | 服务端/客户端分别 `np_begin()`；含 §6.3 的 `s_joining` 保险；握手过直接硬复位开打，无确认 | ~80 | 0.5 | 1.0 | 中 |
| F8 | 权限 / 签名 | `NSLocalNetworkUsageDescription`；验证用 `tccutil reset All com.ines.emulator` 复位；构建后加 ad-hoc `codesign -fs - --deep` 以减少防火墙重复弹窗 | ~5 + 构建 1 处 | 0.5 | 0.5 | 低（影响观感） |
| F9 | 开打/关面板时停广播 | 面板关闭 与 对战开始 双重清理（R9）；**不释放 listen 端口**，原因见 §13"实施中修正" | ~15 | 0.3 | 0.5 | 低 |
| F10 | 昵称与默认名 | 首次进入自动生成 `Player xxxx`（`arc4random_uniform(9000)+1000`）写入 `config.ini`；面板可改，关闭时保存 | ~50（并 F4） | 0.5 | 0.3 | 低 |
| F11 | 自测工具 + 文档 | `/tmp/lanprobe.c`（双进程 beacon 收发：TTL / 自身过滤 / 多实例 / 端口回退）+ `docs/macos-port.md` 小节 | — | 1.0 | — | 低 |

**合计：约 8h 实现 + 4h 验证；新增约 1000 行、改动约 160 行。**

### 8.2 可选项（第二批，视实测决定）

| # | 功能 | 收益 | 成本 | 建议 |
|---|---|---|---|---|
| F12 | 组播回退（§4 方案 B） | 规避个别网络丢弃有限广播 | +30 行 / 0.5h | 视实测网络再定 |
| F13 | 昵称持久化到 `config.ini` | 免得每次重填 | +30 行 / 0.5h | 待定（§12） |
| F14 | 面板显示 busy 房间 | 知道"对方正在打"（现在只能看到房间消失） | 已含在报文，+20 行 / 0.3h | 可选 |

### 8.3 明确不做

| 项 | 原因 |
|---|---|
| ~~win32 同步实现~~ | 原表项已作废：R1 于 2026-09-19 改为"macOS + win32"，win32 端已实现（§15） |
| 互联网 / NAT 穿透 / 中继 | 违反 R7，且引入服务端与安全模型，成本数量级上升 |
| 断线重连 | R8，沿用既有"卡帧等待"表现 |
| 服务端踢人 / 单方面结束 | 见 R2，非本次范围 |
| 聊天气泡（`NET_CMD_CHAT` 协议已定义未实现） | 与快速配对无关 |

---

## 9. 边界与失败处理清单（实现时逐条落实）

| 场景 | 处理 |
|---|---|
| 面板关闭 / 对战开始 | 必须 `lan_close()` 并关 UDP socket，不留悬挂 fd |
| 本机多实例 | `peer_id` 区分；靠 F2 自动端口避免 8891 冲突；**允许本机双开对战**（便于自测） |
| 自身广播 | 按 `peer_id` 丢弃 |
| 8892 被发现端口占用 | 退化为"只能加入不能发布"，面板提示并隐藏自己那一行的发布状态 |
| 8891 被占用 | `port=0` 自动取空闲端口，写进 beacon 告知对方（F2） |
| ROM 不同 | UI 禁选（F6）+ 握手 `code=2` 二次拦截（R6） |
| 版本不一致 | 握手 `code=1` 拦截；beacon `ver` 不匹配的行直接不显示 |
| 对方消失 | TTL 3s 后从列表移除 |
| 空列表 | 显示"未发现房间：请确认双方在同一局域网（部分 Wi-Fi 开启 AP 隔离时不可见），或使用『联网对战…』手动连接" |
| UDP flood | 每 tick 最多处理 32 个包 |
| macOS 15 本地网络权限 | F8 补描述键 + ad-hoc 签名减少重复询问 |
| macOS 防火墙入站 | 服务端首次 `net_listen` 会触发系统询问，与现有手动模式一致，非本次引入 |

---

## 10. 蓝牙对战可行性评估（需求 5 的答复）

**结论：技术上可行，但不建议做。瓶颈是延迟抖动，不是带宽。**

| 维度 | 实测级数据 | 结论 |
|---|---|---|
| 每帧数据量 | 4 B × 60 fps = **240 B/s** | 任何蓝牙都远远够，带宽不是问题 |
| Wi-Fi 局域网 TCP | RTT 0.5–3 ms，抖动 < 5 ms | 现有 4 帧延迟线（≈66 ms 窗口）绰绰有余 |
| **BLE (CoreBluetooth)** | 连接间隔 7.5–30 ms（macOS 常协商到 15–30 ms），加栈排队 → **单向 20–60 ms、抖动 10–20 ms** | `NP_CACHE_MAX=5`（83 ms）窗口下会**偶发卡帧**；拉到 10 帧（`NET_CACHE_MAX_SIZE=10`，166 ms）能压住卡顿但换来明显操作延迟 |
| 经典蓝牙 RFCOMM / SPP | 10–30 ms，好于 BLE | macOS 需 IOBluetooth + 系统配对，Apple 已弱化；iOS 不经 MFi 不可用；Win/Android 又各一套栈 → 不可行 |

成本：需先抽出 transport 抽象（把收发从 socket 换成回调，约 1 人日），再做 macOS CoreBluetooth 原型
（central 扫描 + peripheral 广播 service UUID + 两个 characteristic + `NSBluetoothAlwaysUsageDescription` + 设备列表 UI），
**约 3–5 人日，且仅 mac↔mac / mac↔iPhone 可用**。回滚式 netcode（rollback）需改 `core/` 状态快照机制，属另一量级，不在讨论内。

**更好的替代（零开发）**：需求本质是"没有路由器也能对战"→ 任一端开 Wi-Fi 热点，另一端连上，**直接用现在这套 LAN 方案**。

---

## 11. 测试方案

| 层级 | 手段 |
|---|---|
| 发现层单元自测 | `/tmp/lanprobe.c` 双进程：验证 beacon 收发、TTL 过期、自身过滤、多实例共存、8891 占用时端口回退 |
| 协议一致性 | 打印 beacon 十六进制 diff，确认两端字节序一致 |
| UI 自动化 | `osascript` 点菜单「文件 > 局域网快速对战…」（沿用已验证的 `System Events` 方式）；**先 `quit app "iNES"` 确认无残留进程** |
| 真机双机 | 两台 mac 载入同一 ROM 各发各的；再验证不同 ROM 时列表可见但不可选 |
| 日志 | `~/Library/Application Support/iNES/iNES.log`：beacon 收发、端口回退、握手结果（与现有 `netplay: connected, run as ...` 同风格） |
| 回归 | 跑 5 个 ROM 各 600 帧哈希比对，确认对战路径未影响渲染 |

---

## 12. 决策记录

| 编号 | 决策 | 来源 |
|---|---|---|
| D1 | ~~不同步实现 win32（本机无 Win32 环境）~~ → **2026-09-19 用户撤销**：win32 同步实现且要与 mac 跨端对战（见 §15） | 用户 2026-09-17 / 撤销 2026-09-19 |
| D2 | 先发布者 = 服务端，拥控制权；加入无需其确认，配对成功直接开打 | 用户 2026-09-17 |
| D3 | 发布时可填昵称，留空则自动 `Player xxxx`（4 位随机数字） | 用户 2026-09-17 |
| D4 | 后加入者 = 客户机，不接受者可自行发布房间 | 用户 2026-09-17 |
| D5 | 发现用 UDP 有限广播 8892（组播留作回退） | 本方案 §4 |
| D6 | 发现期与对战期互斥 → 不加线程、不加锁 | 本方案 §3 |
| D7 | 发现层放 `comm/`（跨平台 C），UI 放 `mac/`，为将来 win32 留口子但本次不写 | 本方案 §7 |

**已确认（用户 2026-09-18，均已实现）**

1. 昵称持久化到 `config.ini`（`[netplay] nickname`）：首次进入自动生成 `Player xxxx`，可修改。
2. 对战中的房间不再广播、不再进入列表（R9）；报文因此不设 busy 位。
3. 不显示 IP（R10）：列表只有昵称与 ROM 名。

---

## 13. 实施计划与进展

| 阶段 | 内容 | 验收标准 | 状态 |
|---|---|---|---|
| 0 | 文档与规则定稿 | 计划、成本、决策记录齐全 | 完成 |
| 1 | 发现层 `comm/lan.{h,c}` + `net_get_local_port()`（F1/F2/F3） | 双进程互见、自身过滤、TTL 剔除、同机多实例共存 | 完成 |
| 2 | mac 面板 `iNESLanLobby.{h,m}` + 菜单接入（F4/F5/F6/F10） | 可发布可发现；ROM 不同不可选；昵称持久化；关面板即停广播 | 完成 |
| 3 | 角色指派与端到端（F7/F9） | 服务端被动接入 / 客户机主动加入双向跑通且角色正确 | 完成 |
| 4 | 权限与文档（F8/F11） | `NSLocalNetworkUsageDescription` + `docs/macos-port.md` 小节 | 进行中 |

### 阶段 1~3 实测记录（2026-09-18，全部 PASS）

- `/tmp/lanprobe.c` 双进程：`alice` / `bob` 1 秒内互见，各自只看到对端（自身过滤生效），昵称/CRC/端口/ROM 名传递正确；对端退出后 3 秒 TTL 内房间消失。
- 同机多实例共存：`SO_REUSEPORT` 生效，两个实例都能收到广播（本机局域网 IP 192.168.10.22）。
- 服务端被动接入：`/tmp/npsession c 127.0.0.1 8891 <crc>` 连入 → `lan: paired, run as server` → `lanplay: start as server, cache_num=4`；对端 `role=client is_server=0`。
- 客户机主动加入：`/tmp/lanhost`（广播 + 会话层监听）作服务端 → app 列表选中并点"加入" → `lan: discovery closed` → `netplay: connect at 192.168.10.22:9001` → `lan: paired, run as client` → `lanplay: start as client, cache_num=4`；对端 `HOST: paired, is_server=1`；面板自动关闭。
- ROM 不同：房间照常显示且带"ROM 不同"标注，选中被拒、加入按钮 disabled。

### 实施中修正的设计点

- **开打后不能关闭监听 socket**（原 F9 的一部分）：`comm/net.c` 的 `net_is_server()` 以"监听 socket 是否存在"为判据，而 `np_frame_input()` 用它决定主/副手柄路由；关掉会让服务端按客户机取值（**手柄反转**）。因此 `net_close_listen()` 一并删除，监听 socket 统一由 `np_end()` 关闭 —— 已有连接时 `net_is_connected()` 不会再 accept，保留它对对战没有任何影响。
- 握手失败后一律"恢复发布"（`resumeHosting`），服务端继续等下一个人，不会把面板卡死在失败态。
- 广播文本按字节截断并做了 UTF-8 边界保护（`lobby_clip_utf8`），中文 ROM 名不会出现乱码。

---

## 14. win32 同步实现（2026-09-19 盘点 → **已按方案 A 实施，见 §15**）

> 目标：win32 也提供「局域网快速对战…」，且 **win ↔ mac 可以互相发现、互相对战**。
> 本节保留开工前的事实盘点与决策记录（§14.6 为拍板结论），实际落地见 §15。

### 14.1 win32 现状盘点（与 §2 对照）

| 项 | 现状 | 影响 |
|---|---|---|
| `comm/lan.{h,c}` | 已完成，且**已编入两个目标**（`CMakeLists.txt:134` 的 `INES_COMMON_COMM_SOURCES` 同时给 `inescore` 与 `iNES`） | **发现层零改动即可在 win32 用**：UDP 8892、`SO_BROADCAST`、非阻塞、Windows `SO_REUSEADDR` 分支、房间表/TTL/自身过滤都写好了 |
| `comm/net.c` | `net_listen(port=0)` + `net_get_local_port()`（`getsockname`）已具备；`net_is_server()` = "监听 socket 存在" | 端口回退(F2) 可用；§13 的"开打后不能关监听"同样适用 |
| **会话层** | **win32 没有 `np_*`**：mac 的 `mac/iNESNetPlaySession.{h,c}` 是 mac 独有；win32 把握手状态机写在 `win32/dlgNetPlay.c`（`dlgNetPlay_TimedCheck`），把帧收发写在 `win32/iNES.c`（`send_frame / recv_frame / cache_add_mine / cache_add_other / cache_get`） | **最大差异点**，见 §14.4 |
| 帧包格式 | 两端都是 `struct _net_frame {cmd, joypad, ctrl}`（packed，3B），语义一致 | 互通无碍 |
| 握手 | 两端都是 `NET_CMD_START` / `NET_CMD_START_RSP`，`NET_VER=0x0101`，code 0/1/2，超时 5s | 互通无碍 |
| 缓存槽 / 默认帧数 | win32 `MAX_BUF_NUM=10`、`net_cache_num=4`；mac `NP_CACHE_SLOTS=10`、`NP_CACHE_DEFAULT=4` | 一致 |
| 配置 | win32 `GetConfigStr/SetConfigStr`（`win32/iNES.c:2082`，写 `config.ini`）与 mac `iNESConfig.c` **同名同签名** | 昵称持久化(R4/F10) 可直接对齐 `[netplay] nickname` |
| `net_init()` | `win32/iNES.c:475` 启动时已调用 | `lan_open()` 前置条件满足 |
| 菜单 | 文件菜单只有「联网对战」= `IDM_NET_PLAY`(32902)；**无灰显机制**，未载入 ROM 时弹 MessageBox | 新菜单项沿用同一风格（见 Q7） |
| 空闲命令号 | `Resource.h` 的 `_APS_NEXT_COMMAND_VALUE = 32903`、`_APS_NEXT_CONTROL_VALUE = 1011` | 新菜单用 32903，新控件从 1011 起 |

### 14.2 跨端（win ↔ mac）互通性核对

| 环节 | 结论 |
|---|---|
| 发现报文 `lan_beacon_t`（128B，网络字节序） | 两端同一份 `comm/lan.c` → **一致** |
| **文本编码（昵称 / ROM 名）** | ⚠️ **win32 有 bug**：`lan_t2a/lan_a2t` 在 `UNICODE` 下用 `wcstombs` / `mbstowcs`，依赖 C locale，Windows 默认非 UTF-8 → 中文变 `?` 或失败；mac（非 UNICODE）直通 UTF-8。**必须改成 `WideCharToMultiByte(CP_UTF8)` / `MultiByteToWideChar(CP_UTF8)`**（只改 `#ifdef UNICODE` 分支，mac 行为不变） |
| TCP 握手 / 帧包 / 手柄路由 / 复位控制码 | **一致** |
| **缓冲帧数对齐** | ⚠️ mac 客户端读 `START_RSP.fno >> 32` 对齐服务端 `cache_num`；**win32 客户端不读 `fno`**（栈上的 `nst.fno` 甚至未清零），固定用本地 `net_cache_num`。win32 客户端 + mac 服务端且 mac 帧数 ≠ 4 时，两端延迟不同 → 帧号恒定偏移、表现为持续卡帧。**必须给 win32 客户端补上 fno 对齐** |
| 开打后保留 listen socket | 两端同一约束（`net_is_server()` 判据）→ **win32 同样不能关** |

### 14.3 win32 侧特有的差异（不影响协议，但影响体验）

1. **没有会话层**（§14.4）。
2. **Windows 防火墙**：UDP 8892 / TCP 8891 首次入站会弹「是否允许访问」，需用户允许（专用网络）。不做程序化放行（要提权 `netsh`），仅在文档与面板空态文案里说明。
3. **Windows `SO_REUSEADDR` ≠ POSIX `SO_REUSEPORT`**：Windows 下多个 socket 绑同一 UDP 端口后，广播数据报**只投递给其中一个绑定者**（且可被无关进程抢占）→ **win32 上"同机双开两个 iNES 互见"大概率不成立**（mac 实测可）。自测退而求其次：用手动 IP 模式连 `127.0.0.1`，或两台真机。
4. **字符集**：win32 是 UNICODE 构建，昵称/ROM 名内部都是宽字符；截断逻辑要按 UTF-8 **字节**做（对齐 `lobby_clip_utf8`），否则中文 ROM 名会出现半个字。

### 14.4 会话层方案（待决策 Q1）

| 方案 | 做法 | 改动量 | 风险 |
|---|---|---|---|
| **A（推荐）** | 把 `mac/iNESNetPlaySession.{h,c}`（纯 C，不依赖 Cocoa）**下沉到 `comm/npsession.{h,c}`**，两端共用；win32 的手动「联网对战」与新的「局域网快速对战」都走 `np_*`，`win32/iNES.c` 的 `send_frame/recv_frame/cache_*` 换成 `np_frame_begin()/np_frame_input()` | 大 | 中：动到现有对战主链路，需回归手动模式 |
| B | 只把会话层**复制**一份到 `win32/wNetSession.{h,c}`（内容与 mac 版一致），**仅 LAN 快速对战使用**；手动模式保持 `dlgNetPlay.c` 原样 | 中 | 低（不动现有链路），但两端两份代码需同步维护 |
| C | 不引入会话层：LAN 对话框内自管握手（复制 `dlgNetPlay_TimedCheck` 的状态机） | 最小 | 与 mac 结构不对齐，状态机两份 |

**无论选哪个，都必须补两件跨端必需项**：① win32 客户端解析 `START_RSP.fno` 高 32 位对齐帧数（§14.2）；② `comm/lan.c` 的 UTF-8 转换改用 Win32 API（§14.2）。

### 14.5 win32 改动文件清单（按方案 A 估算）

| 文件 | 改动 |
|---|---|
| `comm/lan.c` | 修 `#ifdef UNICODE` 分支的 UTF-8 转换（**共享文件**，mac 逻辑不变） |
| `comm/npsession.{h,c}` | 由 `mac/iNESNetPlaySession.{h,c}` 迁来（A）／或新增 `win32/wNetSession.{h,c}`（B） |
| `win32/dlgLanMatch.{c,h}` | **新文件**：昵称 + 本机 ROM + 房间列表 + 加入/关闭 + 50ms 定时器（对齐 `iNESLanLobby` 的状态推进） |
| `win32/iNES.rc` | 新增对话框资源 + 「局域网快速对战(&L)...」菜单项（BOM 必须保留） |
| `win32/Resource.h` | 对话框 / 控件 / 菜单 ID |
| `win32/iNES.c` | 菜单命令处理（ROM 已载入 / 非对战中校验 + 配对成功后硬复位），方案 A 时 `OnIdle` 帧循环改走会话层 |
| `CMakeLists.txt` | 新源文件登记（方案 A 还要把 `comm/npsession.c` 加进 `INES_COMMON_COMM_SOURCES` 并从 mac 列表里去掉旧文件） |
| 本文档 | §14 更新为"已实施"，补 §15 实施计划与实测 |

**不改动**：`core/`、发现层协议（`comm/lan.h`）、`comm/net.c` 的既有 TCP 行为。

### 14.6 决策结果（用户 2026-09-19 拍板）

| # | 问题 | 结论 |
|---|---|---|
| Q1 | 会话层 | **A**：`mac/iNESNetPlaySession.{h,c}` 下沉为 `comm/npsession.{h,c}`，两端共用；win32 手动「联网对战」与新的快速对战都走 `np_*`，`win32/iNES.c` 的 `send_frame/recv_frame/cache_*` 全部替换 |
| Q2 | 房间列表控件 | ListView（`LVS_REPORT` + `NM_CUSTOMDRAW` 灰显），双击加入 |
| Q3 | `comm/lan.c` 的 UTF-8 转换 | **允许**：`UNICODE` 分支改用 `WideCharToMultiByte(CP_UTF8)` / `MultiByteToWideChar(CP_UTF8)` |
| Q4 | 缓冲帧数 | win32 面板**提供 1~5 下拉框**（mac 面板没有此项，属两端 UI 差异）；客户端仍按 `START_RSP.fno` 高 32 位对齐 |
| Q5 | 手动模式 | 随 Q1=A 一并改走会话层 |
| Q6 | 同机双开 | 接受 win32 上不可行（Windows `SO_REUSEADDR` 语义），自测用手动 IP + 真机 |
| Q7 | 菜单与提示 | 新命令 `IDM_LAN_MATCH`(32903)，文案「局域网快速对战(&L)」放在「联网对战」下面；未载入 ROM 沿用 MessageBox 提示 |
| Q8 | 验证条件 | 用户有 mac 可做真机跨端验证；本侧只做 win32 构建 + win↔win / 协议层校验 |

---

## 15. win32 实施记录（2026-09-19）

状态：**代码已完成，win32 Release 构建 0 警告**；真机跨端(win↔mac)由用户验证。

### 15.1 实际改动清单

| 文件 | 改动 |
|---|---|
| `comm/npsession.{h,c}` | **新文件**：由 `mac/iNESNetPlaySession.{h,c}` 迁来，两端共用。新增 `np_copy_tstr()`(原生→原生) 与 `np_copy_utf8()`(原生→UTF-8)，使 `np_poll()` 的提示文本在 Unicode 与非 Unicode 目标下都正确；`np_begin()` 的 `ip` 参数改为 `ines_cstr_t`(平台原生编码) |
| `mac/iNESNetPlaySession.{h,c}` | **删除**（内容迁到 `comm/`） |
| `mac/iNESLanLobby.m`、`mac/iNESNetPlayDialog.m`、`mac/iNESApp.m` | include 改为 `#include "../comm/npsession.h"`，注释同步 |
| `comm/lan.c` | `#ifdef UNICODE` 的 `wcstombs/mbstowcs` → `WideCharToMultiByte(CP_UTF8)` / `MultiByteToWideChar(CP_UTF8)`（中文昵称/ROM 名不再乱码） |
| `win32/dlgLanMatch.{c,h}` | **新文件**：快速对战对话框（昵称 + 本机 ROM + 房间 ListView + 缓冲帧数下拉 + 加入/关闭），500ms 定时器推进握手与发现，1s 广播一次 |
| `win32/dlgNetPlay.c` | 手动模式改为 `np_begin / np_poll` 驱动（50ms 定时器不变），本地状态机与 `net_cache_num` extern 依赖删除 |
| `win32/iNES.c` | `net_init()` → `np_init()`；`OnIdle` 帧循环改走 `np_frame_begin / np_input_ready / np_frame_input`；删除 `send_frame/recv_frame/cache_*` 与 `net_cache[]`、`net_cache_size`、`net_cache_num`；`net_close()` → `np_end()`（关 ROM/退出/暂停切换）；新增 `IDM_LAN_MATCH`；`WM_DESTROY` 加 `np_fini()` |
| `win32/iNES.rc` | 新增 `IDD_LANMATCH` 对话框 + 「局域网快速对战(&L)」菜单项（BOM 已复查保留） |
| `win32/Resource.h` | `IDD_LANMATCH`(132)、`IDC_LANMATCH_*`(1011-1016)、`IDM_LAN_MATCH`(32903) |
| `CMakeLists.txt` | `INES_COMMON_COMM_SOURCES` 加 `comm/npsession.c`；`INES_MAC_C_SOURCES` 去掉 `mac/iNESNetPlaySession.c`；`INES_WIN32_SOURCES` 加 `win32/dlgLanMatch.c` |

**未改动**：`core/`、发现层协议（`comm/lan.h`）、`comm/net.c` 的既有 TCP 行为。

### 15.2 与 mac 端的行为对齐点

- 面板一打开即 `np_begin(server)` 发布房间，UDP 8892 广播自己的 `(crc32, tcp_port, 昵称, ROM 名)`，1s 一次；TCP 8891 占用时 `np_begin(..., 0)` 交系统分配端口，`net_get_local_port()` 回填到广播里。
- 加入他人房间 → `lan_close()` 停广播 → `np_begin(client)`；本机随即从别人的列表消失，"后加入者必为客户机"。
- 握手失败/超时一律"恢复发布"（服务端继续等人），与 mac 的 `resumeHosting` 一致。
- **开打后不关监听 socket**（`net_is_server()` 是手柄路由判据），只关发现通道。
- 昵称持久化在 `config.ini` 的 `[netplay] nickname`，键名与 mac 端一致（两端各自存各自的）。
- ROM 不同的房间可见但灰字 + "（ROM 不同）"后缀，且不可选中加入。

### 15.3 跨端(win↔mac)必需项，均已落到 `comm/npsession.c`

1. 客户端读 `START_RSP.fno` 高 32 位对齐服务端的缓冲帧数（旧版对端填 0 时沿用默认值，互通无影响）。
2. 提示文本对外统一 UTF-8，界面层各自转回（`MultiByteToWideChar(CP_UTF8)` / `NSString UTF8String`）。
3. 帧包、握手包、手柄路由、复位控制码两端同构。

### 15.4 已知限制

- **win32 同机双开互见大概率不成立**：Windows 的 `SO_REUSEADDR` 不等于 POSIX `SO_REUSEPORT`，多 socket 绑同一 UDP 端口时广播只投递给其中一个。自测请用两台真机，或手动 IP 模式连 `127.0.0.1`。
- **Windows 防火墙**会对 UDP 8892 / TCP 8891 的入站弹询问，需用户允许（专用网络）；程序不做放行（需提权 `netsh`）。
- win32 面板多一个"缓冲帧数 1~5"下拉框（mac 面板没有），这是 Q4 明确接受的两端 UI 差异；改动它会立即以新值重新发布。

### 15.5 待用户验证（真机）

1. win↔mac 互相发现（中文昵称/ROM 名显示正确）。
2. win↔mac 对战：手柄路由正确（服务端主手柄、客户端副手柄）、无卡帧。
3. 两端缓冲帧数不一致时（例如 win 服务端设 2、mac 客户端）仍能正常开局。
4. 手动「联网对战…」回归（win↔win / win↔mac）。

