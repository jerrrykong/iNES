# 联机读档（状态同步）方案（待审阅）

> 需求：联网对战中允许**载入存档**，且两端必须回到同一状态，不能 desync。
> 本文只出方案，未改任何代码。审阅确认后再实现。
> **第 2 稿（按审阅意见修订）**：失败处理由"结束联网"改为**硬件复位、不中断连接**（§7）；不再引入 caps 能力位（§4，版本相同即支持）；`NET_VER` 随本次改动 +1（不兼容修改）。
> 相关：[netplay-control-plan.md](netplay-control-plan.md)（控制权限）、[netplay-protocol-version-plan.md](netplay-protocol-version-plan.md)（版本与兼容）

## 1. 现状（代码事实）

| 事实 | 位置 | 影响 |
|---|---|---|
| 存档是**字节流**（`FILE*` 顺序写），不是内存对象 | `core/nes.c` `ines_save_state()` | 传输前要先读文件入内存；对端要么写临时文件再 load，要么新增内存版 API |
| 存档 = header + host + SRAM(≤8×8KB) + mapper + cpu(2KB RAM) + ppu(4KB NT) + VRAM + apu + joypad | 同上 | 典型 **20~100KB**，最坏 ~150KB |
| 存档结构全部为定长类型 + `#pragma pack(1)`，无指针/无 double | `core/nes.c/ppu.c/cpu.c/apu.c` | **小端平台上字节级一致**，可直接跨端传输 |
| `s_recv_buffer` 只有 **1024 字节** | `comm/net.c:31` | 必须分片，单包不能超缓冲 |
| socket 是**非阻塞**，`net_send()` 只 send 一次且不检查返回值 | `comm/net.c:466` | 大包/分片必须新增"循环发送"，否则必然丢数据 |
| 帧同步靠"延迟线"：双方预置 `cache_num` 个空帧，每帧收发 `NET_CMD_FRAME` | `comm/npsession.c` | 读档后必须**清空并重铺延迟线**，否则帧号错位 |
| 控制码（软/硬复位）走 `NET_CMD_FRAME.ctrl`，双方在同一帧执行 | 同上 | 复位已有"同帧执行"机制；读档数据量太大，不能塞进 ctrl |

结论：读档无法复用"控制码"，需要一个**独立的、带冻结与确认的同步子流程**。

## 2. 目标 / 非目标

- 目标：主机读本地存档槽 N → 两端在同一逻辑点载入同一份状态 → 继续对战，输入与画面一致。
- 目标：任一步失败都能**优雅回滚**（对战继续）或**明确失败**（直接断线，绝不 desync）。
- 非目标：跨 ROM 读档（ROM crc32 必须一致，已在握手里校验）。
- 非目标：从机本地存档参与同步（从机槽位文件保持不动）。

## 3. 总体思路

```
主机(模拟线程)                                  从机(模拟线程)
① 读本地槽位文件 -> buf, 校验 magic/crc/mapper
② 冻结(不跑帧/不提交输入), 发 STATE_REQ
                        ------- REQ(total_len, crc32(buf), state_ver) ------>
                                                ③ 冻结, 校验 -> 回 STATE_RSP(ready/reject)
                        <------ RSP ------------------------------------------
④ 分片发 STATE_DATA(512B/片)
                        ------- DATA(seq, payload) × N --------------------->
                                                ⑤ 拼装 -> 写临时文件 -> ines_load_state()
                                                   成功: 重铺延迟线, 回 STATE_DONE(ok, sign)
                                                   失败: 解冻, 回 STATE_DONE(fail)
                        <------ DONE(ok, sign) --------------------------------
⑥ 自己 load 同一份 buf, 算 sign 比对
   一致: 重铺延迟线, 回 STATE_GO(go)
   不一致 / 自己 load 失败 / 收到 DONE(fail): 回 STATE_GO(reset)
                        ------- GO(go/reset) -------------------------------->
                                                ⑦ go  : 解冻, 继续对战
                                                  reset: 解冻, 主机在解冻后**首帧**提交
                                                         NET_CTRL_CODE_HARDRESET
                                                         (走现有 ctrl 机制, 延迟线保证双方同帧执行)
```

关键设计点：

1. **从机先 load、主机后 load**：顺序保证"主机落败时自己还没变"，配合硬件复位即可收场，不需要断线。
2. **冻结期画面静止**：双方都不 `doframe`，只收包。100KB 在局域网约几十毫秒 ~ 数百毫秒，用户感知为一次短暂卡顿。
3. **重铺延迟线**：解冻时清空 cache 并预置 `cache_num` 个空帧（与 `np_enter_playing()` 一致），双方的帧号基准重新对齐。
4. **双重校验**：传输层 crc32(buf) + 语义层 `sign`（load 后的状态摘要，见 §5），sign 不一致视为载入失败。
5. **权限**：仅主机可发起（与复位一致）。从机点击 → 提示"联网对战中只有主机可以载入存档"。
6. **失败 = 硬件复位，不中断连接**（审阅意见 ①）：任一方加载不成功，主机就在解冻后首帧提交 `NET_CTRL_CODE_HARDRESET`，双方回到 ROM 启动状态继续对战。**连接始终保持。**

## 4. 协议新增（草案，命令字 0x20~0x24）

```c
#define NET_CMD_STATE_REQ   0x20  // 20  slot:1  total_len:4  crc32:4  state_ver:2   主机->从机
#define NET_CMD_STATE_RSP   0x21  // 21  code:1 (0:ready 1:reject)                   从机->主机
#define NET_CMD_STATE_DATA  0x22  // 22  seq:4  len:2  data:len(≤512)                主机->从机, N 片
#define NET_CMD_STATE_DONE  0x23  // 23  code:1 (0:ok 1:fail) sign:4                从机->主机
#define NET_CMD_STATE_GO    0x24  // 24  code:1 (0:go 1:reset=对端执行硬复位)        主机->从机
```

- 全部 `pack(1)`，与现有包一致；片长 512B（接收缓冲 1024B，留余量）。
- 顺序由 TCP 保证，无需分片重排；`total_len` 用于收齐判定与内存分配上限（建议上限 512KB，超出 reject）。
- `state_ver` = `INES_STATE_HEADER_VERSION`（当前 1），两端不一致 → reject（提示"存档格式版本不一致"）。
- **不需要能力协商**：按 [版本方案](netplay-protocol-version-plan.md)，版本号必须相同才允许连接 → 两端必然都支持 `STATE_*`；旧版本根本连不上。因此**不引入 caps 位**。
- `STATE_*` 属于**不兼容修改**（需要两端同时理解）→ `NET_VER` 随本次改动 +1（版本方案 §2、§8）。

## 5. 状态摘要 sign（防"存档不完整"）

`sign` = 对 load 后的关键状态算 32 位校验，建议取：

```
crc32( cpu.RAM[0x800] ‖ ppu.NT_RAM[0x1000] ‖ frame_count(8B) ‖ cpu.PC/A/X/Y/SP ‖ SRAM前 256B )
```

理由：这三项覆盖了"RAM/VRAM 是否一致 + 执行位置是否一致 + 帧号是否一致"。若某个 mapper 的 `savestate` 是桩、状态没写全，这个值几乎必然对不上 → 立刻按 §7 走硬件复位，而不是跑几帧后 desync。

### 存档格式与向下兼容（审阅意见 ④）

- 存档格式版本 = `INES_STATE_HEADER_VERSION`（当前 1）。**原则上向下兼容**：同一格式版本内，任一端写出的存档另一端都能读（结构定长 + `#pragma pack(1)` + 小端，字节级一致）。
- `STATE_REQ` 携带 `state_ver`，两端必须一致；不一致 → 对端 reject → 按 §7 处理（**硬件复位**）。
- **例外**：将来 mapper 存档结构变化 / 新增字段 → 提升 `INES_STATE_HEADER_VERSION` → 老格式存档在另一端加载不成功 → 同样按 §7 处理（**硬件复位**，不中断连接）。
- 因此实现时三条失败路径（`load` 返回失败、`state_ver` 不匹配、`sign` 不一致）**统一收口到"硬件复位"**，不做区分。

## 6. 分层改动

| 层 | 改动 | 说明 |
|---|---|---|
| `comm/net.{h,c}` | 新增 `net_send_all()`：循环 send + `select` 等可写，5 秒超时后返回 -1 | **必做**：现有 `net_send` 在非阻塞 socket 上只 send 一次，大包必丢 |
| `comm/net.h` | 新增 5 个命令字与包结构；`NET_VER` 1 → 2（不兼容修改） | 见 §4 |
| `comm/npsession.{h,c}` | 同步子状态机 + `np_sync_set_handler / begin / poll / state / clear / cancel`；内部子状态 `SYNC_WAIT_RSP / SEND / WAIT_DONE / RECV_DATA / WAIT_GO` | 主机靠 `np_sync_poll()` 推进；从机全程在 `np_frame_begin()` 里被动应答 |
| `core/` | **不改** | 两端都写临时文件（`state/.sync.tmp`）后 `ines_load_state()`，用完删除 |
| `mac/iNESApp.m` | 联网读档改走 `syncStateOnThread:`（仅主机）；每帧 `pollStateSyncOnThread` 冻结/取结果；`NP_SYNC_RESET` 时主机提交 `NET_CTRL_CODE_HARDRESET` | 菜单：联网 + 非主机 → 灰显 |
| `win32/iNES.c` | `OnMenuLoadState()` 联网时改走 `OnMenuSyncState()`（仅主机）；`OnIdle()` 里 `OnIdleSyncState()` 推进；`UpdateMenuLoadState()` 联网时从机灰显 | 单线程前端，直接调用即可 |

## 7. 失败处理矩阵

**统一规则（审阅意见 ①）：同步流程一旦开始，任何一步失败 → 硬件复位，连接保持不断。**
只有"还没发起"和"链路真断"两种情形例外。

| 阶段 | 失败情形 | 状态是否已变 | 处理 |
|---|---|---|---|
| ① 本地校验 | 存档不存在 / magic、PROM crc32、mapper 不匹配 | 否（还没发包） | 提示，**对战继续**（不冻结、不复位） |
| ②~⑤ 预约 → 传输 → 从机载入 | 对端 reject（`state_ver` 不一致、长度超限）/ 收不全 / 3s 超时 / crc32 不过 / 从机写文件或 `ines_load_state()` 失败 | 否 | **硬件复位** + 提示"存档载入失败，已复位重开" |
| ⑥ 主机载入 | 主机 `load` 失败 / `sign` 与从机不一致 | **是**（从机已 load） | **硬件复位**（必须，否则必然 desync） |
| 任意 | TCP 链路断开 | 已分叉 | 结束联网（唯一必须断线的情形） |

**硬件复位怎么执行**：主机在解冻后的**首帧**调用 `np_frame_input(joypad, NET_CTRL_CODE_HARDRESET, …)` —— 走现有的控制码机制（`NET_CMD_FRAME.ctrl`），延迟线保证双方在**同一逻辑帧**执行硬复位。现有"主机发起硬复位"的代码路径可直接复用。

**为什么不断线也能救回来**：输入是单向（主机 → 从机）经延迟线传递的，即使双方状态已分叉，输入包仍能正常送达；复位生效前最多有 `cache_num` 帧（≈4 帧 / 67ms）画面不一致，复位生效后双方同时回到 ROM 启动状态，重新一致。

## 8. 备选方案对比

| 方案 | 做法 | 结论 |
|---|---|---|
| **A. 传输存档字节流（推荐）** | 主机把存档字节流发给从机，双方 load 同一份 | 唯一能保证两端状态一致的做法；代价是要新增同步子流程 |
| B. 双方各读本地同槽存档 | 主机发"读槽 N"控制码，各自 load 本地文件 | ❌ 不可行：双方槽位内容本来就不同，必 desync |
| C. 只读档 + 硬复位重放 | 用输入重放追平 | ❌ 不适用：没有输入录制基础设施，追平耗时不可控 |
| D. 阻塞式同步 | 模拟线程里同步等所有响应 | 实现最简，但冻结期不可取消、异常收场困难；**不推荐**（与现有 `np_poll` 风格也不一致） |
| E. 新增内存版 state API | `ines_save_state_mem/load_state_mem`，改 core 的 6 个 save/load | 更干净但改动面大（core 是双端共享、要回归存档兼容）；**当前用临时文件即可，后续有需要再提** |

## 9. 风险

| 风险 | 说明 | 对策 |
|---|---|---|
| mapper `savestate` 不完整 | 部分 mapper 可能是桩，load 后行为分叉 | sign 校验 + 失败即**硬件复位**；后续补 mapper 存档回归（用现有 `/tmp` 自测扩展） |
| 冻结期用户操作 | 冻结时按菜单/关窗口 | 冻结期禁用读档/复位入口；退出走既有"确认 + 通知对端"流程 |
| SRAM / 电池记忆 | 读档会覆盖从机 SRAM | 属于预期（同一局）；从机不落盘，退出时按既有规则保存 |
| 跨端存档格式 | 结构体 pack + 定长类型，但依赖小端 | 两端当前都是小端；把"存档字节流按小端定长"写成硬约束，写进 `docs/api.md` |
| 大包与丢包 | 非阻塞 socket + 1024B 收缓冲 | `net_send_all()` + 512B 分片 + 收齐判定 + 3s 超时 |
| 帧号/延迟线错位 | 读档后 `frame_count` 回退，在途帧包残留 | 冻结期丢弃 FRAME，解冻时重铺 `cache_num` 空帧 |
| 复位生效前的短暂分叉 | 从机已 load、主机未 load 时的最多 `cache_num` 帧 | 接受（≈67ms），复位后即一致；提示语说明"已复位重开" |

## 10. 验证

1. `/tmp/npsync.c`（双进程，桩 load/sign 回调）：主机发一份 70000 字节的存档 → 完整流程 → 断言两端 `SYNC OK`、字节级一致（**已跑通**：两端均 `SYNC OK (loaded=1)`，客户端 `len=70000`）。
2. 注入失败：从机 load 失败 → 断言走**硬件复位**、连接未断（**已跑通**：服务端 `SYNC RESET`、客户端 `SYNC RESET (loaded=0)`）。其余（传输超时 / sign 篡改 / 主机 load 失败）待补。
3. 真机：mac ↔ win32 同 ROM，主机存档 → 对战中读档 → 双方画面一致、继续对战 300 帧无异常。
4. 大 SRAM ROM（如带 64KB SRAM 的卡带）测一次传输耗时。

## 11. 工作量（粗估）

| 项 | 人日 |
|---|---|
| `comm/net` `net_send_all` + 命令与包结构 | 0.5 |
| `comm/npsession` 同步状态机 + API | 1.0 |
| mac 前端（放开菜单 + 状态机接入 + 提示） | 0.5 |
| win32 前端（同 + `OnIdle` 推进） | 0.5 |
| 自测与联调 | 0.5~1.0 |
| **合计** | **≈ 3 天** |

## 12. 拍板结果（第 2 稿审阅后已定，且已实现）

1. **失败 = 硬件复位，不中断连接**：任一方载入不成功 → 主机在解冻后首帧提交 `NET_CTRL_CODE_HARDRESET`；**本地校验失败不复位、继续游戏**（还没发包、状态没变）；只有链路断开/超时才结束联网。
2. **读档仅主机发起**（从机灰显）；**存档（保存状态）双方均可**。
3. 冻结期画面静止（数百毫秒），接受。
4. 从机**不落盘**：写临时文件 `state/.sync.tmp`，载入后即删，不覆盖从机自己的槽位。
5. `NET_VER` 重新编号：1 = 基线，**2 = 本轮（STATE_* 等）**。

### 实现要点（后续维护对照）

- **冻结判定在前端**：模拟线程每帧照常 `np_frame_begin()`，然后 `np_sync_state() == NP_SYNC_BUSY` 时**只调 `np_sync_poll()` 并跳过 `doframe`**。冻结期绝不能停 `np_frame_begin()`，否则收不到包。
- **从机全程被动**：REQ / DATA / GO 都在 `np_frame_begin()` 里处理；**从机不能自行复位**（会打乱帧号），统一由主机的 ctrl 经延迟线保证同帧执行。
- **`sign` 数据**（两端实现必须完全一致）：RAM(2KB) + 名称表 + OAM + `frame_count` + CPU 寄存器（A/X/Y/P/SP/PC）的 CRC32。
- **分片**：单片 512B（接收缓冲 1024B，留余量）、每帧最多 8 片；100KB 约 25 帧。
- **变长包的收包**：`NET_CMD_STATE_DATA` 未收全时必须整包留在缓冲里等下一帧，绝不能落进 `default` 分支（那样会按 1 字节丢弃、把包拆坏）。
- 主机在 `NP_SYNC_RESET` 时**不复位自己**，而是提交控制码 —— 这样两端在同一逻辑帧复位。
