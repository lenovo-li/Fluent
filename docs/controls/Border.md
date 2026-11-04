# Border 布局容器

## 概述

单子元素容器:背景、边框、圆角、内边距。UI 里「卡片」这个视觉概念的实现基础 ——
demo 的每一张示例卡片都是一个 Border。

**命名空间**: `fluent`
**头文件**: `FluentUI/layout/Border.h`

---

## 继承层次

```
Visual
  └─ UIElement
      └─ FrameworkElement
          └─ Border
```

注意它**不是 `Panel`** —— 它只持有一个子元素,所以没有 `Add`,而是 `SetChild`。这也意味着
树遍历工具(如自我调试页的 Inspector)通过 `Panel::ChildAt` 走不到它的子元素:
单子元素容器需要单独处理。

---

## 公开 API

### 子元素

```cpp
template <class T> T* SetChild(std::unique_ptr<T> child);
```

返回裸指针便于继续配置。再次调用会替换(并销毁)原来的子元素。

### 外观

| 成员 | 说明 | 脏标记 |
| --- | --- | --- |
| `SetBackground(const D2D1_COLOR_F&)` | 填充色(支持 alpha) | Render |
| `SetBorderColor(const D2D1_COLOR_F&)` | 边框色 | Render |
| `SetBorderThickness(float)` | 边框粗细,0 = 不描边 | **Measure** |
| `SetCornerRadius(float)` | 圆角半径 | Render |
| `SetPadding(const Thickness&)` | 内边距 | **Measure** |

#### 为什么 BorderThickness 和 Padding 是 Measure 级

两者都进入 `ContentInset()` —— 它们**挤压子元素可用的空间**,所以改变它们会改变子元素需要
测量的宽度,进而可能改变整体高度(换行文字尤其明显)。

而 `CornerRadius` 只影响描边路径的形状,不改变任何尺寸,所以是 Render 级。

---

## 代码示例

### 一张卡片

```cpp
#include "FluentUI/layout/Border.h"

auto* card = parent->Add(std::make_unique<Border>());
card->SetPadding(Thickness{16.0f});
card->SetBorderThickness(1.0f);
card->SetCornerRadius(8.0f);

auto* content = card->SetChild(std::make_unique<StackPanel>());
content->SetSpacing(8.0f);
```

### 带透明度的语义底色

```cpp
// alpha 支持是有意的：语义色用淡淡的一层比实心块更可读
card->SetBackground(D2D1::ColorF(0x0078D4, 0.14f));
card->SetBorderColor(D2D1::ColorF(0x0078D4, 1.0f));
card->SetBorderThickness(1.5f);
```

---

## 备注

### 子元素的 Margin：Border 尊重，Viewbox / Expander 忽略

`Border` 与 `GroupBox` **尊重**子元素的 `Margin` —— 两者的 Arrange 都走
`FrameworkElement::ComputeArrangeRect`，
该函数第一步就是 `slot - child->Margin()`。

```cpp
auto* child = border->SetChild(std::make_unique<TextBlock>());
child->SetMargin(Thickness{20.0f});   // ✓ Border 会照做
border->SetPadding(Thickness{20.0f}); // ✓ 也可以用容器自己的 Padding
// 两者会叠加：内容距 Border 边界 40 DIP
```

真正忽略子元素 Margin 的只有 `Viewbox`
与 `Expander`。给这两个
容器的子元素设 Margin 不会有任何效果，要留白就用容器的 `Padding`。

### 性能特征

- **Render**:一个圆角矩形填充 + 一个圆角矩形描边。
- 未调用 `SetBackground` / `SetBorderColor` 时**什么都不画**（完全透明）。
  `hasBg_` / `hasBorder_` 默认为 false，Render 直接跳过填充与描边；
  `borderThickness_` 与 `cornerRadius_` 默认也都是 0。
- `SetBorderThickness(0)` 会**跳过**描边绘制,不是画一条 0 宽的线。
- **脏区遍历按子树脏标记剪枝**:内容没有被失效过时,`CollectDirtyBounds` /
  `ClearDirtySubtree` 在 Border 这一层就返回,不往下走。Border、GroupBox、Viewbox、Expander
  这几个单子元素容器的写法相同:重写 protected 的 `...Override` 钩子,通过子元素的公开入口
  递归。Viewbox 对子元素脏矩形的缩放映射不受影响,由
  `SubtreeDirtyPruning.ViewboxScalingStillAppliesToPrunedCollection` 和
  `SubtreeDirtyPruning.ContentContainersForwardThroughTheMark` 锁定。

### 使用注意事项

- **不要给包含换行文字的 Border 写死高度。** 子元素的高度取决于
  `可用宽度 - Padding - BorderThickness`,写死外层高度会让内容被静默裁剪
  (框架的 Arrange 契约:子元素永不超出容器)。
- **描边画在边界内侧**,所以圆角卡片的内容区实际是 `bounds - padding - thickness`。
- 想要多个子元素请套一个 `StackPanel` 或 `Grid` 进去。

### 主题 Token

**Border 不读任何主题 token。** `Border.cpp` 中一次 `Theme()` 调用都没有——颜色、
粗细、圆角完全由调用方通过 `SetBackground` / `SetBorderColor` /
`SetBorderThickness` / `SetCornerRadius` 提供，未提供即透明/为 0。

要让 Border 跟随主题，调用方自己从 `Theme()` 取值传进来：

```cpp
const auto& th = Theme();
border->SetBackground(th.colors.cardFill);
border->SetBorderColor(th.colors.controlStrokeDefault);
border->SetCornerRadius(th.spacing.cornerRadiusNormal);
```

这是刻意的：Border 是纯粹的绘制原语，主题决策留给使用它的控件。

### 演示页

demo 导航 → 布局容器 → Border(nav 505)。原「样式定制大全」里的 Border 完整演示已并入
此页,含五档边框粗细、五档圆角、语义色卡、三层嵌套。
