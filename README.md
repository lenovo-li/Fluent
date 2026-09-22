# FluentUI

一个从零手写的 Windows 桌面 UI 框架 —— 不依赖 WinUI / XAML / WinRT，
基于 Win32 + Direct2D + DirectWrite + DirectComposition，全部 C++20。

元素模型、布局、输入路由、主题系统、合成器集成均为手写实现。
API 刻意对齐 WPF/WinUI 的心智模型（Measure/Arrange、DirtyFlags、路由事件、
token 化主题），有 XAML 经验即可平移。

## 截图

演示应用展示了各种控件的状态、样式与交互效果。

## 特性

- **WPF 式事件模型**：`Event<Sender, Args>` 多订阅者 + `Subscription` RAII 退订，
  零堆分配（函数指针槽位，非 `std::function`）。UIElement 暴露 13 个基础事件
  （指针/键盘/焦点/尺寸/右键菜单），控件另有语义事件（Click、TextChanged、
  ScrollChanged、ValueChanging…）
- **retained-mode 元素树**：Visual → UIElement → FrameworkElement → Control/Panel
  四层，每层只加一个关注点
- **WPF 式两趟布局**：Measure/Arrange，带 Measure 缓存短路
- **DirectComposition 渲染**：窗口内容绘制在 DComp virtual surface 上，
  属性动画跑在合成器线程（ProgressBar 不定态扫光），UI 线程繁忙也不卡
- **文本虚拟化**：TextArea 的 NoWrap 模式精确虚拟化（28 MB / 20 万行秒开），
  Wrap 模式按段估算虚拟化；高吞吐 log 追加（AppendText O(1)，SetMaxLines 环形上限）
- **托盘图标集成**：单窗口设计（无需隐藏消息窗口），RAII 生命周期管理，
  支持气泡通知和右键菜单；`demos/TrayAppDemo` 演示最小化到托盘
- **主题 token 化**：亮/暗双主题，控件零硬编码颜色
- **每窗口 DPI**：PerMonitorV2，跨屏拖动实时切换
- **idle 零 CPU**：无轮询定时器，消息循环无事时 INFINITE 阻塞

## 控件

基础：Button / RepeatButton / CheckBox / RadioButton / ToggleSwitch / Slider / ProgressBar /
TextBlock / TextBox / PasswordBox / Hyperlink / Image / Separator / Tooltip

选择与导航：ComboBox（可编辑）/ ListBox（虚拟化，O(visible) 绘制）/
TreeView（虚拟化，双击展开/折叠）/ DataGrid（虚拟化，列定义、排序、单元格编辑）/
TabControl / MenuBar / MenuFlyout（上下文菜单、下拉、子菜单）/
ToolBar（命令条，溢出进 flyout）/ StatusBar

文本与日期：TextArea（NoWrap/Wrap 两模式虚拟化，28 MB 秒开）/ RichTextBox（段落缩进）/
LogViewer（高吞吐 log 追加）/ DatePicker / Calendar

对话框：ContentDialog / MessageDialog

分组：GroupBox / Expander

窗口与系统集成：Window（含托盘图标支持，见 `window/TrayIcon.h` 和 `demos/TrayAppDemo`）

布局：StackPanel / Grid / DockPanel / Canvas / WrapPanel / Border / ScrollPanel（通用可滚动容器，不虚拟化）/ GridSplitter（可拖动分隔条）

另有一个内部零件：`layout/ScrollViewer.h` 是滚动条引擎（滚动状态、thumb 几何、
平滑滚动动画、自动淡出/悬停展开），被 ListBox/TreeView/TextArea/ScrollPanel/ComboBox
内嵌复用。

## 构建

需要 Visual Studio 2022+、Windows 10 1803+ SDK。仅 x64。

最省事的方式是在 VS 里直接打开 `Fluent.slnx`。命令行构建两条路都可以，
用 `vswhere` 定位安装，避免把某台机器的路径写死（版本与版次因机器而异）：

```bash
# MSBuild：定位 vswhere -> 找到 MSBuild.exe -> 构建
VSWHERE="/c/Program Files (x86)/Microsoft Visual Studio/Installer/vswhere.exe"
MSBUILD=$("$VSWHERE" -latest -requires Microsoft.Component.MSBuild \
    -find 'MSBuild/**/Bin/MSBuild.exe' | tr -d '\r')
"$MSBUILD" Fluent.slnx -p:Configuration=Debug -p:Platform=x64
```

```bash
# CMake：VS 自带一份，无需单独安装（下面用 vswhere 找到它）
VSROOT=$("$VSWHERE" -latest -property installationPath | tr -d '\r')
CMAKE="$VSROOT/Common7/IDE/CommonExtensions/Microsoft/CMake/CMake/bin/cmake.exe"
"$CMAKE" -S . -B build -A x64        # 生成器用默认的最新 VS 即可
"$CMAKE" --build build --config Debug
```

CMake 构建产出静态库 `FluentUI.lib`（及可选的 `FluentSettings.lib`），
并提供安装/`find_package` 支持；`examples/Minimal` 是最小消费示例
（用 `-DFLUENTUI_BUILD_EXAMPLES=ON` 一并构建）。测试与 demo 只在
MSBuild 工程里定义，跑测试请走上面的 MSBuild 路径。

## 项目

| 项目 | 类型 | 说明 |
|---|---|---|
| FluentUI | 静态库 | 框架本体 |
| FluentUIDemo | GUI 应用 | 验收 demo + 参考消息循环 |
| FluentUITests | 控制台 | 零依赖 headless 测试（数量见运行输出末行 summary） |
| FluentUIBench | 控制台 | 性能场景（CPU 可测的跑，GPU 的打桩） |
| FluentSettings | 静态库 | JSON 配置持久化 |

## 测试

```bash
./x64/Debug/FluentUITests.exe          # 全量
./x64/Debug/FluentUITests.exe Opacity  # 按 "Suite.Name" 子串过滤
```

## 架构文档

见 `CLAUDE.md`（构建命令、帧管线、DComp 渲染契约、脏区/透明度/主题约定，
以及每个新控件的准入标准）。

## 已知限制

- **无障碍功能未实现**：尚无 UI Automation 支持，屏幕阅读器无法识别界面内容。有无障碍
  合规要求的应用不应在此功能补齐前采纳 FluentUI。
- **仅支持 x64**：Win32/x86 和 ARM64 未构建测试。
- **无 RTL 布局**：从右到左语言（阿拉伯语、希伯来语）未测试。
- **静态库，无 ABI 稳定性**：消费者需匹配 MSVC 工具集版本重新编译。
- **视觉正确性手动验证**：无自动化截图回归测试。

## 参与贡献

见 `CONTRIBUTING.md`。两条最容易被忽略的硬性要求：新增 `.cpp` 必须同时注册到
`.vcxproj` 和 `CMakeLists.txt`（两边都没用 glob），以及新测试必须做变异验证
——把修复改回坏的样子，确认测试真的会红。

## 第三方依赖

- **nlohmann/json** (MIT) — `FluentSettings/third_party/nlohmann/json.hpp`  
  JSON for Modern C++ v3.12.0, Copyright (c) 2013-2026 Niels Lohmann  
  https://github.com/nlohmann/json

## 许可

MIT，见 `LICENSE`。
