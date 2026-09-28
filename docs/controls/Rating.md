# Rating 控件

## 概述

星级评分:一排星形字形,点击或用键盘设定评分,悬停时预览。

**典型使用场景**:
- 评价打分
- 优先级/重要程度的直观表达
- 只读展示已有评分(`SetReadOnly(true)`)

---

## 继承层次

```
Visual
  └─ UIElement
      └─ FrameworkElement
          └─ Control
              └─ Rating
```

**命名空间**: `fluent`
**头文件**: `FluentUI/controls/Rating.h`

---

## 公开 API

### 属性

| 成员 | 说明 | 脏标记 |
| --- | --- | --- |
| `SetValue(float)` / `Value()` | 当前评分,夹到 `[0, MaxValue]` | Render |
| `SetMaxValue(int)` / `MaxValue()` | 星星数量(即最大值),夹到 ≥1 | **Measure** |
| `SetIconSize(float)` / `IconSize()` | 字形边长(DIP),夹到 **[12, 48]** | **Measure** |
| `SetReadOnly(bool)` / `IsReadOnly()` | 只读展示 | — |

`MaxValue` 与 `IconSize` 都是 Measure 级,因为**控件宽度是它们的函数**(见下)。

⚠️ **没有 `SetFontSize`。** 星星不是文本,字形尺寸由 `SetIconSize` 控制。给 Rating 设
`SetFontSize` 不会有任何效果 —— `Render` 只读 `EffectiveAccentColor` 与
`EffectiveFontWeight`。同理 `CornerRadius` / `BorderThickness` / `Background` 在这个控件上
没有可作用的几何,设了也不会有变化。

#### SetReadOnly 的连带效果

只读会**同时放弃可聚焦性**并清除悬停预览。所以只读的 Rating 被 Tab 跳过、也永不预览 ——
一个不能改的控件出现在焦点顺序里只是噪音。

### 事件

```cpp
Event<Rating, float>& ValueChanged();
```

⚠️ **只有用户操作触发**(点击或按键),`SetValue` 不触发 —— 与 `ToggleButton::SetChecked`
同一条契约:事件报告用户意图,不是每一次赋值。

```cpp
sub_ = rating->ValueChanged().Subscribe(this,
    [](void* owner, Rating&, float& v) {          // 注意 float&
        static_cast<MyPage*>(owner)->SubmitScore(v);
    });
```

⚠️ 返回的 `Subscription` 必须持有(RAII)。

---

## 代码示例

### 可交互

```cpp
#include "FluentUI/controls/Rating.h"

auto* rating = panel->Add(std::make_unique<Rating>());
rating->SetMaxValue(5);
rating->SetValue(4.0f);
rating->SetHAlign(HAlign::Left);      // 默认 Stretch 只会加空白，见下
```

### 只读展示

```cpp
auto* shown = panel->Add(std::make_unique<Rating>());
shown->SetValue(3.0f);
shown->SetReadOnly(true);             // 同时不再可聚焦、不再预览
```

### 更大的字形

```cpp
rating->SetIconSize(32.0f);           // 夹到 [12, 48]；不是 SetFontSize
```

---

## 备注

### ⚠️ 它的宽度完全不看可用空间

`Measure` **完全忽略 `availW`** —— 宽度是 `MaxValue × IconSize + 间隙`,一个纯粹由内容决定的值。
两个后果:

1. **`HAlign::Stretch`(默认)只会在右侧加空白,永远不会把星星撑开分布。** 想让星星靠左紧凑
   请设 `SetHAlign(HAlign::Left)`。
2. **`Width` 设得比自然宽度小会溢出,而不是被裁。** `Render` 不做裁剪 —— 这是本控件与框架
   一般契约(压缩)的一个例外,因为它压根没参与协商。demo 里有一个 `Width=60` 的刻意对照。

正确的「让它变小」的做法是 `SetIconSize`,不是 `SetWidth`。

### 性能特征

- **Render**:`MaxValue` 个星形字形,几何绘制。
- **不请求动画帧**;悬停预览只是重绘。
- `Gap()` 是方法而不是常量,因为它读主题 token —— 而 token 只在 attach 后有效。

### 使用注意事项

- **提交用的是 click 手势而不是 pointer-pressed**:按下后拖出去再松开**不会**提交,和这里
  所有其他按钮行为一致。
- **焦点环画在 `bounds_` 之外**,`VisualOverflowDip()` **无条件**上报外扩量(包括未聚焦时)——
  这样焦点**离开**的那一帧也会清掉环占据的像素。
- 冗余点击(值未变)不会安排新帧。
- **小数值**:`Value` 是 float,可以存 3.5,但绘制按整颗星 —— 没有半星字形。

### 键盘导航

| 按键 | 行为 |
| --- | --- |
| Tab | 进入焦点顺序(只读时跳过) |
| ← / → | ±1 |
| Home / End | 跳到最小 / 最大 |

### 主题 Token

| Token | 用途 |
| --- | --- |
| `accent` | 已点亮的星(未设 AccentColor 时) |
| `accentHover` | 悬停预览时的星星（`.cpp:76`） |
| `controlStrokeDefault` | 未点亮的星(空心) |
| `focusStroke` | 焦点环 |
| `textSecondary` | 禁用态星星（`.cpp:78`） |
| `spacing.spacingXSmall` | 字形间隙（`.cpp:51`） |

### 演示页

demo 导航 → 输入控件 → Rating。含状态与只读、忽略 availW 导致的 Stretch 无效对照、
`Width` 过小溢出与 `IconSize` 正确缩小的对照、五种 AccentColor。
