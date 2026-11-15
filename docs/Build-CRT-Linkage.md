# CRT 链接方式切换指南

FluentUI 支持静态链接（`/MT`）和动态链接（`/MD`）C++ 运行时，可通过构建参数切换。

---

## 快速切换

### 使用 PowerShell 脚本（推荐）

```powershell
# 静态链接（默认）— EXE 自包含，体积较大
.\scripts\build.ps1 -Configuration Release -CRTLinkage Static

# 动态链接 — EXE 较小，需要 VC Runtime DLL
.\scripts\build.ps1 -Configuration Release -CRTLinkage Dynamic
```

### 使用 MSBuild 直接调用

```powershell
msbuild Fluent.slnx -p:Configuration=Release -p:Platform=x64 -p:FluentCRTLinkage=Static
msbuild Fluent.slnx -p:Configuration=Release -p:Platform=x64 -p:FluentCRTLinkage=Dynamic
```

---

## 两种模式对比

| 项目 | Static (`/MT`) | Dynamic (`/MD`) |
|---|---|---|
| **EXE 体积** | FluentUIDemo: 1996 KB<br>TrayAppDemo: 425 KB | FluentUIDemo: 1640 KB (-18%)<br>TrayAppDemo: 278 KB (-35%) |
| **运行时依赖** | 无，EXE 自包含 | 需要 `vcruntime140.dll`, `msvcp140.dll` |
| **多 EXE 场景** | 每个 EXE 都嵌入 CRT，总体积大 | 共享一份 CRT DLL，总体积小 |
| **发布方式** | 直接复制 EXE | EXE + DLL，或安装 VC Redist |
| **启动性能** | 略快（无 DLL 加载） | 相近（Windows 缓存 DLL） |

---

## 发布指南

### Static 模式（默认）

直接发布 EXE，无需额外文件。

### Dynamic 模式

**方案 1：手动打包 DLL**（便携式发布）

将以下 DLL 与 EXE 放在同一目录：
- `vcruntime140.dll`
- `vcruntime140_1.dll`（x64）
- `msvcp140.dll`

DLL 位置：`C:\Program Files\Microsoft Visual Studio\<version>\VC\Redist\MSVC\<ver>\x64\Microsoft.VC143.CRT\`

**方案 2：使用 VC Redistributable**（推荐）

用户安装 [Visual C++ Redistributable for Visual Studio 2022](https://aka.ms/vs/17/release/vc_redist.x64.exe)，系统会自动提供所需 DLL。

**方案 3：静默安装 Redist**（安装包场景）

在安装程序中静默安装 VC Redist：
```cmd
vc_redist.x64.exe /quiet /norestart
```

---

## 修改默认值

编辑 `Directory.Build.props`，修改第 15 行：

```xml
<!-- 改为 Dynamic 作为默认值 -->
<FluentCRTLinkage Condition="'$(FluentCRTLinkage)'==''">Dynamic</FluentCRTLinkage>
```

---

## 技术细节

### 实现方式

通过 `Directory.Build.props` 定义全局属性 `FluentCRTLinkage`，所有 `.vcxproj` 文件引用该属性：

```xml
<!-- Directory.Build.props -->
<FluentCRTLinkage Condition="'$(FluentCRTLinkage)'==''">Static</FluentCRTLinkage>
<FluentRuntimeLibrary_Debug Condition="'$(FluentCRTLinkage)'=='Static'">MultiThreadedDebug</FluentRuntimeLibrary_Debug>
<FluentRuntimeLibrary_Debug Condition="'$(FluentCRTLinkage)'=='Dynamic'">MultiThreadedDebugDLL</FluentRuntimeLibrary_Debug>
<FluentRuntimeLibrary_Release Condition="'$(FluentCRTLinkage)'=='Static'">MultiThreaded</FluentRuntimeLibrary_Release>
<FluentRuntimeLibrary_Release Condition="'$(FluentCRTLinkage)'=='Dynamic'">MultiThreadedDLL</FluentRuntimeLibrary_Release>
```

```xml
<!-- 所有 .vcxproj 文件 -->
<RuntimeLibrary Condition="'$(Configuration)'=='Debug'">$(FluentRuntimeLibrary_Debug)</RuntimeLibrary>
<RuntimeLibrary Condition="'$(Configuration)'=='Release'">$(FluentRuntimeLibrary_Release)</RuntimeLibrary>
```

一处修改，全局生效，无需逐个修改 12 个项目文件。

---

## 建议

- **开发阶段**：用 Static 模式，无需管理 DLL，调试更简单
- **发布阶段**：
  - **单 EXE 工具**：用 Static，方便分发
  - **应用套件（多 EXE）**：用 Dynamic，节省 80 KB × N 的重复
  - **企业/机构部署**：用 Dynamic + Redist，IT 部门统一管理运行时

---

## 参考

- [Microsoft Docs: C 运行时库链接](https://docs.microsoft.com/zh-cn/cpp/c-runtime-library/crt-library-features)
- [VC++ Redistributable 下载](https://learn.microsoft.com/zh-cn/cpp/windows/latest-supported-vc-redist)
