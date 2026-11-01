# DataGrid 控件

## 概述

虚拟化文本表格（可选的单元格编辑、多选、列宽拖动、行拖拽重排、冻结列）。数据应用的核心控件：数千行、数十列的行情快照或财务报表。

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
enum class SortDirection { None, Ascending, Descending };
enum class SelectionMode { Single, Multiple };

struct Column {
    std::wstring header;
    float width = 96.0f;      // DIP
    Align align = Align::Left;
    bool resizable = true;    // 允许拖动表头分隔线改宽（需 SetColumnResizeEnabled(true)，默认即开）
    bool frozen = false;      // 钉在左侧，不随横向滚动
};

using CellProvider     = std::function<std::wstring(int row, int col)>;
using RowColorProvider = std::function<bool(int row, D2D1_COLOR_F& out)>;
using CellValidator    = std::function<bool(int row, int col, const std::wstring& text)>;
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
| `SetSelectedRow(int)` | 选中行，-1 为无（Multiple 模式下操作第一个选中行） | Render |
| `SetSelectionMode(SelectionMode)` | Single（默认）/ Multiple（Ctrl/Shift 多选） | Render |
| `SetColumnResizeEnabled(bool)` | 表头分隔线拖动改列宽，默认开 | — |
| `SetCopyEnabled(bool)` | Ctrl+C 复制选中行为 TSV，默认开 | — |
| `SetRowReorderEnabled(bool)` | 拖拽选中行重排，默认关 | — |
| `SetEditingEnabled(bool)` | F2 / 双击进入单元格编辑，默认关 | — |
| `SetCellValidator(CellValidator)` | 提交前校验，返回 false 拒绝并保持编辑框 | — |
| `SetSortColumn(int, SortDirection)` / `ClearSort()` | 表头排序箭头（**只画指示，不排序数据**） | Render |

`RefreshCells()`：行数不变但数据变了时调用。`SetRowCount` 在行数不变时直接返回，
不会重绘，不调它会继续显示上次拉取的文字。

查询：`Columns()`、`RowCount()`、`RowHeight()`、`SortColumn()`、`GetSortDirection()`、
`GetSelectionMode()`、`SelectedRow()`、`SelectedRows()`、`IsRowSelected(int)`，
以及各开关对应的 `ColumnResizeEnabled()` / `CopyEnabled()` / `RowReorderEnabled()` /
`EditingEnabled()`。`ClearSelection()` 两种模式都可用。

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

### 列宽自适应

```cpp
float MeasureColumnContent(int col) const;  // 返回适配宽度，不修改列
void  AutosizeColumn(int col);              // 测量并应用
```

只测量**视口内可见的行**加表头（全表测量是 O(rows)，会毁掉虚拟化），下限 24 DIP。
所以结果取决于当前滚动位置，这是刻意的取舍。

### 单元格编辑

```cpp
void BeginEdit(int row, int col);   // 等同于在该格按 F2
void CancelEdit();
bool IsEditing() const;
int  EditingRow() const;
int  EditingColumn() const;
```

Enter 提交（先过 `CellValidator`，被拒则编辑框保持打开），Escape 取消；编辑中点击其他行、
滚轮滚动、拖动滚动条或调用 `SetVerticalOffset`、失去焦点都等于提交（否则行被滚走，编辑框还留在原处）。编辑中的 Enter/Escape 由 DataGrid 先处理，不会落到行导航或外层对话框。
F2 和双击目前都打开**第 0 列**（双击尚未按点击位置定位列）；要编辑其他列用
`BeginEdit(row, col)`。

`ScrollRowIntoView` 只在目标行确实不可见时移动视口 —— 已可见时移动会让方向键导航
每次按键都抖一下。

滚动是**内部的**（自己持有一个 `ScrollViewer` 成员）而不是委托给外层
`ScrollPanel`：表头必须钉住而表体滚动，外层滚动容器会把表头滚出视野。

---

### 事件

```cpp
Event<DataGrid, RoutedEventArgs>& SelectionChanged();
Event<DataGrid, RoutedEventArgs>& ColumnHeaderClicked();
int ColumnClickedIndex() const;              // 在 ColumnHeaderClicked 处理器里读

Event<DataGrid, RoutedEventArgs>& CellEdited();   // 校验通过、提交之后
int EditedRow() const;
int EditedColumn() const;
const std::wstring& EditedText() const;

Event<DataGrid, RoutedEventArgs>& RowsReordered();
const std::vector<int>& ReorderedOldIndices() const;  // 原索引，升序
int ReorderedNewIndex() const;                        // 第一行落点
```

所有载荷都通过 sender 的访问器读取，处理器签名统一是
`void(*)(void*, DataGrid&, RoutedEventArgs&)`。

**控件不排序，也不重排数据。** 表头点击只上报列号；应用排好自己的数据后调
`SetSortColumn` 更新箭头。`RowsReordered` 同理：应用移动自己的存储，再调 `RefreshCells()`。

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

## UI Automation

DataGrid 的 peer（`automation/DataPeers.h`）：

| 项 | 值 |
|---|---|
| ControlType | DataGrid |
| 模式 | Selection（单选/多选）、Scroll（横向与纵向，范围与控件自己的滚动一致，冻结列不计入横向） |
| 行 | DataItem，Name = 各单元格文本用 “, ” 连接（最多 16 列，新的公开 `CellText`）；SelectionItem：Select 走新的公开 `PerformSelectRow`（与点击相同：先提交另一行上正在进行的编辑，再 `SetSelectedRow`），多选的 Add/Remove 走新的公开 `PerformToggleRowSelection`（与 Ctrl+点击相同），都触发 `SelectionChanged`；ScrollItem 走新的公开 `PerformScrollRowIntoView`（视图需要移动时先提交编辑，同 `SetVerticalOffset` 的规则）；横向 ViewSize 用新的公开 `ScrollableViewportWidth`（去掉冻结列） |
| 命中测试 | 新的公开 `RowIndexAt`（点击用的同一算式），O(1) |

每一行的子节点是它的单元格（按需创建，随行释放）：ControlType Custom（与 WPF 的 DataGridCell 相同），Name = 单元格文本，HelpText = 列标题，矩形是画出来的单元格（裁到视口，滚动列不压在冻结列上）；命中测试会一直落到单元格（新的公开 `VisibleCellRect` / `ColumnIndexAt`）。

**只暴露视口附近的行。** 客户端遍历子节点时，得到的是可见行加上下各一页（最多 256 行），不是全部。原因（实测）：Inspect 会列出焦点元素的全部兄弟节点，真实客户端每行约 0.45 ms，5 万行要约 22 秒，Inspect 卡死；改为窗口后同样的遍历是 6 行、1 ms。窗口外的行仍然存在：选中、焦点、ScrollItem 都能到达；滚动后窗口移动并发 ChildrenInvalidated。这与 WPF/WinUI 虚拟化列表在 UIA 树中的表现相同。

行按下标作 key：重新排序后行名会在下一次检查时更新，行数变化时全部释放并发 ChildrenInvalidated。尚未提供：列标题节点、Grid/Table 模式（客户端可以逐行逐格读，但不能按 (行, 列) 直接取单元格）。新的公开 `MaxVerticalOffset` / `MaxHorizontalOffset`。

惰性创建：只有 UIA 客户端访问到这个控件时才创建 peer，没有客户端时零开销。行（项）不是元素，客户端访问到才创建节点。模式操作经投递消息执行，执行前重新检查控件仍在树上且已启用。自定义方式见 inheritance-guide.md 的“UI Automation”一节。

验证：无头测试 `tests/unit/AutomationDataPeerTests.cpp`，真实 UIA 客户端自测 `FluentUITests.exe --uia-smoke`；12 处变异（把操作换回不发事件的 setter、行名只取一列、日期按下标作 key、日志行序号不前进等）全部被测试抓到。讲述人实际朗读的效果需要在真机上听。

## 备注

### 性能特征

- **每帧成本 O(可见单元格)**，与总行列数无关。实测 41 列 × 60 行放进 820×220
  的区域 → 10 列 × 7 行 = **70 个单元格**是活的。
- **`SetRowCount` 不分配**：它只是一个整数。
- **单元格文本每格每帧分配一次**（超出短字符串缓冲时）。`CellProvider` 按值返回
  `std::wstring`，这次分配发生在访问器返回时，grid 改变不了；grid 直接用这个返回值
  绘制，不再复制。以前复制进一个复用的 scratch buffer，并没有省掉这次分配，反而每格
  多一次 memcpy，所以去掉了。一屏 70 个单元格就是每帧最多 70 次小分配。
- **平滑滚动**：`WantsAnimationTick()` 跟随内部 `ScrollViewer`，滚动停止后回到
  零成本空闲。

### 使用注意事项

- **必须写显式高度**（或放进定高容器）。这与 `Metric` 相反，值得说清区别：
  Metric 有自然高度（三行文字），写死会切字；DataGrid **没有**自然高度 —— 它的
  自然高度是全部行加起来（5 万行 × 28 = 140 万 DIP）。视口大小只能由调用者决定，
  这正是显式尺寸的正当用途。
- **列宽不会自动适配。** 全量适配意味着为某列测量**每一行**的文字，是 O(rows)，
  会在 10 万行表上卡死 UI 线程。`AutosizeColumn` 只按当前视口抽样，需要时显式调用。
- 单元格访问器在渲染路径上，**每格每帧调用一次**。要廉价，尽量不分配。

### 键盘导航

| 按键 | 行为 |
| --- | --- |
| ↑ / ↓ | 上下移动选中行并滚入视野（Multiple 模式下 Shift 扩展选区） |
| PageUp / PageDown | 翻页（视口行数 - 1） |
| Home / End | 首行 / 末行 |
| F2 | 编辑当前行（需 `SetEditingEnabled(true)`） |
| Enter / Escape | 编辑中：提交 / 取消 |
| Ctrl+C | 复制选中行为 TSV（`SetCopyEnabled`，默认开） |
| 滚轮 | 平滑滚动 3 行/档；所有行都放得下时不处理，冒泡给外层 `ScrollPanel` |

鼠标：点击表体会取得焦点；Multiple 模式下 Ctrl+点击切换、Shift+点击按锚点扩展。

方向键从**活动行**出发：最近一次选择手势落到的那一行（Shift 范围的移动端、Ctrl+点击切换的那一行、
普通点击或方向键选中的那一行）。没有活动行时退回第一个选中行。

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

### 修过的 bug

| 问题 | 修复 | 证明 |
| --- | --- | --- |
| Shift+↓ 只能扩展到两行：方向键从第一个选中行出发，`{5,6}` 的第一行还是 5，再按一次又要 6 | 新增活动行（`activeRow_`），方向键从它出发；Shift+↑ 从移动端收缩 | `DataGrid.ShiftDownKeepsGrowingTheRange` |
| 范围不变的手势（末行上再按 Shift+↓、重复 Shift+点击同一行）也重绘并触发 `SelectionChanged` | 集合不变时直接返回 | `DataGrid.UnchangedRangeDoesNotRaiseSelectionChanged` |
| 横向滚动后，滚动列的竖网格线穿过冻结列的文字（单元格裁剪到冻结带右侧，网格线只裁剪到视口） | `VerticalGridLineX` 丢弃滑到冻结带下面的滚动列边线 | `DataGrid.ScrolledGridLinesDoNotCrossTheFrozenBand` |
| 指针划过列分隔线时整表重绘两次（进入、离开各一次），而 Render 根本不画分隔线悬停 | 只记录悬停（`Cursor()` 用），不标脏；反馈是窗口通过 WM_SETCURSOR 设的调整光标 | `DataGrid.DividerHoverDoesNotRepaint` |
| 行数少、没东西可滚时滚轮仍标记 handled，指针在表上时外层页面滚不动 | 没有可滚距离时不处理滚轮 | `DataGrid.WheelBubblesWhenNothingToScroll` |

编辑中滚动先提交的行为由 `DataGrid.ScrollingWhileEditingCommitsTheEdit`、`DataGrid.WheelWhileEditingCommitsTheEdit`、
`DataGrid.ClickingADifferentRowWhileEditingCommitsTheEdit`、`DataGrid.LosingFocusCommitsTheEdit` 覆盖。

新增的公开几何查询：

```cpp
// boundary 0 = 表格左边缘，b > 0 = 第 b-1 列的右边缘。应绘制时写出 x 并返回 true；
// 在屏幕外或会落在冻结带上时返回 false。冻结列自己的边线不随滚动移动，总是绘制。
bool VerticalGridLineX(int boundary, float& x) const;
```

未验证：网格线和光标是像素 / 窗口层面的效果，需真机。冻结一列后横向拖动，看冻结列文字上不再有
扫过的竖线；指针划过表头分隔线时光标变成调整光标，F12 HUD 上不出现整表脏区。

### 尚未实现（刻意）

- **数据排序 / 筛选**：控件只画排序箭头、上报表头点击，数据由应用处理。
- **单元格内放控件**：会毁掉虚拟化。
- **单元格换行**：行高会变成数据相关，破坏使滚动保持 O(可见) 的常数时间行索引。
- **双击按列定位编辑**：双击目前总是编辑第 0 列。

### 演示页

demo 导航 → 数据控件 → DataGrid（nav id 404）。含 5 万行实况（屏幕上打印实时
可见范围）、跳转按钮、外观开关、以及与 Metric 组合的涨跌着色表。
