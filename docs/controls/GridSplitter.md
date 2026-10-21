# GridSplitter 控件

## 概述

`Grid` 两条轨道之间的一根可拖拽细条。拖动它把相邻两列（或两行）的尺寸此消彼长地重新分配，
用来做左右分栏、上下分栏这类用户可调的布局。

**它必须放在 Grid 的中间轨道里。** 分割条调整的是它**自己所在轨道的前一条和后一条**——
放在第 1 列就调整第 0 列和第 2 列。放在首尾轨道时一侧无可调整对象，拖动自动失效
（光标也不会变成双向箭头）。

**典型使用场景**:
- 文件树 + 编辑器的左右分栏
- 编辑器 + 输出面板的上下分栏
- 任何「用户想自己决定两块区域各占多少」的布局

---

## 继承层次

```
Visual
  └─ UIElement
      └─ FrameworkElement
          └─ Control
              └─ GridSplitter   (final)
```

`final` 类，不设计为可派生。

**命名空间**: `fluent`
**头文件**: `FluentUI/controls/GridSplitter.h`

---

## 公开 API

### 构造函数

```cpp
GridSplitter();
```

默认 4 DIP 宽/高、`Orientation::Vertical`（调整列宽）。另一个轴向由布局拉伸。

### 嵌套类型

```cpp
enum class Orientation { Horizontal, Vertical };
```

命名遵循 WPF 习惯，指的是**分割条本身的朝向**，不是拖动方向：

| Orientation | 分割条外观 | 拖动方向 | 调整对象 |
|---|---|---|---|
| `Vertical`（默认） | 竖条 | 左右 | **列**宽 |
| `Horizontal` | 横条 | 上下 | **行**高 |

### 属性

#### Orientation

```cpp
void SetOrientation(Orientation o);
Orientation GetOrientation() const;
```

**说明**: 决定调整列还是行。必须与所在 Grid 的轨道结构匹配——竖条要放在「三列 + 单行」
结构的中间列，横条要放在「三行 + 单列」结构的中间行。

**脏标记等级**: Render（只影响光标与绘制，尺寸由 Grid 的轨道定义决定）。

#### Cursor（override）

```cpp
HCURSOR Cursor() const override;
```

悬停时返回 `IDC_SIZEWE`（竖条）或 `IDC_SIZENS`（横条）。**不可调整时返回 `nullptr`**，
所以放错位置的分割条不会给出「可以拖」的错误暗示。

---

### 事件

三个事件构成一次完整拖动的生命周期：`DragStarted` → `DragDelta`（每次移动一次）→ `DragCompleted`。

#### DragStarted

```cpp
Event<GridSplitter, RoutedEventArgs>& DragStarted();
```

**触发时机**: 左键按下且该分割条可调整时。在指针捕获与锚点记录之后触发。

**用途**: 拖动前快照布局状态（用于「取消拖动」或撤销）。

#### DragDelta

```cpp
struct GridSplitterDragDeltaArgs {
    float deltaDip = 0.0f;
};
Event<GridSplitter, GridSplitterDragDeltaArgs>& DragDelta();
```

**参数**: `deltaDip` — 相对**拖动起点**的累计偏移，不是相对上一次移动的增量。

这一点容易搞错：连续三次移动 10、20、30 DIP，三次 `DragDelta` 的 `deltaDip` 依次是
10、20、30，而不是 10、10、10。这个值与轨道实际被调整的量一致。

**触发时机**: 每次 `OnPointerMoved`，**在轨道尺寸已更新之后**——所以处理器里读 Grid
能拿到新尺寸。

用 `HasSubscribers()` 做了短路，无人订阅时不构造 args。

#### DragCompleted

```cpp
Event<GridSplitter, RoutedEventArgs>& DragCompleted();
```

**触发时机**: 左键释放。在清除拖动状态之后触发——处理器看到的是一个已经稳定的分割条，
不是还标记着「正在拖动」的。

**用途**: 这是持久化列宽/行高的地方。读 `Grid::ResolvedColumnSizes()` /
`ResolvedRowSizes()` 拿已解析的 DIP 值；不要读轨道定义——Star 轨道拖动后仍是 Star
（见下「拖动按原单位写回」），读到的是星权重而不是 DIP。

```cpp
// 拖完就存配置
dragEndSub_ = splitter->DragCompleted().Subscribe(this,
    [](void* owner, GridSplitter&, RoutedEventArgs&) {
        static_cast<MyPage*>(owner)->SaveLayout();
    });
```

⚠️ **返回的 `Subscription` 必须持有**（RAII，丢掉即退订）。

---

## 代码示例

### 左右分栏

```cpp
#include "FluentUI/controls/GridSplitter.h"

auto* grid = panel->Add(std::make_unique<Grid>());
grid->SetColumns({
    GridLength::Star(),        // 左栏
    GridLength::Pixels(4.0f),  // 分割条自己的轨道
    GridLength::Star()         // 右栏
});
grid->SetRows({GridLength::Star()});

auto* left = grid->Add(std::make_unique<Border>());
grid->SetCell(left, 0, 0);

auto* splitter = grid->Add(std::make_unique<GridSplitter>());
grid->SetCell(splitter, 0, 1);       // 中间列

auto* right = grid->Add(std::make_unique<Border>());
grid->SetCell(right, 0, 2);
```

### 上下分栏

```cpp
grid->SetRows({
    GridLength::Star(),
    GridLength::Pixels(4.0f),
    GridLength::Star()
});
grid->SetColumns({GridLength::Star()});

auto* splitter = grid->Add(std::make_unique<GridSplitter>());
grid->SetCell(splitter, 1, 0);       // 中间行
splitter->SetOrientation(GridSplitter::Orientation::Horizontal);
```

### 持久化布局

```cpp
// 读已解析的 DIP 尺寸存盘；轨道定义可能仍是 Star，读出来是权重不是 DIP
sub_ = splitter->DragCompleted().Subscribe(this,
    [](void* owner, GridSplitter& s, RoutedEventArgs&) {
        auto* self = static_cast<MyPage*>(owner);
        const auto& cols = self->grid_->ResolvedColumnSizes();
        self->config_.leftWidth = cols[0];
        self->config_.Save();
    });
```

---

## 备注

### 拖动按原单位写回，网格保持弹性

拖动**不会**把 `Star` 轨道变成 `Pixel`。写回逻辑（`GridSplitter.cpp:161-181` 的
`writeBack`）按三条规则保留原单位：

| 两条轨道的单位 | 写回后 |
|---|---|
| Star ↔ Star | 都仍是 `Star`，重新分配星权重，**权重和守恒** |
| Star ↔ Pixel/Auto | `Star` 侧仍是 `Star`，Pixel 侧仍是 `Pixel` |
| Auto | 唯一被钉成 `Pixel` 的情况 |

`Auto` 是例外因为它的尺寸由内容决定，没有可写回的参数——要让拖动生效只能把它固化成
具体 DIP 值。

**这与 WPF 不同。** WPF 的 GridSplitter 会把两侧都冻结成 Pixel；本实现刻意保持弹性，
否则窗口缩放时被冻结的轨道会与 Star 轨道之间出现缝隙。

正确性前提是定义快照（`defBeforeStart_` / `defAfterStart_`）：
拖动开始时记下两条轨道的原始 `GridLength`，每次移动都基于它决定写回形式，而不是读
当前值（那样会在连续拖动中逐步丢失单位信息）。

### 零和调整与 1 DIP 下限

一次移动里「一条轨道增加多少，另一条就减少多少」，Grid 的总内容宽度不变，所以分割条
以外的元素不会跟着位移。

两条轨道都有 `kMinTrackDip = 1.0f` 的下限。到达极限后这次移动不再是零和的——这是有意的
取舍：0 DIP 的轨道无法被命中测试，分割条一旦把某侧压到 0 就再也拖不回来了。

### 增量基于拖动起点的快照，而非实时值

`OnPointerPressed` 时采样两条轨道的**已解析尺寸**（`ResolvedColumnSizes()`），后续
每次移动都用 `快照 + delta` 计算，而不是累加到实时值上。

这是个真实的坑：累加实时值会重复读入上一次移动已经写进去的数字，导致漂移；而且一旦某条
轨道撞到 1 DIP 的钳位，累加就变成单向棘轮（越拖越偏，回不去）。

### 性能特征

- **Render**: 一个圆角矩形填充 + 一圈描边，无文本布局。
- **只在 hover / dragging 时填充**，静止时只有描边（读起来像两个面板之间的一条缝）。
  显式设过 `Background` 的话静止时也填充，否则设了会看起来没生效。
- **拖动本身触发 Grid 重新布局**，成本取决于 Grid 里的内容——分割条自己的开销可忽略。

### 使用注意事项

- **至少 3 条轨道**，且分割条在内部轨道（索引 `> 0` 且 `< count - 1`），否则 `CanResize()`
  为假，拖动与光标变化都不生效。
- **不在 Grid 里也不会报错**：`ParentGrid()` 返回 null 时分割条照常绘制，只是什么都不做。
  这是刻意的——放错容器是布局错误，不该崩。
- **分割条自己的轨道用固定像素**（示例里是 4 DIP）。给它 `Star` 会让分割条随窗口变粗。
- **首次 Arrange 之前拖动无效**：轨道的已解析尺寸还不存在，`trackBeforeStart_` 为空。

### 键盘导航

**不支持。** 当前没有键盘调整轨道尺寸的能力（WPF 的 GridSplitter 支持方向键，
FluentUI 尚未实现）。分割条也不参与 Tab 焦点顺序。

### 主题 Token

| Token | 用途 |
| --- | --- |
| `controlFillHover` | 悬停时的填充 |
| `controlFillPressed` | 拖动中的填充 |
| `controlStrokeDefault` | 描边（始终绘制） |

三者都走 `Control` 的属性层（`EffectiveBackground` / `EffectiveBorderBrush`），
所以单个分割条可以不改主题就重新配色。

### 演示页

demo 导航 → 布局 → GridSplitter。含左右分栏与上下分栏两个 520×240 的实例。

---

## 相关控件

- [Grid](Grid.md) — 分割条唯一有意义的父容器，轨道定义都在这里
- [Border](Border.md) — 示例里用来做分栏面板的容器
