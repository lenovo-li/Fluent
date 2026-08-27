# FluentUI 文档

本目录集中所有进版本库的项目文档：API 分层、技术笔记。

## 目录结构

| 文档 | 内容 |
|---|---|
| [api-surface.md](api-surface.md) | API 三层划分（Stable / Extension / Internal） |

### 技术笔记（实战复盘）

| 文档 | 内容 |
|---|---|
| [无边框窗口的 Resize 实现](技术笔记-无边框窗口的Resize实现.md) | `WS_THICKFRAME` + DWM + `WM_NCCALCSIZE` + `WM_NCHITTEST` 四步组合 |
| [合成器视觉的裁剪与坐标系](技术笔记-合成器视觉的裁剪与坐标系.md) | **改合成控件前必读** —— D2D 裁剪管不住 DComp 视觉 |

## 快速导航

### 新会话接手工作

1. 读仓库根的 [CLAUDE.md](../CLAUDE.md) — 权威架构约束与非协商项：
   - 元素四层模型（Visual / UIElement / FrameworkElement / Control|Panel）
   - 帧管线与脏标记传播
   - 服务注入（UIContext）与生命周期
   - 文本虚拟化的两种模式
   - 性能非妥协项

2. 读根 [README.md](../README.md) — 特性、控件清单、构建指令

### 实施新控件前必读

1. [CLAUDE.md](../CLAUDE.md) — 性能非妥协项（O(可见) 绘制、idle 零成本、compositor 动画、最小脏区）
2. 若控件要创建 compositor visual（TextArea / TreeView / ProgressBar 那一类）：
   [技术笔记-合成器视觉的裁剪与坐标系.md](技术笔记-合成器视觉的裁剪与坐标系.md) — 附检查清单
3. 若控件涉及窗口边缘 / 非客户区：
   [技术笔记-无边框窗口的Resize实现.md](技术笔记-无边框窗口的Resize实现.md)

### 查 API 支持边界

- [api-surface.md](api-surface.md) — 哪些是 Stable（应用层 API）、哪些是 Extension（自定义控件需要）、哪些是 Internal（框架内部）

---

## 与仓库其他位置的关系

| 位置 | 角色 |
|---|---|
| `CLAUDE.md` | 权威架构文档（进版本库，团队共享） |
| `README.md` | 项目主页（特性、控件清单、构建指令、已知限制） |
| `DISTRIBUTION_GUIDE.md` | 打包分发与接收方构建指南 |
| `.claude/` | 工具会话状态（被 gitignore，不进版本库） |
| `examples/` | 最小应用示例（Minimal / PackageConsumer） |
| `tests/unit/` | 单元测试，也是行为契约的可执行文档 |

## 维护约定

- **进版本库的放这里**（团队可见）
- **不进版本库的工具状态放 `.claude/`**（被 gitignore）
- 技术笔记写「为什么这样做」和「错误做法的症状」，不只写 API 用法
- 引用代码时带上文件路径和行号，便于对照
- 过时的删掉，不要留着

## 验证约定

任何改动的验收标准（见 CLAUDE.md）：

```bash
# 两个配置都要干净
MSBuild Fluent.slnx -p:Configuration=Debug   -p:Platform=x64
MSBuild Fluent.slnx -p:Configuration=Release -p:Platform=x64

# 全部测试通过
./x64/Debug/FluentUITests.exe
```

**视觉正确性无法从无头会话验证** —— 残影、闪烁、流畅度、像素对齐都需要真机确认。
两篇技术笔记里的检查清单列出了具体该看哪里。
