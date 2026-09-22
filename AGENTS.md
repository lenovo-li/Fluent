# AGENTS.md — Hermes 入口

本文件是 Hermes Agent 在本仓库的入口。权威项目约束不在此处，而在
**`CLAUDE.md`**——该文件最初为 Claude Code 编写，但其中的构建命令、架构
说明、不可妥协的性能约束、TASK_STATE.md 规程对任何 agent 同样适用。

**开始任何工作前，先完整阅读 `CLAUDE.md`。** 以下内容只是 Hermes 特有的
补充，不重复 CLAUDE.md 的任何条目；两边冲突时以 CLAUDE.md 为准。

## 长任务与断点恢复

CLAUDE.md 末尾规定了 `.claude/TASK_STATE.md` 的维护规程。Hermes 同样遵守：

- 多步任务开始时读 `.claude/TASK_STATE.md`；有实质进展时在原地更新它。
- 断点恢复顺序：读 TASK_STATE.md → `git status --short` → 看相关 diff →
  从「Exact Next Actions」第一条继续。不要重做已完成步骤，不要在没有新
  证据时重复已记录的失败方案。
- 设计文档在 `docs/design/`，被 TASK_STATE.md 和 CLAUDE.md 引用，进版本库可见。

## Hermes 特有的环境事实

- **Shell 是 git-bash (MSYS)，不是 PowerShell。** CLAUDE.md 里的构建命令是
  bash 语法，可直接用；但 `~/.claude/CLAUDE.md` 全局配置或
  `.claude/settings.local.json` 里出现的 `pwsh -NoProfile -Command ...`
  写法在 Hermes 的 terminal 工具里不适用——直接用 MSBuild 短命令即可：
  ```bash
  MSBUILD="C:/Program Files/Microsoft Visual Studio/18/Community/MSBuild/Current/Bin/MSBuild.exe"
  "$MSBUILD" Fluent.slnx -p:Configuration=Debug -p:Platform=x64 -verbosity:minimal
  ```
- 路径：MSYS 风格 `/d/project/Fluent` 和原生 `D:\project\Fluent` 都可用；
  仓库位置因机器而异，不要把某台机器的绝对路径写进文档或脚本。
- Python：`python3` 不存在，用 `python`（3.11）或 `uv`。
- 两个测试二进制的坑（`x64/Debug/FluentUITests.exe` 才是新的，
  `tests/x64/Debug/` 会过期）在 CLAUDE.md 已说明，此处仅再次强调。

## 构建路径

主构建方式是 **Visual Studio solution** (`Fluent.slnx`)，用 MSBuild 编译——
CLAUDE.md 里的命令即为此。仓库同时维护 **CMake** (`CMakeLists.txt`) 作为
备用构建路径，供 CLion / VS Code CMake Tools / 纯命令行环境使用：

```bash
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build build
```

两份源文件清单（`CMakeLists.txt` 里的 `FLUENTUI_SOURCES` 和
`FluentUI/FluentUI.vcxproj` 里的 `<ClCompile>` 条目）是**独立手工维护**的，
添加新 `.cpp` 时必须同时注册两处。`scripts/check_source_lists.py` 检查两边
是否同步；`scripts/check_filters.py` 检查 Solution Explorer 树与磁盘文件夹
布局是否镜像；`scripts/check_all.py` 一键跑全部卫生检查。这些检查是纯
Python、只读、快速的，不依赖编译器或网络——触碰项目文件或源列表时跑一遍。

## NativeWindowHost 重构（2026-08）

NativeWindowHost（原名 BackdropWindow）在 2026-08 完成重构，从 God Object
（26 个成员、6 大子系统集成在一个类）拆分为 5 个独立 Host 类 + 1 个协调器：

- **GraphicsHost** (`window/GraphicsHost.h/.cpp`) - D2D/DWrite/DComp 图形栈、资源缓存
- **InputHost** (`window/InputHost.h/.cpp`) - 输入路由、焦点管理、点击检测、拖放
- **FrameHost** (`window/FrameHost.h/.cpp`) - 帧调度器、动画注册表
- **ThemeHost** (`window/ThemeHost.h/.cpp`) - 主题令牌管理、原生窗口外观
- **DiagnosticsHost** (`window/DiagnosticsHost.h/.cpp`) - 性能环形缓冲、会话日志

NativeWindowHost 现在只保留 12 个成员（从 26 降到 12），职责清晰为「Win32
窗口宿主协调器」。Window 和 DialogWindow 继承它，但该继承关系被文档化为
「私有实现细节」——应用代码应只使用 Window/DialogWindow 的 authoring API，
不要 dynamic_cast 到 NativeWindowHost 或访问 Host 内部。

详见 `.claude/refactor_backdropwindow_plan.md`（本地文档，不在版本库）。
重构跨 5 个阶段（Phase 0-5），每阶段保持 1611/1611 测试通过，零 API 破坏。

## 验证约定（与 CLAUDE.md 一致，此处仅为强调）

MSBuild Debug + Release 双配置干净编译，然后 `x64/Debug/FluentUITests.exe`
全绿。视觉正确性（闪烁、重影、平滑度）无法从本会话验证——涉及滚动/动画/
渲染的改动，明确告诉用户去真机的哪个位置检查。

## 提交纪律

未经明确要求不 commit、不 push。CLAUDE.md 同样如此规定。
