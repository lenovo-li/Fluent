# 仓库根用户文档审计（README / CONTRIBUTING / DISTRIBUTION_GUIDE）

审计日期：2026-09-25　基线：`54380e2`（分支 `fix/scroll-jitter`）

范围：`README.md`(131) / `README.public.md`(124) / `CONTRIBUTING.md`(64) /
`CONTRIBUTING.public.md`(94) / `DISTRIBUTION_GUIDE.md`(191)。
`docs/controls/AUDIT_TASKS.md` 只覆盖 `docs/controls/`，这五份是新用户真正的入口，
此前从未核对。

## 本次实测的地面事实（全部亲自跑过，非推断）

| 事实 | 命令 / 证据 |
|---|---|
| Debug+Release 全解决方案干净编译 | `MSBuild Fluent.slnx -p:Configuration={Debug,Release} -p:Platform=x64`，exit 0 |
| 测试总数 **1806** 全绿 | `./x64/Debug/FluentUITests.exe` → `=== summary: 1806 run, 1806 passed, 0 failed ===` |
| 子串过滤可用 | `./x64/Debug/FluentUITests.exe Opacity` → `44 run, 44 passed, 1762 skipped by filter` |
| 性能门禁通过 | `./x64/Release/FluentUIBench.exe --gate` → `5 measured, 7 GPU/interactive stubs, 1 budgeted, 0 over budget`，exit 0 |
| `tests/x64/Debug/` 只有 .obj，无 exe | `ls tests/x64/Debug/*.exe` → No such file |
| 工具集 v145 只存在于 VS **18**（2026） | `MSBuild/Microsoft/VC/v180/Platforms/x64/PlatformToolsets/` 下仅 `v145`；本机 `vswhere` → `18.10.2 Community` |
| 全部 7 个 vcxproj 均 `v145` + `WindowsTargetPlatformVersion 10.0` | `FluentUI/FluentUI.vcxproj:26,33`，其余同 |
| CMake 只定义 3 个 target | `CMakeLists.txt:111 FluentUI`、`:149 FluentSettings`、`:173 FluentUIMinimal`；**无 Demo / Tests / Bench** |
| CMake `-A x64` 配置成功；`-G Ninja` 失败 | ninja 不在 PATH（只在 VS 的 `CommonExtensions/.../CMake/Ninja/` 里） |
| 仓库体积 | 工作树 971 MB、`x64/` 178 MB、`.vs/` 65 MB、`.git/` 11 MB、`git archive HEAD` 6.08 MB |
| `.claude/` 是**受保护**目录，清理脚本不删 | `scripts/clean_for_distribution.ps1:94 $ProtectedDirs`、`:319 保留：…、.claude\` |
| 清理脚本在 `scripts/` 下，不在根 | `ls *.bat` → No such file；实际 `scripts/clean_for_distribution.bat` |
| `FluentUI/` 实际子目录 | animation, base, composition, controls(+primitives), core, diagnostics, dialogs, graphics, input, layout, services, styling, text, window —— **没有 `theme/`、没有 `panels/`** |
| 控件/容器数量 | `controls/*.h` 派生类 39 个 + `dialogs/` 2 个；`layout/*.h` 容器 12 个 |
| UIElement 基础事件 13 个 | `core/UIElement.h:375-407`（README 的「13 个」正确） |
| ScrollContentHost 已删 | `git log 92f13bc`「Remove compositor-driven scrolling from TextArea and TreeView」，`FluentUI/composition/` 下已无此文件 |
| 测试框架恰好 5 个断言宏 | `tests/framework/Test.h:81,88,95,106,115`（无 EXPECT_GT/LT/STREQ） |
| `python scripts/check_all.py` 可用 | 输出 `OK - all 2 check(s) passed`（source lists + filters） |

## 0. 先说最重要的结论

**五份文档里没有一行 C++ 代码片段。** 任务清单里担心的 `Application app;`、
`SetContent`、`SetRoot` 可见性、`wWinMain` 签名、`Subscription` 未保存、
嵌套 vs 全局 `Orientation` 等编译错误，在 `docs/controls/TEMPLATE.md` 和
`Button.md` 里成群出现，但在根文档里**一处都没有**——因为根文档压根没给过
可编译示例。这本身是最大的缺口：新用户读完 README 仍然不知道怎么写第一个窗口。

唯一权威的可编译最小示例是 `examples/Minimal/main.cpp`（38 行，含
`Application app(instance);` / 受保护的 `OnInitialize()` 里调 `SetRoot` /
`closeSub_` 保存订阅 / `int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int)`）。
`README.md:82` 只把它当作「CMake 消费示例」提了一句，`README.public.md`
连 `examples/` 都没提过。

---

## 1. README.md

| 行 | 问题 | 依据 / 正确形式 |
|---|---|---|
| 60 | **「需要 Visual Studio 2022+」不成立。** 全部 vcxproj 用 `v145`，VS 2022(v17) 只带 v143，工程直接加载失败 | `FluentUI/FluentUI.vcxproj:33`；`.../VC/v180/Platforms/x64/PlatformToolsets/` 仅 `v145`。应写「Visual Studio 2026（v18）+ MSVC v145 工具集」 |
| 36 | 把 **Tooltip 列为控件**。没有 Tooltip 控件类 | 只有 `core/UIElement.h:285 SetTooltip()` 和内部 `services/TooltipService.h:27`。应从控件清单移除，改为「工具提示（`UIElement::SetTooltip`）」 |
| 35-52 | **控件清单漏了 8 个已发布控件** | `controls/Rating.h:31`、`NumericUpDown.h:29`、`InfoBar.h:40`、`Metric.h:30`、`Chart.h:36`、`dialogs/FileDialog.h:65,78`(Open/Save)、`layout/UniformGrid.h:35`、`layout/Viewbox.h:45`。全部已注册进 vcxproj+CMake |
| 54-56 | **ScrollViewer 复用清单两头都错**：写了 ComboBox（不用它），漏了 3 个真在用的 | ComboBox 走 `core/ScrollMath.h` + `scrollOffset_`（`ComboBox.cpp:9,93-97`），无 ScrollViewer；实际内嵌者还有 `DataGrid.h:365`、`LogViewer.h:211`、`RichTextBox.h:24` |
| 10-12 | **「## 截图」一节没有截图**，只有一句说明；仓库里零张图片 | `git ls-files | grep -E '\.(png\|jpg\|gif\|webp)$'` 为空 |
| 119-121 | 只要求新增 `.cpp` 注册进 `.vcxproj` + `CMakeLists.txt`，**漏了 `.vcxproj.filters`** | `CLAUDE.md:59-70` 明确要求，且 `scripts/check_filters.py` 会红。同样没提 `python scripts/check_all.py`（`CONTRIBUTING.public.md:43` 反而提了） |
| 112 | 「仅支持 x64」与 slnx 仍声明 Win32 平台不一致（无害，但会让人在 VS 里选到 Win32 后困惑） | `Fluent.slnx:4`、`FluentUI.vcxproj:4,8` |

正确、无需改动的部分（已实测）：65-71 的 vswhere+MSBuild 片段、73-79 的
`-A x64` CMake 片段、81-84「测试与 demo 只在 MSBuild 工程里」、98-101 测试命令、
92「数量见运行输出末行」（自更新写法，值得其他文档照抄）、16-19 的 13 个事件、
125-127 的 nlohmann/json v3.12.0 + 路径（`json.hpp:78-80` 版本宏吻合）。
