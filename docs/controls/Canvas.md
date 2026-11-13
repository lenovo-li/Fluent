# Canvas 布局容器

## 概述

绝对定位容器，通过静态附加属性（Left/Top/Right/Bottom/ZIndex）指定子元素的坐标。每个子元素独立测量（无约束），按其附加属性排布到画布上的绝对位置。ZIndex 控制绘制顺序（数值越大越靠前）。

**典型使用场景**:
- 拖拽式设计器（控件可任意摆放）
- 覆盖层（对话框遮罩、浮动按钮）
- 自定义布局（坐标精确控制）

**命名空间**: `fluent`  
**头文件**: `FluentUI/layout/Canvas.h`

---

## 继承层次

```
Visual
  └─ UIElement
      └─ FrameworkElement
          └─ Panel
              └─ Canvas
```

---

## 公开 API

### 静态附加属性

附加属性存储在静态 map 中，对任何 UIElement 有效（不仅是 Canvas 的子元素）。

```cpp
static void SetLeft(UIElement* element, float dip);
static void SetTop(UIElement* element, float dip);
static void SetRight(UIElement* element, float dip);
static void SetBottom(UIElement* element, float dip);
static void SetZIndex(UIElement* element, int z);

static float GetLeft(UIElement* element);    // 默认 0.0
static float GetTop(UIElement* element);     // 默认 0.0
static float GetRight(UIElement* element);   // 默认 NaN（未设置）
static float GetBottom(UIElement* element);  // 默认 NaN（未设置）
static int GetZIndex(UIElement* element);    // 默认 0
```

**定位规则**:
- 只设 Left：子元素左边距 Canvas 左边 Left DIP，右边 = 左边 + 宽度
- 只设 Right：子元素右边距 Canvas 右边 Right DIP
- **Left 和 Right 都设：Right 优先**（从右边定位）
- Top/Bottom 同理：**都设时 Bottom 优先**

**ZIndex**: 数值越大绘制越靠后（覆盖低 ZIndex 的元素）。相同 ZIndex 按添加顺序绘制。

**Right/Bottom 默认 NaN 的原因**: 0 是合法值（"紧贴边缘"），不能同时表示"未设置"。NaN 让 ArrangeOverride 能区分两者。

### 清除方法

```cpp
static void ClearPositions(UIElement* element);
```

清除指定元素的所有附加属性（Left/Top/Right/Bottom/ZIndex），从内部 map 中移除，元素恢复默认值。用于元素复用，或在 `Clear()` 后避免旧条目残留（见备注）。

### 渲染与命中测试（public override）

```cpp
void Render(const DrawingContext& dc) override;
UIElement* HitTestDeep(float dipX, float dipY) override;
UIElement* HitTestDropTarget(float dipX, float dipY) override;
```

三者都按 ZIndex 顺序遍历子元素：绘制升序，命中测试和拖放目标查找降序（视觉最上层优先）。Canvas 自身不是交互目标，`HitTestDeep` 未命中任何子元素时返回 `nullptr`。

---

### 子元素

继承自 Panel 的通用 API：

```cpp
template <class T> T* Add(std::unique_ptr<T> child);
size_t ChildCount() const;
FrameworkElement* ChildAt(size_t i) const;
void Clear();
```

`Add` 返回裸指针便于链式调用。添加顺序不影响位置（位置由附加属性决定），但影响**相同 ZIndex 的绘制顺序**。

---

## 代码示例

### 最小示例

```cpp
#include <FluentUI/FluentUI.h>
#include "FluentUI/layout/Canvas.h"

using namespace fluent;

class MainWindow final : public Window {
public:
    MainWindow() {
        SetTitle(L"Canvas Example");
        SetClientSize(640.0f, 480.0f);
    }

protected:
    void OnInitialize() override {
        auto canvas = std::make_unique<Canvas>();
        canvas->SetWidth(640.0f);   // Canvas 不自动撑开，需显式尺寸
        canvas->SetHeight(480.0f);

        // 左上角文本
        auto* label1 = canvas->Add(std::make_unique<TextBlock>());
        label1->SetText(L"Top-Left");
        Canvas::SetLeft(label1, 10.0f);
        Canvas::SetTop(label1, 10.0f);

        // 右下角按钮（用 Right/Bottom 定位）
        auto* btn = canvas->Add(std::make_unique<Button>());
        btn->SetText(L"Bottom-Right");
        Canvas::SetRight(btn, 10.0f);
        Canvas::SetBottom(btn, 10.0f);

        SetRoot(std::move(canvas));
    }
};

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int) {
    Application app(instance);
    MainWindow window;
    if (FAILED(window.Show(app))) return 1;
    return app.Run();
}
```

### ZIndex 分层

```cpp
auto canvas = std::make_unique<Canvas>();
canvas->SetWidth(400.0f);
canvas->SetHeight(300.0f);

// 背景矩形（ZIndex 0，默认最底层）
auto* bg = canvas->Add(std::make_unique<Border>());
bg->SetWidth(200.0f);
bg->SetHeight(150.0f);
bg->SetBackground(D2D1::ColorF(0.9f, 0.9f, 0.9f));  // 浅灰
Canvas::SetLeft(bg, 50.0f);
Canvas::SetTop(bg, 50.0f);
Canvas::SetZIndex(bg, 0);

// 前景文本（ZIndex 1，覆盖背景）
auto* fg = canvas->Add(std::make_unique<TextBlock>());
fg->SetText(L"On Top");
Canvas::SetLeft(fg, 100.0f);
Canvas::SetTop(fg, 100.0f);
Canvas::SetZIndex(fg, 1);

// 即使先添加 bg，ZIndex 更高的 fg 也会绘制在上层
```

---

## 备注

### 性能特征

- **Measure**: O(子元素数)，每个子元素测量一次（无约束 infinity×infinity）。Canvas 自身返回 (0,0) 除非显式设 Width/Height。
- **Arrange**: O(子元素数)。每个可见子元素按附加属性和自身 `Desired()` 算出槽位，子元素以 desired 尺寸排布（不拉伸）。
- **Render**: 按 ZIndex 升序绘制；与 Canvas 自身 `bounds_` 或本帧 `ClipHint` 不相交的子元素被跳过。因此完全移出 Canvas 范围的子元素不会被绘制。
- **HitTest**: 按 ZIndex **降序**遍历（从上往下），命中第一个即停止。
- **Idle 成本**: 零。Canvas 本身不持有动画状态。
- **ZIndex 排序缓存**: Render 与命中测试前调用 `EnsureZOrder()`，仅在 ZIndex 世代号变化或子元素数量变化时稳定排序一次，平时零分配。
- **附加属性不触发失效**: `SetLeft/SetTop/SetRight/SetBottom/SetZIndex` 只写静态 map，不调用任何 `Invalidate`。新值要到 Canvas 下一次 Arrange（或下一帧 Render，对 ZIndex 而言）才生效。`InvalidateArrange()` 是 `Visual` 的 protected 成员，外部代码无法直接调用；在窗口显示后移动子元素，需要借助其他会让 Canvas 重排的操作，或在 Canvas 子类内部调用 `InvalidateArrange()`。

### 自然尺寸与约束

Canvas 的 `MeasureOverride` **不依赖子元素位置**：
- 未设 Width/Height：返回 (0, 0)（会在无约束轴上坍缩）
- 已设 Width/Height：返回设定值

**子元素超出 Canvas 边界不会撑大 Canvas**。Canvas 不聚合子元素的范围。

### 附加属性的生命周期（地址复用陷阱）

```cpp
Canvas::SetLeft(element, 100.0f);  // 存入 positions_[element] = {100, ...}
delete element;
auto* newElement = new TextBlock();  // 可能复用同一地址
// positions_[newElement] 意外命中旧条目！
```

Canvas 析构时会把仍属于它的直接子元素从 map 中移除。以下两种情况**不会**自动清理，需要手动调用 `Canvas::ClearPositions`：

- 子元素被移出 Canvas 后单独销毁；
- 调用 `Panel::Clear()`（非虚函数，Canvas 无法拦截）。

### 使用注意事项

1. **必须显式设置 Canvas 尺寸**: `SetWidth/SetHeight` 或外层约束，否则 Canvas 报告 (0,0) 会消失
2. **不做裁剪**: 子元素可以位于负坐标或伸出 Canvas 边界；伸出部分照常绘制，但整个子元素与 Canvas 不相交时会被跳过
3. **Left+Right 同时设时 Right 优先**: 从右边定位，Left 被忽略（Top+Bottom 同理）
4. **附加属性跨 Canvas 生效**: `SetLeft` 是静态方法，存在全局 map；元素从一个 Canvas 移到另一个 Canvas 时坐标保持
5. **`Clear()` 之后要清理附加属性**: 见上节「地址复用陷阱」

---

## 相关控件

- [Grid](Grid.md) — 行列网格布局，适合对齐式布局（Canvas 无对齐能力）
- [StackPanel](StackPanel.md) — 顺序堆叠，适合流式布局
- [Border](Border.md) — 边框容器

