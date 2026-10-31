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

**`DropDownClosed` 在弹出窗口完全关闭之后才触发**，所以处理器里可以重新打开下拉框
（打开不会被拒绝，重新打开的下拉框照样能被轻触关闭）。

⚠️ **不要在 `DropDownClosed` 或 `SelectionChanged` 处理器里同步销毁这个 ComboBox**（也不要同步把它
移出树再销毁）。`PopupHost::Close` 扛得住，但 `Event::Raise` 在每个处理器返回后还要读自己的订阅列表；
由列表行点击触发的选择和关闭，调用栈上还有弹出窗口的 `WndProc`、它的 `InputManager` 和被点击的列表。
要让控件消失，推迟到之后（`PostMessage`、下一帧）。同步 detach（例如在处理器里切换 TabControl 页）
是安全的，见「使用注意事项」。

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

弹出窗口生命期、关闭顺序与按键：

- **detach 不再释放正在执行的弹出窗口。** 点击列表行 → `SetSelectedIndex` → `SelectionChanged` →
  应用处理器切换 TabControl 页（或移除这个控件）→ detach，这一串全部发生在弹出窗口的 `WndProc` 和
  列表的指针处理器返回之前。以前 detach 里重置了 `popup_` 和 `listView_`，把调用栈上的 `PopupHost`
  （以及正在处理 PointerPressed 的 `InputManager`）和列表释放掉。现在 detach 只关闭不销毁。
  未验证：需真机。在某个 TabControl 页上放一个 ComboBox，在 `SelectionChanged` 里切到别的页，选一项后
  不应崩溃；切回来再打开下拉框应正常。
- **弹出窗口自己关闭时也报告关闭。** 点在卡片留白上时 `PopupHost` 走自己的轻触关闭。以前那条路径只清掉
  打开标志和解散钩子：`DropDownClosed` 不触发，滚轮转发钩子一直挂着。现在所有关闭路径都经过同一个幂等的
  收尾函数。未验证：需真机。打开下拉框后点卡片边缘的留白，确认 `DropDownClosed` 触发。
- **关闭事件在弹出窗口完全关闭后才触发，重新打开后轻触关闭仍然有效。** 窗口调用解散钩子时用本地副本，只有
  调用期间没人重新注册时才清空槽位。槽位规则由 `PopupDismiss.HookThatReopensKeepsTheNewHook`、
  `PopupDismiss.HandledHookClearsSlotAndRetiresItsToken`、`PopupDismiss.DecliningHookStaysRegistered` 锁定；
  `PopupHost::Close` 的回调顺序需要真实 HWND，未验证：需真机（在 `DropDownClosed` 里重新打开，再点外部应能关闭）。
- **可编辑模式下 Space 和 Enter 不再打开下拉框**（见键盘导航表）。以前输入「new york」时每个空格都弹出列表，
  列表打开后转发的 Space 还会选中悬停行、覆盖已输入的文字。由 `ComboBoxEditable.SpaceIsTextNotADropdownCommand`、
  `ComboBoxEditable.EnterIsLeftToTheSurroundingUi`、`ComboBoxEditable.DownStillOpensTheListWhenEditable`、
  `ComboBoxEditable.ReadonlySpaceAndEnterStillOpenTheList` 锁定。
- **弹出窗口的重绘合并成一次。** 弹出窗口的失效回调（内容根的，以及每次指针移动后 `InputManager` 的）以前每次
  都同步整窗重绘：鼠标划过列表时每个 WM_MOUSEMOVE 至少重绘一次，换悬停行时两次。现在第一次请求向弹出窗口投递
  一条消息，之后的请求被吸收，一批输入只画一次。合并规则由 `PopupRenderCoalescer.BurstOfRequestsPostsOnce`、
  `PopupRenderCoalescer.RequestAfterRenderPostsAgain`、`PopupRenderCoalescer.SynchronousRenderCoversQueuedRequest`
  锁定；实际帧成本未验证：需真机，在下拉列表上快速移动鼠标看 F12 HUD 的 CPU Max。

### 性能特征

- **头部 Render**: 一个圆角矩形 + 文本布局 + V 形箭头字形。
- **弹出列表滚动**：虚拟化由 `ComboListView`（内部类）处理 —— 只渲染可见项。
- **无 idle tick**：头部静态、插入符闪烁走 `WantsBlink`，不在无输入时 tick。

### 键盘导航

| 键 | 动作 |
| --- | --- |
| `Space` / `Enter` / `Down` / `Alt+Down` | 打开下拉框（只读模式） |
| `Esc` | 关闭下拉框 |
| `Up` / `Down` | 弹出打开时：导航列表项 |
| `Home` / `End` | 弹出打开时：首项 / 末项 |
| **可编辑模式** |  |
| `Backspace` | 删除插入符前字符 |
| `Delete` | 删除插入符后字符 |
| `Left` / `Right` | 移动插入符 |
| `Home` / `End` | 跳到文本开头 / 结尾 |
| 可打印字符 | 插入到插入符位置 |
| `Space` | 当作字符插入，不打开下拉框；下拉框打开时也不会选中悬停项 |
| `Enter` | 下拉框关闭时不处理（留给外层，例如对话框默认按钮）；打开时选中高亮项 |
| `Down` / `Alt+Down` | 打开下拉框 |

### 使用注意事项

- **弹出窗口是独立顶层 HWND**：它不参与父窗口的 Z-order，可以溢出父窗口边界。
- **detach 关闭弹出窗口但不销毁它**：弹出窗口和列表保留到 `~ComboBox`，窗口钩子随关闭释放。
  重新 attach 到同一个窗口时直接复用；attach 到另一个窗口时重建（HWND 的所有者在创建时固定）。
  保留的代价是一个隐藏的 HWND。
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

## UI Automation

ComboBox 的 peer：

| 项 | 值 |
|---|---|
| ControlType | ComboBox |
| 模式 | ExpandCollapse（新的公开 `IsDropDownOpen` / `SetDropDownOpen`）、Selection、Value（显示的文本；可编辑时可写）。下拉项在关闭时也存在（WPF 方式），此时为 offscreen |
| AutomationId | `SetAutomationId` |
| HelpText | `Tooltip()` |

可编辑时 Value 的 `SetValue` 走新的公开 `ReplaceTextAsUserEdit`（等同用户输入）并触发 `TextChanged`，而 `SetText` 是程序设置，不触发。表单填充工具改的值因此会进入应用的数据模型。

惰性创建：只有 UIA 客户端访问到这个控件时才创建 peer，没有客户端时零开销。模式操作经投递消息执行，执行前重新检查控件仍在树上且已启用。属性变化只在真正变化且有客户端监听时发送。自定义方式见 inheritance-guide.md 的“UI Automation”一节。

验证：无头测试 `tests/unit/AutomationControlPeerTests.cpp` / `AutomationSelectorPeerTests.cpp`，真实 UIA 客户端自测 `FluentUITests.exe --uia-smoke`；14 处变异（如把 UIA 操作换回不发事件的 setter、去掉项缓存上限）全部被测试抓到。

## 相关控件

- [MenuFlyout](MenuFlyout.md) — 同样的弹出基础设施，但用于上下文菜单
- [ListBox](ListBox.md) — 始终展开的列表
