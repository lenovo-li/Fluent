# 像素级自动化测试：能测什么，不能测什么

用户提问的起点是一组截图：好几个控件被点中后，蓝色焦点框（focus ring）显示不全，
缺一到三条边。问的是「能不能做自动化截图测试」。

结论：**能，而且已经做了** —— 但它不是「截图对比」，而是**离屏渲染 + 像素断言**。
这个区别是本文的主要内容，因为选错形式会得到一个长期骗人的测试套件。

---

## 1. 为什么已有的 1423 条测试抓不到这个 bug

`tests/unit/DirtyBoundsOverflowTests.cpp` 已经在测焦点框，方式是**纯几何**：
把控件的 `VisualOverflowDip()` 和「Render 应该画到哪」手算出来对比。

这能抓住「控件申报的重绘区域不够大」，也确实抓住了 Hyperlink 和 Expander 两处。
但它抓不到截图里的这一类，因为：

> 控件申报得**完全正确**，环也**确实画了**，只是在到达 surface 之前被祖先容器的
> `ClipGuard` 裁掉了。

几何测试里没有 surface，没有 device context，没有裁剪栈。它问的是「控件想画多大」，
而 bug 在「画出来的东西有没有活着到屏幕上」。这两个问题之间隔着整个 D2D 裁剪栈。

**这是一个测试盲区的一般教训**：断言的是意图（geometry / 参数 / 调用次数）还是结果
（pixels）。CLAUDE.md 里已经记了同类事故 ——「Several bugs here shipped past large
green suites because the test exercised a path the fake backend stubbed out」。

---

## 1.5 先厘清两个被混为一谈的词：headless ≠ 不渲染

这个混淆是上述盲区能长期存在的直接原因，所以单独写一节。

**headless 和 HWND 不是一对反义词，它们不在同一层级。** HWND 是一个具体的 Win32
资源（操作系统给的窗口句柄）；headless 是一种运行模式。headless 的对立面是
**headed / on-screen**（有可见窗口、有消息循环、有屏幕输出），HWND 只是构成 headed
的必要条件之一。

本仓的 headless 实际拿掉了四层，HWND 只是第一层：

- **HWND** —— 无 `CreateWindowExW`（全仓只有 `NativeWindowHost.cpp:163` 和
  `PopupHost.cpp:71` 两处创建窗口，测试都不走）
- **消息循环** —— 无 `MsgWaitForMultipleObjectsEx`，因此无 `WM_PAINT` / `WM_MOUSEMOVE`
  等消息驱动
- **DirectComposition** —— 无合成器，由 `FakeCompositionBackend` 顶替
- **屏幕输出** —— 无 present / commit，像素不去任何显示器

**而关键在于：HWND 管的是输入、坐标和系统集成，不管绘制。** 全仓真正需要 HWND 的
调用是 `ImmGetContext`（输入法）、`SetCapture`（鼠标捕获）、`ClientToScreen`
（坐标转换）、`GetDpiForWindow`（DPI 查询）—— 一个也不是绘制调用。

绘制侧完全不需要窗口：`D2DContext::Initialize()` 建 D3D11 设备时没有 swap chain，
而 `ID2D1DeviceContext` 可以以一张普通 bitmap 为 target。这就是 `PixelSurface.h`
成立的全部原因 —— **真渲染、真读回像素、真抗锯齿，但零窗口。**

于是本套件处在一个很多人以为不存在的位置：

| | HWND | 消息循环 | DComp | 真 D2D 设备 | 真像素 |
|---|---|---|---|---|---|
| 原有 1423 条 | ✗ | ✗ | ✗ | ✗ | ✗ |
| 新增 11 条 | ✗ | ✗ | ✗ | ✓ | ✓ |
| 真机 demo | ✓ | ✓ | ✓ | ✓ | ✓ |

中间那一行**仍然是 headless 的** —— 无窗口、无消息循环、不上屏，1434 条测试合计仍
在 1.6 秒内跑完。它只是不再「无渲染」。

**把 headless 等同于「不能测像素」是一个具体的、有代价的误解**：它让人以为想验证
渲染就必须开窗口，于是干脆不验证 —— 焦点框缺边正是这样长期无人自动检测的。

---

## 2. 为什么这个项目不做「截图对比」（golden image）

先说清楚：**golden image 不是坏方法，主流框架大量在用。** 它只是与本项目当前的规模
不匹配。把这两件事分开说，否则下一个读到这里的人会以为下面在贬低 Chromium。

### 2.1 别人怎么做的（行业现状：分层，不是二选一）

**Chromium / WebKit 就是 golden image 的重度用户。** Blink web tests 分两类：HTML 比
HTML，以及 HTML 比 `-expected.png`；不同平台还要各存一份 PNG。代价体现在他们的
rebaseline 流程上 —— 改一处渲染要「提交 `[ Failure Pass ]` 标记 → 等 1-2 天让 CL 滚
进 Chromium → 手动生成新基准 → 移除标记并提交基准」，而 Skia 的文档直接承认可能要
反复几轮，原因包括 *flaky tests*。他们负担得起是因为浏览器的规范符合性**只能**用像素
证明，而且有专职 infra 团队和试运行机器池。这是投入换来的能力，不是免费默认项。

**Flutter 的 golden test 恰好印证了字体那条否决理由。** 社区长期报告 macOS 与 Linux
下文字抗锯齿不同导致 golden 必然失败，公认的应对只有两条：只在单一平台跑 golden、
其他平台整体跳过（`skipGoldenAssertion: () => !Platform.isMacOS`）；或改用 **Ahem
字体**把每个字符渲染成不透明黑块，从根上消灭抗锯齿差异。后者值得停一下看：为了让
像素可复现，**他们放弃渲染真实文字**。要么牺牲保真度，要么牺牲确定性。

其余几家的分层也很清楚：Qt 用 `QTest` 测逻辑、另有独立的 `lancelot` 渲染对比；
Android 是 Robolectric（JVM、无设备）+ instrumented test（真机）+ Paparazzi 截图三层；
Web 组件库普遍是 jsdom（无渲染引擎，连布局都没有）+ Chromatic / Percy 云端截图。
WPF / WinForms 基本没有公开的自动化视觉测试传统，靠手工 QA。

**所以行业通例是分层：**

| 层 | 测什么 | 代价 |
|---|---|---|
| 无渲染（jsdom、Robolectric、本仓原有 1423 条） | 逻辑、状态、布局算术 | 便宜、快、确定 |
| 真渲染 + 语义断言（本仓新增 11 条） | 特定视觉契约 | 中等，需图形栈 |
| golden image（Chromium、Flutter、Percy） | 整体外观 | 贵，跨平台易碎，需 rebaseline 流程 |

本仓原本只有第一层，现在有第一和第二层，缺第三层。

### 2.2 为什么本项目现在不上第三层

**投入产出比不匹配当前规模**，而不是方法本身有问题。Chromium 有专职团队维护
rebaseline 基础设施；本仓是单人项目，1434 条测试 1.6 秒跑完。引入 golden image 要
建基准图管理、跨平台策略、rebaseline 流程，而它能多抓的东西有相当一部分开着 demo
看一眼就发现了 —— 焦点框缺边正是用户自己看出来的。

具体的技术否决理由有四条，每条都足以单独否决**在本仓**引入它：

1. **需要真窗口。** 测试是 headless 的（见 1.5 节），截图测试要拉起
   `NativeWindowHost` + DComp + 消息循环，等于把最难复现、最依赖机器状态的一层引入到
   1400 条测试的前置条件里。
2. **字体渲染不可复现。** DirectWrite 抗锯齿受系统 ClearType 设置、字体版本、GPU
   驱动影响（Flutter 踩的正是这个）。基准图换机器必然不一致，于是容差被调大，而容差
   一旦能容忍字体差异，就同时容忍一条 1.5 DIP 的细线消失 —— **那正是要抓的 bug。**
3. **失败信息没有价值。** 「图片有 0.3% 像素不同」不告诉任何人哪里坏了。而
   `EXPECT_FALSE(e.top)` 直接说：上边缘没画出来。
4. **基准图会被随手更新。** 视觉一变就重新生成基准，是缺少 rebaseline 纪律时的必然
   归宿，此后它只能证明「和上次一样」，不能证明「是对的」。

### 2.3 第二层不能替代第三层（重要）

必须写明，否则会高估现有能力：**语义探针只能验证「已经想到要验证」的契约。**
焦点框缺边现在能测，是因为**先有人看见了它**。间距变丑、配色错、某控件整体偏移十几
像素 —— 探针不会问这些问题，golden image 会。

当前的取舍是：用探针钉死每一个**已知**的视觉契约（焦点框、下划线、残影带），把
「整体外观对不对」交给人眼。单人项目规模下人眼更可靠且成本更低。等到有第二个人参与、
或控件多到看不过来时，再重新评估第三层。

---

## 3. 实际做法：离屏 D2D + 语义化像素探针

关键发现是 **`D2DContext::Initialize()` 不需要 HWND**（`graphics/D2DContext.cpp`）：
它建 D3D11 设备 + D2D 设备，没有 swap chain。硬件失败自动回落 WARP 软件渲染器，
所以在没有 GPU 的构建机上也能跑。`ID2D1DeviceContext` 可以以一张普通 bitmap 为
target。于是可以**真渲染、真读回像素，但完全不需要窗口**。

### `tests/framework/PixelSurface.h`

- 建 D2D 设备栈 + 一张 `D2D1_BITMAP_OPTIONS_TARGET` 目标位图 + 一张
  `CPU_READ` 暂存位图（D2D 要求读回必须是独立的 CPU_READ 位图，所以画完
  `CopyFromBitmap` 再 `Map`）。
- `Paint(bg, fn, clipHint)` 用**和 `NativeWindowHost` 一样的方式**构造
  `DrawingContext`（真 device context、同一个共享 brush、dpiScale 1.0），把它交给
  `fn`。走的是产品的真实绘制路径，不是模拟。
- **dpiScale 固定 1.0，所以 1 DIP == 1 像素。** 这是刻意的：测试里的坐标可以直接
  从 DIP bounds 写出来，可读。不要「改进」成带缩放。
- 读回时**反预乘**（D2D 存预乘 alpha），这样断言可以直接对比 theme token 的通道值。
- `Valid()` 为假表示机器上建不出 D2D 设备。测试**必须**检查并跳过，
  且 `SkipIfNoDevice` 会 **printf 一行 `[SKIP]`** —— 在 1400 条测试里静默跳过和
  通过无法区分，而那正是渲染回归溜过绿色套件的方式。

### 探针不比较颜色，而是判定「有没有环墨」

`ProbeRing` 沿环的四条边各扫一条带，问「这条带上有没有像素明显偏向环的颜色」。
三个设计决定：

- **不做精确颜色相等。** 1.5 DIP 居中描边 + 抗锯齿，边上像素是部分覆盖，精确相等
  是错的断言。
- **跳过圆角。** 环的圆角半径是 `cornerRadius + inset`，两条边共享一个角；不跳过的话
  一条边的墨会替邻边作证，于是「缺一条边」测不出来。
- **`ClippedFocusRingTests.cpp` 里改用色相判据**（蓝通道领先红通道），因为那里画面
  上还有控件自己的灰色边框和文字抗锯齿，「偏离背景」不足以区分。

---

## 4. 测试文件的分工，以及「自检」为什么必须存在

### `tests/component/FocusRingPixelTests.cpp` — 机制与自检（6 条）

前两条是**探针自检**，它们的存在理由值得写下来：

- `HarnessSeesAllFourEdgesOfAnUnclippedRing` —— 没画错的环，四条边都要看得见。
- `HarnessReportsNoEdgesWhenNothingIsDrawn` —— 空白 surface 必须报告 0 条边。

**没有这两条，一个坏掉的探针会对所有控件报告「四条边全缺」，看起来像灾难性产品
bug；或者反过来永远报告「全在」，让后面所有断言变成空断言。** 一个只会说 yes 的
断言和没有断言等价。

余下四条锁住机制：

| 测试 | 锁住的契约 |
|---|---|
| `ContentClipRemovesEveryRingEdge` | 把 `FocusVisual.h` 里「环必须在 content clip 之前画」的注释变成有断言的契约 |
| `ClipAtContentBottomRemovesOnlyTheBottomEdge` | 单边缺失的签名（截图里 Button 那种倒 U 形） |
| `ClipHintDoesNotClipRingPixels` | **`clipHint` 不裁剪绘制**，它只影响 `Panel::Render` 的遍历剪枝 |
| `UnderDeclaredOverflowLeavesRingResidue` | 申报不足导致的残影，作为像素可见 |

第三条是防止误诊的：如果一个环在申报正确的情况下仍然缺边，那是 `ClipGuard` 问题，
**不是** `clipHint` 问题。搞混这两者会让人去改错的地方。

### `tests/component/ClippedFocusRingTests.cpp` — 真控件真容器（5 条）

真 `Button` / `CheckBox`，真 `ScrollPanel`，真 `Render` 调用链。所以这里失败指向的是
产品缺陷，而不是 D2D 裁剪的性质。

`ClipIgnoresDeclaredOverflowForCheckBoxToo` 顺带钉死一个容易被误解的点：
**`VisualOverflowDip()` 对裁剪毫无影响。** 它只决定重绘区域。CheckBox 申报 4.75
（比 Button 的 3.75 大），照样被一样地裁掉。

---

## 5. 变异测试：结果

按 CLAUDE.md 的规程，「缺边」类断言必须验证它不是因为别的原因通过的。把
`ScrollPanel::Render` 的 `ClipGuard clip = dc.PushClip(...)` 换成裸 `ClipGuard clip;`
（不裁剪），重新构建后：

```
ScrollPanelClipCutsRingOfChildFlushWithTopEdge     FAIL  EXPECT_FALSE(e.top)
ScrollPanelClipCutsRingOfChildFlushWithBottomEdge  FAIL  EXPECT_FALSE(e.bottom)
ClipIgnoresDeclaredOverflowForCheckBoxToo          FAIL  EXPECT_FALSE(inkAboveViewport)
UnclippedButtonShowsAllFourEdges                   OK    ← 不依赖裁剪，正确地没动
ScrollPanelChildInsetByRingPadKeepsAllEdges        OK    ← 内缩够远，正确地没动
```

三条依赖裁剪的断言全部翻转，两条不依赖的保持绿色。这正是想要的判别力：
测试确实在测裁剪，而且没有过度耦合。`ScrollPanel.cpp` 已还原为零 diff。

---

## 6. 这套测试**不能**回答什么

必须写清楚，否则下一个人会高估它：

- **不能判断「看起来对不对」。** 它数边，不评审设计。间距难看、颜色不搭、动画突兀，
  一概测不出。
- **不测 DirectComposition。** 合成路径（`ScrollContentHost`、`ICompositionBackend`）
  仍然只有 `FakeCompositionBackend`，而那个 fake 传 null dc 和零偏移 —— CLAUDE.md 已
  指出它抓不到 surface 变换那一类 bug。本套件是 D2D 直绘路径。
- **不测滚动/动画的流畅度。** 单帧静态渲染，看不到帧间行为。
- **不测弹出窗口。** `MenuFlyout` 的菜单在独立 popup 窗口里，headless 到不了。
  这也是 `MeasureText` 那处优化至今无覆盖的原因。
- **抗锯齿容差是软的。** 阈值 0.25 覆盖率。一条比这更淡的边算「不存在」。

---

## 7. 扩展这套测试时的注意事项

1. **先写自检。** 新加一类探针（比如测下划线、测残影带），第一条测试必须证明探针
   在「东西确实在」时报告存在，第二条证明在空白上报告不存在。
2. **一定做变异测试，而且要选对断言。** `docs/OPTIMIZATIONS.md` 记了教训：门控类
   优化断言操作次数归零，缓存类断言 hit 升而非 miss 归零。像素测试的对应版本是：
   **「缺失」类断言必须验证在 bug 修好后会翻转**，否则它可能因为探针位置算错而恒真。
3. **不要引入基准图。** 见第 2 节。
4. **新文件要注册进 `tests/FluentUITests.vcxproj` 和 `.filters`**（无 glob），
   `<Filter>` 必须等于磁盘上的相对目录。
