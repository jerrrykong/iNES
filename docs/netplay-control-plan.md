# 联网对战：控制权限与结束通知

> 范围：`comm/`（会话层与协议，两端共用）+ `mac/iNESApp.m` + `win32/iNES.c`。
> 对应需求：① 联网下部分控制只允许主机；② 换 ROM / 退出前二次确认并通知对端；
> ③ 对端收到通知后退回单机并提示；④ 标题栏显示"联网对战中"。

## 1. 控制权限矩阵（联网对战中）

"主机"= 服务端（`np_is_server()`，即先发布/监听的一方），拥有对局控制权（见 `docs/lan-quick-match-plan.md`）。

| 功能 | 主机 | 从机 | 理由 |
|---|---|---|---|
| 重新上电 / 软件复位 | ✅ | ❌ | 复位会重跑**双方**的 ROM，从机发起等于打断主机；走 `NET_CTRL_CODE_*` 由双方在同一帧执行 |
| 载入存档（读档） | ❌ | ❌ | 读档只改本机状态、无法与对端同步，必然 desync（win32 原本就是"连网游戏不能加载进度"） |
| 即时存档（写档） | ✅ | ✅ | 只写本地文件，不影响对端 |
| 暂停 / 单帧执行 | ❌ | ❌ | 停帧后本方不再发输入包，对端会一直空转等待 |
| 截图 / 静音 / 音量 / 缩放 / 比例 / 全屏 / OSD / 日志 | ✅ | ✅ | 纯本地表现，与同步无关 |
| 调试视图 / CPU TRACE | ✅ | ✅ | 只读视图；TRACE 只影响本机性能 |
| 卸载 ROM / 载入新 ROM | ✅（二次确认） | ✅（二次确认） | 会结束本方对局，须先确认并通知对端（见 §2） |
| 退出程序 / 关闭窗口 | ✅（二次确认） | ✅（二次确认） | 同上 |

两端的落地方式不同（平台惯例）：

- **macOS**：`validateMenuItem:` 直接禁用（不可点）；复位额外在动作里兜底提示。
- **win32**：菜单项没有统一的禁用机制，改为点击后 `MessageBox` 提示并拦截。

## 2. 结束对局的通知协议

### 2.1 新增命令

```c
#define NET_CMD_QUIT  0x11   // 仅 cmd, 无载荷; 对端收到后退回单机
```

放在 `comm/net.h` 与 `NET_CMD_CHAT` 同组。`np_frame_begin()` 解析到它时置 `s_peer_quit` 并整包丢弃；
未知命令仍按 1 字节丢弃（旧版对端不会发这个命令，互通无影响）。

### 2.2 会话层接口（`comm/npsession.h`）

| 接口 | 调用方 | 说明 |
|---|---|---|
| `void np_notify_quit(void)` | 模拟线程（win32：UI 线程即可） | 仅在 `NP_ST_PLAYING` 时发一次 `NET_CMD_QUIT`；尽力而为，失败不影响本方退出 |
| `int  np_peer_quit(void)` | 模拟线程，每帧 `np_frame_begin()` 之后 | 取并清除"对端已退出"标志（一次性） |

**线程约定**：`comm/net.c` 非线程安全，对战期由模拟线程独占；因此 mac 端不能在主线程直接发包 ——
主线程只置 `s_ctl.net_quit`，由模拟线程在同一循环里执行"发包 + `np_end()`"。
win32 的 `OnIdle()` 与菜单同在 UI 线程，可直接调用 `np_notify_quit()`。

### 2.3 时序

```
本方用户点"载入ROM / 卸载 / 退出"
      -> 二次确认："正在联网游戏中，是否确认结束当前游戏？"
      -> (取消: 整体放弃, 当前 ROM 与对局保持不变)
      -> 确认: np_notify_quit() + np_end()   [mac: 经 s_ctl.net_quit 由模拟线程执行]
对端 -> np_frame_begin() 收到 NET_CMD_QUIT -> np_peer_quit() 返回非 0
      -> np_end() + 刷新标题 -> 提示"对方已退出游戏，继续以单机模式运行。"
```

mac 端用画面上方浮层（`showToast`，2.2s 淡出），win32 用 `MessageBox`（无浮层机制）。

## 3. 标题栏标识

| 端 | 格式 |
|---|---|
| macOS | `iNES - <ROM> - 联网对战中（运行中）` |
| win32 | `iNES - <ROM> (联网对战中 - 运行)` |

刷新点（两端一致）：进入对战、`np_end()` 之后（`mac: endNetPlayOnThread` / `win32: NotifyPeerQuitAndEnd`）、
对端退出时。mac 的 `refreshTitle` 自带主线程切换，模拟线程可直接调。

## 4. 改动清单

- `comm/net.h` / `comm/npsession.{h,c}`：`NET_CMD_QUIT`、`np_notify_quit()`、`np_peer_quit()`
- `mac/iNESApp.m`：`s_ctl.net_quit`、`confirmStopNetPlay`、`requestQuitNetPlay`、
  `validateMenuItem:` 权限矩阵、`refreshTitle` 联网标识、`applicationShouldTerminate:`、
  模拟线程处理"本方退出"与"对端退出"；`openROM:` / `openRecentFile:` / `closeROM:` / `openFile:` 均走确认
- `win32/iNES.c`：`ConfirmStopNetPlay()` / `NotifyPeerQuitAndEnd()`、`UpdateTitle()` 联网标识、
  `IDM_SOFTRESET/HARDRESET` 主机限制、`IDM_PAUSE/FRAME_STEP` 联网拦截、`NesOpenFile()` 与
  `OnMenuClose()` 确认（覆盖菜单 / 最近文件 / 拖拽）、`IDM_EXIT` 与 `WM_CLOSE` 确认 + 通知、
  `OnIdle()` 处理对端退出、`OnMenuLoadState()` 给出提示

## 5. 决策记录（2026-09-19，用户拍板）

1. 复位只允许主机；从机禁用（mac 灰掉、win32 提示）。
2. 换 ROM / 卸载 ROM / 退出都要二次确认，确认后**必须通知对端一次**；对端以 Tip 提示并退回单机。
3. 标题栏显示"联网对战中"，退回单机时刷新标题。
4. **载入存档在联网中主机也禁用**（用户原话是"只允许主机使用"）—— 因为读档无法同步给对端，
   主机读档会立刻 desync，比"从机不能用"更糟；win32 原本就是联网禁读档，两端保持一致。
   若后续要做"双方同时读档"，需要新增同步控制码并约定同一槽位，届时再议。
5. 暂停 / 单帧执行联网中禁用（win32 原本可点但会卡死对端）。
