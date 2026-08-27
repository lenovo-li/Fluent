# DataGrid 控件

## 概述

只读的虚拟化文本表格。数据应用的核心控件：数千行、数十列的行情快照或财务报表。

**虚拟化是它存在的全部理由，而且是双向的：**

- **行**：只绘制与视口相交的行
- **列**：只绘制与横向滚动窗口相交的列

因此 5000×80 的表每帧成本和 20×8 的表相同。列虚拟化在这里和行虚拟化一样重要 ——
财务宽表有 80+ 期列，全部绘制会吃掉整帧，而在小测试表上完全看不出问题。

**拉取式，不持有数据**：调用者提供 `(row, col) -> wstring` 访问器，所以百万行表
「加载」成本为零，应用保留自己的存储、格式化和排序。

**典型使用场景**:
- 全市场行情快照（几千行）
- 财务报表宽表（几十期列）
- 筛选/查询结果
- 日志或事件列表

---

## 继承层次

```
Visual
  └─ UIElement
      └─ FrameworkElement
          └─ Control
              └─ DataGrid
```

**命名空间**: `fluent`
**头文件**: `FluentUI/controls/DataGrid.h`

---

## 公开 API

### 构造函数

```cpp
DataGrid();
```

默认可聚焦（键盘行导航）、`HAlign/VAlign::Stretch`、行高 28 DIP、显示网格线、
不显示斑马纹、无选中行。

### 嵌套类型

```cpp
enum class Align { Left, Right, Center };

struct Column {
    std::wstring header;
    float width = 96.0f;      // DIP，固定宽度
    Align align = Align::Left;
};

using CellProvider     = std::function<std::wstring(int row, int col)>;
using RowColorProvider = std::function<bool(int row, D2D1_COLOR_F& out)>;
```

`Align` 存在的原因：数字列靠比较位数来读，位数必须对齐，所以数值列**必须**右对齐。
这不是全局设置能解决的。

---

### 属性

| 成员 | 说明 | 脏标记 |
| --- | --- | --- |
| `SetColumns(std::vector<Column>)` | 列定义 | Measure |
| `SetRowCount(int)` | 行**数**（不是行数据） | Measure |
| `SetCellProvider(CellProvider)` | 单元格文本访问器 | Render |
| `SetRowColorProvider(RowColorProvider)` | 整行着色（可选） | Render |
| `SetRowHeight(float)` | 行高，最小 16 DIP | Measure |
| `SetAlternatingRowFill(bool)` | 斑马纹，默认关 | Render |
| `SetShowGridLines(bool)` | 网格线，默认开 | Render |
| `SetSelectedRow(int)` | 选中行，-1 为无 | Render |

`SetRowCount` 缩小时会把越界的选中行拉回范围内 —— 否则下一帧会向访问器请求一个
已不存在的行。

斑马纹默认关闭：它帮助宽表、干扰窄表，所以由调用者决定。

---

### 虚拟化契约（公开，因为这就是控件的存在理由）

```cpp
float TotalContentHeight() const;              // rowCount * rowHeight
float TotalContentWidth() const;               // 所有列宽之和
void  VisibleRowRange(int& first, int& last) const;
void  VisibleColumnRange(int& first, int& last) const;
RectDip HeaderRect() const;                    // 钉住的表头
RectDip ViewportRect() const;                  // 表体区域
RectDip CellRect(int row, int col) const;      // 计入两个滚动偏移
```

`VisibleRowRange` / `VisibleColumnRange` 在无内容时返回 `{0, -1}`（`last < first`），
这是调用者循环时应当依赖的「空」信号。

这几个必须公开，因为**虚拟化无法从像素验证**：一个绘制 10 万行并裁掉 99,980 行的
表格与正确实现产生完全相同的像素，只是会拖死 UI 线程。测试断言的是这些数字。

部分滚入视口的行**计为可见**（`first` 用 floor、`last` 用 ceil），否则滚动时顶边
会出现不到一行高的空隙。

### 滚动

```cpp
void  SetVerticalOffset(float dip);
float VerticalOffset() const;
void  SetHorizontalOffset(float dip);
float HorizontalOffset() const;
void  ScrollRowIntoView(int row);
```

`ScrollRowIntoView` 只在目标行确实不可见时移动视口 —— 已可见时移动会让方向键导航
每次按键都抖一下。

滚动是**内部的**（自己持有一个 `ScrollViewer` 成员）而不是委托给外层
`ScrollPanel`：表头必须钉住而表体滚动，外层滚动容器会把表头滚出视野。

---

### 事件

```cpp
Event<DataGrid, RoutedEventArgs>& SelectionChanged();
Event<DataGrid, RoutedEventArgs>& ColumnHeaderClicked();
int ColumnClickedIndex() const;      // 在 ColumnHeaderClicked 处理器里读
```

**控件不排序。** 表头点击只上报列号，排序由应用做（改自己的顺序后重新指向访问器）。
控件对顺序没有意见。

⚠️ `Subscription` 必须持有，它是 RAII 的。

---

## 代码示例

### 最小示例

```cpp
#include "FluentUI/controls/DataGrid.h"

auto* grid = panel->Add(std::make_unique<DataGrid>());
grid->SetHeight(320.0f);          // 视口高度必须由调用者定，见「使用注意事项」
grid->SetColumns({
    {L"代码",   90.0f, DataGrid::Align::Left},
    {L"最新价", 90.0f, DataGrid::Align::Right},
});
grid->SetRowCount(50000);
grid->SetCellProvider([](int row, int col) -> std::wstring {
    const auto& r = MyData()[row];
    return col == 0 ? r.code : Format(r.price);
});
```

### 涨跌着色

```cpp
// 主题指针而不是拷贝 ColorTokens：拷贝会把颜色冻结在构建时，切到深色主题后
// 行还是浅色主题的色
grid->SetRowColorProvider([win](int row, D2D1_COLOR_F& out) {
    const auto& c = win->Theme().colors;
    out = MyData()[row].changePct >= 0 ? c.dataPositive : c.dataNegative;
    return true;                  // 返回 false = 用默认 textPrimary
});
```

---

## 备注

### 性能特征

- **每帧成本 O(可见单元格)**，与总行列数无关。实测 41 列 × 60 行放进 820×220
  的区域 → 10 列 × 7 行 = **70 个单元格**是活的。
- **`SetRowCount` 不分配**：它只是一个整数。
- **单元格文本复用一个 scratch buffer**（`cellScratch_.assign(...)`），所以一屏
  单元格不会每帧每格分配一次字符串。
  ⚠️ 必须用 `assign()` 而不是 `operator=` —— 后者是移动赋值，会窃取临时对象的
  内存块并释放已增长的容量，导致每格重新分配。
- **平滑滚动**：`WantsAnimationTick()` 跟随内部 `ScrollViewer`，滚动停止后回到
  零成本空闲。

### 使用注意事项

- **必须写显式高度**（或放进定高容器）。这与 `Metric` 相反，值得说清区别：
  Metric 有自然高度（三行文字），写死会切字；DataGrid **没有**自然高度 —— 它的
  自然高度是全部行加起来（5 万行 × 28 = 140 万 DIP）。视口大小只能由调用者决定，
  这正是显式尺寸的正当用途。
- **列宽固定，没有自动适配。** 自动适配意味着为某列拉取并测量**每一行**的文字，
  是 O(rows) 的测量 —— 会毁掉本控件存在的理由，并在 10 万行表上每次 resize 卡死
  UI 线程。需要适配请自己抽样测量后设定列宽。
- 单元格访问器在渲染路径上，**每格每帧调用一次**。要廉价，尽量不分配。

### 键盘导航

| 按键 | 行为 |
| --- | --- |
| ↑ / ↓ | 上下移动选中行并滚入视野 |
| PageUp / PageDown | 翻页（视口行数 - 1） |
| Home / End | 首行 / 末行 |
| 滚轮 | 平滑滚动 3 行/档 |

### 主题 Token

| Token | 用途 |
| --- | --- |
| `accent` / `onAccent` | 选中行底色 / 其上文字 |
| `textPrimary` | 单元格文字 |
| `textSecondary` | 表头文字 |
| `controlFillDefault` | 表头底色 |
| `controlStrokeDefault` | 表头下边线 |
| `gridLine` | 网格线 |
| `rowFillAlternate` | 斑马纹 |
| `dataPositive` / `dataNegative` | 由调用者在 RowColorProvider 里取用 |

选中行的文字色**优先于**应用的行着色 —— 着色行叠在 accent 底上可能对比度不足。

### 尚未实现（刻意）

- **编辑**：需要一整套焦点/提交/校验模型，那是另一个控件。
- **排序 / 筛选**：只上报表头点击。
- **单元格内放控件**：会毁掉虚拟化。
- **单元格换行**：行高会变成数据相关，破坏使滚动保持 O(可见) 的常数时间行索引。
- **列宽拖动**：需要在表头做命中测试与拖拽状态。

### 演示页

demo 导航 → 数据控件 → DataGrid（nav id 404）。含 5 万行实况（屏幕上打印实时
可见范围）、跳转按钮、外观开关、以及与 Metric 组合的涨跌着色表。
