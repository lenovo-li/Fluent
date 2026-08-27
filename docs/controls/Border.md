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

### ⚠️ 单子元素容器忽略子元素的 Margin

`Border`、`GroupBox`、`Viewbox`、`Expander` **一致忽略**其子元素的 `Margin` —— 这是本框架
的约定,不是 bug。

```cpp
auto* child = border->SetChild(std::make_unique<TextBlock>());
child->SetMargin(Thickness{20.0f});   // 无效！Border 不看它
border->SetPadding(Thickness{20.0f}); // ✓ 用容器自己的 Padding
```

理由:两者会叠加成难以推理的间距,而单子元素容器已经有 `Padding` 表达同一件事。

### 性能特征

- **Render**:一个圆角矩形填充 + 一个圆角矩形描边。
- 未调用 `SetBackground` / `SetBorderColor` 时走主题默认值,没有额外开销。
- `SetBorderThickness(0)` 会**跳过**描边绘制,不是画一条 0 宽的线。

### 使用注意事项

- **不要给包含换行文字的 Border 写死高度。** 子元素的高度取决于
  `可用宽度 - Padding - BorderThickness`,写死外层高度会让内容被静默裁剪
  (框架的 Arrange 契约:子元素永不超出容器)。
- **描边画在边界内侧**,所以圆角卡片的内容区实际是 `bounds - padding - thickness`。
- 想要多个子元素请套一个 `StackPanel` 或 `Grid` 进去。

### 主题 Token

| Token | 用途 |
| --- | --- |
| `cardFill` | 默认背景(未设 Background 时) |
| `controlStrokeDefault` | 默认边框色 |
| `spacing.borderWidth` | 默认边框粗细 |
| `spacing.cornerRadiusNormal` | 常用圆角 |

### 演示页

demo 导航 → 布局容器 → Border(nav 505)。原「样式定制大全」里的 Border 完整演示已并入
此页,含五档边框粗细、五档圆角、语义色卡、三层嵌套。
