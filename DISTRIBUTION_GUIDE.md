# FluentUI 项目分发指南

## 清理和打包步骤

### 1. 清理构建产物

运行项目根目录下的清理脚本：

```cmd
clean_for_distribution.bat
```

此脚本会删除：
- `.vs\` - Visual Studio 缓存（约 5 GB）
- `x64\` - 构建输出目录（约 400 MB）
- `.claude\` - Claude Code 会话数据（24 KB）
- `*.user` - 用户特定配置文件
- `*.suo` - 解决方案用户选项
- 临时文件和日志

保留的内容：
- 所有源代码（`.h`, `.cpp`）
- 项目文件（`.vcxproj`, `.slnx`）
- 文档和脚本
- `.git` 仓库

### 2. 打包项目

清理后，项目大小会从 **5.5+ GB 降至约 50-100 MB**。

**方式 A：使用 git archive（推荐）**
```cmd
git archive -o FluentUI.zip HEAD
```
- 只包含 git 跟踪的文件，自动排除 `.vs`、`x64` 等
- 生成纯净的源码包

**方式 B：手动压缩**
1. 运行 `clean_for_distribution.bat`
2. 使用 7-Zip / WinRAR 压缩整个目录
3. 确保排除 `.git` 文件夹（如果不需要版本历史）

### 3. 验证清理效果

```cmd
dir /s
```

预期大小：
- 带 `.git` 仓库：约 50-80 MB
- 不带 `.git`：约 10-20 MB（纯源码）

## 接收者构建指南

### 环境要求

- **Visual Studio 2026** (v18)
- **工具集**: v145
- **Windows SDK**: 10.0.22621.0 或更高
- **C++ 桌面开发工作负载**

### 构建步骤

1. **打开解决方案**
   ```cmd
   start Fluent.slnx
   ```
   或直接在 VS 中打开 `Fluent.slnx`

2. **选择配置**
   - Debug：开发调试用，包含符号信息
   - Release：分发版本，优化编译，体积小

3. **构建项目**
   
   **通过 Visual Studio**：
   - 菜单：`生成` → `生成解决方案` (Ctrl+Shift+B)
   
   **通过命令行**：
   ```cmd
   "C:\Program Files\Microsoft Visual Studio\18\Community\MSBuild\Current\Bin\MSBuild.exe" Fluent.slnx -p:Configuration=Release -p:Platform=x64 -verbosity:minimal
   ```

4. **运行 Demo**
   ```cmd
   x64\Release\FluentUIDemo.exe
   ```

5. **运行测试**（可选）
   ```cmd
   x64\Debug\FluentUITests.exe
   ```
   预期输出：`1304 run, 1304 passed, 0 failed`

### 常见问题

**Q: 编译错误 "找不到 Windows SDK"**
- A: 安装 Windows 10/11 SDK，或修改 `Fluent.slnx` 中的 `<WindowsTargetPlatformVersion>`

**Q: 链接错误 "无法打开 d2d1.lib"**
- A: 确保安装了 "使用 C++ 的桌面开发" 工作负载

**Q: Release 构建后 exe 在哪里？**
- A: `x64\Release\FluentUIDemo.exe`（约 1 MB）

**Q: 需要 .NET Framework 吗？**
- A: 不需要！这是纯 Win32 + Direct2D 项目，零运行时依赖

## 项目结构

```
Fluent/
├── FluentUI/              # 核心框架（静态库）
│   ├── core/             # 元素基类、布局、事件
│   ├── controls/         # Button、TextBox、CheckBox 等
│   ├── panels/           # StackPanel、Grid、ScrollPanel
│   ├── graphics/         # Direct2D 渲染、DComp 集成
│   ├── text/             # 文本虚拟化、行索引、布局缓存
│   └── window/           # 窗口、输入、帧调度
├── FluentUIDemo/          # Gallery 演示应用
│   ├── pages/            # 各控件的演示页面
│   └── GalleryMain.cpp   # 导航和页面路由
├── FluentUITests/         # 1304 个单元测试
├── FluentUIBench/         # 性能测试
├── FluentSettings/        # JSON 配置持久化
├── CLAUDE.md             # 项目架构和规范
└── README.md             # 项目介绍

构建产物（清理后不存在）：
├── x64/                  # 编译输出（清理前约 400 MB）
│   ├── Debug/           # Debug 配置
│   └── Release/         # Release 配置
└── .vs/                  # VS 缓存（清理前约 5 GB）
```

## 新增功能（本次更新）

### 增强版 Gallery 页面

1. **CheckBox 高级** (`EnhancedBasicPages.cpp`)
   - 三组独立多选框（功能开关/权限/样式）
   - 状态联动和实时反馈
   - 字体自定义（字号/字重/颜色）

2. **RadioButton 高级** (`EnhancedBasicPages.cpp`)
   - 三个单选组（主题/语言/格式）
   - 互斥逻辑演示
   - 表情符号增强 UI

3. **TextBox 高级** (`EnhancedTextPages.cpp`)
   - 字体样式（12-20pt，常规/粗体，多种字体族）
   - 输入验证器（Email/URL/数字）
   - 占位符文本和实时字符计数

4. **TextArea 高级** (`EnhancedTextPages.cpp`)
   - 换行模式对比（NoWrap vs Wrap）
   - 代码编辑器样式（Consolas + VS Code 配色）
   - 大文本性能演示（500 行虚拟化滚动）

5. **🖼️ Border 与主题对照** (`BorderThemePages.cpp`) — 原「样式大全」分类已撤销，各控件的样式演示并回了控件自己的页面
   - Button 9 种颜色组合
   - Panel 背景/边框/圆角定制
   - Slider/ProgressBar 6 种主题色
   - TextBlock 排版样式梯度
   - 透明度和混合效果

## 性能特点

- **exe 大小**: 1.0 MB (Release)
- **启动时间**: < 100 ms（冷启动）
- **内存占用**: ~20 MB（Demo 应用）
- **帧率**: 60 fps（静态 UI 时 0% CPU）
- **文本容量**: 支持 40+ MB 文档（虚拟化）

## 技术亮点

- ✅ **零运行时依赖**：无需 .NET、WinRT、XAML
- ✅ **WPF 风格布局**：Measure/Arrange 两阶段布局
- ✅ **Compositor 集成**：滚动和动画在独立线程，UI 卡顿也流畅
- ✅ **虚拟化渲染**：文本和列表 O(可见行)，不受文档大小影响
- ✅ **部分重绘**：只更新脏区域，减少 GPU 开销
- ✅ **Per-Monitor V2 DPI**：跨屏拖拽自动适配不同 DPI
- ✅ **完整测试**：1304 个单元测试覆盖核心逻辑

## 许可证

（根据项目实际情况填写）

## 联系方式

项目维护者：YuChao Li
