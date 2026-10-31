# ScrollPanel 布局容器

## 概述

通用的滚动容器：子元素从上到下堆叠，总高度超过视口时垂直滚动；某个子元素的自然宽度超过
视口时（例如一排固定宽度的日历放在窄窗口里），出现底部滚动条横向滚动。两条滚动条都是
覆盖层（来自内嵌的 [ScrollViewer](ScrollViewer.md)），内容溢出时常驻显示。

**它不虚拟化**：测量并绘制全部子元素，再裁剪到视口。大量同构的行请用 `ListBox` / `TreeView`
/ `DataGrid`，长文本用 `TextArea`。

**典型使用场景**:
- 设置页、长表单
- demo 页面那种「很多控件竖着堆」的内容
- 需要滚动的 TabControl 页内容

---

## 继承层次

```
Visual
  └─ UIElement
      └─ FrameworkElement
          └─ Panel
              └─ ScrollPanel
```

**命名空间**: `fluent`
**头文件**: `FluentUI/layout/ScrollPanel.h`

---

## 公开 API

### 构造函数

```cpp
ScrollPanel();
```

可聚焦（键盘滚动），间距 0，内边距 0，滚轮步长 32 DIP，溢出时滚动条常驻。

### 布局

```cpp
void SetSpacing(float dip);          float Spacing() const;     // 子元素之间的竖向间距
void SetPadding(Thickness p);        void SetPadding(float all);
Thickness Padding() const;                                      // 面板边缘到子元素的距离
void SetWheelStep(float dip);        float WheelStep() const;   // 每「行」的 DIP，下限 1
```

**脏标记**: `SetSpacing` / `SetPadding` 是 Measure；`SetWheelStep` 不标脏（只影响下一次滚轮）。

一点内边距（4–8 DIP）能保住子元素的焦点环：焦点环画在控件边界外几个 DIP，贴着面板边缘的
子元素会被视口裁掉一半焦点环。

### 滚动

```cpp
float VerticalOffset() const;
float TargetVerticalOffset() const;   // 平滑滚动的目的地；不在动画中时等于 VerticalOffset()
void  SetVerticalOffset(float dip);   // 立即跳转
float MaxVerticalOffset() const;      // 内容高度 - 视口高度，>= 0

float HorizontalOffset() const;
void  SetHorizontalOffset(float dip);
float MaxHorizontalOffset() const;    // 全部能放下时为 0

bool ScrollBarHovered() const;        // 指针在任一滚动条的悬停条带上（给测试用）
```

**偏移直接写进子元素的 bounds**（`ArrangeOverride` 里 `y = content.y - offset`），所以子元素的
bounds 就是它在屏幕上的位置。合成器支撑的子孙按 bounds 定位自己的视觉对象，D2D 变换移不动
它们。代价是偏移每次变化都要重新 Arrange，而不只是重绘。

---

## 代码示例

```cpp
#include "FluentUI/layout/ScrollPanel.h"

auto scroll = std::make_unique<ScrollPanel>();
scroll->SetSpacing(12.0f);
scroll->SetPadding(8.0f);
scroll->Emplace<TextBlock>()->SetText(L"常规设置");
scroll->Emplace<CheckBox>()->SetText(L"开机启动");
// ...更多子元素
SetRoot(std::move(scroll));     // 在 Window 子类的 OnInitialize 里
```

---

## 备注

### 修过的 bug：指针一动不再整页重绘

滚动条的唤醒 / 空闲倒计时 / 淡出是一个约 1.5 秒的 tick 周期，指针在面板上任何移动都会
启动它。以前这个周期里每一帧都把**整块面板**报为脏区，在一个满是控件的页面上，鼠标一动
就是 1.5 秒的整页重绘。现在 tick 按它实际改变的东西报告：

| 这一帧的 tick | 报告的脏区 |
| --- | --- |
| 移动了滚动偏移 | 整块面板（所有子元素都动了） |
| 只推进了淡入淡出 / 展开 | 只有右侧和底部 16 DIP 的滚动条条带 |
| 内容已不再溢出（窗口变大、Expander 收起、子元素被移除） | 立即结束淡出周期（`ScrollViewer::HideBarsNow`），整块面板重绘一次以擦掉残留的滚动条 |

另外两处：

- **内容不溢出时，指针移动不再唤醒滚动条**：根本没有滚动条可画，以前却照样跑完整个 tick 周期。
- **悬停状态无条件刷新**：内容停止溢出时如果指针正停在滚动条上，旧的悬停状态以前会一直保留到
  指针离开面板，下次溢出时滚动条直接以展开状态出现、而且不会自动隐藏。

证明（headless 单元测试，修复经过变异检验）：

| 行为 | 测试 |
| --- | --- |
| 淡出帧只报滚动条条带 | `ScrollPanel.FadeTickDirtiesOnlyTheRail` |
| 滚动帧报整块面板 | `ScrollPanel.ScrollTickDirtiesWholePanel` |
| 不溢出时指针移动不唤醒 | `ScrollPanel.PointerMoveWithoutOverflowDoesNotWake` |
| 内容不再溢出时 tick 停止 | `ScrollPanel.BarAwakeWhenContentStopsOverflowingStopsTicking` |
| 溢出结束后指针移动清掉悬停 | `ScrollPanel.PointerMoveAfterOverflowEndsClearsRailHover` |

未验证：需真机。在 demo 任一长页面上打开 F12 HUD，鼠标在页面上移动但不滚动，看脏区只覆盖
右侧滚动条那一条，CPU Max 不再随鼠标移动升高；再把窗口拉大到内容不溢出，确认滚动条残影被擦掉。

### 性能特征

- **测量、排列、绘制都是 O(子元素数)**，不虚拟化；绘制按视口裁剪，裁剪范围通过
  `DrawingContext::WithClipHint` 传给子孙，所以嵌套的 `StackPanel` 只画视口内的行。
- **合成器子孙也被裁剪**：`GetViewportClipForDescendants` 声明视口，任意深度的合成视觉对象
  按它裁剪（D2D 裁剪够不着合成视觉对象）。
- **滚轮在不能滚时冒泡**：竖向不溢出时普通滚轮不处理，横向不溢出时 Shift+滚轮不处理。
- **idle 零成本**：滚动与淡出结束后 `WantsAnimationTick()` 为 false。
- **只重绘滚动条的帧在剪枝后照样能被收集**：`InvalidateRails` 不设置 `dirty_`，只设
  `railsDirty_` 并调用 `InvalidateDirty(DirtyFlags::None)`。脏区遍历现在会跳过子树脏标记为空的
  子树，所以 None 失效也会在自己和所有祖先的标记里记一个 `Render`，否则嵌在干净父容器里的
  ScrollPanel 淡出时根本不会被访问，滚动条条带也就不会重绘。由
  `SubtreeDirtyPruning.ScrollPanelRailRepaintUnderCleanParentIsCollected` 锁定（把 None 失效
  改成不记 Render，这个测试就失败）。

### 使用注意事项

- **横向溢出只看「比约束更宽」的子元素**：被测量拉伸到视口宽度的子元素不会触发横向滚动条，
  只有自然宽度确实更宽的才会。
- **命中测试**：滚动条覆盖在内容之上，优先命中；视口外被裁掉的子元素部分不响应点击；
  空白处的滚轮仍然落到面板本身。
- **不要放大量同构行**：那是 `ListBox` / `TreeView` / `DataGrid` 的场景。

### 键盘导航

| 按键 | 行为 |
| --- | --- |
| ↑ / ↓ | 平滑滚动 16 DIP |
| PageUp / PageDown | 平滑滚动一页（视口高度减一行） |
| ← / → | 横向溢出时滚动 16 DIP |
| Home / End | 跳到顶 / 底 |
| 滚轮 / Shift+滚轮 | 竖向（平滑）/ 横向（立即）滚动，步长 = 系统滚动行数 × `WheelStep()` |

### 主题 Token

ScrollPanel 自己不画背景；滚动条由内嵌的 `ScrollViewer` 绘制，token 见 [ScrollViewer.md](ScrollViewer.md)。

---

## UI Automation

ScrollPanel 的 peer：

| 项 | 值 |
|---|---|
| ControlType | Pane（以前作为可获得焦点的元素被报告成无名的 Custom） |
| 模式 | Scroll：百分比 0..100，不可滚动的轴为 -1；`SetScrollPercent` 超出 0..100 返回 `E_INVALIDARG` |
| AutomationId | `SetAutomationId` |
| HelpText | `Tooltip()` |

惰性创建：只有 UIA 客户端访问到这个控件时才创建 peer，没有客户端时零开销。模式操作经投递消息执行，执行前重新检查控件仍在树上且已启用。属性变化只在真正变化且有客户端监听时发送。自定义方式见 inheritance-guide.md 的“UI Automation”一节。

验证：无头测试 `tests/unit/AutomationControlPeerTests.cpp` / `AutomationSelectorPeerTests.cpp`，真实 UIA 客户端自测 `FluentUITests.exe --uia-smoke`；第 2 阶段 14 处变异全部被测试抓到。

## 相关控件

- [ScrollViewer](ScrollViewer.md) — 内嵌的滚动条模型（偏移、淡出、拖动）
- [StackPanel](StackPanel.md) — 不滚动的竖向堆叠
- [TabControl](TabControl.md) — 页内容需要滚动时放一个 ScrollPanel
