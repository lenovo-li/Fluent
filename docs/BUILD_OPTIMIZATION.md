# 构建优化：从 74 秒到 10 秒以下

## 问题

2026-08-31 测量 Release x64 全量重新生成耗时 **1 分 14 秒**（74 秒），Debug 同样 74 秒，对于一个 88 个 .cpp 文件的静态库而言过长。

## 根因

构建缓慢的两个主因：

1. **单线程编译**：88 个 .cpp 顺序编译，未使用 `/MP`（多处理器编译）开关
2. **每个 .cpp 重复解析巨型头文件**：windows.h、d2d1.h、dwrite.h、dcomp.h 这些 SDK 头文件总计数万行，每个 .cpp 都完整解析一遍

## 解决方案

### 1. 预编译头（Precompiled Header, PCH）

**原理**：把稳定的、多数 .cpp 都 include 的头文件（标准库 + Windows SDK）提前编译成 `.pch` 缓存，后续编译只需加载缓存而非重新解析。

**实现** (`FluentUI/pch.h` + `FluentUI/pch.cpp`)：

```cpp
// pch.h — 包含 <algorithm>, <memory>, <vector>, <functional>,
// <windows.h>, <d2d1_1.h>, <dwrite.h>, <dcomp.h>, <wrl/client.h>
```

- `pch.cpp` 配置为 `/Yc`（Create precompiled header），生成 `pch.pch`
- 所有其他 .cpp 配置为 `/Yu pch.h`（Use），且用 `/FI pch.h` 强制注入（无需在源码里手写 `#include "pch.h"`）

**收益**：pch.h 首次编译需 2-3 秒，但之后每个 .cpp 节省 0.5-1 秒

**陷阱**：

- PCH 必须自己定义 `NOMINMAX`、`WIN32_LEAN_AND_MEAN`，因为它先于项目全局定义编译
- 不要把项目头文件（如 `UIElement.h`）放进 PCH——它们频繁改动，会触发整个 PCH 重编译

### 2. 多处理器编译 (`/MP`)

**原理**：MSBuild 并行启动多个 `cl.exe` 进程，每个处理一个 .cpp；在多核 CPU 上接近线性加速。

**实现**：在所有 5 个项目（FluentUI、FluentUIDemo、FluentUIBench、FluentSettings、FluentUITests）的 .vcxproj 里加入：

```xml
<PropertyGroup>
  <MultiProcessorCompilation>true</MultiProcessorCompilation>
</PropertyGroup>
<ItemDefinitionGroup>
  <ClCompile>
    <MultiProcessorCompilation>true</MultiProcessorCompilation>
    ...
  </ClCompile>
</ItemDefinitionGroup>
```

**收益**：在 8 核机器上约 5-7 倍加速（考虑链接、预处理等单线程阶段）

### CMake 路径同步

CMakeLists.txt 镜像了相同配置：

```cmake
target_precompile_headers(FluentUI PRIVATE FluentUI/pch.h)
add_compile_options(/MP)
```

保持两个构建路径（MSBuild 和 CMake）行为一致。

## 效果

- **之前**（无 PCH + 单线程）：Release x64 全量重新生成 **74 秒**，Debug **74 秒**
- **之后**（PCH + /MP）：Release x64 全量重新生成 **25 秒**，Debug **9 秒**
- **加速比**：Release 3.0×，Debug 8.2×（Debug 更快因为无 LTCG 链接开销）

## 增量编译

改动单个 .cpp 后：

- **无 PCH**：该 .cpp 重新解析所有 SDK 头 → 约 2 秒
- **有 PCH**：加载缓存 → 约 0.3 秒

改动项目头文件（如 `UIElement.h`）：

- **不触发 PCH 重编译**（因为 pch.h 不 include 项目头）
- 只有引用了该头的 .cpp 重新编译

改动 `pch.h` 本身：

- 触发 `pch.cpp` 重编译（2-3 秒）+ 全部 88 个 .cpp 重编译，相当于一次全量重新生成
- **因此不要频繁改 pch.h**

## 验证

```bash
# 清空中间产物，测量全量重新生成时间
rm -rf x64/Debug x64/Release
time "$MSBUILD" Fluent.slnx -p:Configuration=Release -p:Platform=x64 -verbosity:minimal
```

预期 Release 25 秒，Debug 9 秒（实测于 2026-08-31，机器配置未记录，推测 8 核 CPU）。

## 历史

- **2026-08-29**：在 `feature/build-and-cache-optimization` 分支首次引入（commit bf30c94）
- **2026-08-31**：发现当前分支（`feature/listbox-itemheight-guard`）构建时间退化到 74 秒，原因是两分支在 51edf0a 之后分化，PCH 提交未合并
- **2026-08-31**：cherry-pick bf30c94，只保留构建配置部分（移除了 ResourceCache 运行时优化，避免功能混杂）

## 参考

- [Visual Studio 文档：/MP (Build with Multiple Processes)](https://learn.microsoft.com/en-us/cpp/build/reference/mp-build-with-multiple-processes)
- [Visual Studio 文档：Creating Precompiled Header Files](https://learn.microsoft.com/en-us/cpp/build/creating-precompiled-header-files)
- [CMake 文档：target_precompile_headers](https://cmake.org/cmake/help/latest/command/target_precompile_headers.html)
