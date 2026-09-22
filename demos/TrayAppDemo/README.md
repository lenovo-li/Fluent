# FluentUI 托盘应用演示

本演示展示如何实现"点击关闭按钮时最小化到系统托盘"的应用程序。

## 功能特性

- ✅ 点击窗口关闭按钮（×）时，窗口隐藏到托盘而不是退出
- ✅ 双击托盘图标可恢复窗口
- ✅ 右键托盘图标弹出菜单（显示窗口 / 退出）
- ✅ 隐藏时显示气泡通知提示
- ✅ 只有通过托盘菜单"退出"才真正关闭程序

## 实现原理

### 1. Application 设置显式退出模式

```cpp
app.SetShutdownMode(ShutdownMode::OnExplicitShutdown);
```

三种退出模式：
- `OnMainWindowClose`（默认）：主窗口关闭时退出
- `OnLastWindowClose`：所有窗口关闭时退出
- `OnExplicitShutdown`：**只有调用 `app.Shutdown()` 才退出** ← 托盘应用必须用这个

### 2. OnClosing 拦截关闭事件

```cpp
void OnClosing(WindowClosingArgs& args) override {
    args.cancel = true;              // 取消销毁
    ShowWindow(Hwnd(), SW_HIDE);     // 只是隐藏，HWND 继续存活
}
```

关键点：
- `args.cancel = true` 阻止窗口销毁
- 使用 `ShowWindow(hwnd, SW_HIDE)` 隐藏窗口
- HWND 依然有效，可以随时用 `SW_SHOW` 恢复

### 3. 托盘图标 + 双窗口设计

**隐藏的消息窗口**（`HWND_MESSAGE`）：
- 接收托盘图标消息（`WM_TRAYICON`）
- 处理右键菜单、双击事件
- 生命周期 = 整个进程

**FluentUI 主窗口**：
- 正常的 UI 窗口
- 可以随时隐藏/恢复
- `OnClosing` 拦截销毁

## 编译方式

### 使用 MSBuild

添加到 `Fluent.slnx` 后：

```bash
"C:/Program Files/Microsoft Visual Studio/18/Community/MSBuild/Current/Bin/MSBuild.exe" \
  demos/TrayAppDemo/TrayAppDemo.vcxproj \
  -p:Configuration=Debug -p:Platform=x64
```

### 使用 CMake

在 `demos/CMakeLists.txt` 中添加：

```cmake
add_executable(TrayAppDemo
    TrayAppDemo/TrayAppDemo.cpp
)
target_link_libraries(TrayAppDemo PRIVATE FluentUI shell32)
```

然后：

```bash
cmake --build build --target TrayAppDemo
```

## 使用方法

1. 运行 `TrayAppDemo.exe`，会弹出主窗口
2. 点击窗口右上角的关闭按钮（×）
3. 窗口消失，任务栏的应用图标也消失
4. 在系统托盘区（右下角）找到托盘图标
5. 双击托盘图标，或右键选择"显示窗口"，窗口恢复
6. 右键托盘图标选择"退出"，程序才真正关闭

## 扩展建议

### 自定义托盘图标

```cpp
// 从资源文件加载图标
HICON hIcon = LoadIconW(hInstance, MAKEINTRESOURCEW(IDI_MYICON));
CreateTrayIcon(g_hiddenWindow, hIcon);
```

### 更新托盘提示文字

```cpp
wcscpy_s(g_nid.szTip, L"新的提示文字");
Shell_NotifyIconW(NIM_MODIFY, &g_nid);
```

### 显示气泡通知

```cpp
g_nid.uFlags = NIF_INFO;
wcscpy_s(g_nid.szInfo, L"通知内容");
wcscpy_s(g_nid.szInfoTitle, L"标题");
g_nid.dwInfoFlags = NIIF_INFO;  // 或 NIIF_WARNING, NIIF_ERROR
Shell_NotifyIconW(NIM_MODIFY, &g_nid);
```

### 添加更多托盘菜单项

```cpp
HMENU hMenu = CreatePopupMenu();
AppendMenuW(hMenu, MF_STRING, ID_SETTINGS, L"设置(&S)");
AppendMenuW(hMenu, MF_STRING, ID_ABOUT, L"关于(&A)");
AppendMenuW(hMenu, MF_SEPARATOR, 0, nullptr);
AppendMenuW(hMenu, MF_STRING, ID_SHOW, L"显示窗口(&W)");
AppendMenuW(hMenu, MF_STRING, ID_EXIT, L"退出(&X)");
```

## 注意事项

1. **Window 对象是一次性的**  
   FluentUI 的 `Window` 一旦 HWND 销毁就不能再次 `Show()`。所以 `OnClosing` 必须拦截，不能让窗口真销毁。

2. **托盘菜单需要 SetForegroundWindow**  
   否则菜单不会在点击空白处自动关闭（Windows 平台要求）。

3. **程序退出时清理托盘图标**  
   调用 `Shell_NotifyIconW(NIM_DELETE, &nid)` 删除托盘图标，否则图标会残留直到鼠标悬停。

4. **跨线程操作**  
   如果后台线程需要显示窗口，使用 `PostMessage` 而不是直接调用 `ShowWindow`。

## 参考项目

完整的生产级实现见 `D:\project\FKeyUsageMonitor` 项目：
- 双窗口设计（消息窗口 + FluentUI 窗口）
- Raw Input 键盘监听
- DataGrid 实时数据展示
- 定时器管理
- CSV 数据持久化
