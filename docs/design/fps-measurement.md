# HUD 帧率是怎么测的，以及为什么不放在独立线程

一个反复出现的问题：**HUD 的 FPS 显示跑在 UI 线程上，UI 线程卡住时，测量本身是不是也跟着失效、导致计数偏高？**

对多数框架的实现，这个担心是对的。对这里的实现，不成立 —— 但提问的方向指出了两个真实缺口。三层分开说。

---

## 1. 计数式 vs 时间戳式

如果 FPS 这样实现（常见做法）：

```
每秒定时器触发 → 读 frameCount → FPS = frameCount，frameCount = 0
```

那么 UI 线程卡 3 秒时定时器不响，**采样点根本不发生**，HUD 上的数字冻结在卡顿前那个好看的值上。这是最坏的失效模式：卡的时候看起来最正常。

这里不是这样。`NativeWindowHost.cpp:766` 记录的是**帧间隔时间戳差值**：

```cpp
if (FrameIntervalIsCadenceSample(lastFrameQpc_ != 0, loopWasContinuous_)) {
    const double intervalMs = QpcToMs(frameEndQpc - lastFrameQpc_);
    intervalRing_.Push(static_cast<float>(intervalMs));
}
```

QPC 是硬件时间戳，不依赖任何线程被调度到。UI 线程卡 3 秒 → 那一帧最终落地时，推进环里的是一个 **3000ms 的样本**。

关键性质：**这个测量是追溯式的，不是按频率采样的。** 卡顿期间没有任何东西丢失，卡顿本身就是被测量的那个量。所以「计数不对」不成立，数据是准的。

配套的 `loopWasContinuous_` 门（`FramePacing.h:178`）负责区分「卡了 3 秒」和「空闲等了 3 秒」—— 它查的是上一帧结束时调度器有没有待办工作，不是间隔的大小。这两种情况**无法用量级区分**，因为慢帧就是真的慢那么久。

---

## 2. 两个真实缺口

### 缺口 A：显示滞后

HUD 是**帧的一部分**画出来的（`NativeWindowHost.cpp:688`，`DrawHud` 在 `RenderNow` 内部），并且读的是 `prevFrameStats_` —— 上一帧的完整数据，因为 HUD 画的时候当前帧的字段还没填完。

所以卡顿**进行中**屏幕上是旧值。数字最终正确，但要等那一帧画完才看得见。想看到「已经卡了 2.7 秒且还在继续」，当前架构做不到。

那需要的是**卡死检测器（hang detector）**，和 FPS 是两种不同的仪器 —— 它不是「更准的 FPS」。

### 缺口 B：FPS 测的是 UI 线程产帧，不是眼睛看到的画面

这一条独立线程也解决不了，而且比缺口 A 更重要。

滚动和动画跑在 **DComp 合成线程**上（`ScrollContentHost`、`ICompositionBackend`），所以完全可能：

- 屏幕在 120Hz 流畅滚动（合成器在平移已有 surface）
- UI 线程同时卡着不动
- HUD 显示 FPS 3

三个数字都没错，它们回答不同的问题。HUD 的 FPS 回答「UI 线程每秒能产出几帧」，**不回答**「屏幕上每秒换了几次画面」。后者要问 DComp / DXGI frame statistics，加线程读不出来。

### 附带：真实的观测者效应

`NativeWindowHost.cpp:800` 是 `if (hudVisible_) RequestFrame(FrameReason::Paint);` —— 开着 HUD 就永久请求下一帧，直接摧毁「空闲零成本」。加上 `DrawHud` 每帧建 DWrite layout（约 0.8ms），代码注释记录了这正是「120Hz 面板测出 110fps 而非 120」的原因。**读 HUD 数字时要知道 HUD 自己在里面。**

---

## 3. 为什么不加独立线程

**准确性上不需要** —— 时间戳差值已经抗卡顿。

**而且有实际代价。** `PerformanceCounters.h:9-11` 把「UI 单线程」写成显式不变量：

> *Recording a value is a plain field write / increment (no allocation, no locking): the UI is single-threaded*

`FrameStats` 是裸 `double` / `uint32_t`，无任何同步。加一个读线程就是 data race（`double` 撕裂 + UB），要么改 atomic 要么上 seqlock，把成本压到热路径上 —— 为一个诊断功能，不划算。

---

## 值得做的：独立的卡死检测线程

一个只读 `lastFrameQpc_` 的线程（做成 `atomic<int64_t>`，relaxed load 几乎零成本），超过阈值报「UI 线程已无响应 N ms」。

这是当前完全没有的能力，且不与 FPS 抢职责：

| 仪器 | 回答的问题 | 线程 |
|---|---|---|
| interval ring → FPS | UI 线程每秒产出几帧（追溯）| UI |
| CPU ring → P50/P99/Max | 建一帧花多少 CPU | UI |
| hang detector（未实现）| 现在是不是正卡着，卡了多久 | 独立 |
| DComp frame stats（未实现）| 屏幕实际刷新率 | — |

---

## 读 HUD 的规则

CLAUDE.md 已有的一条，这里重申因为它和上面直接相关：**低 interval P50 配上巨大的 CPU Max 意味着双峰分布，FPS 数字只描述了便宜的那一半。这种时候信 Max。**
