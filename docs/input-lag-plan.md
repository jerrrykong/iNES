# macOS 前端输入延迟：定位与优化方案

> 状态：**待审阅**（方案不含改动承诺，除 §5.1 的 P0 已在本机实现并实测，可一键回退）
> 平台范围：仅 macOS（按约定不同步实现 win32）
> 相关文件：`mac/iNESApp.m`（帧循环/输入）、`mac/iNESVideo.m`（画面提交/绘制）、`mac/iNESAudio.m`（音频）
> 对照实现：`win32/iNES.c` 的 `OnIdle()`

---

## 1. 结论摘要

| 项 | 结论 |
| --- | --- |
| 用户猜测的"线程间传递" | **不是延迟源**。主线程 `keyDown` 里直接写 `s_ctl.keys`，写入是即时的，锁无争用；实测"按键时刻 → latch 前"的传递开销在 1ms 量级 |
| 真正的根因 | **按键采样点错位一整帧**：`keys` 在帧首（等待**之前**）采样，却在等待约 16.7ms 之后才被 `ines_joypad_update_bits()` 使用，等于每帧都在用"上一帧开头"的快照（§3） |
| 实测基线 | 按键按下 → 被模拟器 latch：**平均 22.2ms，最大 32.3ms**（§2.3） |
| 修好后 | 同一指标：**平均 8~10ms，最大 ~16ms**，省掉约 **13~14ms（约 1 帧）**（§5.1 已实测） |
| 次要因素 | 画面"提交 → 主线程绘制完成"还有 **avg 4.8~7.5ms / max ~17ms**；音频在飞缓冲 4 个 ≈ **67ms**，比画面晚约 3 帧，明显拖累"手感同步感" |
| 不可消除部分 | 60Hz 帧同步固有的"等下一帧起点"（0~16.7ms，平均 8.3ms）+ 显示合成，真机同样存在；要再压只能上 Run-Ahead（§5.5，成本高） |

一句话：**先修采样点（一行代码级改动，实测 -13ms），再降音频缓冲，最后再考虑呈现链路与 Run-Ahead。**

---

## 2. 现状链路与实测

### 2.1 输入链路（macOS）

```
[主线程] NSEvent keyDown/keyUp
        -> iNESVideoView setKey:down:      (改 _pressedKeys，即时)
        -> videoViewKeyStateDidChange:     (加锁写 s_ctl.keys，即时)
                       |
                  ( s_ctl 互斥量，无争用 )
                       v
[模拟线程] simulationLoop 每帧一次：
        帧首取快照 -> 帧率等待(约16.7ms) -> ines_joypad_update_bits() -> ines_host_doframe()
                                                ^^^^ 按键在这里才生效
        -> presentIndexedPixels -> dispatch_async 主线程 setNeedsDisplay -> drawRect
```

### 2.2 帧循环关键顺序（`mac/iNESApp.m`）

```1988:2200:mac/iNESApp.m
// 1) 取请求与实时状态（含 keys = s_ctl.keys）   <-- 采样点在等待之前
// 4) 帧率控制 + while 等待到 frame_us           <-- 约 16.7ms
// 5) ines_joypad_update_bits(&host.joypad, ...) <-- 这里才用 keys
```

而 win32 的 `OnIdle()` 是**先判时间、未到就 `Sleep(1)` 返回（不采样）**，到达帧起点后才 `GetAsyncKeyState()` 采样并立即 `ines_joypad_update_bits()`：

```1166:1298:win32/iNES.c
// set input state
if(GetForegroundWindow() == hMainWnd) { ...GetAsyncKeyState 采样... }
...
ines_joypad_update_bits(&host.joypad, main_key_state, second_key_state);
ines_host_doframe(&host, screen_back);
```

**差异即根因**：mac 版把"取快照"放在了循环开头（为了一次性把 `pause/volume/osd/keys` 全部拷出），但 `keys` 与其它控制量不同——它对**采样时刻**敏感。结果每帧都在用陈旧一帧的输入。

### 2.3 实测数据

**方法**（诊断补丁见附录 A，脚本见附录 B）：主线程记录"按键变化时刻"，模拟线程在按键真正生效的那一帧（本帧取用的 `keys` 发生变化）计算差值，每秒输出 `input lag: latch_stale avg/max`。ROM 用 `bin/ROM/90tank.nes`，`osascript` 发方向键（按下保持 50ms 后抬起，避免短按被帧采样漏掉）。

| 指标 | 修复前（基线） | 修复后（P0） |
| --- | --- | --- |
| 按键 → latch（avg） | **22.2 ms**（多轮 20.7 / 21.8 / 22.1 / 22.4 / 23.5） | **8~10 ms**（多轮 8.3 / 9.4 / 10.5 / 13.6，随按键相位波动） |
| 按键 → latch（max） | **32.3 ms** | **16.1~16.5 ms** |
| 帧周期 / 忙碌 | 16.67ms / busy≈0.85ms（sim 0.38 + render 0.37 + push 0.05） | 不变 |
| 画面 提交→绘制完成 | avg **7.35 / 7.17 / 4.77 ms**，max **15.9~18.5 ms** | 同量级（该段与输入采样无关） |
| fps | 60.0±0.2 | 60.0±0.2 |

模型校验：基线 ≈ 等采样点(平均 8.33) + 陈旧等待(16.67 − busy 0.85 ≈ 15.8) + 模拟(0.4) ≈ **24.5ms**，与实测 22.2ms 吻合；修复后只剩"等采样点" ≈ 8.3ms，与实测吻合。**多出来的整整 15.8ms 就是那一个陈旧帧。**

### 2.4 端到端延迟预算（60Hz 显示器，不含显示器面板与外设本身）

| 阶段 | 现状 | 修 P0 后 |
| --- | --- | --- |
| 事件投递 + 写 `s_ctl` | <1ms | <1ms |
| 等下一次采样（0~16.7ms，平均 8.3） | 8.3 | 8.3 |
| 采样陈旧（根因，可消除） | **15.8** | 0 |
| `doframe` 模拟 | 0.4 | 0.4 |
| 提交 → 主线程绘制完成 | 4.8~7.5 | 4.8~7.5 |
| WindowServer 合成 → 上屏（未实测，随刷新相位 0~16.7） | 0~16.7 | 0~16.7 |
| **合计（不含上屏）** | **21~40，典型 ~30ms** | **6~25，典型 ~16ms** |

注：游戏自身在 VBlank 读一次手柄（`$4016` strobe）是真机行为，模拟器与真机一致，不计入"可优化"部分。

---

## 3. 根因（按贡献排序）

1. **P0 —— 按键采样点错位一整帧**（§2.2）：贡献约 **15.8ms**，占比最大，且纯属实现顺序问题，与"跨线程"无关。
2. **P1 —— 音频缓冲过深**：`APP_AUDIO_CACHE_NUM = 4`，每个缓冲 735 样本 ≈ 16.7ms → 声音比画面晚约 **50ms（3 帧）**。玩家听到按键反馈明显滞后，主观"手感糊"。
3. **P1 —— 画面提交到绘制存在 0~17ms 抖动**：模拟线程 60Hz 自由运行，不与显示器刷新对齐；`presentIndexedPixels` 用 `dispatch_async(main_queue)`（默认 mode），主线程处于事件跟踪模式时会排队；`drawRect` 每帧 `CGBitmapContextCreateImage()` 现拷现建（约 240KB/帧）。
4. **P2 —— 线程调度与系统省电**：模拟线程用默认 QoS，`nanosleep` 唤醒受 timer coalescing / App Nap 影响（当前实测帧周期抖动仅 ±0.2ms，但低电量或窗口不可见时会放大）。
5. **P2 —— 极短按键会被吞**：按下与抬起落在同一帧间隔内（<16.7ms）时，帧采样看不到该按键。人类操作一般不触发，但"快速连点"场景会偶发丢输入（真机同样会丢，属物理采样特性，可选择性地修）。
6. **不在本次范围**：联网对战 `np_cache_num = 4` 帧的输入延迟是协议设计（抗网络抖动），改动需双方同步且会显著降低抗抖动能力。

---

## 4. 优化方案总览

| 级别 | 方案 | 预期收益 | 改动量 | 风险 |
| --- | --- | --- | --- | --- |
| **P0** | §5.1 按键改在"本帧真正开始前"采样 | **−15.8ms（实测 22.2→8.3）** | ~6 行 | 极低 |
| **P1** | §5.2 音频在飞缓冲 4→2 | 音画失配 3 帧→1 帧（−33ms 音频延迟） | 3 个常量 | 低（可能增加欠载，需实测） |
| **P1** | §5.3 呈现链路（runloop mode + CGImage 缓存） | 减少主线程排队与每帧拷贝，抖动收敛 | ~20 行 | 低 |
| **P2** | §5.4 模拟线程 QoS + 抑制 App Nap/timer 合流 | 消除调度与唤醒抖动（低电量/后台更明显） | ~10 行 | 低 |
| **P2** | §5.6 输入事件队列（短按不丢） | 连点不丢输入 | ~40 行 | 中（`s_ctl` 语义变更） |
| **P3** | §5.5 Run-Ahead（预跑回滚） | 再省 ~8ms（消除"等下一帧起点"） | 大（需核心支持内存态快照） | 高 |

建议：**本次只落 P0 + P1（音频）**，观感改善最大且风险可控；呈现链路与 QoS 视实测再定；Run-Ahead 单独立项。

---

## 5. 方案细节

### 5.1 【P0】按键采样点前移（已实现并实测，待确认保留）

把 `keys` 的读取从"帧首一次性快照"移到"帧率等待之后、`update_bits` 之前"，与 win32 `OnIdle()` 时序一致。

```objc
// --- 帧首（1) 取请求与实时状态）：移除 keys 采样 ---
-			osd    = s_ctl.osd;
-			keys   = s_ctl.keys;
+			osd    = s_ctl.osd;
+			// keys 故意不在这里采样: 帧首取到的是"上一帧开头"的快照, 而它要在
+			// 等待(约 16.7ms)之后才被用于 latch, 白白多出一整帧的输入延迟。
+			// 真正的采样点见 4.55(本帧真正开始前), 与 win32 OnIdle 的时序一致。

// --- 帧率等待之后、输入之前：新增 4.55 ---
+			// ---- 4.55) 按键在本帧真正开始前采样(与 win32 OnIdle 时序一致) ----
+			ines_mutex_lock(&s_mutex_ctl);
+			keys = s_ctl.keys;
+			ines_mutex_unlock(&s_mutex_ctl);
```

配套：`while` 内局部变量 `ines_dword_t keys;` 需初始化为 `= 0`（`np_input_ready()` 不 ready 时会 `continue`，不经过 4.55）。

* 影响面：`keys` 仅用于输入段；`pause/volume/mute/osd` 等仍在帧首读取，行为不变；连发（turbo）相位 `key_flash_count` 不变。
* 联网对战：`np_frame_input()` 之前多读一次 `s_ctl.keys`，顺序不变。
* **实测**：22.2ms → 8~10ms（max 32.3 → 16.5）。

### 5.2 【P1】降低音频延迟（在飞缓冲 4 → 2）

`mac/iNESApp.m` / `mac/iNESAudio.h`：

```c
#define APP_AUDIO_CACHE_NUM      4   ->  2   // 在飞缓冲目标值(约 33ms)
#define APP_AUDIO_LOW_WATER      2   ->  1   // 低于该值补静音
#define INES_AUDIO_PREROLL_COUNT 4   ->  2   // 启动预填充帧数
```

* 预期：音频端到端 67ms → 33ms，与画面（~16ms + 上屏）基本同量级，按键音反馈不再"慢半拍"。
* 风险：欠载（爆音）概率上升。日志里 `audio diag: ... underrun=1550 / frames=55800` 是**累计值**，需看"每秒增量"是否变坏。
* 兜底：若 `underrun` 增量明显上升（比如 >5 帧/秒），回退到 `CACHE_NUM = 3`。
* 验证：连续跑 60s，看 `audio feed: ... play=44100/s surplus≈0` 与 `audio diag` 的 `underrun` 增量；主观听无爆音。

### 5.3 【P1】呈现链路收敛

1. **重绘请求改用 CommonModes**：`presentIndexedPixels` / `clearScreen` 里的 `dispatch_async(dispatch_get_main_queue(), ...)` 改为
   ```objc
   [self performSelectorOnMainThread:@selector(setNeedsDisplay:)
                          withObject:@YES
                       waitUntilDone:NO
                               modes:@[NSRunLoopCommonModes]];
   ```
   避免主线程处于 `NSEventTrackingRunLoopMode`（拖动窗口/菜单按下）时重绘被压住。
2. **CGImage 提前生成**：把 `drawRect` 里的 `CGBitmapContextCreateImage()`（每帧分配 + 拷贝 240KB）挪到模拟线程的 `presentIndexedPixels` 中，缓存在 ivar 里（加锁），`drawRect` 只做 `CGContextDrawImage`。`CGImage` 不可变、可跨线程创建/释放。
3. （可选）`drawRect` 只标脏实际画面矩形，减少无效绘制。

预期：主线程每帧省掉一次大块分配与拷贝，绘制峰值更平，`draw lag` 的 max（17ms）有机会收敛。不改变端到端延迟的理论下限。

### 5.4 【P2】模拟线程 QoS 与抑制系统省电

在模拟线程入口（`nes_proc` / `simulationLoop` 开头）：

```objc
pthread_set_qos_class_self_np(QOS_CLASS_USER_INTERACTIVE, 0);
```

在 `startup` 中持有、在 `shutdown` 中结束：

```objc
self.latencyActivity = [[NSProcessInfo processInfo]
    beginActivityWithOptions:(NSActivityLatencyCritical | NSActivityUserInitiated)
                      reason:ISTR("低延迟模拟")];
```

作用：抑制 App Nap 与 timer coalescing，保证 `nanosleep` 唤醒与线程调度按交互级优先级处理。当前前台实测抖动仅 ±0.2ms，收益有限，属"保险项"。

### 5.5 【P3】Run-Ahead（预跑 + 回滚，仅记录不实施）

修完 P0 后剩下的"等下一帧起点"（0~16.7ms，平均 8.3ms）是 60Hz 帧同步固有延迟。Run-Ahead 的做法：保存帧 N−1 的状态，用"最新输入"多跑一帧得到画面 N 并显示，再回滚到 N−1 用真实输入继续。可把这 8.3ms 也吃掉，代价是需要**内存态快照**（当前 `ines_host_save_state/load_state` 走 `FILE*`，太重，需新增内存版 API），且每个 mapper/APU/PPU 的状态序列化必须完整。收益/成本比偏低，建议单独立项评估。

### 5.6 【P2】输入事件队列（修"短按被吞"）

把 `s_ctl.keys` 从"状态位掩码"升级为"按键事件队列"（`{mask, down}` + 时间戳），主线程 push、模拟线程在帧起点 drain 并重放出当前状态。好处：同一帧间隔内的按下+抬起不会被漏掉，且能保留按下时刻（为将来更精细的时序处理留口子）。缺点：`s_ctl` 语义变更，`resetKeys`（失焦清键）逻辑要同步调整。优先级低。

---

## 6. 实施计划与验收

| 步骤 | 内容 | 验收标准 |
| --- | --- | --- |
| 1 | 落 P0（§5.1，本机已完成）+ 保留附录 A 诊断 | `input lag` avg ≤ 11ms、max ≤ 18ms（当前实测 8~10 / 16） |
| 2 | 冒烟：5 个 ROM 各跑 10s（90tank / 4人麻将 / Maajan(J) / nestest / nestest2） | fps 60.0±0.2，无崩溃，日志无新增 ERR |
| 3 | 落 P1 音频（§5.2） | `audio diag` 的 `underrun` 每秒增量 ≤ 1；`audio feed` 的 `play ≈ 44100/s`、`surplus` 在 ±100/s 内；主观无爆音 |
| 4 | （可选）落 §5.3 呈现收敛 | `draw lag` 的 max 从 ~17ms 下降或持平，avg 下降 |
| 5 | （可选）落 §5.4 QoS | 帧周期抖动不劣化 |
| 6 | 验收后移除附录 A 的临时诊断（或降为 `LOG_DBG`） | 每秒日志条数回到 2 条 |

---

## 7. 附录 A：临时诊断补丁（当前已加在工作区，验收后可移除）

`mac/iNESApp.m`：

1. `ines_ctl_t` 增加 `ines_int64_t key_stamp_us;`（仅诊断用）。
2. `videoViewKeyStateDidChange:` 内、写 `s_ctl.keys` 之后加：
   ```objc
   s_ctl.key_stamp_us = app_cur_time_us();   // 记录按键变化时刻
   ```
3. 帧循环内（4.6 段）统计"按键变化 → 本帧真正被 latch"的耗时，每秒输出：
   ```
   input lag: latch_stale avg=22.2ms max=32.3ms n=17
   ```

`mac/iNESVideo.m`：

4. `presentIndexedPixels:` 记录 `CLOCK_MONOTONIC` 提交时刻；`drawRect:` 绘制完成后累计差值，每 120 帧输出：
   ```
   draw lag: present->draw avg=7.35ms max=18.49ms n=120
   ```

## 8. 附录 B：按键测试脚本

```applescript
tell application "iNES" to activate
delay 0.5
tell application "System Events"
	tell process "iNES"
		repeat 60 times
			set c to 123 + (random number from 0 to 3)   -- 方向键 123..126
			key down c
			delay 0.05                                   -- 必须按住 >16.7ms, 否则会被帧采样漏掉
			key up c
			delay 0.04
		end repeat
	end tell
end tell
```

跑法：

```sh
cmake --build project/build --target iNES -j 8
: > "$HOME/Library/Application Support/iNES/iNES.log"
./bin/iNES.app/Contents/MacOS/iNES "bin/ROM/90tank.nes" &
osascript /tmp/ines_keytest.scpt
grep -E "input lag|draw lag" "$HOME/Library/Application Support/iNES/iNES.log" | tail -n 8
```
