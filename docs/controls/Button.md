# Button 控件

## 概述

标准按钮控件，提供可点击的交互元素。支持三种视觉风格（Standard/Accent/Subtle），包含悬停/按下状态动画、键盘激活（Space/Enter）、焦点环显示，以及可选的 Flyout 弹出菜单集成。

**典型使用场景**:
- 表单提交/取消按钮
- 工具栏操作按钮
- 带下拉菜单的操作按钮（通过 SetFlyout 集成）

---

## 继承层次

```
Visual
  └─ UIElement
      └─ FrameworkElement
          └─ Control
              └─ ContentControl
                  └─ ButtonBase
                      └─ Button
```

**命名空间**: `fluent`  
**头文件**: `<FluentUI/controls/Button.h>`

---

## 公开 API

### 构造函数

```cpp
Button();
```

默认构造一个 Standard 风格的空白按钮。按钮默认可聚焦、可点击（由 ButtonBase 设置）。

---

### 属性

#### Kind（按钮风格）

```cpp
enum class Kind { Standard, Accent, Subtle };

void SetKind(Kind k);
// 注意：没有 Kind 的 getter。Kind 是「调用者告诉控件要什么外观」的单向输入，
// 控件不提供回读 —— 需要记住当前 Kind 的调用者自己存。
```

**说明**: 控制按钮的视觉风格  
**默认值**: `Kind::Standard`  
**脏标记**: Render

**风格说明**:
- `Standard` — 填充 + 边框（默认样式）
- `Accent` — 主题强调色填充，用于主要操作（例如"确定"按钮）
- `Subtle` — 静止时透明无边框，悬停/按下时才显示填充（CommandBar/ToolBar 按钮样式）

#### Text（按钮文本）

```cpp
void SetText(const std::wstring& text);  // 继承自 ContentControl
```

**说明**: 设置按钮显示的文本标签  
**脏标记**: Measure（文本长度影响自然尺寸）

#### Flyout（关联弹出菜单）

```cpp
void SetFlyout(IContextMenu* flyout);
IContextMenu* Flyout() const;
```

**说明**: 关联一个弹出菜单。设置后，点击按钮会在按钮下方打开 flyout，而不是触发 Click 事件。  
**所有权**: Button 持有非拥有指针，调用方需保持 flyout 对象存活。  
**默认值**: `nullptr`（无关联 flyout）

---

### 事件

#### Click

```cpp
Event<Button, RoutedEventArgs>& Click();
```

**参数**: `RoutedEventArgs`
- `Handled` — 设置为 true 可阻止事件继续冒泡

**说明**: 按钮被激活时触发（鼠标点击、Space 键、Enter 键）。如果按钮已通过 `SetFlyout` 关联菜单，则点击时打开菜单而不触发此事件。

**订阅示例**:
```cpp
// handler 是普通函数指针 void(*)(void* owner, Button&, RoutedEventArgs&)，
// 不是捕获 lambda —— 需要状态就放在 owner 对象上（Event.h 刻意不支持捕获
// lambda，那会破坏零分配设计）。
//
// Subscribe 返回的 Subscription 必须保存：它是 RAII 句柄，析构即取消订阅。
// 丢弃返回值 = 回调立刻失效、点击没反应。Subscribe 标了 [[nodiscard]]，
// 忘记保存会得到编译警告。
clickSub_ = button->Click().Subscribe(this, [](void* owner, Button& sender, RoutedEventArgs& args) {
    auto* self = static_cast<MyPage*>(owner);
    self->OnButtonClicked();
});

// 成员函数写法（同样零分配，推荐）：
clickSub_ = button->Click().Subscribe<&MyPage::OnSave>(this);
// void MyPage::OnSave(Button&, RoutedEventArgs&) { ... }
```

---

## 代码示例

### 最小示例

```cpp
#include <FluentUI/FluentUI.h>

using namespace fluent;

int main() {
    Application app;

    auto window = std::make_unique<Window>();
    window->SetTitle(L"Button Example");
    // 窗口尺寸不是一个 setter：它通过 Show() 的 WindowState 传入
    // （见 FluentUIDemo/main.cpp）。这里省略，只关注 Button 本身。

    auto button = std::make_unique<Button>();
    button->SetText(L"Click Me");
    // Subscription 必须活到不再需要回调为止。这里存成局部变量，它的作用域覆盖
    // app.Run()，所以整个消息循环期间订阅有效；写成 button->Click().Subscribe(...)
    // 丢弃返回值，回调会在这一行结束时就被取消。
    Subscription clickSub = button->Click().Subscribe(
        nullptr, [](void*, Button& sender, RoutedEventArgs&) {
            sender.SetText(L"Clicked!");
        });

    window->SetRoot(std::move(button));
    window->Show();

    return app.Run();
}
```

### 三种风格对比

```cpp
auto stack = std::make_unique<StackPanel>();
stack->SetOrientation(Orientation::Vertical);
stack->SetSpacing(10.0f);

auto standardBtn = std::make_unique<Button>();
standardBtn->SetText(L"Standard Button");
standardBtn->SetKind(Button::Kind::Standard);
stack->Add(std::move(standardBtn));

auto accentBtn = std::make_unique<Button>();
accentBtn->SetText(L"Accent Button");
accentBtn->SetKind(Button::Kind::Accent);
stack->Add(std::move(accentBtn));

auto subtleBtn = std::make_unique<Button>();
subtleBtn->SetText(L"Subtle Button");
subtleBtn->SetKind(Button::Kind::Subtle);
stack->Add(std::move(subtleBtn));

window->SetRoot(std::move(stack));
```

### 带 Flyout 的按钮

```cpp
auto button = std::make_unique<Button>();
button->SetText(L"Options ▾");

// MenuFlyout 一次性接收整个 items 向量，没有逐条 AddItem —— 菜单结构是
// 声明式的，子菜单靠 MenuItem::submenu 嵌套，分隔符用 MenuItem::Sep()。
auto menu = std::make_shared<MenuFlyout>();
menu->SetItems({
    MenuItem{L"Open"},
    MenuItem{L"Save"},
    MenuItem::Sep(),
    MenuItem{L"Exit"},
});

button->SetFlyout(menu.get());  // 点击按钮打开菜单

auto panel = std::make_unique<StackPanel>();
panel->Add(std::move(button));
// menu 用 shared_ptr 持有（SetFlyout 存的是裸指针，不能让它悬空）；保持 menu 存活

window->SetRoot(std::move(panel));
```

### 对话框按钮组

```cpp
auto buttonPanel = std::make_unique<StackPanel>();
buttonPanel->SetOrientation(Orientation::Horizontal);
buttonPanel->SetSpacing(8.0f);
buttonPanel->SetHAlign(HAlign::Right);

// 对话框把两个 Subscription 存成自己的成员（okSub_ / cancelSub_），
// 于是对话框销毁时订阅自动注销 —— 这正是 RAII 想要的效果。
auto okBtn = std::make_unique<Button>();
okBtn->SetText(L"确定");
okBtn->SetKind(Button::Kind::Accent);  // 主要操作用 Accent
okSub_ = okBtn->Click().Subscribe(this, [](void* owner, Button&, RoutedEventArgs&) {
    auto* dlg = static_cast<MyDialog*>(owner);
    dlg->Submit();       // 提交表单
    dlg->CloseWindow();
});

auto cancelBtn = std::make_unique<Button>();
cancelBtn->SetText(L"取消");
cancelBtn->SetKind(Button::Kind::Standard);
cancelSub_ = cancelBtn->Click().Subscribe(this, [](void* owner, Button&, RoutedEventArgs&) {
    static_cast<MyDialog*>(owner)->CloseWindow();
});

buttonPanel->Add(std::move(okBtn));
buttonPanel->Add(std::move(cancelBtn));
```

---

## 备注

### 性能特征

- **渲染**: 按钮本体和悬停/按下高亮都绘制到窗口内容表面（不创建独立 DComp surface）
- **动画**: 高亮色调在 UI 线程通过 `OnAnimationTick` 淡入淡出（小控件不拥有独立 surface 的设计决策）
- **Idle 成本**: 静止时 `WantsAnimationTick()` 返回 false，零 CPU 开销
- **脏标记**: 
  - `SetKind` 仅触发 Render（只改变颜色，不影响布局）
  - `SetText` 触发 Measure（文本长度改变自然尺寸）

### 自然尺寸

- **高度**: 32 DIP（WinUI 默认值）
- **宽度**: 文本宽度 + 水平 padding（自动计算）

若不重写 `Measure`，Button 会继承 FrameworkElement 的默认行为（报告可用尺寸），在无约束轴（infinity）中会坍缩为零而消失。

### 视觉溢出

焦点环绘制在 `bounds_` 外侧（`VisualOverflowDip()` 返回 3.75 DIP），确保：
- `CollectDirtyBounds` 覆盖焦点环像素
- 部分重绘时不出现残影

### 键盘导航

| 按键 | 行为 |
|------|------|
| Tab | 移动焦点到按钮/离开按钮 |
| Space | 激活按钮（触发 Click 或打开 Flyout） |
| Enter | 激活按钮（触发 Click 或打开 Flyout） |
| Esc | 当 Flyout 打开时，关闭 Flyout |

### 主题 Token

按钮使用以下主题 token（见 `styling/ThemeTokens.h`）：

**Standard 风格**:
- `controlFill` — 静止填充色
- `controlStroke` — 边框色
- `controlFillHover` — 悬停填充色
- `controlFillPressed` — 按下填充色

**Accent 风格**:
- `accent` — 静止填充色（主题强调色）
- `accentHover` — 悬停填充色
- `accentPressed` — 按下填充色

**Subtle 风格**:
- 静止时透明（alpha 0）
- 悬停/按下时使用 `controlFillHover` / `controlFillPressed`

**文本色**:
- `onAccent` — Accent 按钮的文本色（确保对比度）
- `textPrimary` — Standard/Subtle 按钮的文本色

### 使用注意事项

1. **Accent 按钮应节制使用**: 一个对话框/页面通常只有一个 Accent 按钮（主要操作）
2. **Flyout 所有权**: `SetFlyout` 接受非拥有指针，确保 flyout 对象在按钮生命周期内存活
3. **焦点可见性**: 按钮默认显示焦点环，通过 Tab 导航时自动显示
4. **禁用状态**: 调用 `SetEnabled(false)` 后，按钮变灰且不响应交互（由 UIElement 提供）

---

## 相关控件

- [RepeatButton](RepeatButton.md) — 按住时重复触发的按钮（用于滚动条、数字微调器）
- [ToggleButton](ToggleButton.md) — 开/关状态按钮（用于工具栏的按下态按钮）
- [MenuFlyout](MenuFlyout.md) — 可通过 `SetFlyout` 关联到按钮的弹出菜单
- [ButtonBase](../primitives/ButtonBase.md) — 所有按钮类控件的基类

---

## 另见

- [元素模型概述](../ELEMENT_MODEL.md)
- [主题定制指南](../THEMING_GUIDE.md)
- [性能最佳实践](../PERFORMANCE_BEST_PRACTICES.md)
