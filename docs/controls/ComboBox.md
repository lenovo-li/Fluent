# ComboBox 控件

## 概述

Fluent 下拉列表选择器。头部（绘制在父窗口）是一个圆角按钮，显示当前选中项文本加一个
V 形箭头字形。点击或按下 Space/Enter/Alt+Down 打开一个弹出窗口，内含可滚动的项目列表。
弹出窗口是独立的顶层 HWND（`PopupHost`），位于头部下方（或屏幕底部附近时改为上方），
通过点击外部、选择项目或按 Esc 关闭。

**可编辑模式**（`SetEditable(true)`）让头部接受输入文字而非只显示选中项，用户可以输入
列表中不存在的值（经典的「combo」行为：自由文本框上叠加一个建议列表）。

**典型使用场景**:
- 单选下拉列表（语言、主题、排序方式）
- 可编辑的选择框（建议列表 + 自由输入）
- 有限选项中的一次性选择

---

## 继承层次

```
Visual
  └─ UIElement
      └─ FrameworkElement
          └─ Control
              └─ ItemsControl<std::wstring>
                  └─ Selector<std::wstring>
                      └─ ComboBox   (final)
```

`Selector<std::wstring>`：项目向量与选中索引存储（+ 钳制/去重）
在 `Selector`/`ItemsControl` 基类里。早期的双重存储（`ComboBox::items_` 镜像到
`ComboListView::items_`）已消除 —— 列表视图现在持有共享 `items_` 向量的指针。

**命名空间**: `fluent`
**头文件**: `FluentUI/controls/ComboBox.h`

---

## 公开 API

### 构造函数

```cpp
ComboBox();
```

默认无项目、无选择（索引 -1）、不可编辑。

### 项目与选择

```cpp
void SetItems(std::vector<std::wstring> items);
std::vector<std::wstring>& Items();
const std::vector<std::wstring>& Items() const;

void SetSelectedIndex(int index);
int SelectedIndex() const;
```

**`SetItems`** 替换当前列表（继承自 `ItemsControl`）。`SetSelectedIndex` 会钳制并触发
`SelectionChanged` 事件（覆盖自 `Selector`）。

索引 -1 表示无选择；有效索引 `[0, items.size())`。

### 可编辑模式

```cpp
void SetEditable(bool editable);
bool IsEditable() const;

std::wstring Text() const;              // 可编辑时是输入文本，否则是选中项文本
void SetText(std::wstring text);        // 替换输入文本（仅可编辑模式）
UINT32 CaretIndex() const;              // 插入符在输入文本中的位置
```

**可编辑模式下的文本由 ComboBox 自己绘制和编辑**，而非嵌入一个 TextBox —— 嵌入会引入
第二个可聚焦元素（Tab 会停在文本框和组合框两次）、加上自己的 attach 生命周期、插入符
闪烁注册和上下文菜单，这些对于单行无选择的场景太重了。实际需要的只是插入符索引和
Backspace/Delete/方向键，足够小到直接拥有。

**刻意不做过滤**：输入不会自动选择匹配项、不会缩窄列表。应用想要哪种（前缀 vs 子串、
大小写、是否自动提交）是策略决定 —— `TextChanged` 把文本交给应用，应用自己调 `SetItems`。

**`SetText` 不触发 `TextChanged`**（程序设置不是用户输入）；也不触碰选择。

**`CaretIndex()` 暴露**因为每个编辑键都由它对插入符的操作定义，而测试看不到绘制的插入符。

### 弹出窗口

```cpp
void SetPopupOpacity(float opacity);    // [0..1]，默认 1.0（不透明）
float PopupOpacity() const;
```

**attach 之前调用安全**：值被存储并在 `OnAttachedToTree` 创建弹出窗口时应用。以前是
直接转发给 `popup_`，后者还是 null 时什么都不做 —— 常见的「构建页面→配置→attach」
顺序会静默丢失设置（而且静默，因为没有弹出窗口来投诉）。模仿 MenuFlyout 保持
`cardOpacity_` 并重新应用到它创建的每个弹出级别的方式。

### 事件

#### SelectionChanged

```cpp
Event<ComboBox, int>& SelectionChanged();
```

**触发时机**: 选择改变（用户选择项目）；载荷是新索引。替换 `SetOnChange(std::function<void(int)>)`。

```cpp
sub_ = combo->SelectionChanged().Subscribe(this,
    [](void* owner, ComboBox&, int& newIndex) {
        static_cast<MyPage*>(owner)->OnLanguageChanged(newIndex);
    });
```

#### DropDownOpened / DropDownClosed

```cpp
Event<ComboBox, RoutedEventArgs>& DropDownOpened();
Event<ComboBox, RoutedEventArgs>& DropDownClosed();
```

**触发时机**: 弹出窗口打开后 / 关闭后，**任何路径**都触发 —— 点击、键盘、项目选择、
轻触关闭或 detach 导致的关闭。两者每次转换时恰好触发一次：`OpenPopup` 和 `ClosePopup`
在弹出窗口已处于请求状态时提前返回，所以第二次点击不会重新触发。

**`DropDownOpened` 在弹出窗口已经起来之后触发**，所以处理器调用 `SetItems` 仍然赶在
第一次绘制之前。这是延迟加载的挂钩点（只在用户真正要看时填充列表）。

```cpp
sub_ = combo->DropDownOpened().Subscribe(this,
    [](void* owner, ComboBox&, RoutedEventArgs&) {
        // 延迟加载：只在用户打开下拉框时填充（可能是网络请求）
        static_cast<MyPage*>(owner)->LoadComboItems();
    });
```

#### TextChanged

```cpp
Event<ComboBox, std::wstring>& TextChanged();
```

**触发时机**: 每次用户驱动的文本改变（输入、Backspace、Delete）。

**不由 `SetText` 触发**（程序设置不算）。

**不由选择列表项触发** —— 那会触发 `SelectionChanged`，同时监听两者的处理器不会看到
同一个用户动作两次。

```cpp
sub_ = combo->TextChanged().Subscribe(this,
    [](void* owner, ComboBox& cb, std::wstring& text) {
        // 过滤列表项：前缀匹配
        auto* self = static_cast<MyPage*>(owner);
        self->FilterComboItems(text);
    });
```

⚠️ **所有返回的 `Subscription` 必须持有**（RAII，丢掉即退订）。

---

## 代码示例

### 最小示例（只读下拉）

```cpp
#include "FluentUI/controls/ComboBox.h"

auto* combo = panel->Add(std::make_unique<ComboBox>());
combo->SetItems({L"简体中文", L"English", L"日本語"});
combo->SetSelectedIndex(0);
combo->SetWidth(200.0f);

sub_ = combo->SelectionChanged().Subscribe(this,
    [](void* owner, ComboBox& cb, int& idx) {
        // 用户选择了新语言
        static_cast<MyApp*>(owner)->SwitchLanguage(idx);
    });
```

### 可编辑组合框（自由输入 + 建议）

```cpp
auto* combo = panel->Add(std::make_unique<ComboBox>());
combo->SetEditable(true);
combo->SetItems({L"apple", L"banana", L"cherry", L"date"});
combo->SetWidth(200.0f);

// 输入时过滤列表（前缀匹配示例）
sub_ = combo->TextChanged().Subscribe(this,
    [](void* owner, ComboBox& cb, std::wstring& text) {
        auto* self = static_cast<MyPage*>(owner);
        auto filtered = self->FilterPrefix(self->allFruits_, text);
        cb.SetItems(std::move(filtered));
    });
```

### 延迟加载（只在打开时填充）

```cpp
auto* combo = panel->Add(std::make_unique<ComboBox>());
combo->SetWidth(200.0f);

sub_ = combo->DropDownOpened().Subscribe(this,
    [](void* owner, ComboBox& cb, RoutedEventArgs&) {
        // 首次打开才加载（可能是网络请求或数据库查询）
        auto* self = static_cast<MyPage*>(owner);
        if (!self->itemsLoaded_) {
            cb.SetItems(self->FetchRemoteList());
            self->itemsLoaded_ = true;
        }
    });
```

### 半透明弹出卡片

```cpp
combo->SetPopupOpacity(0.95f);  // 5% 透明
```

---

## 备注

### 修过的 bug（值得知道）

**可编辑模式下从下拉选一项，显示不更新。** 可编辑时 `HeaderText()` 返回输入缓冲，而选择
变化只改了 `selectedIndex_`，从没写回缓冲 —— 于是选择确实生效了，但框里还是你打的字，
看起来像点了没反应。

现在 `OnSelectionChanged` 会在可编辑模式下把选中项写入缓冲并把光标移到末尾。两个边界是
刻意的：清空选择(-1)**不**清掉正在键入的文字（「没选东西」不等于「选了空字符串」）；
键入仍然覆盖文字且不动选择。

### 性能特征

- **头部 Render**: 一个圆角矩形 + 文本布局 + V 形箭头字形。
- **弹出列表滚动**：虚拟化由 `ComboListView`（内部类）处理 —— 只渲染可见项。
- **无 idle tick**：头部静态、插入符闪烁走 `WantsBlink`，不在无输入时 tick。

### 键盘导航

| 键 | 动作 |
| --- | --- |
| `Space` / `Enter` / `Alt+Down` | 打开下拉框 |
| `Esc` | 关闭下拉框 |
| `Up` / `Down` | 弹出打开时：导航列表项 |
| `Home` / `End` | 弹出打开时：首项 / 末项 |
| **可编辑模式** |  |
| `Backspace` | 删除插入符前字符 |
| `Delete` | 删除插入符后字符 |
| `Left` / `Right` | 移动插入符 |
| `Home` / `End` | 跳到文本开头 / 结尾 |
| 可打印字符 | 插入到插入符位置 |

### 使用注意事项

- **弹出窗口是独立顶层 HWND**：它不参与父窗口的 Z-order，可以溢出父窗口边界。
- **detach 会关闭弹出窗口**：从树上移除组合框时弹出窗口立即销毁，订阅也随之清理。
- **项目改变时选择重新钳制**：`SetItems` 后 `SelectedIndex` 保持不变（如果仍合法），
  或钳制到 `items.size() - 1`。超出范围时触发 `SelectionChanged`。
- **可编辑模式不自动选择匹配项**：应用想要自动完成请自己在 `TextChanged` 里做。

### 轻触关闭（Light Dismiss）

点击弹出窗口外部、父窗口失去激活、父窗口移动、或按 Esc 都会关闭弹出窗口。组合框注册
到 `WindowServices` 的激活弹出关闭列表，生命周期由 `dismissSub_` 管理（RAII）。

### 主题 Token

| Token | 用途 |
| --- | --- |
| `controlFillDefault` | 头部默认填充 |
| `controlFillHover` | 悬停填充 |
| `controlFillPressed` | 按下填充 |
| `controlStrokeDefault` | 头部描边 |
| `textPrimary` | 项目文本 |
| `accent` | 悬停项高亮 |
| `cardFill` | 弹出卡片背景 |

可以通过 `Control` 的属性层（`SetBackground` / `SetForeground`）覆盖单个组合框的颜色，
但弹出列表的主题是全局的（弹出窗口独立，不继承父控件属性）。

### 演示页

demo 导航 → 基础输入 → ComboBox。包含只读、可编辑、多选项和事件演示。

---

## 相关控件

- [MenuFlyout](MenuFlyout.md) — 同样的弹出基础设施，但用于上下文菜单
- [ListBox](ListBox.md) — 始终展开的列表
