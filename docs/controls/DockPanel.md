# DockPanel 布局容器

## 概述

边缘停靠容器，子元素依次消耗 Left/Top/Right/Bottom 边缘的空间。经典的"工具栏在顶、状态栏在底、侧边栏在左、主内容填充剩余"布局即用 DockPanel 实现。子元素按**添加顺序**依次处理，每个子元素的停靠方向通过静态附加属性 `DockPanel::SetDock` 指定。

**典型使用场景**:
- IDE 主窗口布局（顶部菜单栏、底部输出面板、左侧工具箱、中央编辑器）
- 应用程序外壳（标题栏、状态栏、内容区）
- 对话框（顶部说明、底部按钮条、中间表单）

**命名空间**: `fluent`  
**头文件**: `FluentUI/layout/DockPanel.h`

---

## 继承层次

```
Visual
  └─ UIElement
      └─ FrameworkElement
          └─ Panel
              └─ DockPanel
```

---

## 公开 API

### 嵌套类型

```cpp
enum class Dock { Left, Top, Right, Bottom };
```

停靠边缘枚举，默认 `Dock::Left`。

### 静态附加属性

```cpp
static void SetDock(UIElement* element, Dock dock);
static Dock GetDock(UIElement* element);  // 默认 Dock::Left
```

指定子元素停靠到哪条边。存储在静态 map 中，对任何 UIElement 有效（不仅是 DockPanel 的子元素）。

### 属性

```cpp
void SetLastChildFill(bool fill);
bool LastChildFill() const;
```

**说明**: 控制最后一个子元素是否忽略其 Dock 值并填充剩余空间  
**默认值**: `true`  
**脏标记**: Measure

**LastChildFill 行为**:
- `true`（默认）：最后一个子元素无论 Dock 为何值，都占据剩余全部矩形
- `false`：最后一个子元素也按其 Dock 值消耗一条边，剩余空间留空

### 子元素

继承自 Panel 的通用 API：

```cpp
template <class T> T* Add(std::unique_ptr<T> child);
size_t ChildCount() const;
FrameworkElement* ChildAt(size_t i) const;
void Clear();
```

**处理顺序依赖**: 子元素按添加顺序依次处理。先添加的先消耗边缘，后添加的只能从剩余矩形中停靠。

---

## 代码示例

### 最小示例

```cpp
#include <FluentUI/FluentUI.h>
#include "FluentUI/layout/DockPanel.h"

using namespace fluent;

class MainWindow final : public Window {
public:
    MainWindow() {
        SetTitle(L"DockPanel Example");
        SetClientSize(800.0f, 600.0f);
    }

protected:
    void OnInitialize() override {
        auto dock = std::make_unique<DockPanel>();

        // 顶部工具栏
        auto* top = dock->Add(std::make_unique<Border>());
        top->SetHeight(48.0f);
        top->SetBackground(D2D1::ColorF(0.2f, 0.4f, 0.6f));
        DockPanel::SetDock(top, DockPanel::Dock::Top);

        // 底部状态栏
        auto* bottom = dock->Add(std::make_unique<TextBlock>());
        bottom->SetText(L"Status: Ready");
        bottom->SetHeight(24.0f);
        DockPanel::SetDock(bottom, DockPanel::Dock::Bottom);

        // 左侧边栏
        auto* left = dock->Add(std::make_unique<Border>());
        left->SetWidth(200.0f);
        left->SetBackground(D2D1::ColorF(0.95f, 0.95f, 0.95f));
        DockPanel::SetDock(left, DockPanel::Dock::Left);

        // 中央内容（最后添加，自动填充剩余空间）
        auto* center = dock->Add(std::make_unique<TextBlock>());
        center->SetText(L"Main Content Area");
        // LastChildFill 默认 true，center 忽略 Dock 填充剩余

        SetRoot(std::move(dock));
    }
};

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int) {
    Application app(instance);
    MainWindow window;
    if (FAILED(window.Show(app))) return 1;
    return app.Run();
}
```

### 对话框布局

```cpp
auto dialog = std::make_unique<DockPanel>();

// 顶部标题区
auto* title = dialog->Add(std::make_unique<TextBlock>());
title->SetText(L"确认操作");
title->SetHeight(40.0f);
DockPanel::SetDock(title, DockPanel::Dock::Top);

// 底部按钮条
auto* buttonPanel = dialog->Add(std::make_unique<StackPanel>());
buttonPanel->SetOrientation(StackPanel::Orientation::Horizontal);
buttonPanel->SetHeight(50.0f);
buttonPanel->SetHAlign(HAlign::Right);
buttonPanel->SetSpacing(8.0f);
buttonPanel->Add(std::make_unique<Button>())->SetText(L"确定");
buttonPanel->Add(std::make_unique<Button>())->SetText(L"取消");
DockPanel::SetDock(buttonPanel, DockPanel::Dock::Bottom);

// 中间内容（自动填充）
auto* content = dialog->Add(std::make_unique<TextBlock>());
content->SetText(L"是否继续？");
// LastChildFill = true，content 填充剩余空间
```

---

## 备注

### 性能特征

- **Measure**: O(子元素数)，顺序测量每个子元素，剩余空间逐步收缩
- **Arrange**: O(子元素数)，顺序排布每个子元素到其停靠边
- **Idle 成本**: 零
- **脏标记**: `SetLastChildFill` 触发 Measure；`SetDock` 只写静态 map，不触发失效（同 Canvas）

### 顺序依赖性

DockPanel 的布局结果**强依赖子元素添加顺序**：

```cpp
// 顺序 A：先 Top，后 Left
DockPanel::SetDock(toolbar, Dock::Top);   // 占据整行顶部
DockPanel::SetDock(sidebar, Dock::Left);  // 从剩余高度中停靠左边

// 顺序 B：先 Left，后 Top
DockPanel::SetDock(sidebar, Dock::Left);  // 占据整列左边
DockPanel::SetDock(toolbar, Dock::Top);   // 从剩余宽度（已扣除 sidebar）中停靠顶部
```

两种顺序的 toolbar 宽度不同：A 中 toolbar 占满窗口宽，B 中 toolbar 只占 (窗口宽 - sidebar 宽)。

### 使用注意事项

1. **子元素需显式尺寸**: 停靠到 Top/Bottom 的元素需 `SetHeight`，停靠到 Left/Right 的需 `SetWidth`；否则测量为 0 不可见
2. **LastChildFill 优先**: 最后一个子元素的 Dock 值在 `LastChildFill = true` 时被忽略
3. **空间耗尽**: 前面子元素消耗完所有空间后，后续子元素得到 0×0 的槽位
4. **附加属性默认 Left**: 未调用 `SetDock` 的子元素默认停靠左边

---

## 相关控件

- [Grid](Grid.md) — 行列网格布局，适合需要精确行高列宽控制的场景
- [StackPanel](StackPanel.md) — 顺序堆叠，不消耗边缘
- [Canvas](Canvas.md)— 绝对定位，不自动分配空间
