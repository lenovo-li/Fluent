# ToolBar 控件

## 概述

水平命令条，自动处理按钮溢出。当可用宽度不足以显示所有按钮时，多余的按钮收入"..."溢出菜单。每个按钮自动设为 `Kind::Subtle` 样式（静止透明、悬停显示背景），符合工具栏视觉规范。支持分隔线，触发溢出时显示 MenuFlyout。

**典型使用场景**:
- 应用程序主工具栏（文件操作、编辑命令）
- 富文本编辑器格式栏（粗体、斜体、对齐）
- 数据表格操作栏（刷新、导出、筛选）

**命名空间**: `fluent`  
**头文件**: `FluentUI/controls/ToolBar.h`

---

## 继承层次

```
Visual
  └─ UIElement
      └─ FrameworkElement
          └─ Panel
              └─ ToolBar
```

---

## 公开 API

### 方法

#### AddButton

```cpp
void AddButton(std::unique_ptr<Button> btn, std::function<void()> onClick);
```

**参数**:
- `btn` — 按钮（所有权转移给 ToolBar）
- `onClick` — 点击回调（存储为 `std::function`，可以是 lambda）

**说明**: 添加按钮到工具栏。ToolBar 自动将按钮 `Kind` 设为 `Subtle`，并订阅 Click 事件。按钮文本或图标由调用方在传入前设置。

#### AddSeparator

```cpp
void AddSeparator();
```

**说明**: 添加垂直分隔线（1 DIP 宽，高度与按钮相同）。溢出时分隔线也收入菜单，显示为 `MenuItem::Sep()`。

### 查询

```cpp
int VisibleButtonCount() const;
int TotalButtonCount() const;
bool HasOverflow() const;
```

**VisibleButtonCount**: 当前宽度下可见的按钮数（不含溢出的）  
**TotalButtonCount**: 总按钮数（含溢出的）  
**HasOverflow**: 是否有按钮溢出（溢出按钮数 > 0）

### 事件

#### OverflowOpened

```cpp
Event<ToolBar, RoutedEventArgs>& OverflowOpened();
```

**说明**: 溢出菜单打开时触发。可用于动态更新菜单内容或记录用户操作。

---

## 代码示例

### 最小示例

```cpp
#include <FluentUI/FluentUI.h>
#include "FluentUI/controls/ToolBar.h"

using namespace fluent;

class MainWindow final : public Window {
public:
    MainWindow() {
        SetTitle(L"ToolBar Example");
        SetClientSize(800.0f, 600.0f);
    }

protected:
    void OnInitialize() override {
        auto toolbar = std::make_unique<ToolBar>();

        auto btnNew = std::make_unique<Button>();
        btnNew->SetText(L"新建");
        toolbar->AddButton(std::move(btnNew), [this]() {
            // 新建文件
        });

        auto btnOpen = std::make_unique<Button>();
        btnOpen->SetText(L"打开");
        toolbar->AddButton(std::move(btnOpen), [this]() {
            // 打开文件
        });

        toolbar->AddSeparator();

        auto btnSave = std::make_unique<Button>();
        btnSave->SetText(L"保存");
        toolbar->AddButton(std::move(btnSave), [this]() {
            // 保存文件
        });

        SetRoot(std::move(toolbar));
    }
};

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int) {
    Application app(instance);
    MainWindow window;
    if (FAILED(window.Show(app))) return 1;
    return app.Run();
}
```

### 带溢出事件

```cpp
auto toolbar = std::make_unique<ToolBar>();

for (int i = 1; i <= 12; ++i) {
    auto btn = std::make_unique<Button>();
    btn->SetText(L"命令" + std::to_wstring(i));
    toolbar->AddButton(std::move(btn), [i]() {
        // 执行命令 i
    });
}

sub_ = toolbar->OverflowOpened().Subscribe<&MyWindow::OnOverflow>(this);
// ...
void MyWindow::OnOverflow(ToolBar&, RoutedEventArgs&) {
    // 溢出菜单打开
}
Subscription sub_;
```

---

## 备注

### 性能特征

- **Measure**: O(按钮数)，测量所有按钮，计算可见数量 `visibleCount_`
- **Arrange**: O(按钮数)，可见按钮正常排布，溢出按钮排布到 `{0, -10000, 0, 0}` (屏幕外)
- **Render**: 绘制可见按钮和溢出"..."按钮（若 `HasOverflow()`）
- **溢出计算**: 每次 Measure 重新计算，宽度变化时可见按钮数可能变化
- **Idle 成本**: 零
- **脏标记**: 添加按钮触发 Measure

### 溢出机制

**MeasureOverride**:
1. 测量所有按钮和分隔线
2. 从左到右累加宽度（按钮宽 + `kItemSpacing`）
3. 累加超出 `availW - overflowBtnWidth_` 时，剩余按钮标记为溢出
4. 记录 `visibleCount_` 和 `overflowActive_`

**ArrangeOverride**:
- 可见按钮正常排布（从 `content.x` 开始）
- 溢出按钮排布到 `{0, -10000, 0, 0}`（屏幕外，不可见但保留 Desired 尺寸）
- 溢出按钮的 `Desired` 仍参与父容器的测量，所以 ToolBar 的自然宽度是所有按钮的总宽（不是可见宽度）

**溢出菜单**:
- 点击"..."按钮时调用 `ShowOverflowMenu()`
- `BuildOverflowItems()` 遍历溢出按钮，生成 `MenuItem`（文本从 `Button::Text()` 复制，点击回调从存储的 `clickHandler_` 读取）
- 分隔线在菜单中显示为 `MenuItem::Sep()`

### 常量

```cpp
kButtonHeight = 32.0f;        // 按钮高度
kSeparatorWidth = 1.0f;       // 分隔线宽度
kItemSpacing = 4.0f;          // 按钮/分隔线之间的间距
overflowBtnWidth_ = 32.0f;    // "..."按钮宽度（成员变量）
```

### 按钮自动配置

`AddButton` 时自动执行：
- `btn->SetKind(Button::Kind::Subtle)` — 工具栏统一样式
- 订阅 `btn->Click()`，存储 Subscription 到 `Item::clickSub_`
- 存储 `onClick` 回调到 `Item::clickHandler_`（溢出菜单项需要）

### 内部 Item 结构

```cpp
struct Item {
    Button* button = nullptr;        // borrowed; owned by Panel::children_
    std::function<void()> clickHandler;
    Subscription clickSub;
    bool isSeparator = false;
    float width = 0.0f;
};
```

每个按钮、分隔线都包装为 `Item`。分隔线的 `button` 为 `nullptr`，通过 `isSeparator` 标志区分。

### 使用注意事项

1. **按钮 Kind 被强制设为 Subtle**: 传入前设置的 Kind 会被覆盖
2. **onClick 必须可拷贝**: `std::function` 存储回调，不能是仅移动类型
3. **溢出按钮仍在子元素列表中**: 排布到屏幕外，命中测试不可达，但参与 Measure
4. **窗口缩放时自动重排**: 宽度变化触发 Measure，可见按钮数自动调整
5. **分隔线不触发溢出**: 只有按钮计入 `TotalButtonCount()`，分隔线不计数

---

## 相关控件

- [MenuBar](MenuBar.md) — 菜单栏，顶层菜单项展开子菜单
- [Button](Button.md) — ToolBar 使用的按钮控件
- [MenuFlyout](MenuFlyout.md) — 溢出菜单的实现
