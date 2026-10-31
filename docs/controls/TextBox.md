# TextBox 控件

## 概述

单行文本输入框:插入符、选区、剪贴板、撤销/重做、输入法(IME)组字。多行编辑请用
`TextArea`,密码输入请用 [`PasswordBox`](PasswordBox.md)(密码模式的 `TextBox` 加一个显示按钮)。

**典型使用场景**:
- 表单字段(姓名、股票代码、搜索关键词)
- 单行参数输入
- 需要输入校验或字符过滤的场合

---

## 继承层次

```
Visual
  └─ UIElement
      └─ FrameworkElement
          └─ Control
              └─ TextEditBase
                  └─ TextBox
```

`TextEditBase` 持有全部编辑机制(文本缓冲、插入符、选区、撤销栈、IME、输入策略);
`TextBox` 只加单行布局与可选的密码遮蔽。`TextArea` 是它的同门兄弟。

**命名空间**: `fluent`
**头文件**: `FluentUI/controls/TextBox.h`

---

## 公开 API

### 属性(继承自 `TextEditBase`)

| 成员 | 说明 |
| --- | --- |
| `SetText(const std::wstring&)` / `Text()` | 内容 |
| `SetPlaceholder(std::wstring)` | 空且未聚焦时显示的提示文字 |
| `SetReadOnly(bool)` | 只读(仍可聚焦与选择复制) |
| `SetMaxLength(size_t)` | 最大字符数 |
| `SetCharacterCasing(CharacterCasing)` | 自动转大写/小写 |
| `SetInputFilter(std::function<bool(wchar_t)>)` | 逐字符准入回调 |

`SetInputFilter` 是**唯一**用 `std::function` 的地方(事件系统刻意不用),因为它不是事件
而是一个策略对象,需要携带调用者的状态。

### TextBox 自己的属性

```cpp
void SetPassword(bool on);
```

密码模式:文字画成圆点,复制 / 剪切被拒绝(快捷键和右键菜单都是),获得焦点期间关闭输入法
(与 WinUI `PasswordBox` 一致)。聚焦时切换也立即生效。需要显示按钮(眼睛)时用
[`PasswordBox`](PasswordBox.md)。

给子类的 protected 钩子(`PasswordBox` 用的就是这两个):

- `virtual float ReservedRightDip() const` —— 右侧给框内按钮预留的宽度,滚动钳制、插入符
  可见性和文字裁剪都会扣掉它。值随状态变化时,变化后调一次 `EnsureCaretVisible()`。
- `SetMaskRevealed(bool)` —— 不离开密码模式地显示明文。剪贴板与输入法仍按密码模式拒绝。

### 事件

#### TextChanged

```cpp
Event<TextEditBase, std::wstring>& TextChanged();
```

⚠️ **Sender 类型是 `TextEditBase`,不是 `TextBox`。** 事件定义在基类上,所以处理器签名要写
基类:

```cpp
sub_ = input->TextChanged().Subscribe(this,
    [](void* owner, TextEditBase& src, std::wstring& text) {   // TextEditBase&
        static_cast<MyPage*>(owner)->OnQueryChanged(text);
        (void)src;
    });
```

**只有用户输入会触发**,程序调用 `SetText` 不触发 —— 这个区分让「回填表单」不会触发一轮
校验副作用。

#### SelectionChanged

```cpp
Event<TextEditBase, SelectionChangedArgs>& SelectionChanged();
```

⚠️ 两个事件返回的 `Subscription` 都必须持有(RAII,丢掉即退订)。

---

## 代码示例

### 最小示例

```cpp
#include "FluentUI/controls/TextBox.h"

auto* input = panel->Add(std::make_unique<TextBox>());
input->SetPlaceholder(L"例如 600519");
```

### 只允许数字

```cpp
// InputFilter 是策略而非事件，所以它是 std::function：需要捕获调用者的状态
input->SetInputFilter([](wchar_t c) { return c >= L'0' && c <= L'9'; });
input->SetMaxLength(6);
```

### 表单一行(标签 + 输入 + 按钮)

```cpp
auto* form = card->Add(std::make_unique<Grid>());
form->AddRow(GridLength::Auto());              // Auto：不要写死行高
form->AddColumn(GridLength::Pixels(96.0f));    // 标签
form->AddColumn(GridLength::Star(1.0f));       // 输入框吃掉剩余宽度
form->AddColumn(GridLength::Auto());           // 按钮按内容收缩
```

---

## UI Automation

TextBox / TextArea 的 peer（`automation/ControlPeers.h`）：

| 项 | 值 |
|---|---|
| ControlType | Edit |
| 模式 | Value（`ReadOnly()` 时只读）。`SetValue` 走 `ReplaceAllAsUserEdit`（等同用户全选后输入）：套用大小写/InputFilter/MaxLength，记一步撤销，并触发 `TextChanged`，所以表单填充工具和测试工具改的值会进入应用的数据模型。TextArea 不发 ValueChanged（长文本属于 TextPattern，免得每次按键复制整篇文本）；单行超过 4096 字符时也不比较 |
| Name 来源 | `SetAutomationName`（没有自带标题） |
| AutomationId | `SetAutomationId` |
| HelpText | `Tooltip()` |

惰性创建：只有 UIA 客户端（讲述人、Inspect.exe、测试工具）访问到这个控件时才创建 peer，没有客户端时零开销。模式操作（Invoke/Toggle/Select/SetValue）经投递消息执行，执行前重新检查控件仍在树上且已启用，所以处理函数里删除控件自身是安全的。属性变化事件（Name、IsEnabled、ToggleState、Value）在控件失效后合并检查，只有真正变化且有客户端监听时才发送。自定义方式见 inheritance-guide.md 的“UI Automation”一节。

验证：`tests/unit/AutomationPeerTests.cpp`、`AutomationPeerLifetimeTests.cpp`（无头，用记录调用的假 UIA core），10 处变异全部被测试抓到；真实客户端下的表现需在真机用 Inspect.exe 检查。

## 备注

### 性能特征

- **文本布局有缓存**:`TextBoxLayoutCache` 挡住重复的 DirectWrite 布局构建。文本、宽度、
  排版参数都不变时不会每帧重建。
- **IME 组字期间插入符位置由同一份布局计算**,所以候选窗定位和绘制不会不一致(有测试
  钉住:`CaretRectMatchesDrawnCaretWhileComposing`)。
- **撤销栈按操作分组**,不是逐字符 —— 连续输入算一次撤销。

### 修过的 bug:右键菜单的「剪切」可撤销

右键菜单的「剪切」以前用的是不进撤销栈的删除,而 Ctrl+X 用的是记录删除。菜单剪切之后,撤销栈描述的
还是剪切前的缓冲区:Ctrl+Z 按错位的偏移重放上一条记录,删掉错误的文字,被剪切的文字永远回不来。
现在两者走同一个记录删除(在 `TextEditBase` 里,TextArea 同样受益;密码模式本来就禁用剪切)。
由 `TextEditUndo.ContextMenuCutIsUndoable` 锁定(它调用右键菜单真正的菜单项处理器)。

### 使用注意事项

- **默认 `HAlign::Stretch`**,在表单里通常正是想要的(填满那一列)。
- ⚠️ **`SetForeground` / `SetBackground` 在这里是字面文字色与底色**(不像 toggle 家族那样
  指记号色)。禁用态**不会**淡化显式设过的值 —— 与 `Button` 同一条优先级规则:显式值胜过
  状态机。所以自定义了配色就要自己保证两种主题、两种状态下的对比度。
- **单行**:换行符会被 `SanitizeInput` 清掉。需要多行用 `TextArea`。
- 焦点态边框比常态粗(见主题 token),这部分画在 `bounds_` 内,不需要外扩。

### 键盘导航

| 按键 | 行为 |
| --- | --- |
| Tab | 进入焦点顺序 |
| ← / → / Home / End | 移动插入符;配合 Shift 选择 |
| Ctrl+A / C / X / V / Z / Y | 全选 / 复制 / 剪切 / 粘贴 / 撤销 / 重做 |

### 主题 Token

| Token | 用途 |
| --- | --- |
| `controlFillDefault` | 输入区底色 |
| `controlStrokeDefault` | 边框 |
| `accent` | 聚焦态下边框(强调线) |
| `textPrimary` | 文字与插入符 |
| `textSecondary` | 占位符 |

### 演示页

demo 导航 → 输入控件 → TextBox(nav 201)。原「样式定制大全」里的 TextBox 完整演示已并入
此页。
