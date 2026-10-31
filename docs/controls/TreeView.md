# TreeView 控件

## 概述

轻量的树形视图，行数据是值类型（`TreeViewRow`），按先序存放在一个向量里。展开 / 折叠、
键盘导航、首字母跳转、单选或多选、双击。**虚拟化**：只绘制和命中测试可见的行，行高统一
（默认 28 DIP），所以十万个节点的树每帧成本和一百个节点相同。滚动在 UI 线程上通过内嵌的
`ScrollViewer` 平滑进行。

**典型使用场景**:
- 设备 / 资源层级（计算机 → 磁盘 → 分区）
- 导航树（demo 自己的左侧导航就是一个 TreeView）
- 带多选的文件或节点列表

---

## 继承层次

```
Visual
  └─ UIElement
      └─ FrameworkElement
          └─ Control
              └─ ItemsControl<TreeViewRow>
                  └─ Selector<TreeViewRow>
                      └─ TreeView
```

行向量是 `ItemsControl` 的 `items_`，选中索引和多选集合在 `Selector` 里。

**命名空间**: `fluent`
**头文件**: `FluentUI/controls/TreeView.h`

---

## 公开 API

### 行数据

```cpp
struct TreeViewRow {
    int id = 0;
    int parentId = -1;
    std::wstring text;
    int depth = 0;
    bool dimmed = false;
    bool hasChildren = false;
    bool expanded = true;
    TreeViewIcon icon = TreeViewIcon::Device;   // None/Computer/Folder/Device/Warning/Bluetooth/Disk
    void* user = nullptr;
    ID2D1Bitmap* customIcon = nullptr;          // 优先于 icon；不持有引用
};
```

`TreeViewRow`、`TreeViewIcon`、`TreeSelection`、`TreeViewItemDoubleClickArgs` 都是 `namespace fluent` 级类型。

### 整体替换与增量修改

```cpp
void SetRows(std::vector<TreeViewRow> rows);
void AddRoot(TreeViewRow row);
void AddChild(int parentId, TreeViewRow row);   // 自动填 parentId / depth，父节点 hasChildren = true
void RemoveNode(int nodeId);                    // 连同全部后代
void SetNodeExpanded(int nodeId, bool expanded);
void ExpandAll();
void CollapseAll();
TreeViewRow* FindNode(int nodeId);              // const 重载同名
```

所有修改都按 **id** 保留选中项，并重建内部索引和可见行列表。

### 选择

```cpp
void SetSelectedIndex(int visibleIndex) override;   // 参数是「可见索引」
int SelectedIndex() const;                          // 返回「行索引」（items_ 下标）
const TreeViewRow* SelectedRow() const;

// 继承自 Selector：SetSelectionMode / GetSelectionMode / SelectedIndices /
// IsSelected / ToggleSelection / RangeSelectTo
std::vector<int> SelectedRowIndices() const;        // 行索引
bool IsRowSelected(int rowIndex) const;
void ToggleRowSelection(int rowIndex);
```

⚠️ **设置用可见索引、读取得到行索引**，这个不对称是历史上刻意保留的。`SelectionMode` 是
`namespace fluent` 级枚举（定义在 `Selector.h`），`Single` / `Multiple`。

### 事件

```cpp
Event<TreeView, TreeSelection>& SelectionChanged();                    // {visibleIndex, row}
Event<TreeView, std::vector<int>>& SelectionSetChanged();              // Multiple 模式下集合变化，载荷是行索引
Event<TreeView, TreeViewItemDoubleClickArgs>& ItemDoubleClick();       // 设 handled = true 阻止默认的展开/折叠
void SetDoubleClickTogglesExpansion(bool enable);                      // 默认 true
```

`Subscription` 必须作为成员持有，丢掉即退订。

### 其他

```cpp
int HoveredRowIndex() const;        // 指针下的行索引，-1 = 无（给测试观察悬停）
void ScrollBy(float deltaDip);
```

---

## 代码示例

```cpp
#include "FluentUI/controls/TreeView.h"

auto* tree = panel->Add(std::make_unique<TreeView>());
tree->SetHeight(300.0f);                 // 虚拟化列表没有自然高度
tree->AddRoot({1, -1, L"此电脑", 0, false, false, true, TreeViewIcon::Computer});
tree->AddChild(1, {2, 1, L"本地磁盘 (C:)", 0, false, false, true, TreeViewIcon::Disk});

sub_ = tree->SelectionChanged().Subscribe(this,
    [](void* owner, TreeView&, TreeSelection& s) {
        if (s.row) static_cast<MyPage*>(owner)->Open(s.row->id);
    });
```

---

## 备注

### 修过的 bug

- **Shift 范围选择跳过折叠节点里的行。** 行按先序存放，折叠节点的后代在数值上正好落在两个
  可见行之间。以前范围选择取区间内的每一个行索引：屏幕上高亮两行，`SelectedRowIndices()`
  却还报告了看不见的子节点，应用按「选中项」删除时会删掉用户根本没看到的节点。现在 TreeView
  重写 `Selector::AddRangeToSelectionSet`，只取区间内的**可见**行（在可见行列表上二分查找）。
  由 `TreeViewMultiSelect.ShiftClickRangeSkipsRowsInsideACollapsedNode` 和
  `TreeViewMultiSelect.ShiftArrowRangeSkipsRowsInsideACollapsedNode` 锁定。
- **没有订阅者时不构造集合事件的载荷。** `SelectionSetChanged` 没人订阅时，不再把整个选中集合
  复制并排序成向量。大范围 Shift 选择里这一步是主要成本。没有专门的测试：没有订阅者时本来就没有
  可观测的输出。
- **自定义图标跟随元素透明度。** `customIcon` 通过原始设备上下文绘制，以前不透明度写死 1.0，
  在 `SetOpacity` 淡化的 TreeView 里图标仍然完全不透明；现在用 `dc.Opacity()`。
  未验证：需真机。在一个淡入淡出的页面里放带 `customIcon` 的 TreeView，看图标是否和文字一起淡化。

### 性能特征

- **绘制和命中测试 O(可见行)**；行高统一，行号由滚动偏移直接算出。
- **平滑滚动**在 UI 线程上，`scroll_` 是成员而不是树节点，所以 tick 里由 TreeView 自己 `Invalidate()`。
- **idle 零成本**：滚动和滚动条淡出结束后 `WantsAnimationTick()` 为 false。
- **滚轮只在能滚时处理**：内容不溢出时滚轮不标记 handled，冒泡给外层 `ScrollPanel`。

### 使用注意事项

- ⚠️ **必须给高度**（或放进定高容器）。
- **id 必须唯一**；`AddChild` 的 `parentId` 不存在时什么都不做（不会悄悄加成根节点）。
- `customIcon` 是借用的 `ID2D1Bitmap*`，调用者保证它比 TreeView 活得久，且属于同一个 D2D 设备。

### 键盘导航

| 按键 | 行为 |
| --- | --- |
| ↑ / ↓ | 上一个 / 下一个可见行 |
| ← | 展开的节点：折叠；否则跳到父节点 |
| → | 折叠的节点：展开 |
| Home / End / PageUp / PageDown | 首行 / 末行 / 翻页 |
| 可打印字符 | 按前缀跳转（连续输入累积前缀） |
| Shift+方向键 / Home / End / PageUp / PageDown | Multiple 模式：从锚点扩展范围 |
| Ctrl+Space | Multiple 模式：切换当前行 |

鼠标：Multiple 模式下 Ctrl+点击切换、Shift+点击按锚点扩展；双击有子节点的行切换展开。

### 主题 Token

| Token | 用途 |
| --- | --- |
| `accent` | 选中行底色（@ 0.18 alpha）、活动行（`SelectedIndex()`）左侧的 3 DIP 标记条、计算机图标 |
| `textPrimary` | 行文字，悬停底色（@ 0.05 alpha） |
| `textSecondary` | `dimmed` 行文字、内置图标 |
| `cardFill` | 内置图标的镂空点 |

### 演示页

demo 导航 → 数据控件 → TreeView（nav id 402）；demo 左侧的导航树本身也是 TreeView。

---

## 相关控件

- [ListBox](ListBox.md) — 扁平的虚拟化列表，同样基于 `Selector`
- [ScrollViewer](ScrollViewer.md) — 内嵌的滚动条模型
