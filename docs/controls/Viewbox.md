# Viewbox 布局容器

## 概述

单子元素缩放容器，按指定的 Stretch 模式将子元素缩放到容器尺寸。子元素先按自然尺寸测量，再在 Arrange 阶段计算缩放比例，绘制时应用 D2D 变换矩阵缩放。支持 None（不缩放）、Fill（填充可能变形）、Uniform（等比适应）、UniformToFill（等比裁剪）四种模式。

**典型使用场景**:
- 矢量图标自适应缩放
- 复杂控件组的整体缩放（仪表盘、图表）
- 固定宽高比的内容填充任意容器

**命名空间**: `fluent`  
**头文件**: `FluentUI/layout/Viewbox.h`

---

## 继承层次

```
Visual
  └─ UIElement
      └─ FrameworkElement
          └─ Panel
              └─ Viewbox
```

---

## 公开 API

### 嵌套类型

```cpp
enum class Stretch { None, Fill, Uniform, UniformToFill };
```

**None**: 不缩放，子元素按自然尺寸绘制（可能超出 Viewbox 边界）  
**Fill**: 分别缩放宽高以填充 Viewbox（宽高比可能改变）  
**Uniform**: 等比缩放，完整显示子元素（可能有空白边）  
**UniformToFill**: 等比缩放至填满 Viewbox（可能裁剪子元素部分内容）

### 属性

#### Stretch

```cpp
void SetStretch(Stretch s);
Stretch GetStretch() const;
```

**说明**: 缩放模式  
**默认值**: `Stretch::Uniform`  
**脏标记**: Measure（改变缩放模式会影响子元素的可用空间计算）

### 子元素

```cpp
template <typename T>
T* SetChild(std::unique_ptr<T> child);
FrameworkElement* Child() const;
```

**说明**: 设置/获取唯一子元素。Viewbox 只持有一个子元素。  
**脏标记**: Measure

继承自 Panel 的 `Add` 也可用，但多次调用会添加多个子元素（只有第一个被实际使用，其余被忽略）。推荐用 `SetChild` 明确单子元素语义。

### 坐标映射

```cpp
RectDip MapChildRectToSelf(const RectDip& childRect) const override;
```

**说明**: 将子元素坐标系中的矩形映射到 Viewbox 坐标系（应用缩放和偏移）。用于脏区收集。

---

## 代码示例

### 最小示例

```cpp
#include <FluentUI/FluentUI.h>
#include "FluentUI/layout/Viewbox.h"

using namespace fluent;

class MainWindow final : public Window {
public:
    MainWindow() {
        SetTitle(L"Viewbox Example");
        SetClientSize(640.0f, 480.0f);
    }

protected:
    void OnInitialize() override {
        auto viewbox = std::make_unique<Viewbox>();
        viewbox->SetStretch(Viewbox::Stretch::Uniform);

        // 子元素按自然尺寸测量（100×50），Viewbox 缩放它适应窗口
        auto* text = viewbox->SetChild(std::make_unique<TextBlock>());
        text->SetText(L"Scalable");
        text->SetWidth(100.0f);
        text->SetHeight(50.0f);

        SetRoot(std::move(viewbox));
    }
};

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int) {
    Application app(instance);
    MainWindow window;
    if (FAILED(window.Show(app))) return 1;
    return app.Run();
}
```

### 四种 Stretch 模式对比

```cpp
auto stack = std::make_unique<StackPanel>();
stack->SetOrientation(StackPanel::Orientation::Horizontal);
stack->SetSpacing(8.0f);

// 原始内容：200×100 的蓝色矩形
auto createContent = []() {
    auto border = std::make_unique<Border>();
    border->SetWidth(200.0f);
    border->SetHeight(100.0f);
    border->SetBackground(D2D1::ColorF(0.3f, 0.5f, 0.8f));
    return border;
};

// None: 不缩放（200×100 不变）
auto* vbNone = stack->Add(std::make_unique<Viewbox>());
vbNone->SetStretch(Viewbox::Stretch::None);
vbNone->SetWidth(300.0f);
vbNone->SetHeight(200.0f);
vbNone->SetChild(createContent());

// Uniform: 等比适应（缩放到 200×100 适应 300×200，实际 200×100，居中）
auto* vbUniform = stack->Add(std::make_unique<Viewbox>());
vbUniform->SetStretch(Viewbox::Stretch::Uniform);
vbUniform->SetWidth(300.0f);
vbUniform->SetHeight(200.0f);
vbUniform->SetChild(createContent());

// Fill: 拉伸填充（宽拉伸 1.5×，高拉伸 2×，变形）
auto* vbFill = stack->Add(std::make_unique<Viewbox>());
vbFill->SetStretch(Viewbox::Stretch::Fill);
vbFill->SetWidth(300.0f);
vbFill->SetHeight(200.0f);
vbFill->SetChild(createContent());

// UniformToFill: 等比填满（缩放 2× 填满高度，宽度超出被裁剪）
auto* vbUTF = stack->Add(std::make_unique<Viewbox>());
vbUTF->SetStretch(Viewbox::Stretch::UniformToFill);
vbUTF->SetWidth(300.0f);
vbUTF->SetHeight(200.0f);
vbUTF->SetChild(createContent());
```

---

## 备注

### 性能特征

- **Measure**: O(1)，子元素测量一次（提供 kNaturalConstraint = 100000.0f，避免 infinity 导致子元素无限拉伸）
- **Arrange**: O(1)，计算缩放比例 `scaleX_` / `scaleY_`，子元素按自然尺寸排布，偏移使缩放后的边界居中
- **Render**: O(子元素复杂度)，通过 `TransformGuard` 应用缩放矩阵，子元素在变换后绘制
- **HitTest**: 指针坐标通过逆变换映射到子元素坐标系（`ToContentSpace`）
- **Idle 成本**: 零
- **脏标记**: `SetStretch` 触发 Arrange；`SetChild` 触发 Measure

### 缩放计算

Viewbox 在 `ArrangeOverride` 中计算 `scaleX_` 和 `scaleY_`：

```cpp
float naturalW = child->Desired().w;
float naturalH = child->Desired().h;
float scaleX = bounds_.w / naturalW;
float scaleY = bounds_.h / naturalH;

switch (stretch_) {
case Stretch::None:
    scaleX = scaleY = 1.0f;
    break;
case Stretch::Uniform:
    scaleX = scaleY = std::min(scaleX, scaleY);
    break;
case Stretch::UniformToFill:
    scaleX = scaleY = std::max(scaleX, scaleY);
    break;
case Stretch::Fill:
    // 保持各自计算的 scaleX, scaleY
    break;
}
```

子元素排布在自然尺寸的槽位，缩放后的边界居中对齐到 Viewbox 边界。

### 坐标变换

- **Render**: `TransformGuard` 推送 `D2D1::Matrix3x2F::Scale(scaleX_, scaleY_) * Translation(...)`
- **HitTest**: 鼠标坐标除以 scale 后传给子元素
- **脏区收集**: 子元素的脏矩形通过 `MapChildRectToSelf` 乘以 scale 映射到父坐标系，并 `inflated(1.0f)` 避免像素边界舍入残影

### kNaturalConstraint

Viewbox 测量子元素时提供 `kNaturalConstraint`（100000.0f）而非 infinity，原因：

- Infinity 会让某些控件（如 Stretch 对齐的 StackPanel）报告 infinity 作为 Desired，导致除零
- 100000.0f 足够大，子元素按"不受约束"测量，但返回有限值

### 使用注意事项

1. **只缩放第一个子元素**: 多个子元素时只有第一个被缩放和绘制
2. **UniformToFill 会裁剪**: 超出 Viewbox 边界的部分不可见（但不做显式 clip，依赖父容器裁剪）
3. **交互坐标**: HitTest 自动映射坐标，子元素接收的是未缩放的坐标
4. **脏区膨胀**: `MapChildRectToSelf` 的 `inflated(1.0f)` 确保像素边界不残留，但可能导致稍大的重绘区域

---

## 相关控件

- [Border](Border.md) — 简单容器，不做缩放
- [Image](Image.md) — 图片控件，内置 Stretch 支持（Viewbox 可缩放任意控件组）
- [Canvas](Canvas.md)— 绝对定位，不做缩放
