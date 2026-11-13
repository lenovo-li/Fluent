# GroupBox 布局容器

## 概述

带标题的边框容器，在一个子元素周围绘制圆角矩形边框，顶部显示文本标题。标题位于边框上方（不是嵌在边框线上），避免 Mica 等透明背景下的视觉问题。常用于表单分组、设置面板的分节显示。

**典型使用场景**:
- 表单分组（"基本信息"、"高级选项"等分区）
- 设置面板的功能分区
- 对话框中逻辑相关的控件组

**命名空间**: `fluent`  
**头文件**: `FluentUI/layout/GroupBox.h`

---

## 继承层次

```
Visual
  └─ UIElement
      └─ FrameworkElement
          └─ GroupBox
```

**注意**: GroupBox 直接继承 FrameworkElement，**不是** Panel 或 Control。它只持有一个子元素（`std::unique_ptr<FrameworkElement> child_`），多个子元素需要先包在 Panel 里。

---

## 公开 API

### 构造函数

```cpp
GroupBox();
explicit GroupBox(std::wstring header);
```

默认构造空标题的分组框，或直接传入标题文本。

### 属性

#### Header

```cpp
void SetHeader(const std::wstring& text);
const std::wstring& Header() const;
```

**说明**: 顶部标题文本  
**默认值**: 空字符串  
**脏标记**: Measure（标题长度影响最小宽度）

#### Child

```cpp
template <typename T>
T* SetChild(std::unique_ptr<T> child);
FrameworkElement* Child() const;
```

**说明**: 设置/获取唯一子元素。返回裸指针便于链式调用。  
**脏标记**: Measure

#### Padding

```cpp
void SetPadding(const Thickness& p);
const Thickness& Padding() const;
```

**说明**: 边框内边距（子元素与边框线的距离）  
**默认值**: `Thickness{12.0f}`（四边均 12 DIP）  
**脏标记**: Measure

#### BorderThickness

```cpp
void SetBorderThickness(float dip);
float BorderThickness() const;
```

**说明**: 边框线宽  
**默认值**: 1.0f  
**脏标记**: Measure（边框宽度影响内部可用空间）

#### CornerRadius

```cpp
void SetCornerRadius(float dip);
float CornerRadius() const;
```

**说明**: 边框圆角半径。-1.0f 表示使用主题默认值。  
**默认值**: -1.0f（使用主题的 `controlCornerRadius` token）  
**脏标记**: Render

### 几何查询

```cpp
RectDip HeaderRect() const;
RectDip BoxRect() const;
```

**HeaderRect**: 标题文本的边界矩形（相对于 GroupBox 自身）  
**BoxRect**: 边框矩形的边界（标题下方，填充子元素的框）

---

## 代码示例

### 最小示例

```cpp
#include <FluentUI/FluentUI.h>
#include "FluentUI/layout/GroupBox.h"

using namespace fluent;

class MainWindow final : public Window {
public:
    MainWindow() {
        SetTitle(L"GroupBox Example");
        SetClientSize(640.0f, 480.0f);
    }

protected:
    void OnInitialize() override {
        auto group = std::make_unique<GroupBox>(L"用户信息");

        // GroupBox 只持有一个子元素，多个控件需包在 Panel 里
        auto* stack = group->SetChild(std::make_unique<StackPanel>());
        stack->SetSpacing(8.0f);
        stack->Add(std::make_unique<TextBlock>())->SetText(L"姓名：张三");
        stack->Add(std::make_unique<TextBlock>())->SetText(L"年龄：28");

        SetRoot(std::move(group));
    }
};

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int) {
    Application app(instance);
    MainWindow window;
    if (FAILED(window.Show(app))) return 1;
    return app.Run();
}
```

### 表单分组

```cpp
auto form = std::make_unique<StackPanel>();
form->SetSpacing(16.0f);

// 基本信息分组
auto* basic = form->Add(std::make_unique<GroupBox>(L"基本信息"));
basic->SetPadding(Thickness{16.0f});
auto* basicStack = basic->SetChild(std::make_unique<StackPanel>());
basicStack->SetSpacing(8.0f);
basicStack->Add(std::make_unique<TextBox>())->SetPlaceholder(L"姓名");
basicStack->Add(std::make_unique<TextBox>())->SetPlaceholder(L"邮箱");

// 高级选项分组
auto* advanced = form->Add(std::make_unique<GroupBox>(L"高级选项"));
auto* advStack = advanced->SetChild(std::make_unique<StackPanel>());
advStack->SetSpacing(8.0f);
advStack->Add(std::make_unique<CheckBox>())->SetText(L"启用调试模式");
advStack->Add(std::make_unique<CheckBox>())->SetText(L"自动更新");
```

---

## 备注

### 性能特征

- **Measure**: O(1)，测量标题和子元素各一次
- **Render**: 绘制标题文本、边框矩形；无独立 surface，绘制到父容器的 DrawingContext
- **Idle 成本**: 零
- **脏标记**: `SetHeader/SetChild/SetPadding/SetBorderThickness` 触发 Measure，`SetCornerRadius` 触发 Render

### 标题位置设计

标题绘制在边框**上方**（不是传统的嵌在边框线上），边框从标题底部开始。原因：

- 传统设计（标题横跨边框线，边框线在标题两侧断开）在 Mica 等透明背景下，断开处透出下层内容，视觉不整齐
- 上方标题避免透明问题，且与 Expander 的标题位置一致

间隙：标题底部到边框顶部的间隙是 `kHeaderGapDip`（6.0f DIP）。

### 几何计算

- **HeaderBandHeight**: `headerHeight_ + kHeaderGapDip`，标题占据的总高度
- **BoxRect**: 从 `HeaderBandHeight` 开始，到 GroupBox 底边
- **内部可用空间**: `BoxRect` 减去 `borderThickness_` 和 `padding_` 的四边

### 主题 Token

GroupBox 使用以下主题 token（`styling/ThemeTokens.h`）：

- `controlStrokeDefault` — 边框线颜色
- `textSecondary` — 标题文本颜色
- `controlCornerRadius` — 默认圆角半径（当 `CornerRadius() == -1.0f` 时）

标题字号：`kHeaderFontSizeDip`（14.0f），字重 `SEMI_BOLD`。

### 使用注意事项

1. **只能有一个子元素**: 需要多个控件时，先包在 StackPanel/Grid 等 Panel 里
2. **SetChild 替换**: 多次调用 `SetChild` 会替换（旧子元素被 unique_ptr 释放）
3. **空标题合法**: Header 为空时只绘制边框，不绘制标题文本
4. **不支持自定义标题控件**: Header 只能是 wstring，不能放 CheckBox 等控件（需要这种效果请用 Expander 或自定义控件）

---

## 相关控件

- [Border](Border.md) — 简单边框容器，无标题
- [Expander](Expander.md) — 可折叠分组框，标题可点击
- [StackPanel](StackPanel.md) — GroupBox 内部常用的子容器