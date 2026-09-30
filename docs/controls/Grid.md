# Grid 布局容器

## 概述

行列网格。每一行/列是一条 `GridLength` 轨道,每个子元素被指派到一个单元格,可以跨行跨列。
这是本框架**最重要的布局容器** —— 表单、仪表盘、工具栏、任何需要「对齐成列」的界面都用它。

三种轨道类型对应三种意图:

| 轨道 | 含义 | 用来做什么 |
| --- | --- | --- |
| `GridLength::Auto()` | 按内容需要的尺寸 | 标签列、按钮列 —— 内容多宽就多宽 |
| `GridLength::Pixels(n)` | 固定 n DIP | 需要跨行严格对齐的列(如数值列) |
| `GridLength::Star(w)` | 按权重分配剩余空间 | 主内容区 —— 吃掉剩下的所有宽度 |

**命名空间**: `fluent`
**头文件**: `FluentUI/layout/Grid.h`

---

## 继承层次

```
Visual
  └─ UIElement
      └─ FrameworkElement
          └─ Panel
              └─ Grid
```

---

## 公开 API

### 定义轨道

```cpp
void AddColumn(GridLength w);
void AddRow(GridLength h);
void SetColumns(std::vector<GridLength> cols);   // 一次性替换
void SetRows(std::vector<GridLength> rows);
const GridLength& ColumnAt(size_t i) const;
void SetColumnAt(size_t i, GridLength w);        // 运行时改单条轨道
```

### 放置子元素

```cpp
void SetCell(FrameworkElement* child, int row, int col,
             int rowSpan = 1, int colSpan = 1);
```

⚠️ **`SetCell` 是 Grid 的成员函数,先 `Add` 再 `SetCell`**:

```cpp
auto* label = grid->Add(std::make_unique<TextBlock>());   // Add 返回裸指针
grid->SetCell(label, 0, 0);                               // 再指派单元格
```

单元格位置存在 **Grid 里**(一张以子元素指针为键的映射),而不是挂在
`FrameworkElement` 基类上 —— 这样 grid 专有的状态不会泄漏到所有元素上。

### 间距

```cpp
void SetColumnSpacing(float dip);
void SetRowSpacing(float dip);
```

轨道之间的间隙。比给每个子元素设 Margin 更可靠 —— Margin 会在相邻元素间叠加。

---

## 代码示例

### 表单:标签 + 输入 + 按钮

```cpp
auto* form = card->Add(std::make_unique<Grid>());
form->AddRow(GridLength::Auto());              // Auto 行：高度由内容决定
form->AddColumn(GridLength::Pixels(96.0f));    // 标签列固定宽，多行才能对齐
form->AddColumn(GridLength::Star(1.0f));       // 输入框吃掉剩余宽度
form->AddColumn(GridLength::Auto());           // 按钮按自身内容收缩

auto* label = form->Add(std::make_unique<TextBlock>());
form->SetCell(label, 0, 0);
label->SetVAlign(VAlign::Center);

auto* input = form->Add(std::make_unique<TextBox>());
form->SetCell(input, 0, 1);

auto* submit = form->Add(std::make_unique<Button>());
form->SetCell(submit, 0, 2);
```

### 等分的 KPI 条

```cpp
// 四个等宽 Star 列。这是 st.columns 的正确对应物 —— 不是 UniformGrid，见下面的陷阱。
auto* strip = card->Add(std::make_unique<Grid>());
strip->AddRow(GridLength::Auto());
for (int i = 0; i < 4; ++i) strip->AddColumn(GridLength::Star(1.0f));
```

### 不等权重

```cpp
row->AddColumn(GridLength::Star(3.0f));   // 图表占 3/4
row->AddColumn(GridLength::Star(1.0f));   // 侧栏占 1/4
```

---

## 备注

### 常见陷阱

**1. ⚠️ 不要给包含文字的行写死高度。**

这是本项目真实发生过的 bug:KPI 条写了 `SetHeight(58)`,而三行的 `Metric` 需要
**62.6 DIP**,于是第三行文字被切掉 4.6 DIP。

本框架的 Arrange 契约是「子元素永不超出容器」—— 不够的部分**静默裁剪**,不溢出、不警告。
所以写死高度等于自己承诺「我算得比布局准」,而字体、DPI、主题字号阶梯任一变化就不准了。

```cpp
grid->AddRow(GridLength::Auto());   // ✓ 高度由内容决定
grid->SetHeight(58.0f);             // ✗ 猜一个字体的行高
```

**2. ⚠️ `UniformGrid` 不是「等分列」。**

`UniformGrid` 把每个 cell 的尺寸定为**最大子元素的 desired 尺寸**,并且**从不撑开 cell
去填满 arrange 宽度**。四个 `Metric` 放进 800 DIP 的 `UniformGrid`,每个只得到约
**63 DIP**,右边 550 DIP 空着。

这对它声明的用途(日历那种等大、内容定尺寸的格子)是合理的,但**不是**「均分可用宽度」。
均分请用 Grid + Star 轨道。

**3. 只有 `Viewbox` 和 `Expander` 忽略子元素 Margin。**

`Viewbox`（`Viewbox.cpp:91` 直接 Arrange natural size）与 `Expander`
（`Expander.cpp:166` 直接 `content_->Arrange(contentRect)`）确实忽略子元素 Margin。

但 `Border` 与 `GroupBox` 其实**尊重** Margin —— 两者都走 `FrameworkElement::ComputeArrangeRect`
（`Border.cpp:45`、`GroupBox.cpp:122`），该函数第一步就是 `slot - child->Margin()`。
给 Border 的子元素设 Margin 是有效的，它会与 Border 自己的 Padding 叠加。
在 `Viewbox` / `Expander` 里要留白，请用容器自己的 `Padding`。

### 性能特征

- **轨道解算 O(轨道数)**,与子元素数量无关;`ResolveTracks` 是纯函数(private static,测试通过公开的 `ResolvedColumnSizes()` / `ResolvedRowSizes()` 间接覆盖)。
- **不可见子元素被跳过**:`Panel` 在 Measure 与 Arrange 两处都查 `IsVisible()`,所以
  `SetVisible(false)` 的元素既不参与测量也不影响轨道尺寸,常驻成本只是内存。
- `Auto` 轨道需要测量该轨道上的所有子元素;`Pixels` 不需要。轨道很多且都是 Auto 时,
  改成 Pixels 能省下测量。

### 演示页

demo 导航 → 布局容器 → Grid。含三种轨道类型的并列对照、跨行跨列、嵌套组合出的
「工具栏 + 侧栏 + 内容」外壳,以及上面三条陷阱的对错对照。
