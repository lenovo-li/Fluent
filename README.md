# FluentUI

一个从零手写的 Windows 桌面 UI 框架 —— 不依赖 WinUI / XAML / WinRT，
基于 Win32 + Direct2D + DirectWrite + DirectComposition，全部 C++20。

元素模型、布局、输入路由、主题系统、合成器集成、UI Automation 均为手写实现。
API 刻意对齐 WPF/WinUI 的心智模型（Measure/Arrange、DirtyFlags、路由事件、
token 化主题），有 XAML 经验即可平移。

## 演示

仓库暂无截图。构建后运行 `x64/Debug/FluentUIDemo.exe`，左侧导航列出全部控件页
和框架功能演示页，展示各控件的状态、样式与交互效果。F12 打开性能 HUD。

## 特性

- **WPF 式事件模型**：`Event<Sender, Args>` 多订阅者 + `Subscription` RAII 退订，
  零堆分配（函数指针槽位，非 `std::function`）。UIElement 暴露指针/键盘/焦点/
  尺寸/右键菜单等基础事件，控件另有语义事件（Click、TextChanged、
  ScrollChanged、ValueChanging…）
- **retained-mode 元素树**：Visual → UIElement → FrameworkElement → Control/Panel
  四层，每层只加一个关注点
- **WPF 式两趟布局**：Measure/Arrange，带 Measure 缓存短路
- **DirectComposition 渲染**：窗口内容绘制在 DComp virtual surface 上，局部重绘；
  属性动画跑在合成器线程（ProgressBar 不定态扫光），UI 线程繁忙也不卡
- **UI Automation**：服务端 provider 手写实现，按钮、开关、范围、文本、列表、
  树、表格、选项卡等控件有对应 peer，屏幕阅读器和 UIA 自动化工具可识别
- **文本虚拟化**：TextArea 的 NoWrap 模式精确虚拟化（28 MB / 20 万行秒开），
  Wrap 模式按段估算虚拟化；高吞吐 log 追加（AppendText O(1)，SetMaxLines 环形上限）
- **托盘图标集成**：单窗口设计（无需隐藏消息窗口），RAII 生命周期管理，
  支持气泡通知和右键菜单；`demos/TrayAppDemo` 演示关闭到托盘
- **主题 token 化**：亮/暗双主题，控件零硬编码颜色
- **每窗口 DPI**：PerMonitorV2，跨屏拖动实时切换
- **idle 零 CPU**：无轮询定时器，消息循环无事时 INFINITE 阻塞

## 控件

每个控件的 API 参考在 [`docs/controls/`](docs/controls/README.md)。

基础：Button / CheckBox / RadioButton / ToggleSwitch / Slider / ProgressBar /
TextBlock / TextBox / PasswordBox / Hyperlink / Image / NumericUpDown

选择与导航：ComboBox（可编辑）/ ListBox（虚拟化，O(visible) 绘制）/
TreeView（虚拟化，双击展开/折叠）/ DataGrid（虚拟化，列定义、排序、单元格编辑）/
TabControl / MenuBar / MenuFlyout（上下文菜单、下拉、子菜单）/
ToolBar（命令条，溢出进 flyout）/ StatusBar

文本：TextArea（NoWrap/Wrap 两模式虚拟化，28 MB 秒开）/ RichTextBox（段落缩进）/
LogViewer（高吞吐 log 追加）

状态与反馈：InfoBar（内联状态消息，四个严重级别）

对话框：ContentDialog / MessageDialog / OpenFileDialog / SaveFileDialog（系统文件对话框封装，
见 `dialogs/FileDialog.h`）

分组：GroupBox / Expander

窗口与系统集成：Window（含托盘图标支持，见 `window/TrayIcon.h` 和 `demos/TrayAppDemo`）；
工具提示不是独立控件，通过 `UIElement::SetTooltip` 设置

布局：StackPanel / Grid / DockPanel / Canvas / WrapPanel / UniformGrid / Border /
Viewbox（缩放）/ ScrollPanel（通用可滚动容器，不虚拟化）/ GridSplitter（可拖动分隔条）

内部零件：`layout/ScrollViewer.h` 是滚动条引擎（滚动状态、thumb 几何、
平滑滚动动画、自动淡出/悬停展开），被 ListBox/TreeView/DataGrid/TextArea/RichTextBox/
LogViewer/ScrollPanel 内嵌复用（ComboBox 的下拉列表自己算滚动，不用它）。
`controls/primitives/` 下的 ButtonBase / ToggleButton / RangeBase / ItemsControl /
Selector 是控件基类，供继承用。

### 扩展库

自定义控件放在链接 `FluentUI.lib` 的独立静态库里，而不是改框架源码。
`extensions/` 下有三个可用的范例，demo 和测试都链接它们：

| 库 | 内容 |
|---|---|
| `extensions/CustomControls` | BigButton、LogSlider、SpinnerControl（继承模式的模板） |
| `extensions/InstallerControls` | WizardControl、FolderPicker |
| `extensions/NavigationControls` | NavRail、PageHost、OverlayHost |

每个控件哪些成员是 `protected`、为什么，见
[`docs/controls/inheritance-guide.md`](docs/controls/inheritance-guide.md)。

## 构建

### 要求

- **工具集**：Visual Studio 2026（v18，MSVC v145）。VS 2022 只带 v143，构建报 MSB8020；
  库是静态库、无 ABI 稳定性，调用方也必须用同一工具集
- **C++ 标准**：库本身以 C++20 编译；调用方的源文件最低 `/std:c++17`（公开头文件用到
  `std::optional`、`template <auto>`、`inline` 变量）。`examples/Minimal/MinimalCpp17.vcxproj`
  就是这条下限的验证工程，`MinimalCpp14.vcxproj` 预期编译失败
- **平台**：Windows 10 1803+ SDK，仅 x64

### 构建步骤

最省事的方式是在 VS 里直接打开 `Fluent.slnx`。命令行用仓库自带的脚本，
它通过 `vswhere.exe` 定位 MSBuild，不依赖某台机器的安装路径：

```bash
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/build.ps1 -Configuration Debug
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/build.ps1 -Configuration Release
```

C++ 运行时默认静态链接（`/MT`）；加 `-CRTLinkage Dynamic` 改为 `/MD`。注意直接调用
MSBuild 且不传 `-p:FluentCRTLinkage=...` 时，`Directory.Build.props` 的默认值是 Dynamic。
两种模式的取舍见 [`docs/Build-CRT-Linkage.md`](docs/Build-CRT-Linkage.md)。

CMake 是备用构建路径（CLion / VS Code CMake Tools / 纯命令行）：

```bash
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build build
```

CMake 产出静态库 `FluentUI.lib`（及 `FluentSettings.lib`，`FLUENTUI_BUILD_SETTINGS`
默认 ON），提供安装/`find_package` 支持；`-DFLUENTUI_BUILD_EXAMPLES=ON` 一并构建
`examples/Minimal`，`-DFLUENTUI_STATIC_RUNTIME=OFF` 切到动态 CRT。
`examples/PackageConsumer` 演示安装后用 `find_package` 消费。测试、demo 和扩展库
只在 MSBuild 工程里定义，跑测试请走 `build.ps1`。

## 第一个窗口

可编译的最小示例是 `examples/Minimal/main.cpp`（约 40 行）。要点：`Application app(instance)`
没有默认构造；`SetRoot` 是 protected，控件树在 `Window` 子类的 `OnInitialize()` 里构建；
`Subscribe` 返回的 `Subscription` 必须存成成员，丢弃即退订。

```cpp
#include <FluentUI/FluentUI.h>

using namespace fluent;

class MainWindow final : public Window {
public:
    MainWindow() { SetTitle(L"Hello"); SetClientSize(640.0f, 360.0f); }
protected:
    void OnInitialize() override {
        auto root = std::make_unique<StackPanel>();
        auto* button = root->Emplace<Button>();
        button->SetText(L"Close");
        closeSub_ = button->Click().Subscribe<&MainWindow::OnClose>(this);
        SetRoot(std::move(root));
    }
private:
    void OnClose(Button&, RoutedEventArgs&) { Close(); }
    Subscription closeSub_;
};

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int) {
    Application app(instance);
    MainWindow window;
    if (FAILED(window.Show(app))) return 1;
    return app.Run();
}
```

## 项目

| 项目 | 类型 | 说明 |
|---|---|---|
| `FluentUI` | 静态库 | 框架本体 |
| `FluentUIDemo` | GUI 应用 | 验收 demo + 参考消息循环 |
| `FluentUITests` | 控制台 | 零依赖 headless 测试（数量见运行输出末行 summary） |
| `FluentUIBench` | 控制台 | 性能场景（CPU 可测的跑，GPU 的打桩）；`--gate` 超预算时 exit 1 |
| `FluentSettings` | 静态库 | JSON 配置持久化（内置 nlohmann/json） |
| `extensions/*` | 静态库 | 扩展控件库，见上文「扩展库」 |
| `demos/TrayAppDemo` | GUI 应用 | 关闭到托盘示例 |
| `examples/Minimal`、`examples/PackageConsumer` | GUI 应用 | 最小消费者（C++14/17/20 下限检查、CMake `find_package`） |

## 测试

```bash
./x64/Debug/FluentUITests.exe          # 全量
./x64/Debug/FluentUITests.exe Opacity  # 按 "Suite.Name" 子串过滤
./x64/Release/FluentUIBench.exe --gate # 性能预算门禁
python scripts/check_all.py            # 源文件列表、Solution Explorer 树、文档覆盖检查
```

跑仓库根目录 `x64/Debug/` 下的测试二进制。只构建测试工程会输出到 `tests/x64/Debug/`，
那一份会过期。

## 文档

- [`docs/controls/`](docs/controls/README.md)：每个控件的 API 参考与行为契约
- [`docs/controls/inheritance-guide.md`](docs/controls/inheritance-guide.md)：继承扩展点
- [`docs/api-surface.md`](docs/api-surface.md)：公开 API 约定
- [`docs/BUILD_OPTIMIZATION.md`](docs/BUILD_OPTIMIZATION.md)、[`docs/Build-CRT-Linkage.md`](docs/Build-CRT-Linkage.md)：构建
- `docs/learn/`：技术笔记（合成器裁剪、无边框窗口 Resize、DirectWrite 字体回退、UI Automation）
- `CLAUDE.md` 与 `.claude/rules/`：架构说明（元素模型、帧管线、DComp 渲染契约、
  脏区/透明度/主题约定）和每个新控件的准入标准

## 已知限制

- **仅支持 x64**：Win32/x86 和 ARM64 未构建测试。`Fluent.slnx` 定义有 Win32 平台，
  未维护，在 VS 里请选 x64。
- **无 RTL 布局**：从右到左语言（阿拉伯语、希伯来语）未测试，无 FlowDirection 支持。
- **静态库，无 ABI 稳定性**：消费者需匹配 MSVC 工具集版本重新编译。
- **视觉正确性手动验证**：无自动化截图回归测试。

## 参与贡献

见 [`CONTRIBUTING.md`](CONTRIBUTING.md)。两条最容易被忽略的硬性要求：

1. 新增 `.cpp` 必须同时注册到 `FluentUI.vcxproj`、`.vcxproj.filters` 和 `CMakeLists.txt`
   （都没用 glob，`python scripts/check_all.py` 会检查三者是否同步）
2. 新测试必须做变异验证——把修复改回坏的样子，确认测试真的会红

## 第三方依赖

- **nlohmann/json** (MIT) — `FluentSettings/third_party/nlohmann/json.hpp`  
  JSON for Modern C++ v3.12.0, Copyright (c) 2013-2026 Niels Lohmann  
  https://github.com/nlohmann/json

## 许可

MIT，见 `LICENSE`。
