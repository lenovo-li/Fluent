# StackPanel 布局容器

## 概述

把子元素沿一个方向依次排开:垂直(默认)或水平。最简单也最常用的容器 —— 表单的一列、
工具栏的一行、卡片里的若干段落。

**它不做对齐分配。** 堆叠轴上每个子元素拿到自己 desired 的尺寸,依次排列;
**非堆叠轴**上则受子元素自己的 `HAlign`/`VAlign` 控制。需要「按比例分配空间」请用
`Grid` + Star 轨道。

**命名空间**: `fluent`
**头文件**: `FluentUI/layout/StackPanel.h`

---

## 继承层次

```
Visual
  └─ UIElement
      └─ FrameworkElement
          └─ Panel
              └─ StackPanel
```

---

## 公开 API

### 嵌套类型

```cpp
enum class Orientation { Vertical, Horizontal };
```

默认 `Vertical`。

### 属性

```cpp
void SetOrientation(Orientation o);
void SetSpacing(float dip);
```

`SetSpacing` 是子元素**之间**的间隙(N 个子元素有 N-1 个间隙,首尾不加)。

**优先用 Spacing 而不是给每个子元素设 Margin**:Margin 在相邻元素之间会**叠加**
(上一个的 bottom 加下一个的 top),导致间距忽大忽小;Spacing 是一个统一的值。

### 子元素

```cpp
template <class T> T* Add(std::unique_ptr<T> child);   // 继承自 Panel，返回裸指针
size_t ChildCount() const;
FrameworkElement* ChildAt(size_t i) const;             // 只读，越界返回 nullptr
void Clear();
```

`Add` 返回裸指针,便于链式配置:

```cpp
auto* label = stack->Add(std::make_unique<TextBlock>());
label->SetText(L"标题");
```

---

## 代码示例

### 垂直(默认)

```cpp
#include "FluentUI/layout/StackPanel.h"

auto* stack = card->Add(std::make_unique<StackPanel>());
stack->SetSpacing(8.0f);            // 统一间距，别用 Margin 叠加
stack->Add(std::make_unique<TextBlock>())->SetText(L"第一行");
stack->Add(std::make_unique<TextBlock>())->SetText(L"第二行");
```

### 水平(工具栏 / 句子中嵌链接)

```cpp
auto* row = card->Add(std::make_unique<StackPanel>());
row->SetOrientation(StackPanel::Orientation::Horizontal);
row->SetSpacing(4.0f);

// 本框架没有富文本，所以「一句话中间嵌链接」要用水平 StackPanel 拼
auto* pre = row->Add(std::make_unique<TextBlock>());
pre->SetText(L"继续即表示你同意");
pre->SetVAlign(VAlign::Center);     // 非堆叠轴上的对齐由子元素自己决定

auto* link = row->Add(std::make_unique<Hyperlink>());
link->SetText(L"服务条款");
link->SetVAlign(VAlign::Center);
```

---

## 备注

### 什么时候不该用 StackPanel

- **需要按比例分配空间** → `Grid` + `GridLength::Star`。StackPanel 只会给每个子元素它
  自己要的尺寸。
- **需要跨行/跨列对齐成表格** → `Grid`。多个水平 StackPanel 叠起来**不会**纵向对齐,
  因为每一行的列宽各算各的。
- **子元素多到需要滚动** → 外面套 `ScrollPanel`。
- **数千个同构条目** → `ListBox` / `DataGrid`(它们虚拟化;StackPanel 不虚拟化,
  所有子元素每帧都参与布局)。

### 性能特征

- **Measure/Arrange 是 O(子元素数)**,不虚拟化。几十个子元素没问题,几千个会在每帧都
  付出代价。
- **不可见子元素被跳过**:`IsVisible() == false` 的元素既不测量也不排布,也不占用
  Spacing 的间隙 —— 所以隐藏一项不会留下一个空洞。demo 的页面切换就依赖这一点
  (31 个页面常驻在树上,只有当前页可见)。
- **脏区遍历只走脏路径**:宿主每个绘制帧都要跑 `AnyDirtyInSubtree(Measure)`、
  `AnyDirtyInSubtree(Arrange)`、`CollectDirtyBounds`、`ClearDirtySubtree`。以前这四个调用
  都会访问整棵树,所以光标闪烁或一次悬停的成本是 O(全部节点)。现在每个 `Visual` 带一个子树
  脏标记(`SubtreeDirty()`):`InvalidateDirty` 沿 `Parent()` 往上走找根回调时顺手把标记
  OR 进每个祖先,`ClearDirtySubtree` 清掉它;标记为空的子树直接跳过。所有 Panel 子类都继承
  这一点,自定义容器的写法见 `inheritance-guide.md` 的 Panel 一节。
  - 证据:`FluentUIBench DirtyWalk`(2,221 个节点,每帧 1 个叶子 Render 脏,Release),
    改前 11.4–13.7 µs/帧,改后 0.12–0.14 µs/帧;两边都是每帧恰好 1 个脏矩形、0 次布局。
  - 测试:`tests/unit/SubtreeDirtyTests.cpp` 的 `SubtreeDirtyPruning.*`。变异验证:去掉
    `InvalidateDirty` 的祖先标记、去掉 `CollectDirtyBounds` 的剪枝、让 `SetParent` 不传递
    标记、让 None 失效不记 Render、在 override 之后才清标记、`Panel` 清理时跳过隐藏子元素,
    每一种都至少让一个测试失败。

### 使用注意事项

- **非堆叠轴默认 Stretch**:垂直 StackPanel 里的子元素默认横向占满。要收缩到内容宽度
  请设 `SetHAlign(HAlign::Left)`。
- Spacing 不加在首尾;要外边距请用容器(`Border`/`GroupBox`)的 `Padding`。

### 演示页

demo 导航 → 布局容器 → StackPanel。含两种方向、Spacing vs Margin 叠加的对照、
非堆叠轴上的对齐行为,以及嵌套组合。
