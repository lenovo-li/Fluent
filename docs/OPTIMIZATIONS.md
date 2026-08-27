# 性能优化记录

本文件记录对**已有控件**的性能优化。新增控件的文档在 `docs/controls/`。

每条记录必须包含四部分：**慢在哪里**、**修复机制**、**如何证明**（变异测试中哪条断言会失败）、**未验证的部分**。第三部分不可省略：一个性能修复如果没记录它是怎么被证明有效的，下一个人会把它当成冗余代码删掉，而回归是静默的。

---

## 2026-08-22 — 抛弃式 text layout（ContentControl / TabControl）

### 背景：已有的缓存只有一个调用者

`graphics/ResourceCache.h` 里有一个 epoch 版本化的 LRU `GetTextLayout`，头注释点名了它的来由（*"TextBox rebuilt an identical IDWriteTextLayout 4-6 times per frame"*）。但全仓只有 `TextBox.cpp:86` 一个调用者，其余测量路径全是每次 `Measure` 现建现扔。

`TextBlock` 自己维护了私有缓存（`cachedLayout_` + `LayoutKey`），说明这个模式已被认可过一次，只是没有推广。

---

### 优化 1：`ContentControl::MeasureLabelWidth`

**慢在哪里** — 这是 Button / CheckBox / RadioButton 共用的标题测量路径。每次 `Measure` 建一个 `IDWriteTextLayout`、读一个 float、立刻丢弃。一个装着几十个按钮的面板，每帧重建几十个 layout，标题文本从未改变。

**修复机制** — 接到已有的 `ResourceCache::GetTextLayout` 上。这正是该缓存被设计出来的用法，不新增任何抽象层。相同标题的多个控件现在共享同一个 layout 对象。

**如何证明** — `tests/component/LabelMeasureCacheTests.cpp`。变异：把 `Context().resourceCache` 强制为 `nullptr` 绕过缓存 → 两条断言失败。同标题控件共享 layout 的断言同时覆盖了跨控件复用。

---

### 优化 2：`TabControl::MeasureHeaders`

**慢在哪里** — `MeasureOverride` **无条件**调用 `MeasureHeaders()`，后者为每个 tab 标题建**两个** layout（Normal + SemiBold，用于选中态字重切换不跳动）。但 `headerWidths_` 只依赖标题字符串，与 `availW` / `availH` 无关。

resize 拖拽时每帧约束都在变，`MeasureCached()` 的短路条件（不脏 + 约束匹配）失效，于是每帧重建 `2 × N` 个 layout 并全部丢弃。

**修复机制** — 加 `headerWidthsDirty_` 门。每处修改 `headers_` 的地方置脏，`MeasureHeaders` 在不脏时直接返回。

**顺带修的正确性缺口** — 原写法在 `Dwrite()` 存在但 `Initialize()` 失败（factory 为空）时仍会清掉脏标记，导致设备重建后**永远**拿不到真实字宽，tab 宽度被永久冻结在按字符数估算的值上。现在这种情况提前返回并**保持脏**，估算值留用，后续 attach 或设备重建仍能测量。

**如何证明** — 这一条的变异测试第一次**没抓住**，过程值得记录：

断言原本写的是 `textLayoutCacheMisses == 0`。注释掉门之后，测试**照样全绿** —— 因为 layout 走缓存，删掉门只是让每次查询都命中，miss 仍然是 0。

真正的判别信号是 `hits == 0`：它区分"这段遍历根本没跑"和"跑了但便宜"。改用 hits 断言后抓住了，数字也正好印证机制：

```
40 帧拖拽 → hits = 240 = 6/帧 = 3 tabs × 2 字重
```

教训已写入项目记忆：门控类优化断言操作次数归零，缓存类优化断言 miss 低而 hit 升。

---

### 验证状态

| 项目 | 结果 |
|---|---|
| Debug x64 | exit 0 |
| Release x64 | exit 0 |
| 测试 | 1405 / 1405 通过（新增 8 条）|
| 已删除的跟踪文件 | 无 |

**未验证** — 实际帧时间收益。headless 会话跑不出 DWrite 成本，`FluentUIBench` 的 TextStress 场景至今是 stub（`Scenes.cpp:96`）。需真机 F12：拖拽窗口时看 HUD 的 **CPU Max**，不是 P50 —— resize 拖拽的帧时间是双峰分布，中位数只反映便宜的那一半（见 CLAUDE.md「Reading the HUD」）。有 TabControl 的页面（数据控件 → TabControl）对比最明显。

---

---

## 2026-08-22（续）— SessionLog interval 门 + 剩余三处抛弃式 layout

### 修复 1：SessionLog 的 interval 门用了被否决的旧写法

**慢在哪里** — 不是性能问题，是**诊断数据丢失**。`NativeWindowHost.cpp` 的 SessionLog 喂入处仍是：

```cpp
if (gap <= 500.0) intervalMs = static_cast<float>(gap);
```

这正是 `FramePacing.h` 用 13 行注释论证「无论选什么阈值都修不好」的那个 bug：阈值无法区分「窗口空闲了 3 秒」和「这一帧耗了 3 秒」，而它在**恰好要读日志的那种负载**下选错。修复当初应用到了 interval ring，漏了 SessionLog，且原注释还写着 *"matching the interval-ring guard"* —— 已经不匹配。

后果：真的耗时 3 秒的帧被记成 `intervalMs = 0`，从 jank 列表消失，并且这个 0 会进入 `sumIntervalMs` **拉高** avgFps。SessionLog 是关窗落盘、用于事后证明时序回归的，也就是最严重的卡顿恰好不在日志里，而报告反而显示帧率很高。

**修复机制** — 与 ring 共用 `FrameIntervalIsCadenceSample(...)`。同时移除了冗余的第二道过滤：`RecordFrame` 本身已按 `phase != SessionPhase::Idle` 排除空闲，空闲间隔在那里被丢弃，不需要在调用点先归零。

**如何证明** — `tests/unit/SessionLogTests.cpp` 新增 3 条。两次变异均被抓住：
- 去掉谓词的 continuity 项 → `IdleResumeStillContributesZero` 失败
- 恢复旧的 `gap <= 500.0` 规则 → **3 条全部失败**，包括 `avgFps=241`（真实计算：8.3ms 与 3000ms 两帧，慢帧归零后均值塌成 4.15ms）

**测试范围的诚实说明** — `RecordFrame` 的 `intervalMs` 是参数，判定属于调用方，而调用方需要窗口。测试镜像了调用点那一个表达式（`HostInterval` 辅助函数），所以能锁住**共用规则**的回归，锁不住直接在 `NativeWindowHost.cpp` 里手写的回归。这一点写在测试文件的注释里。

---

### 修复 2：`ToggleSwitch::Measure`

**慢在哪里** — 它继承 `ContentControl` 却用不了 `MeasureLabelWidth`：需要标题的**高度**（`h = max(TrackH, label.h)`），而共享 helper 只返回宽度。所以在 Button/CheckBox/RadioButton 被修好之后，它仍留在抛弃式路径上。

**修复机制** — 新增 `ContentControl::MeasureLabelSize`，从**同一个缓存条目**返回宽和高；`MeasureLabelWidth` 改为委托它。这样类内只有一处 key 构造和一处 fallback —— 之前两份拷贝已经漂移过一次（key 声明的段落对齐与 fallback 实际构造的不一致）。

**顺带发现的真 bug** — `Measure` 用 `Theme().typography.bodySize`，而 `Render` 用 `EffectiveFontSize(...)`。调用 `SetFontSize(48)` 后，标题按 48 **绘制**、按 14 **测量** —— 标题溢出自己的控件，且溢出部分在上报的脏区之外。CheckBox 和 RadioButton 两个 pass 一直都用 `EffectiveFontSize`，只有这里是例外。这个 bug 是写测试时暴露的：`ToggleSwitchTallLabelStillGrowsHeight` 第一次运行就失败。

**如何证明** — 变异「把 `EffectiveFontSize` 换回原来的裸 token」→ `ToggleSwitchTallLabelStillGrowsHeight` 失败。变异「`MeasureLabelSize` 绕过缓存」→ 含 ToggleSwitch 在内 3 条测试失败。

---

### 修复 3：`GroupBox::Measure`

**慢在哪里** — 它是 `FrameworkElement`，拿不到 `ContentControl` 的 helper，所以自带一份 header 的 `CreateTextLayout`。作为容器，它的 Measure 在 resize 拖拽时**按定义**每帧都跑，而 header 字符串是编译期固定的。

**修复机制** — 走 `ResourceCache`，保留 cache-less 直建 fallback。

**如何证明** — 变异「绕过缓存」→ 3 条失败。`GroupBoxHeaderRevisitedWidthHits` 专门锁住 key 的宽度分量：回到已构建过的宽度必须命中 —— 忽略 `availW` 的 key 会在所有宽度下报同一个 header 高度，而混入了不稳定量（指针、帧计数）的 key 则永远命不中。

---

### 修复 4：`MenuFlyout::MeasureText`

**慢在哪里** — 四处中最重的一处，因为它是 **O(items) per MeasureLevel**，不是每控件一次：一个带快捷键的 20 项菜单要建最多 40 个 layout、各读一个 float、全部丢弃；而 MeasureLevel 在每次子菜单展开、每次已打开菜单重排时都会再跑一遍。

**修复机制** — `MeasureText` 走缓存（菜单文本与快捷键都经由它）。这里 `maxWidth` 用的是 100000 哨兵值（菜单行不换行，行宽本身就是被测量的量），所以 key 跨重排稳定 —— 与两处标题缓存不同，它在提供宽度变化时**不会**失效。

**未被测试覆盖** — 它的测量位于一个私有内部类（`MenuListView`），只能通过打开的 popup 窗口到达，headless 不可达。此项**仅经代码审查修改**，需要真机验证。这一点写在测试文件头部，不假装有覆盖。

---

### 验证状态（本轮结束）

| 项目 | 结果 |
|---|---|
| Debug x64 | 干净 |
| Release x64 | 干净 |
| 测试 | 1415 / 1415 通过（本轮新增 18 条）|
| 变异测试 | 4 处修复全部有断言可抓 |
| 已删除的跟踪文件 | 无 |

**需真机验证** — MenuFlyout 的菜单展开（窗口 & 对话框 → MenuFlyout，注意多级子菜单）；ToggleSwitch 设过 `SetFontSize` 后标题是否还溢出。帧时间收益仍需 F12 看 **CPU Max**（非 P50）。

---

---

## 2026-08-22（续三）— Expander 展开动画的 UI 线程成本：实测 + 修掉一处帧浪费

### 起因
用户问：动画会不会卡死 UI 线程？「任何会卡死UI线程或者在UI线程的耗时操作在当前项目
都是不允许的」。这个问题不能靠推理回答，所以加了一个 bench 场景实测。

### 慢在哪里 —— 先说结论：不慢，但发现一处纯浪费

`Expander::OnAnimationTick` 是**全框架最贵的动画形状**：它抬 `DirtyFlags::Measure`
而不是 `Render`（reveal 高度进入自身 desired size，父级必须每帧重跑布局），
所以成本是 **O(内容子树) 每 tick**，而 Button 的 tint、ToggleSwitch 的旋钮都是 O(1)。

新增 `FluentUIBench` 场景 **ExpanderReveal** 实测（200 个子元素的内容子树，
1000 次 tick+relayout）：

| 指标 | 数值 |
|---|---|
| 每帧 tick + 重新布局 | **0.0037 ms** |
| 占 60Hz 单帧预算 | **0.02%** |
| 结论 | 不可能卡住 UI 线程 |

作为对照，`AnimationStress` 场景显示 200 个活跃动画 / 5000 元素树是
0.00025 ms/tick，**成本跟活跃集走，不跟树大小走** —— 这是 pull-once 收集
（`AnimationRegistry::Collect` 走一次树，`Tick` 只遍历活跃集）的直接证据。

### 但找到并修掉一处真浪费：snapEps 偏紧了 10 倍

`OnAnimationTick` 原本给 `Approach` 传 `snapEps = 0.001`，而其他每个动画控件
（Button tint / CheckBox check / RadioButton dot / Hyperlink hover）都用
`AnimatedValue` 的默认 **0.01**。后果：

| | tick 数 | 约合时长(60Hz) |
|---|---|---|
| 修复前（eps 0.001） | 26 | **432 ms** |
| 修复后（eps 0.01） | 18 | **300 ms** |

多出来的约 14 帧全是浪费：reveal 最后 0.9% 的位移在任何真实内容尺寸下都是亚像素级，
看不见，**但每一帧都要重新布局整个内容子树**，而且推迟了「回到零成本空闲」的下降沿。

在 Render-only 的动画上收紧 epsilon 几乎免费；在 **Measure 级**动画上收紧 epsilon
是用真实的每帧代价买不可见的精度。这条教训写进了 `OnAnimationTick` 的注释。

同时把 `WantsAnimationTick` 的 epsilon 也改成默认值。**两者必须一致**：如果
`Animating` 的 eps 比 `Approach` 的 snap eps 更紧，`Approach` 已停止移动（落在 snap
带内）而 `Animating` 仍报「还在动」，元素就永久留在活跃集里被 tick —— 这正是
本项目绝不接受的空闲成本泄漏。注释里写明了「改一个就要改另一个」。

### 如何证明
- `ExpanderTransitionTests.EaseTerminatesWithinABoundedFrameCount` —— 断言收敛时长
  落在主题自己的 `MotionTokens.slowMs`(300) + 一帧余量内。**这条测试第一版是失败的，
  而失败正是有价值的部分**：它把 432ms 这个数字暴露出来，才找到 epsilon 问题。
  测试注释里保留了 432 → 300 的实测历史。
- `ExpanderTransitionTests.ReturnsToZeroCostIdleAfterAnimating` —— 双向跑完缓动后
  `WantsAnimationTick()` 必须为 false，且对已静止的元素再 tick 10 次不得重新武装。
  这条锁住的是「空闲零成本」这个硬约束。
- 变异测试（把 `if (transition == Transition::Instant)` 改成 `if (true)`）：
  14 条中 5 条失败，且正好是依赖 Animate 的 5 条。

### 修正：帧预算必须按本机刷新率算，不能写死 60Hz

用户指出「我当前电脑是240HZ刷新率，所以60HZ刷新率是不是不适用当前机器」——这是对的，
而且错的方向很危险：**60Hz 的 16.67ms 会把负载低估 4 倍**。
240Hz 的真实预算是 **4.17ms**。同一个 0.0036ms 的开销，
按 60Hz 报是 0.02%，按 240Hz 报是 **0.086%**。

`ExpanderReveal` 场景改为用 `EnumDisplaySettingsW` 查本机刷新率
（与 `NativeWindowHost::RefreshHz()` 同一个来源，所以 bench 与运行时对「一帧是多久」
的理解一致），查不到才退回 60Hz **并在输出里说明**，不静默假装。
实测本机输出：`0.086% of this display's frame (240 Hz -> 4.17 ms)`。

### 动画本身是刷新率无关的（已实测，不是推断）

框架的 `FrameScheduler::ComputeDt` 从 QPC 取真实经过时间，
所以 `Approach(target, dtSec, tau)` 的缓动速度只取决于挂钟时间。
新增两条测试实测三种刷新率：

| 刷新率 | 缓动时长 | tick 数 |
|---|---|---|
| 60 Hz | 283 ms | 17 |
| 120 Hz | 283 ms | 34 |
| 240 Hz | 279 ms | 67 |

**时长一致（±1 tick），tick 数 3.94 倍** —— 高刷买到的是更平滑，不是更快。
这正是「按 dt 缓动」与「按固定步长缓动」的分水岭：后者在 240Hz 上会跑快 4 倍。
`EaseDurationIsIndependentOfRefreshRate` 锁住控件这一侧的契约。

反过来也记下了（`HigherRefreshRateYieldsMoreIntermediateFrames`）：
240Hz 下同样 300ms 内有 4 倍的 tick，而每个 tick 都是一次 Measure 级重新布局 ——
这就是为什么 per-tick 成本必须保持很小，也是 snapEps 那 14 帧浪费在高刷机器上
实际是 ~56 帧浪费的原因。

### 顺带发现：`DisplaySyncScheduler` 是死代码
`FluentUI/diagnostics/DisplaySyncScheduler.{h,cpp}`（149 行）实现了完整的
刷新率检测 + 分级帧预算 + DWM VSync 同步线程，注释里连 240Hz 的跨帧策略都写好了，
但**全仓零个使用者**（`grep` 确认）。真正驱动帧节奏的是
`NativeWindowHost::RefreshHz()` + `FrameWaitRemainingMs()`，两套机制并存且互不知情。
未处理 —— 删除还是接入属于架构决策，留给用户定。

### 未验证
真机观感（是否流畅、有无撕裂/重影）。headless 只能证明每帧 CPU 成本极小、
帧数有界、时长与刷新率无关、结束后回到空闲；「看起来顺不顺」必须真机确认。
另外 `ExpanderReveal` 场景刻意不含 DirectWrite 与 GPU 成本（内容是 BenchLeaf，
Expander 未 attach 所以 Render 提前返回），隔离出的正是动画本身新增的布局成本。

---

## 2026-08-22（续二）— DirtyFlags 分级审计，顺带修掉第三处 Measure/Render 字体不一致

### 审计动机与结论（先说结论：过度标注**没有**发现）

CLAUDE.md 的约束是「挑实际需要的最高层级 —— 把只需重绘的改动标成 Measure 会把整帧
塌成全窗口重绘」。所以逐个检查了 27 处 `SetProperty(..., DirtyFlags::Measure)`。

**没有一处过度标注。** Border / GroupBox 的 `borderThickness_` 进入 `ContentInset()`，
Separator 的 `thickness_`、Rating 的 `iconSize_` 都直接进入 `SetDesired`，
全部名副其实。

反方向（**标得太低**）才是真问题，因为它更危险：过度标注只是慢，标得太低会让某一帧
拿着**过期的 desired size** 去布局，而且症状是间歇的 —— 直到某个无关改动触发重新测量。

### 找到的缺陷：`MeasureLabelSize` 把字重硬编码为 NORMAL

**慢在哪里** —— 这条不是性能问题，是**正确性**问题（但归档在此处，因为它是上一轮
把测量接入 ResourceCache 时引入的同一段代码）。

`ContentControl::MeasureLabelSize` 在缓存 key 和 cache-less fallback 两处都写死
`DWRITE_FONT_WEIGHT_NORMAL`，而所有绘制路径都走 `EffectiveFontWeight(fallback)`。
SemiBold 比 Normal **宽**，于是任何绘制字重不是 Normal 的控件都测窄了，标题溢出
自己 Measure 申请到的框。两条到达路径：

- `Button::Kind::Accent` 按控件语义绘制 SemiBold，却按 Normal 测量
- 任何被调用 `SetFontWeight(SEMI_BOLD)` 的 ContentControl：Render 认这个覆盖值，
  Measure 忽略它

**修复机制** —— `MeasureLabelSize` / `MeasureLabelWidth` 新增 `weight` 形参
（默认 NORMAL，保持既有调用兼容），四个调用点传入**与 Render 同一个表达式**。
`Button::SetKind` 的脏标记从 `Render` 改为 `Measure` —— 它选择字重，因此确实改变
desired size；原注释写着「only swaps the fill palette ... so it is Render-only」，
那是错的。

**如何证明** —— `tests/component/LabelWeightMeasureTests.cpp` 8 条。
变异（把两处 weight 改回硬编码 NORMAL）→ **8 条中 7 条失败**，数字直接显示 bug：
SemiBold 与 Normal 报出完全相同的宽度（336.41 vs 336.41）。唯一存活的是
`SemiBoldIsMeasurablyWiderThanNormal` —— 它只查 DWrite 本身，正确地不依赖控件代码。

数量断言也对上了：Accent 与 Standard 按钮的 desired 宽度差 **9.41**，
与 DWrite 报告的两种字重宽度差**完全一致**（`EXPECT_NEAR(..., 1.0f)`）。

覆盖了缓存与 cache-less 两条路径（`UIContext` 成员按设计都可为 null，只修缓存分支
会漏掉另一条），并有一条专测缓存 key 必须区分字重 —— key 若漏掉字重，第二次查询会
命中第一条记录，两个控件报同一宽度。

### 这是同一类缺陷的第三次

前两次都已修：ToggleSwitch 的字**号**不一致（Measure 用 `typography.bodySize`，
Render 用 `EffectiveFontSize`），Expander 把 em size 当行盒高度。
**教训已写进 `ContentControl.h` 的注释**：新增任何测量标题的路径，
排版参数必须取自绘制代码使用的同一批访问器。

### 验证状态

| 项目 | 结果 |
|---|---|
| Debug x64 | 干净 |
| Release x64 | 干净 |
| 测试 | 1462 / 1462（本轮新增 8 条）|
| 变异测试 | 7/8 翻转，存活的那条正确地不依赖被测代码 |
| 已删除的用户控件 | 无 |

**未验证** —— 真机视觉。需要看：Accent 按钮（基础控件 → Button）的标题在窄容器里
是否还溢出；以及对 CheckBox / RadioButton / ToggleSwitch 调用
`SetFontWeight(DWRITE_FONT_WEIGHT_SEMI_BOLD)` 后标题是否完整。

---

## 待办：剩余位置

`ListBox::GetItemText` 的每帧每行堆分配**已完成**（scratch 重载，见上文第二轮）。

未实现、与诊断相关（见 `docs/design/fps-measurement.md`）：
- **卡死检测线程** —— 只读 `lastFrameQpc_`（`atomic<int64_t>`，relaxed load），超阈值报「UI 线程已无响应 N ms」。当前完全没有这个能力。
- **DComp frame statistics** —— 屏幕实际刷新率。HUD 的 FPS 测的是 UI 线程产帧，合成器驱动的滚动流畅度它看不见。
