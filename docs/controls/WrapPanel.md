# WrapPanel 布局容器

## 概述

自动换行容器，子元素沿主轴依次排列，超出可用空间时换到下一行（或下一列）。支持水平和垂直两种方向，可选地强制子元素使用统一尺寸（ItemWidth/ItemHeight），适合标签云、按钮组、图片画廊等等宽或自适应的流式布局。

**典型使用场景**:
- 标签云（标签宽度不一，自动折行）
- 工具面板（图标按钮等宽等高，自动换行填充）
- 响应式按钮组（窗口变窄时自动换行）

**命名空间**: `fluent`  
**头文件**: `FluentUI/layout/WrapPanel.h`

---

## 继承层次

```
Visual
  └─ UIElement
      └─ FrameworkElement
          └─ Panel
              └─ WrapPanel
```

---

## 公开 API

### 嵌套类型

```cpp
enum class Orientation { Horizontal, Vertical };
```

**Horizontal**（默认）：从左到右排列，超出宽度时换行  
**Vertical**：从上到下排列，超出高度时换列

### 属性

#### Orientation

```cpp
void SetOrientation(Orientation o);
Orientation GetOrientation() const;
```

**说明**: 主轴方向  
**默认值**: `Orientation::Horizontal`  
**脏标记**: Measure

#### ItemWidth

```cpp
void SetItemWidth(float dip);
void ClearItemWidth();
std::optional<float> GetItemWidth() const;
```

**说明**: 强制所有子元素使用统一宽度（覆盖子元素自身的 Desired 宽度）  
**默认值**: 未设置（子元素使用各自测量尺寸）  
**脏标记**: Measure

设置 ItemWidth 后，所有子元素在主轴（Horizontal）或交叉轴（Vertical）上都按此宽度排布，实现等宽网格效果。

#### ItemHeight

```cpp
void SetItemHeight(float dip);
void ClearItemHeight();
std::optional<float> GetItemHeight() const;
```

**说明**: 强制所有子元素使用统一高度（覆盖子元素自身的 Desired 高度）  
**默认值**: 未设置（子元素使用各自测量尺寸）  
**脏标记**: Measure

设置 ItemHeight 后，所有子元素在主轴（Vertical）或交叉轴（Horizontal）上都按此高度排布。

### 子元素

继承自 Panel 的通用 API：

```cpp
template <class T> T* Add(std::unique_ptr<T> child);
size_t ChildCount() const;
FrameworkElement* ChildAt(size_t i) const;
void Clear();
```

---

## 代码示例

### 最小示例

```cpp
#include <FluentUI/FluentUI.h>
#include "FluentUI/layout/WrapPanel.h"

using namespace fluent;

class MainWindow final : public Window {
public:
    MainWindow() {
        SetTitle(L"WrapPanel Example");
        SetClientSize(640.0f, 480.0f);
    }

protected:
    void OnInitialize() override {
        auto wrap = std::make_unique<WrapPanel>();
        wrap->SetOrientation(WrapPanel::Orientation::Horizontal);

        // 添加多个按钮，窗口变窄时自动换行
        for (int i = 1; i <= 12; ++i) {
            auto* btn = wrap->Add(std::make_unique<Button>());
            btn->SetText(L"Item " + std::to_wstring(i));
        }

        SetRoot(std::move(wrap));
    }
};

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int) {
    Application app(instance);
    MainWindow window;
    if (FAILED(window.Show(app))) return 1;
    return app.Run();
}
```

### 统一尺寸网格

```cpp
auto wrap = std::make_unique<WrapPanel>();
wrap->SetItemWidth(120.0f);
wrap->SetItemHeight(80.0f);

// 所有子元素都按 120×80 DIP 排布，无论自身 Desired 是多少
for (int i = 0; i < 20; ++i) {
    auto* card = wrap->Add(std::make_unique<Border>());
    card->SetBackground(D2D1::ColorF(0.8f, 0.9f, 1.0f));
    auto* label = card->SetChild(std::make_unique<TextBlock>());
    label->SetText(L"Card " + std::to_wstring(i));
}
```

---

## 备注

### 性能特征

- **Measure**: O(子元素数)，两遍遍历（`ComputeLines` 计算换行点，测量所有子元素）
- **Arrange**: O(子元素数)，再次调用 `ComputeLines` 在最终宽度下重新分行，顺序排布
- **Idle 成本**: 零
- **脏标记**: `SetOrientation/SetItemWidth/SetItemHeight` 都触发 Measure

### 行/列计算

WrapPanel 内部通过 `ComputeLines()` 将子元素分组为行（Horizontal）或列（Vertical）：

- **主轴尺寸**（ItemWidth 或子元素 Desired.w）累加，超出可用空间时断行
- **交叉轴尺寸**：每行/列的高度取该行/列中子元素的最大值

`Measure` 和 `Arrange` 各调用一次 `ComputeLines`，因为 Measure 阶段可用宽度可能是 infinity，Arrange 阶段拿到真实分配宽度后需重新计算换行点。

### ItemWidth/ItemHeight 的作用

- **设置前**: 每个子元素按自身 Desired 尺寸排布，宽度/高度各不相同
- **设置后**: 所有子元素在该维度上强制使用统一值，忽略 Desired

**交叉轴**: ItemHeight 在 Horizontal 模式下控制行高，ItemWidth 在 Vertical 模式下控制列宽。

### 使用注意事项

1. **Measure 阶段可能换行多次**: 可用宽度从 infinity 收敛到最终值的过程中，行数可能变化
2. **子元素不会被拉伸**: 即使槽位是 ItemWidth×ItemHeight，子元素按自身 HAlign/VAlign 在槽位内对齐
3. **空子元素占位**: `IsVisible() == false` 的子元素被跳过，不占位
4. **无虚拟化**: 所有子元素都参与布局；数千个子元素会有性能代价

---

## 相关控件

- [StackPanel](StackPanel.md) — 不换行的单向堆叠
- [Grid](Grid.md) — 固定行列的网格，适合需要精确对齐的场景
- [UniformGrid](UniformGrid.md)— 自动计算行列数的等尺寸网格