# Expander 控件

## 概述

可折叠的分组容器，包含一个可点击的标题栏和一个内容区。标题栏点击时切换展开/折叠状态，折叠时内容被 Detach（不仅隐藏），展开动画是 UI 线程高度插值（不走 compositor）。支持 Instant 和 Animate 两种过渡模式，用户点击默认带动画，代码设置可选。

**典型使用场景**:
- 设置面板的高级选项折叠
- FAQ 列表（点击问题展开答案）
- 表单的可选字段分组

**命名空间**: `fluent`  
**头文件**: `FluentUI/controls/Expander.h`

---

## 继承层次

```
Visual
  └─ UIElement
      └─ FrameworkElement
          └─ Expander
```

**注意**: Expander 直接继承 FrameworkElement，**不是** Control 或 Panel。

---

## 公开 API

### 构造函数

```cpp
Expander();
```

默认构造空标题、折叠状态的 Expander。

### 嵌套类型

```cpp
enum class Transition { Instant, Animate };
```

**Instant**: 立即切换状态，无动画  
**Animate**: 高度插值动画展开/折叠

### 属性

#### Header

```cpp
void SetHeader(std::wstring text);
const std::wstring& Header() const;
```

**说明**: 标题栏文本  
**默认值**: 空字符串  
**脏标记**: Measure

#### Content

```cpp
template <typename T>
T* SetContent(std::unique_ptr<T> child);
FrameworkElement* Content() const;
```

**说明**: 设置/获取内容区唯一子元素。返回裸指针便于链式调用。  
**脏标记**: Measure

#### Expanded

```cpp
void SetExpanded(bool expanded, Transition transition = Transition::Instant);
bool IsExpanded() const;
```

**说明**: 展开状态。`SetExpanded` 可指定过渡模式，默认 Instant。  
**默认值**: `false`（折叠）  
**脏标记**: Measure（高度变化影响所需空间）

**Instant vs Animate**:
- `Instant`: `reveal_` 立即设为目标值（0 或 1），无动画
- `Animate`: 启动 `AnimatedValue` 插值，通过 `WantsAnimationTick()` 和 `OnAnimationTick()` 驱动

#### UserToggleTransition

```cpp
void SetUserToggleTransition(Transition t);
Transition UserToggleTransition() const;
```

**说明**: 用户点击标题栏时使用的过渡模式  
**默认值**: `Transition::Animate`  
**脏标记**: 无

代码调用 `SetExpanded` 时传入的 `transition` 参数优先，此属性只影响用户交互。

### 方法

#### PerformSetExpanded

```cpp
void PerformSetExpanded(bool expanded);
```

**说明**: 按 `UserToggleTransition()` 设置展开状态，并触发 `ExpandedChanged` 事件。标题栏点击时调用此方法。代码也可调用它模拟用户点击。

### 事件

#### ExpandedChanged

```cpp
Event<Expander, bool>& ExpandedChanged();
```

**参数**: `bool` — 新的展开状态（`true` = 展开，`false` = 折叠）

**说明**: 展开状态改变时触发。只有 `PerformSetExpanded` 触发此事件，`SetExpanded` 不触发（避免代码设置时循环回调）。

**订阅示例**:
```cpp
sub_ = expander->ExpandedChanged().Subscribe<&MyWindow::OnExpandedChanged>(this);
// ...
void MyWindow::OnExpandedChanged(Expander& sender, bool& expanded) {
    // expanded 是新状态
}
Subscription sub_;
```

### 几何查询

```cpp
RectDip HeaderRect() const;
```

**说明**: 标题栏的边界矩形（相对于 Expander 自身）

```cpp
float VisualOverflowDip() const override;
```

**说明**: 返回焦点环的溢出尺寸（`FocusRingPadDip(FocusRingSpec{})`，通常 3.75 DIP）

---

## 代码示例

### 最小示例

```cpp
#include <FluentUI/FluentUI.h>
#include "FluentUI/controls/Expander.h"

using namespace fluent;

class MainWindow final : public Window {
public:
    MainWindow() {
        SetTitle(L"Expander Example");
        SetClientSize(640.0f, 480.0f);
    }

protected:
    void OnInitialize() override {
        auto expander = std::make_unique<Expander>();
        expander->SetHeader(L"高级选项");

        auto* stack = expander->SetContent(std::make_unique<StackPanel>());
        stack->SetSpacing(8.0f);
        stack->Add(std::make_unique<CheckBox>())->SetText(L"启用调试");
        stack->Add(std::make_unique<CheckBox>())->SetText(L"详细日志");

        sub_ = expander->ExpandedChanged().Subscribe<&MainWindow::OnExpanded>(this);

        SetRoot(std::move(expander));
    }

private:
    void OnExpanded(Expander&, bool& expanded) {
        // 状态变化时的逻辑
    }
    Subscription sub_;
};

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int) {
    Application app(instance);
    MainWindow window;
    if (FAILED(window.Show(app))) return 1;
    return app.Run();
}
```

### FAQ 列表

```cpp
auto faqList = std::make_unique<StackPanel>();
faqList->SetSpacing(8.0f);

auto* q1 = faqList->Add(std::make_unique<Expander>());
q1->SetHeader(L"如何重置密码？");
q1->SetContent(std::make_unique<TextBlock>())->SetText(
    L"点击登录页面的"忘记密码"链接，按提示操作。");

auto* q2 = faqList->Add(std::make_unique<Expander>());
q2->SetHeader(L"支持哪些支付方式？");
q2->SetContent(std::make_unique<TextBlock>())->SetText(
    L"支持信用卡、支付宝、微信支付。");
```

---

## 备注

### 性能特征

- **Measure**: O(1)，测量标题和内容（折叠时也测量内容以计算动画目标高度）
- **Render**: 绘制标题栏背景、标题文本、chevron 图标、内容区
- **动画**: UI 线程通过 `AnimatedValue` 插值，帧率依赖主线程可用性（不是 compositor 动画）
- **Idle 成本**: 折叠或展开完成后 `WantsAnimationTick()` 返回 false，零 CPU
- **脏标记**: `SetHeader/SetContent/SetExpanded` 触发 Measure；`SetUserToggleTransition` 无脏标记

### 动画机制

- **reveal_**: `AnimatedValue<float>`，范围 0.0（完全折叠）到 1.0（完全展开）
- **Instant**: `reveal_.SetImmediate(target)`，立即跳到目标值
- **Animate**: `reveal_` 保持当前值，`WantsAnimationTick()` 返回 true，`OnAnimationTick` 中调用 `reveal_.Approach(target, kRevealTau)`
- **kRevealTau**: 0.06f，控制动画速度（tau 越小越快）
- **收敛判定**: `reveal_` 与目标差距小于 0.01 时 snap 到目标值并停止 tick（不用 0.001，避免长尾帧）

### 内容 Attach/Detach

- **折叠时**: 内容 `DetachFromContext()`，从视觉树移除，不参与布局、绘制、命中测试
- **展开时**: 内容 `AttachToContext()`，重新加入树
- **动画过程**: `reveal_ > 0.001f` 时内容保持 attached 且参与 Measure，高度按 `reveal_` 比例裁剪

折叠时 Detach 而非单纯隐藏，避免大量折叠 Expander 的内容占用布局和命中测试开销。

### 标题栏交互

- **点击**: 调用 `PerformSetExpanded(!expanded_)`，切换状态并触发事件
- **焦点**: 标题栏可聚焦（Tab 导航），Enter/Space 键触发切换
- **Chevron**: 右侧绘制 chevron 图标（展开时旋转 90°）

### 主题 Token

Expander 使用以下主题 token（`styling/ThemeTokens.h`）：

- `subtleFill` — 标题栏背景色（静止/悬停/按下 三态）
- `textPrimary` — 标题文本颜色
- Chevron 图标由代码绘制，颜色同 `textPrimary`

标题字号使用 Control 的 `EffectiveFontSize()` 或主题 `bodySize`。

### 使用注意事项

1. **只能有一个内容元素**: 需要多个控件时，先包在 StackPanel/Grid 里
2. **折叠时内容被 Detach**: 内容控件的状态（焦点、输入）在折叠时丢失
3. **动画是 UI 线程**: 主线程繁忙时动画会卡顿
4. **SetExpanded 不触发事件**: 只有 `PerformSetExpanded` 和用户点击触发 `ExpandedChanged`
5. **VisualOverflowDip 始终返回焦点环尺寸**: 即使未获得焦点，也需要声明溢出以便焦点环显示时正确清除旧像素

---

## 相关控件

- [GroupBox](GroupBox.md) — 静态分组框，无折叠功能
- [TreeView](TreeView.md) — 树形视图，节点可展开折叠
- [StackPanel](StackPanel.md) — Expander 内部常用的子容器
