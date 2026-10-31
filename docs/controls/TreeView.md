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
- **多选集合按区间存储。** 选中集合是 `Selector` 的 `IndexRangeSet selectedSet_`（升序、不相交、
  不相邻的闭区间），以前是逐行的 `unordered_set<int>`。TreeView 的范围填充把可见行切片里连续的行号
  合并成一段，再按升序 `AddRange`：全部展开的一段只占一个区间，每个折叠节点只把它断开一次。
  `IsRowSelected` 是 O(log 区间数)，新增的 `SelectedRanges()` / `SelectedCount()` 是 O(区间数)；
  `SelectedRowIndices()` 签名不变，仍是 O(选中行数)。由
  `TreeViewMultiSelect.ShiftRangeCoalescesVisibleRowsIntoRuns` 锁定（变异：合并时不检查行号连续，
  折叠节点的子行被并进区间，测试失败）。
- **没有订阅者时不构造集合事件的载荷。** `SelectionSetChanged` 的参数是 `vector<int>`，构造它是
  O(选中行数)，也是区间存储之后大范围 Shift 选择里唯一按行计费的一步。没人订阅时不构造。没有专门的
  测试：没有订阅者时本来就没有可观测的输出。
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

## UI Automation

TreeView 的 peer：

| 项 | 值 |
|---|---|
| ControlType | Tree |
| 模式 | Selection、Scroll。子节点是当前可见行（折叠的子树不在树里），按行的稳定 id 作 key，所以展开节点时其他行的节点不失效；每行 TreeItem 提供 ExpandCollapse（无子行为 LeafNode，对其 Expand 返回 `UIA_E_INVALIDOPERATION`）和 SelectionItem。新增的行位置反查表让 `VisibleIndexByRow` 从线性查找变为 O(1) |
| AutomationId | `SetAutomationId` |
| HelpText | `Tooltip()` |

行（项）的事件：行数变化（追加、删除、TreeView 展开/折叠）时容器发 ChildrenInvalidated；按下标作 key 的列表（ListBox、ComboBox、NavRail）在行数变化后释放全部缓存的行节点，因为下标可能已经指向别的行。行自身的属性变化（IsSelected、TreeItem 的 ExpandCollapseState）在容器失效后的下一次 flush 检查，每次最多检查 64 个（焦点行优先，其次是可见行）。缓存超过 1024 时保留焦点行和最近用过的 64 行，所以客户端遍历几千行时，它正站着的那一行不会失效。客户端离开（不再监听）后，下一次行数变化会丢弃整个缓存，之后行数再变化也不投递任何消息。命中测试先把点按 Viewbox 缩放反算回控件坐标，与边界矩形一致。验证：`tests/unit/AutomationItemReviewTests.cpp`。

**行 id 必须唯一。** UIA 节点按 `TreeViewRow::id` 作 key（默认 0）。全部用默认 id 的树，所有行都映射到同一个节点，客户端只能看到一行。要支持 UIA 就给每行设置不同的 id。已知限制：`SelectedCount` 包含折叠子树里被选中的行，而 `GetSelection` 只返回可见行，所以两者可能不一致。

惰性创建：只有 UIA 客户端访问到这个控件时才创建 peer，没有客户端时零开销。模式操作经投递消息执行，执行前重新检查控件仍在树上且已启用。属性变化只在真正变化且有客户端监听时发送。自定义方式见 inheritance-guide.md 的“UI Automation”一节。

验证：无头测试 `tests/unit/AutomationControlPeerTests.cpp` / `AutomationSelectorPeerTests.cpp`，真实 UIA 客户端自测 `FluentUITests.exe --uia-smoke`；14 处变异（如把 UIA 操作换回不发事件的 setter、去掉项缓存上限）全部被测试抓到。

## 相关控件

- [ListBox](ListBox.md) — 扁平的虚拟化列表，同样基于 `Selector`
- [ScrollViewer](ScrollViewer.md) — 内嵌的滚动条模型
