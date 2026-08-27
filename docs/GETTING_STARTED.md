# FluentUI 入门教程

本教程将引导你从零开始使用 FluentUI 构建一个简单的桌面应用程序。

---

## 目录

1. [环境准备](#1-环境准备)
2. [构建 FluentUI](#2-构建-fluentui)
3. [创建第一个窗口](#3-创建第一个窗口)
4. [添加控件](#4-添加控件)
5. [处理事件](#5-处理事件)
6. [使用布局容器](#6-使用布局容器)
7. [下一步](#7-下一步)

---

## 1. 环境准备

### 系统要求

- **操作系统**: Windows 10 1803+ 或 Windows 11
- **编译器**: Visual Studio 2022+（v145 工具集）
- **SDK**: Windows 10 SDK

### 安装 Visual Studio

1. 下载 [Visual Studio 2022 Community](https://visualstudio.microsoft.com/downloads/)
2. 安装时选择以下工作负载：
   - **使用 C++ 的桌面开发**
   - 确保包含 **Windows 10 SDK**

---

## 2. 构建 FluentUI

### 2.1 克隆仓库

```bash
git clone https://github.com/your-org/FluentUI.git
cd FluentUI
```

### 2.2 使用 MSBuild 构建

打开 **Developer Command Prompt for VS 2022**，执行：

```bash
# 设置 MSBuild 路径（根据你的 Visual Studio 安装位置调整）
set MSBUILD="C:\Program Files\Microsoft Visual Studio\2022\Community\MSBuild\Current\Bin\MSBuild.exe"

# 构建 Debug 版本
%MSBUILD% Fluent.slnx -p:Configuration=Debug -p:Platform=x64

# 构建 Release 版本
%MSBUILD% Fluent.slnx -p:Configuration=Release -p:Platform=x64
```

构建成功后，生成物位于：
- `x64/Debug/FluentUI.lib`（静态库）
- `x64/Debug/FluentUIDemo.exe`（示例程序）

### 2.3 运行示例程序

```bash
x64\Debug\FluentUIDemo.exe
```

你应该看到一个展示所有控件的 Demo 窗口。

---

## 3. 创建第一个窗口

### 3.1 项目设置

创建一个新的 C++ 控制台项目，或使用 `examples/Minimal` 作为模板。

下面用 `<仓库根>` 指代你 clone 到的位置，按实际路径替换。

**包含目录**（项目属性 → C/C++ → 常规 → 附加包含目录）:
```
<仓库根>\FluentUI
```

**库目录**（项目属性 → 链接器 → 常规 → 附加库目录）:
```
<仓库根>\x64\Debug
```

**附加依赖项**（项目属性 → 链接器 → 输入）:
```
FluentUI.lib
d2d1.lib
dwrite.lib
dcomp.lib
windowscodecs.lib
```

**子系统**（项目属性 → 链接器 → 系统 → 子系统）:
```
Windows (/SUBSYSTEM:WINDOWS)
```

### 3.2 最小窗口代码

创建 `main.cpp`：

```cpp
#include <FluentUI/FluentUI.h>

using namespace FluentUI;

int WINAPI wWinMain(
    _In_ HINSTANCE hInstance,
    _In_opt_ HINSTANCE hPrevInstance,
    _In_ LPWSTR lpCmdLine,
    _In_ int nShowCmd)
{
    // 初始化 FluentUI 应用程序（管理 COM 公寓、消息循环）
    Application app;
    
    // 创建主窗口
    auto window = std::make_unique<Window>();
    window->SetTitle(L"我的第一个 FluentUI 应用");
    window->SetSize({800, 600});
    
    // 显示窗口
    window->Show();
    
    // 运行消息循环（阻塞直到窗口关闭）
    return app.Run();
}
```

**编译运行**: 你应该看到一个标题为"我的第一个 FluentUI 应用"的空白窗口。

---

## 4. 添加控件

### 4.1 添加一个按钮

修改 `main.cpp`，在 `window->Show()` 之前添加：

```cpp
// 创建一个按钮
auto button = std::make_unique<Button>();
button->SetText(L"点击我");

// 设置为窗口内容
window->SetContent(std::move(button));
```

**完整代码**：

```cpp
#include <FluentUI/FluentUI.h>

using namespace FluentUI;

int WINAPI wWinMain(HINSTANCE, HINSTANCE, LPWSTR, int)
{
    Application app;
    
    auto window = std::make_unique<Window>();
    window->SetTitle(L"按钮示例");
    window->SetSize({400, 300});
    
    auto button = std::make_unique<Button>();
    button->SetText(L"点击我");
    
    window->SetContent(std::move(button));
    window->Show();
    
    return app.Run();
}
```

运行后，你会看到窗口中央有一个按钮（默认居中对齐）。

### 4.2 添加文本标签

让我们添加一个 TextBlock 来显示文本：

```cpp
auto text = std::make_unique<TextBlock>();
text->SetText(L"欢迎使用 FluentUI!");
text->SetFontSize(24.0f);

window->SetContent(std::move(text));
```

---

## 5. 处理事件

FluentUI 使用类型安全的事件系统。每个事件返回一个 `Subscription` 对象（RAII），**你必须保存这个返回值**，否则订阅立即失效。

### 5.1 基础事件订阅

修改按钮代码，添加 Click 事件处理：

```cpp
auto button = std::make_unique<Button>();
button->SetText(L"点击我");

// Subscribe 返回 Subscription —— 必须保存到作用域覆盖整个需要回调的时期。
// 这里存成局部变量 clickSub，它活到 app.Run() 结束，所以整个消息循环期间有效。
// 如果写成 button->Click().Subscribe(...) 丢弃返回值，回调会立即被取消。
Subscription clickSub = button->Click().Subscribe(
    nullptr, [](void*, Button& sender, RoutedEventArgs&) {
        sender.SetText(L"已点击!");
    });

window->SetContent(std::move(button));
```

现在点击按钮时，按钮文本会变为"已点击!"。

**重要**：`Subscribe` 的 handler 签名是 `void(*)(void* owner, Sender&, Args&)`，不是捕获 lambda —— FluentUI 的事件系统刻意不支持捕获 lambda（那会破坏零分配设计）。需要状态就放在 `owner` 对象上，见下节。

### 5.2 访问外部状态

如果需要在事件处理中访问其他控件，把它们放在 owner 对象上，或者作为裸指针传给 owner：

```cpp
// 方案 A：把状态封装进一个结构，作为 owner
struct PageState {
    TextBlock* textPtr;
    int clickCount = 0;
    Subscription buttonSub;  // Subscription 也存在这里，PageState 析构时自动取消
};

auto state = std::make_unique<PageState>();

auto text = std::make_unique<TextBlock>();
text->SetText(L"点击次数: 0");
state->textPtr = text.get();

auto button = std::make_unique<Button>();
button->SetText(L"点击我");

state->buttonSub = button->Click().Subscribe(
    state.get(), [](void* owner, Button&, RoutedEventArgs&) {
        auto* s = static_cast<PageState*>(owner);
        s->clickCount++;
        s->textPtr->SetText(L"点击次数: " + std::to_wstring(s->clickCount));
    });

// 注意：这里需要布局容器来同时显示两个控件（见下一节）
// 并且 state 要保持活着（比如存成窗口的成员或局部变量活到 app.Run() 结束）
```

---

## 6. 使用布局容器

### 6.1 StackPanel（栈式布局）

要在窗口中显示多个控件，需要使用布局容器。最简单的是 StackPanel：

```cpp
#include <FluentUI/FluentUI.h>

using namespace FluentUI;

int WINAPI wWinMain(HINSTANCE, HINSTANCE, LPWSTR, int)
{
    Application app;
    
    auto window = std::make_unique<Window>();
    window->SetTitle(L"布局示例");
    window->SetSize({400, 300});
    
    // 创建垂直 StackPanel
    auto stack = std::make_unique<StackPanel>();
    stack->SetOrientation(Orientation::Vertical);
    stack->SetSpacing(10.0f);  // 控件间距 10 DIP
    
    // 添加标题
    auto title = std::make_unique<TextBlock>();
    title->SetText(L"计数器应用");
    title->SetFontSize(24.0f);
    stack->AddChild(std::move(title));
    
    // 添加计数显示
    auto countText = std::make_unique<TextBlock>();
    countText->SetText(L"点击次数: 0");
    TextBlock* countPtr = countText.get();
    stack->AddChild(std::move(countText));
    
    // 添加按钮
    auto button = std::make_unique<Button>();
    button->SetText(L"增加计数");
    button->SetKind(Button::Kind::Accent);
    
    // 计数放在 owner 指向的对象上；Subscription 存成局部变量，活到 app.Run() 结束
    struct Counter { TextBlock* label; int value = 0; };
    Counter counter{countPtr};
    Subscription btnSub = button->Click().Subscribe(
        &counter, [](void* owner, Button&, RoutedEventArgs&) {
            auto* c = static_cast<Counter*>(owner);
            c->value++;
            c->label->SetText(L"点击次数: " + std::to_wstring(c->value));
        });
    
    stack->AddChild(std::move(button));
    
    // 设置 StackPanel 为窗口内容
    window->SetContent(std::move(stack));
    window->Show();
    
    return app.Run();
}
```

### 6.2 Grid（网格布局）

Grid 提供行列布局，适合表单界面：

```cpp
// 创建 2 行 2 列的 Grid
auto grid = std::make_unique<Grid>();
grid->SetRowDefinitions({ GridLength::Auto(), GridLength::Auto() });
grid->SetColumnDefinitions({ GridLength::Auto(), GridLength::Star(1.0) });

// 第一行：标签 + 文本框
auto nameLabel = std::make_unique<TextBlock>();
nameLabel->SetText(L"姓名:");
Grid::SetRow(nameLabel.get(), 0);
Grid::SetColumn(nameLabel.get(), 0);
grid->AddChild(std::move(nameLabel));

auto nameBox = std::make_unique<TextBox>();
nameBox->SetPlaceholder(L"请输入姓名");
Grid::SetRow(nameBox.get(), 0);
Grid::SetColumn(nameBox.get(), 1);
grid->AddChild(std::move(nameBox));

// 第二行：按钮
auto submitBtn = std::make_unique<Button>();
submitBtn->SetText(L"提交");
submitBtn->SetKind(Button::Kind::Accent);
Grid::SetRow(submitBtn.get(), 1);
Grid::SetColumn(submitBtn.get(), 1);
Grid::SetHorizontalAlignment(submitBtn.get(), HAlign::Right);
grid->AddChild(std::move(submitBtn));

window->SetContent(std::move(grid));
```

### 6.3 其他布局容器

- **Canvas**: 绝对定位（Left/Top 坐标）
- **Border**: 添加边框和背景
- **ScrollPanel**: 可滚动容器（内容超出时显示滚动条）

详见 [布局指南](LAYOUT_GUIDE.md)。

---

## 7. 下一步

恭喜！你已经掌握了 FluentUI 的基础知识。接下来可以：

### 深入学习

- **[控件 API 参考](controls/README.md)** — 查看所有可用控件的详细文档
- **[布局指南](LAYOUT_GUIDE.md)** — 学习 Measure/Arrange 布局系统
- **[主题定制指南](THEMING_GUIDE.md)** — 自定义应用配色
- **[性能最佳实践](PERFORMANCE_BEST_PRACTICES.md)** — 构建高性能应用

### 示例程序

- **FluentUIDemo** (`x64/Debug/FluentUIDemo.exe`) — 所有控件的展示
- **examples/Minimal** — 最小应用程序模板
- **examples/PackageConsumer** — CMake 消费示例

### 进阶主题

- **虚拟化控件**: ListBox、TreeView、TextArea 的大数据集处理
- **合成器动画**: 利用 DirectComposition 实现流畅动画
- **拖放**: 实现文件拖放、自定义拖放行为
- **自定义控件**: 继承 Control/Panel 创建自己的控件

---

## 常见问题

### Q: 编译时找不到 FluentUI.h？

**A**: 检查项目属性中的"附加包含目录"是否正确指向 FluentUI 源码目录。

### Q: 链接错误：无法解析的外部符号？

**A**: 检查：
1. "附加库目录"是否包含 FluentUI.lib 所在目录
2. "附加依赖项"是否包含 `FluentUI.lib` 和所有系统库（d2d1.lib 等）
3. Debug/Release 配置是否匹配（Debug 版本不能链接 Release 库）

### Q: 运行时崩溃：找不到 DLL？

**A**: FluentUI 是静态库，不需要额外的 DLL。如果崩溃，检查：
1. 是否调用了 `Application app;` 初始化应用程序
2. 是否在 `wWinMain` 而非 `main` 入口
3. 子系统设置是否为 Windows（不是 Console）

### Q: 窗口显示为空白？

**A**: 确保调用了 `window->SetContent(...)`，并且内容控件不为空。

### Q: 控件消失或尺寸为零？

**A**: 检查：
1. 布局容器是否正确设置（StackPanel 需要 SetOrientation）
2. Grid 的行列定义是否存在
3. 控件是否有自然尺寸（Button 有默认尺寸，TextBlock 需要有文本）

---

## 获取帮助

- **仓库 Issues**: [GitHub Issues](https://github.com/your-org/FluentUI/issues)
- **架构文档**: [CLAUDE.md](../CLAUDE.md)
- **API 文档**: [docs/controls/](controls/)

---

**祝你使用 FluentUI 构建出色的桌面应用！** 🎉
