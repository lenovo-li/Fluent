# MenuBar 控件

## 概述

窗口顶部的菜单栏（文件 / 编辑 / 视图……）。一行顶层标题画在父窗口的内容层里；点击标题在它
正下方打开对应的 `MenuFlyout`。有菜单打开时，指针移到另一个标题上就切换到那个菜单（经典
菜单栏行为）；再点一次已打开的标题则关闭。打开后的键盘导航（方向键、Enter、Esc、左右进出
子菜单）由 MenuFlyout 处理。

**典型使用场景**:
- 传统桌面应用的主菜单
- 工具类窗口顶部的命令入口

---

## 继承层次

```
Visual
  └─ UIElement
      └─ FrameworkElement
          └─ Control
              └─ MenuBar
```

**命名空间**: `fluent`
**头文件**: `FluentUI/controls/MenuBar.h`

---

## 公开 API

```cpp
MenuBar();
void AddMenu(std::wstring title, std::vector<MenuItem> items);
static RectDip VisibleTitleRect(float titleX, float titleW, const RectDip& bar);
```

- **`AddMenu`** 在末尾追加一个顶层标题及其菜单项。**脏标记: Measure**（期望宽度是所有标题宽度之和）。
  `MenuItem` 的构建与订阅见 [MenuFlyout.md](MenuFlyout.md)。
- **`VisibleTitleRect`** 是纯函数：标题在菜单栏右边缘处被截断后的可绘制部分；标题起点已经在右边缘
  或之外时返回空矩形（这个标题和之后的都不可见）。绘制和命中测试都用它，所以被截掉的标题既不画
  也点不到，部分可见的标题只有可见部分能点。

---

## 代码示例

```cpp
#include "FluentUI/controls/MenuBar.h"

// MenuItem 可移动不可拷贝，没有带文字的构造函数：先建再填字段，再 move 进向量。
std::vector<MenuItem> fileItems;
MenuItem open;
open.text = L"打开...";
open.accelerator = L"Ctrl+O";
open.AddInvokeHandler([this]() { OnOpen(); });   // 菜单项自己持有闭包和订阅
fileItems.push_back(std::move(open));
fileItems.push_back(MenuItem::Sep());
MenuItem quit;
quit.text = L"退出";
quit.AddInvokeHandler([this]() { Close(); });
fileItems.push_back(std::move(quit));

auto* bar = root->Add(std::make_unique<MenuBar>());
bar->AddMenu(L"文件", std::move(fileItems));
```

---

## 备注

### 修过的 bug：运行时追加的菜单看得见、点得到

`AddMenu` 以前只请求 Render。菜单栏布局之后再追加菜单（宽度自适应的槽位、运行时才加的「帮助」
菜单）时，`bounds_` 还是旧宽度，新标题落在外面，被 `VisibleTitleRect` 截掉：不绘制、点不到，
直到别的东西触发重新测量。现在 `AddMenu` 请求 Measure。由 `MenuBarLayout.AddMenuRequestsMeasure` 锁定。

### 性能特征

- **期望尺寸** = min(可用宽度, 标题宽度之和) × 32 DIP；显式 Width / Height 优先。
- **一个 MenuFlyout 复用给所有标题**（借用每个标题的菜单项向量，不复制）。
- 标题宽度在 attach 时（有 DirectWrite 之后）重新测量一次。

### 使用注意事项

- **没有 Alt 助记键**：当前版本只支持鼠标打开顶层菜单。
- **窗口太窄时**尾部标题被截掉而不是折叠进溢出菜单。
- **订阅菜单项事件**的时机和方式见 [MenuFlyout.md](MenuFlyout.md)（`MenuItem` 可移动不可拷贝）。

### 主题 Token

| Token | 用途 |
| --- | --- |
| `accent` | 打开中的标题底色（@ 0.18 alpha）、悬停标题底色（@ 0.10 alpha） |
| `textPrimary` | 标题文字（`EffectiveForeground` 回退值） |

### 演示页

demo 导航 → 窗口 & 对话框 → MenuBar（nav id 602）。

---

## 相关控件

- [MenuFlyout](MenuFlyout.md) — 每个顶层标题打开的下拉菜单
