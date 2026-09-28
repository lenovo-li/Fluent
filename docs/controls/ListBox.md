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
std::vector<int> SelectedIndices() const;
bool IsSelected(int i) const;

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
| `accent` | 选中项背景（@ 0.15 alpha）与焦点环（@ 0.7 alpha，`.cpp:462`） |
| `textPrimary` | 行文字（含悬停 @ 0.05 alpha 背景，`.cpp:427-443`） |

### 演示页

demo 导航 → 数据控件 → ListBox。另见「性能演示 → 虚拟化对比」与「日志追尾」。
