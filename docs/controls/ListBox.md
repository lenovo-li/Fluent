# ListBox 控件

## 概述

可滚动的条目列表,支持单选与多选。两种供数模式:

- **直接模式**:控件持有一个 `std::wstring` 向量。
- **虚拟化模式**:只给条目**总数**和一个按需取文本的回调 —— 只有可见行会被问到。

虚拟化模式是它能承载十万条数据的原因:每帧成本与**可见行数**成正比,与总数无关。

**典型使用场景**:
- 选项列表、文件列表、搜索结果
- 日志尾随(配合虚拟化)
- `ComboBox` 的弹出层内部就用它

---

## 继承层次

```
Visual
  └─ UIElement
      └─ FrameworkElement
          └─ Control
              └─ ItemsControl<std::wstring>
                  └─ Selector<std::wstring>
                      └─ ListBox
```

`ComboBox` 是同门兄弟。

**命名空间**: `fluent`
**头文件**: `FluentUI/controls/ListBox.h`

---

## 公开 API

### 直接模式

```cpp
void SetItems(std::vector<std::wstring> items);   // 继承自 ItemsControl
```

### 虚拟化模式

```cpp
void SetItemCount(size_t count);
std::function<std::wstring(size_t)> ItemTextProvider;   // 公开成员，直接赋值
```

```cpp
list->SetItemCount(100000);
list->ItemTextProvider = [](size_t i) {
    return L"第 " + std::to_wstring(i) + L" 行";
};
```

`ItemTextProvider` 为 null 时回落到直接模式(读 `items_[index]`)。

⚠️ **`ItemCount()` 刻意 shadow 了基类版本**:命中测试、键盘导航、滚动范围、选择夹逼**全部**
必须看到虚拟数量。留着基类版本可见会让「用的是哪个 count」取决于调用点的静态类型 ——
那种 bug 极难查。

### 行高

```cpp
void SetItemHeight(float dip);
float ItemHeight() const;
```

行高是**统一的**(不支持按行变高),这正是虚拟化能用除法直接算出可见区间的前提。

### 取文本的两个重载

```cpp
std::wstring GetItemText(int i) const;                              // 返回值，会拷贝
const std::wstring& GetItemText(int i, std::wstring& scratch) const; // 零分配
```

**任何每帧路径都应该用 scratch 版本。** 返回值版本每帧、每可见行都拷贝一次字符串 ——
虚拟化已经把它限制在 O(可见行),所以不是伸缩性 bug,但一屏行数在滚动时就是每帧几十次
立即丢弃的堆分配;直接模式下还每次都在复制控件自己已经持有的字符串。

scratch 版本的生命期:**有效到下一次条目源变动、或下一次复用同一个 scratch 为止。不要
把这个引用存起来。**

### 选择与事件

```cpp
void SetSelectionMode(SelectionMode mode);        // 继承自 Selector
std::vector<int> SelectedIndices() const;        // O(选中项数)：逐个展开
std::vector<IndexRange> SelectedRanges() const;  // O(区间数)：升序、不相交、不相邻的闭区间
size_t SelectedCount() const;                    // O(区间数)
bool IsSelected(int i) const;                    // O(log 区间数)

Event<ListBox, int>& SelectionChanged();                    // 活动索引变化
Event<ListBox, std::vector<int>>& SelectionSetChanged();    // 仅选择集合变化
```

**两个事件的分工是刻意的**:普通的选择变化(活动索引移动)触发 `SelectionChanged`;
Ctrl/Shift 手势导致集合变化但活动索引没动时触发 `SelectionSetChanged`。这样**同时订阅两者
的代码在一次手势里恰好收到一个事件**,不会重复处理。

⚠️ 返回的 `Subscription` 必须持有(RAII)。

---

## 代码示例

### 直接模式 + 单选

```cpp
#include "FluentUI/controls/ListBox.h"

auto* list = panel->Add(std::make_unique<ListBox>());
list->SetItems({L"深圳", L"上海", L"北京"});
list->SetSelectedIndex(0);
list->SetHeight(200.0f);          // 必须给视口高度，见下

sub_ = list->SelectionChanged().Subscribe(this,
    [](void* owner, ListBox&, int& index) {       // 注意 int&
        static_cast<MyPage*>(owner)->OnCityPicked(index);
    });
```

### 虚拟化 + 多选

```cpp
list->SetItemCount(100000);
list->ItemTextProvider = [](size_t i) { return MakeRowText(i); };
list->SetSelectionMode(SelectionMode::Multiple);
```

---

## UI Automation

ListBox 的 peer：

| 项 | 值 |
|---|---|
| ControlType | List |
| 模式 | Selection（单选/多选，`GetSelection` 最多返回 256 项）、Scroll（纵向）。每一行是 ListItem 节点（不是元素，客户端访问到才创建，缓存上限 1024，超出在下一次 flush 时裁剪）：SelectionItem（多选时 Add/Remove 走 `ToggleSelection`）、ScrollItem（新的公开 `ScrollItemIntoView`）。选中一行发 ElementSelected，批量变化（如 100 万行 Shift+End）只发一次 Selection_Invalidated。新的公开 `ItemRect` / `ItemIndexAt` |
| AutomationId | `SetAutomationId` |
| HelpText | `Tooltip()` |

行（项）的事件：行数变化（追加、删除、TreeView 展开/折叠）时容器发 ChildrenInvalidated；按下标作 key 的列表（ListBox、ComboBox、NavRail）在行数变化后释放全部缓存的行节点，因为下标可能已经指向别的行。行自身的属性变化（IsSelected、TreeItem 的 ExpandCollapseState）在容器失效后的下一次 flush 检查，每次最多检查 64 个（焦点行优先，其次是可见行）。缓存超过 1024 时保留焦点行和最近用过的 64 行，所以客户端遍历几千行时，它正站着的那一行不会失效。客户端离开（不再监听）后，下一次行数变化会丢弃整个缓存，之后行数再变化也不投递任何消息。命中测试先把点按 Viewbox 缩放反算回控件坐标，与边界矩形一致。验证：`tests/unit/AutomationItemReviewTests.cpp`。

`SetScrollOffset` 现在会重绘（Scroll 模式经它滚动；`scroll_` 是成员不是元素，自己不能安排帧）。Scroll 模式的翻页移动一个视口高度；对不能滚动的方向（横向）请求滚动返回 `UIA_E_INVALIDOPERATION`。

惰性创建：只有 UIA 客户端访问到这个控件时才创建 peer，没有客户端时零开销。模式操作经投递消息执行，执行前重新检查控件仍在树上且已启用。属性变化只在真正变化且有客户端监听时发送。自定义方式见 inheritance-guide.md 的“UI Automation”一节。

验证：无头测试 `tests/unit/AutomationControlPeerTests.cpp` / `AutomationSelectorPeerTests.cpp`，真实 UIA 客户端自测 `FluentUITests.exe --uia-smoke`；14 处变异（如把 UIA 操作换回不发事件的 setter、去掉项缓存上限）全部被测试抓到。

## 备注

### 使用注意事项

- ⚠️ **必须给高度**(或放进定高容器)。虚拟化列表**没有自然高度** —— 十万行 × 28 DIP 的
  自然高度是 280 万 DIP,毫无意义。视口尺寸必须由调用者决定,和 `Chart`、`DataGrid` 同类。
  这正是「调用者写死尺寸」的正当场景。
- **行高统一**,不支持按行变高。
- **别把 scratch 版本返回的引用存起来。**

### 性能特征

- **Render 是 O(可见行)**,与总数无关。
- **平滑滚动**通过 `scroll_` 成员的 tick 推进。注意 `OnAnimationTick` 里额外调了
  `Invalidate()`:`scroll_` 是**成员而不是树节点**,它内部的 Invalidate 到不了帧调度器 ——
  少了这一行,滚动动画只会在「碰巧有别的元素也失效」的那些帧上前进,表现为**分段跳动**。
- `SetKeepVisibleWhenOverflow(true)`:内容溢出时滚动条常驻,避免它出现/消失导致内容宽度
  反复变化(那会引起布局抖动)。
- **多选集合按区间存储**(`Selector` 的 `IndexRangeSet selectedSet_`)。以前是 `unordered_set<int>`,
  `SetItemCount(1000000)` 后 Shift+End 要在按键路径上插入一百万个哈希节点。现在 Shift 范围就是一个区间,
  Ctrl+点击在区间里拆分或合并,`IsSelected` 是对区间做二分查找,Render 每行一次 O(log 区间数)。
  `FluentUIBench ListBoxShiftEnd`(Release,同一台机器,240 Hz 显示器,一帧 4.17 ms;每次计时一对
  Shift+Home + Shift+End,即一次完整的 0→999,999 范围构建加一次收回,三次运行):
  - 改前:无订阅者 76–87 ms/对,订阅了 `SelectionSetChanged` 123–135 ms/对;
  - 改后:无订阅者约 0.02 µs/对(批量 1,048,576 对共约 18 ms,远高于计时器分辨率),
    订阅者 1.4–1.5 ms/对,剩下的全是构造一百万个 int 的 `vector<int>` 载荷。
  由 `ListBoxMultiSelect.ShiftEndOnAMillionItemsIsOneRangeAndFast` 锁定(一个区间、计数一百万、
  200 次全范围按键 < 200 ms;把默认范围填充改回逐个插入的变异会让计时断言失败),区间类型本身由
  `IndexRangeSet.*` 按合并、拆分、边缘切换、插入/删除平移、收缩截断逐一锁定,每一条都做过变异验证。
- **没有订阅者时不构造集合事件的载荷**:`SelectionSetChanged` 的参数是 `vector<int>`,签名决定了它是
  O(选中项数)。没人订阅时不构造。关心成本的订阅者可以忽略参数,改读 `SelectedRanges()`。
- **修过的 bug:收缩条目数会留下越界的选中项。** `SetItemCount` / `SetItems` 以前只截断活动索引,
  集合和锚点不动:`IsSelected(超出末尾的行)` 仍为 true,再把列表加长,那些行又「复活」成选中状态,
  锚点越界后的 Shift 范围还会从不存在的行开始。现在调用 `Selector::ClampSelectionToCount`,三者一起截断,
  O(log 区间数)。由 `ListBoxMultiSelect.ShrinkingItemCountClampsTheSet` 锁定(变异:只截断活动索引,测试失败)。
- **收缩若删掉了选中项,会触发 `SelectionSetChanged`**:订阅者上一次拿到的载荷里有已不存在的条目,
  必须得到通知。只增长、或截掉的部分没有选中项时不触发。不会像 Ctrl/Shift 手势那样把活动项滚进视口。
  由 `ListBoxMultiSelect.ShrinkThatDropsSelectedItemsRaisesTheSetEvent` 锁定。
- **`SetItems` 整体替换条目时,选中集合按活动索引重置为单项**(与 TreeView 一致),不再沿用旧集合:
  替换后同一个索引已经是另一个条目,沿用会把用户从没选过的新条目显示为选中。集合有变化时同样触发
  `SelectionSetChanged`。由 `ListBoxMultiSelect.ReplacingItemsReseedsTheSetFromTheActiveItem` 锁定。

### 键盘导航

| 按键 | 行为 |
| --- | --- |
| Tab | 进入焦点顺序 |
| ↑ / ↓ | 移动活动项 |
| PageUp / PageDown | 翻页 |
| Home / End | 首 / 尾 |
| Ctrl+Space | 多选模式下切换当前项 |
| Shift+方向键 | 多选模式下扩展选区 |

### 主题 Token

| Token | 用途 |
| --- | --- |
| `accent` | 选中项背景（@ 0.15 alpha）与焦点环（@ 0.7 alpha） |
| `textPrimary` | 行文字（含悬停 @ 0.05 alpha 背景） |

### 演示页

demo 导航 → 数据控件 → ListBox。另见「性能演示 → 虚拟化对比」与「日志追尾」。
