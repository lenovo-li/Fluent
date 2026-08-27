# ComboBox 控件

## 概述

下拉选择框,两种模式:

- **只读(默认)**:点击展开列表,选中一项。显示的文字就是选中项。
- **可编辑**:除了下拉选择,还可以直接键入 —— 用于「从建议里挑,也允许自己填」的场景。

选项超过五六个时比一组 `RadioButton` 更合适;选项很少且需要全部可见时 RadioButton 更好。

---

## 继承层次

```
Visual
  └─ UIElement
      └─ FrameworkElement
          └─ Control
              └─ ItemsControl<std::wstring>
                  └─ Selector<std::wstring>
                      └─ ComboBox
```

`ItemsControl` 持有条目集合,`Selector` 持有「哪一项被选中」与 `OnSelectionChanged` 钩子。
`ListBox` 是同门兄弟。

**命名空间**: `fluent`
**头文件**: `FluentUI/controls/ComboBox.h`

---

## 公开 API

### 条目与选择(继承)

```cpp
void SetItems(std::vector<std::wstring> items);
void SetSelectedIndex(int index);      // ComboBox 覆写了它
int  SelectedIndex() const;
const std::wstring* SelectedItem() const;   // 无选中时返回 nullptr
```

⚠️ **没有逐条 `AddItem`** —— 一次性给整个向量。

### 可编辑模式

```cpp
void SetEditable(bool editable);
bool IsEditable() const;
void SetText(std::wstring text);       // 只在可编辑模式下有效
std::wstring Text() const;
UINT32 CaretIndex() const;
```

`Text()` 的语义**随模式变化**,这是本控件最容易踩的地方:

| 模式 | `Text()` 返回 |
| --- | --- |
| 只读 | 选中项的文字(无选中时为空) |
| 可编辑 | 输入缓冲的内容 |

**切换模式的行为**:
- `SetEditable(true)` 会用当前选中项**种子**输入缓冲,所以打开编辑不会把已有值清空。
- `SetEditable(false)` **丢弃**已键入的文字 —— 值重新由选中项决定,留着一个陈旧缓冲会让
  `Text()` 和实际绘制的内容不一致。

⚠️ **`SetText` 在只读模式下是空操作**(直接 return)。

### 事件

```cpp
Event<ComboBox, int>& SelectionChanged();          // Args: 新的索引
Event<ComboBox, std::wstring>& TextChanged();      // Args: 完整文本
```

`TextChanged` 只在**用户键入**时触发;程序调用 `SetText` 不触发。

⚠️ 返回的 `Subscription` 必须持有(RAII,丢掉即退订)。

---

## 代码示例

### 只读

```cpp
#include "FluentUI/controls/ComboBox.h"

auto* combo = panel->Add(std::make_unique<ComboBox>());
combo->SetItems({L"Balanced", L"Performance", L"Power saver"});
combo->SetSelectedIndex(0);

sub_ = combo->SelectionChanged().Subscribe(this,
    [](void* owner, ComboBox&, int& index) {       // 注意 int&
        static_cast<MyPage*>(owner)->ApplyPowerMode(index);
    });
```

### 可编辑

```cpp
combo->SetEditable(true);
combo->SetItems({L"Shenzhen", L"Shanghai", L"Beijing"});

// 键入时过滤建议列表
sub_ = combo->TextChanged().Subscribe(this,
    [](void* owner, ComboBox& cb, std::wstring& text) {
        static_cast<MyPage*>(owner)->FilterSuggestions(cb, text);
    });
```

---

## 备注

### 修过的 bug(值得知道)

**可编辑模式下从下拉选一项,显示不更新。** 可编辑时 `HeaderText()` 返回输入缓冲,而选择
变化只改了 `selectedIndex_`,从没写回缓冲 —— 于是选择确实生效了,但框里还是你打的字,
看起来像点了没反应。

现在 `OnSelectionChanged` 会在可编辑模式下把选中项写入缓冲并把光标移到末尾。两个边界是
刻意的:清空选择(-1)**不**清掉正在键入的文字(「没选东西」不等于「选了空字符串」);
键入仍然覆盖文字且不动选择。

### 性能特征

- **弹出层是独立的 popup 窗口**,不在主窗口的视觉树里 —— 所以它能超出主窗口边界。
  代价是树遍历工具(如自我调试页的 Inspector)走不到它。
- 列表项使用 `ListBox` 的虚拟化路径。

### 使用注意事项

- **默认 `HAlign::Stretch`**,在表单里通常正是想要的。
- **`Text()` 的语义随模式变化**,见上表。
- 只读模式下 `SetText` 无效 —— 要改显示内容请改选中项。

### 键盘导航

| 按键 | 行为 |
| --- | --- |
| Tab | 进入焦点顺序 |
| ↑ / ↓ | 移动选中项(弹出层未开时也有效) |
| Enter / Space | 展开 / 确认 |
| Esc | 关闭弹出层 |
| ← / → / Home / End | 可编辑模式下移动插入符(优先于下拉导航) |

可编辑模式下编辑键**优先于**下拉导航,所以移动光标时不会误开弹出层。

### 主题 Token

| Token | 用途 |
| --- | --- |
| `controlFillDefault` / `controlFillHover` | 头部底色 |
| `controlStrokeDefault` | 边框 |
| `accent` | 聚焦边框、列表选中项 |
| `textPrimary` | 文字与插入符 |
| `layerFill` | 弹出层背景 |

### 演示页

demo 导航 → 输入控件 → ComboBox。
