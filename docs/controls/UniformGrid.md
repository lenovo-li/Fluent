# UniformGrid 布局容器

## 概述

自动计算行列数的等尺寸网格容器。所有单元格宽度相同、高度相同，由内容或容器尺寸决定。可指定列数（Columns），行数自动计算；或指定行数（Rows），列数自动计算；或两者都不指定，自动生成近似正方形的网格。支持 Content 和 Stretch 两种单元格尺寸模式。

**典型使用场景**:
- 图标网格（所有图标等大）
- 计算器按钮（统一尺寸）
- 响应式相册（列数固定，行数随内容增长）

**命名空间**: `fluent`  
**头文件**: `FluentUI/layout/UniformGrid.h`

---

## 继承层次

```
Visual
  └─ UIElement
      └─ FrameworkElement
          └─ Panel
              └─ UniformGrid
```

---

## 公开 API

### 嵌套类型

```cpp
enum class CellSizing { Content, Stretch };
```

**Content**: 单元格尺寸取所有子元素 Desired 的最大值  
**Stretch**: 单元格尺寸由可用空间平分（不会小于 Content 模式的尺寸）

### 属性

#### Columns

```cpp
void SetColumns(int c);
int Columns() const;
```

**说明**: 列数。0 表示自动计算。  
**默认值**: 0（自动）  
**脏标记**: Measure

设为正整数时，行数由子元素总数除以列数计算（向上取整）。

#### Rows

```cpp
void SetRows(int r);
int Rows() const;
```

**说明**: 行数。0 表示自动计算。  
**默认值**: 0（自动）  
**脏标记**: Measure

设为正整数时，列数由子元素总数除以行数计算（向上取整）。

**Columns 和 Rows 都设时**: Rows 被忽略，Columns 优先。

#### FirstColumn

```cpp
void SetFirstColumn(int fc);
int FirstColumn() const;
```

**说明**: 第一个子元素放置在第几列（0-based）。前面的单元格留空。  
**默认值**: 0  
**脏标记**: Arrange

用于跳过网格前几个位置，例如棋盘布局或日历（第一天从周三开始）。

#### CellSizing

```cpp
void SetCellSizing(CellSizing s);
CellSizing GetCellSizing() const;
```

**说明**: 单元格尺寸模式  
**默认值**: `CellSizing::Content`  
**脏标记**: Measure

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
#include "FluentUI/layout/UniformGrid.h"

using namespace fluent;

class MainWindow final : public Window {
public:
    MainWindow() {
        SetTitle(L"UniformGrid Example");
        SetClientSize(640.0f, 480.0f);
    }

protected:
    void OnInitialize() override {
        auto grid = std::make_unique<UniformGrid>();
        grid->SetColumns(3);  // 3 列，行数自动

        for (int i = 1; i <= 9; ++i) {
            auto* btn = grid->Add(std::make_unique<Button>());
            btn->SetText(std::to_wstring(i));
        }

        SetRoot(std::move(grid));
    }
};

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int) {
    Application app(instance);
    MainWindow window;
    if (FAILED(window.Show(app))) return 1;
    return app.Run();
}
```

### Stretch 模式（平分空间）

```cpp
auto grid = std::make_unique<UniformGrid>();
grid->SetColumns(4);
grid->SetCellSizing(UniformGrid::CellSizing::Stretch);

// 窗口宽 640，单元格宽 = 640 / 4 = 160（不考虑 margin）
for (int i = 0; i < 12; ++i) {
    grid->Add(std::make_unique<Border>())->SetBackground(
        D2D1::ColorF(0.9f, 0.9f, 1.0f));
}
```

### FirstColumn 跳过前几格

```cpp
auto grid = std::make_unique<UniformGrid>();
grid->SetColumns(7);         // 日历：周日到周六
grid->SetFirstColumn(3);     // 本月 1 号是周三，前 3 格空

for (int day = 1; day <= 31; ++day) {
    auto* cell = grid->Add(std::make_unique<TextBlock>());
    cell->SetText(std::to_wstring(day));
}
```

---

## 备注

### 性能特征

- **Measure**: O(子元素数)，先测量所有子元素找最大 Desired，再计算行列数
- **Arrange**: O(子元素数)，按网格位置顺序排布
- **Idle 成本**: 零
- **脏标记**: `SetColumns/SetRows/SetCellSizing` 触发 Measure；`SetFirstColumn` 触发 Arrange

### 行列数计算

当 Columns 和 Rows 都未设（或为 0）时：

```cpp
int childCount = 可见子元素数 + FirstColumn;
int cols = (int)std::ceil(std::sqrt((float)childCount));
int rows = (childCount + cols - 1) / cols;  // 向上取整
```

生成的网格接近正方形（列数 ≈ 行数）。

当只设 Columns 时：

```cpp
int rows = (childCount + FirstColumn + cols - 1) / cols;
```

### Content vs Stretch

- **Content**: 所有子元素测量后，取最大 `Desired.w` 和 `Desired.h`，作为单元格尺寸。UniformGrid 报告 `(cellW * cols, cellH * rows)` 作为自身 Desired。
- **Stretch**: 在 Content 尺寸的基础上，若可用空间更大，平分可用空间。`cellW = max(maxChildW, availW / cols)`，永不缩小到 Content 以下。

### FirstColumn 机制

FirstColumn 影响第一个子元素的起始网格坐标。子元素索引 0 放置在 `(col=FirstColumn, row=0)`，后续子元素按从左到右、从上到下填充。

前 `FirstColumn` 个位置留空，不绘制、不命中测试。

### 使用注意事项

1. **Columns 和 Rows 都设时 Rows 无效**: Columns 优先，Rows 被忽略
2. **子元素不会被拉伸到单元格大小**: 子元素按自身 HAlign/VAlign 在单元格内对齐
3. **FirstColumn 不能超过列数**: 超过会导致第一个子元素跳到下一行
4. **自动模式在子元素数变化时重新计算**: 添加/删除子元素会改变行列数

---

## 相关控件

- [Grid](Grid.md) — 行列网格，支持不等宽/高、跨行列
- [WrapPanel](WrapPanel.md) — 自动换行，单元格尺寸可以不同
- [StackPanel](StackPanel.md) — 单向堆叠，不做网格
