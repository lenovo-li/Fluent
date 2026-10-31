# Metric 控件

## 概述

一个带标签的数字，外加可选的带方向的变化量 —— 仪表盘里的 KPI 卡片。三行：小号
说明、大号数值、按方向着色的变化行。

**核心设计点：变化量的「符号」和「颜色」是两个独立的问题。**

- 符号来自数值（或调用者给的字符串）
- 颜色来自另一个问题：**这个量上涨是好消息吗？**

股价上涨通常显示为收益。但「错误率」「最大回撤」「资产负债率」上涨是坏消息，不能
用收益色画。没有任何对数值的检查能判断这属于哪一类 —— 那是量本身的属性，只能由
调用者声明，这就是 `SetInverted()` 存在的原因。

**典型使用场景**:
- 行情概览条（最新价、市盈率、市净率、市值）
- 回测统计（累计收益、最大回撤、夏普比率、胜率）
- 运维仪表盘（QPS、错误率、P99 延迟）

---

## 继承层次

```
Visual
  └─ UIElement
      └─ FrameworkElement
          └─ Control
              └─ Metric
```

**命名空间**: `fluent`
**头文件**: `FluentUI/controls/Metric.h`

---

## 公开 API

### 构造函数

```cpp
Metric();
```

默认不可获得焦点（只读展示，没有可激活的东西），`HAlign::Stretch`。

Stretch 是刻意的：Metric 是仪表盘里的一块砖，要填满自己那一列，一排才能对齐。
没有它，砖块各自缩到文字宽度，整条看起来是碎的。

---

### 属性

#### Label

```cpp
void SetLabel(std::wstring text);
const std::wstring& Label() const;
```

数值上方的小号说明，用 `captionSize` 与 `textSecondary`。

**脏标记等级：Measure**。

#### Value

```cpp
void SetValue(std::wstring text);
const std::wstring& Value() const;
```

主数值，用 `subtitleSize` + SemiBold + `textPrimary`。

**接收字符串而不是 double，这是刻意的。** 格式化（货币符号、千分位、百分号、有效
位数、locale）是应用已经必须在别处统一决定的事情。一个自己做格式化的控件会和应用
自己的表格产生分歧。

**脏标记等级：Measure**。

#### Delta / Trend

```cpp
enum class Trend { None, Up, Down, Flat };
void SetDelta(std::wstring text, Trend trend);
const std::wstring& Delta() const;
Trend DeltaTrend() const;
```

变化行的文字与方向，**一次设置**。两者是同一个事实，分开设置会允许出现「文字写着
+1.2% 而 trend 还是上一次的 Down」这样的中间帧。

`Trend::None` 隐藏整行并**收回它的纵向空间**（不是留一行空白）。

**脏标记等级视情况而定**，这是刻意的优化：

| 变化 | 等级 | 理由 |
| --- | --- | --- |
| 只改 trend（文字不变，两端都非 None） | **Render** | 同一个盒子，只换颜色 |
| 改文字 | **Measure** | 宽高可能变 |
| None ↔ 非 None | **Measure** | 整行出现或消失 |

第一条对每秒刷新的仪表盘是必要的：升级成 Measure 会让整个页面每次数据 tick 都
重排一次。

#### Inverted

```cpp
void SetInverted(bool inverted);
bool IsInverted() const;
```

为 true 时，`Up` 用**负面**色、`Down` 用**正面**色。用于「涨是坏事」的量：
错误率、回撤、负债率、延迟。

`Trend::Flat` 不受影响 —— 它没有方向。

**脏标记等级：Render**（只决定方向映射到哪个颜色）。

---

### 方法

#### TrendColor

```cpp
D2D1_COLOR_F TrendColor(Trend t) const;
```

在当前主题与当前 `Inverted` 下，某个方向解析出的颜色。

公开的理由：这是本控件**全部的语义契约**。一个自己重算这套映射的测试只是在断言
它自己的那份逻辑副本，而不是控件的行为。

---

## 代码示例

### 最小示例

```cpp
#include "FluentUI/controls/Metric.h"

auto* m = panel->Add(std::make_unique<Metric>());
m->SetLabel(L"最新价");
m->SetValue(L"40.31");
m->SetDelta(L"+0.61%", Metric::Trend::Up);
```

### KPI 条（st.columns 的形状）

```cpp
// Grid + Star 列均分宽度；Auto 行让高度由内容决定
auto* strip = card->Add(std::make_unique<Grid>());
strip->AddRow(GridLength::Auto());
for (int i = 0; i < 4; ++i) strip->AddColumn(GridLength::Star(1.0f));

auto* debt = strip->Add(std::make_unique<Metric>());
strip->SetCell(debt, 0, 3);
debt->SetLabel(L"资产负债率");
debt->SetValue(L"41.8%");
debt->SetDelta(L"+1.9%", Metric::Trend::Up);
debt->SetInverted(true);            // 负债率上涨是坏消息
```

---

## 备注

### 性能特征

- **Measure 成本**：最多 3 个 DirectWrite 布局（label / value / delta），经共享的
  `ResourceCache` 取得，内容不变时是哈希查找。
- **Render 成本**：最多 3 次 DrawText。无填充、无描边、无动画。推进 y 的行高与 Measure
  共用同一个缓存项（三行都不换行，所以两边用同一个布局宽度作键）。此前 Render 每次重绘
  都为每行新建一个 `IDWriteTextLayout`。由 `LabelMeasureCache.MetricMeasureAndRenderReuseLayouts`
  锁定。
- **不请求动画帧**。

### 使用注意事项

- ⚠️ **不要给容纳它的行写死高度。** 三行 Metric 需要约 **62.6 DIP**（Segoe UI
  14pt 默认字号阶梯下）。曾经写死 `SetHeight(58)` 导致第三行文字被切掉 4.6 DIP ——
  这个框架的 Arrange 契约是「子元素永不超出容器」，不够的部分静默裁剪，没有警告。
  用 `GridLength::Auto()` 让布局自己算。
- **无界测量返回内容宽度，有界测量填满 offer。** 容器（如 `UniformGrid`）会用
  无穷宽度测量子元素来问「你需要多少」，此时直接返回 offer 会上报 `inf`，污染上游
  所有算术。
- **`UniformGrid` 不适合 KPI 条。** 它把每个 cell 的尺寸定为最大子元素的 desired
  尺寸且从不撑开填满 arrange 宽度 —— 四个 Metric 放进 800 DIP 每个只得约 63 DIP。
  要均分可用宽度用 `Grid` + Star 轨道。

### 键盘导航

不可聚焦，不参与 Tab 顺序。

### 主题 Token

| Token | 用途 |
| --- | --- |
| `dataPositive` | Up（未反转）/ Down（已反转） |
| `dataNegative` | Down（未反转）/ Up（已反转） |
| `dataNeutral` | Flat |
| `textPrimary` | 主数值 |
| `textSecondary` | 标签 |
| `typography.captionSize` | 标签与变化行字号 |
| `typography.subtitleSize` | 主数值字号 |

**涨跌配色是地区规范，不是审美偏好。** 中日韩红涨绿跌，欧美相反。默认值取 CJK
习惯（本代码库首个消费者的需求），应用通过 `ThemeInputs::colors.dataPositive /
dataNegative` 覆盖即可，**不需要改控件也不需要维护调色板分支**。

### 尚未实现

- 迷你走势图（sparkline）：需要在卡片里嵌一个 `Chart`，目前请自己组合。
- 数值动画（滚动到新值）：会是 Measure 级动画，成本高，且对每秒刷新的数据会持续
  占用帧。

### 演示页

demo 导航 → 数据控件 → Metric（nav id 407）。含四种 Trend、
`SetInverted` 前后对照、拖滑块实时更新、以及与 ProgressBar/Separator 组合的
仪表盘条。
