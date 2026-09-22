# 技术笔记：无边框窗口的 Resize 实现

> **目标**：实现自绘标题栏的无边框窗口，同时保留标准窗口边缘的 resize 能力。
>
> **方案**：保留 `WS_THICKFRAME` 让 Windows 提供标准 resize 机制，通过 `WM_NCCALCSIZE` 
> 定义非客户区范围，通过 `WM_NCHITTEST` 将窗口边缘映射为 resize 语义。
>
> 应用只需响应三个消息，Windows 自动处理光标、拖动、多显示器、屏幕边缘吸附等全部细节。
> 本文档记录完整实现、DPI 适配、最小尺寸补偿等关键细节。

---

## 一、问题背景

现代桌面应用（Chrome、VS Code、Spotify、Electron 应用）都采用自绘标题栏的无边框设计，
但仍需在窗口边缘提供标准 resize 交互。

### 完全手动实现的代价

如果完全手动实现 resize，需要：

1. 监听 `WM_LBUTTONDOWN` / `WM_MOUSEMOVE` / `WM_LBUTTONUP`
2. 手动改变光标形状（↔ ↕ ↖↘ ↗↙）
3. 计算新窗口尺寸并调用 `SetWindowPos`
4. 处理多显示器坐标转换
5. 实现屏幕边缘吸附（Aero Snap）
6. 处理最小/最大尺寸约束
7. 处理 DPI 变化
8. 处理窗口最大化/还原

### 本项目的方案

**核心思路**：利用 Windows 现有的 resize 机制，只需定义"哪里是边缘"和"边缘是什么语义"。

**三个关键消息**：
- `WM_NCCALCSIZE`：定义非客户区（NC）带的位置和宽度
- `WM_NCHITTEST`：将鼠标位置映射为 resize 语义（`HTLEFT`、`HTRIGHT` 等）
- `WM_GETMINMAXINFO`：设置最小/最大窗口尺寸

**Windows 自动处理**：
- 光标形状自动切换
- 拖动逻辑和尺寸计算
- 多显示器坐标转换
- 屏幕边缘吸附
- DPI 感知
- 最大化/还原

---

## 二、核心概念

### 2.1 三个必须区分的概念

1. **几何非客户区（NC 带）**：`WM_NCCALCSIZE` 定义的窗口矩形与客户区矩形的差值。
   这部分区域不属于客户区坐标系，鼠标事件走 `WM_NC*` 消息族。

2. **视觉内容区域**：Direct2D / DirectComposition 实际绘制的位置。本项目使用 DirectComposition，
   其 visual 坐标跟随客户区原点（已验证：40px 左上 NC 内缩会使内容右下偏移 40px）。

3. **命中测试语义**：`WM_NCHITTEST` 决定某点的交互语义。一个点即使在几何客户区内，
   也可以通过返回 `HTRIGHT` / `HTLEFT` 等被系统视为 resize 边缘。

**三者的关系**：

```
几何 NC 带（WM_NCCALCSIZE 定义）
    ↓ 决定
客户区范围
    ↓ 决定
DComp 内容原点
    
命中测试语义（WM_NCHITTEST 返回）
    ↓ 独立于几何
可以把客户区内的点也判为 resize 边缘
```

### 2.2 当前设计：三边非客户区带

本项目采用**非对称设计**（`FluentUI/window/NativeWindowHost.cpp:1040`）：

| 边 | 几何 NC 带 | 命中测试带 | 原因 |
|---|---:|---:|---|
| 左 | 8 DIP | 8 DIP + 4px 容差 | 真实 NC 带，DWM 不绘制标题栏 |
| 右 | 8 DIP | 8 DIP + 4px 容差 | 真实 NC 带 |
| 底 | 8 DIP | 8 DIP + 4px 容差 | 真实 NC 带 |
| 顶 | **0 DIP** | 8 DIP（客户区内） | 顶部 NC 带会触发 DWM 绘制系统标题栏 |

**关键设计决策**：

1. **顶部不保留 NC 带**：DWM 在检测到顶部非客户区时会绘制自己的标题栏，
   导致"双标题栏"artifact。所以顶部的 resize 命中带必须在客户区内实现。

2. **客户区左上角与窗口对齐**：`params->rgrc[0].top` 不变，所以客户区顶部就是窗口顶部。
   DirectComposition 内容从 (0, 0) 绘制时自然对齐，无需额外偏移。

3. **左右底三边有真实 NC 带**：这些边缘的 8 DIP 完全在客户区外，
   不会与滚动条、内容等客户区元素冲突。

---

## 三、实现步骤

### 3.1 创建保留 `WS_THICKFRAME` 的窗口

**实现**（`FluentUI/window/NativeWindowHost.cpp` 窗口创建）：

```cpp
DWORD style =
    WS_OVERLAPPED |
    WS_THICKFRAME |      // ← 关键：保留系统 resize 能力
    WS_MINIMIZEBOX |
    WS_MAXIMIZEBOX |
    WS_SYSMENU;

hwnd_ = CreateWindowExW(
    WS_EX_NOREDIRECTIONBITMAP,  // DirectComposition 必需
    kClassName,
    title_.c_str(),
    style,               // 注意：不包含 WS_CAPTION
    x, y, width, height,
    nullptr, nullptr, hInstance, this);
```

**关键点**：

1. **保留 `WS_THICKFRAME`**：提供标准 resize 边缘语义、光标变化、拖动逻辑
2. **不使用 `WS_CAPTION`**：避免系统标题栏
3. **不使用 `WS_POPUP`**：`WS_POPUP` 不提供 `WS_THICKFRAME` 的标准 resize 语义，
   需要手动实现全套拖动逻辑

### 3.2 配置 DWM 框架扩展

**实现**（`FluentUI/window/WindowAppearance.cpp`）：

```cpp
// 启用 DWM 非客户区渲染
DWMNCRENDERINGPOLICY policy = DWMNCRP_ENABLED;
DwmSetWindowAttribute(hwnd_, DWMWA_NCRENDERING_POLICY, &policy, sizeof(policy));

// 将 DWM 框架向客户区扩展 1px
MARGINS margins = {1, 1, 1, 1};
DwmExtendFrameIntoClientArea(hwnd_, &margins);
```

**作用**：

- `DwmExtendFrameIntoClientArea` 将 DWM 框架向客户区扩展 1px，参与窗口边缘、阴影和合成
- **它不直接隐藏边框** — 边框的移除由 `WM_NCCALCSIZE` 完成
- 扩展 1px 足以让 DWM 认识到这是一个无边框窗口，提供正确的阴影效果

### 3.3 通过 `WM_NCCALCSIZE` 定义客户区

**实现**（`FluentUI/window/NativeWindowHost.cpp:1040`）：

```cpp
case WM_NCCALCSIZE:
{
    if (wParam == TRUE) {
        auto* params = reinterpret_cast<NCCALCSIZE_PARAMS*>(lParam);
        
        // 输入：params->rgrc[0] 是窗口矩形（屏幕坐标）
        // 输出：修改后的 rgrc[0] 是客户区矩形
        // 非客户区 = 窗口矩形 - 客户区矩形
        
        const int insetPx = ResizeGripOuterPx(dpi_);  // 8 DIP @ 当前 DPI
        
        // 三边内缩，顶部不动
        params->rgrc[0].left   += insetPx;
        params->rgrc[0].right  -= insetPx;
        params->rgrc[0].bottom -= insetPx;
        // params->rgrc[0].top 保持不变 ← 顶部 NC 带会触发系统标题栏
        
        return 0;  // 返回 0 表示接受这个客户区定义
    }
    break;
}
```

**几何示意**（假设窗口 800×600，`insetPx = 12` @ 150% DPI）：

```
窗口矩形 [0, 0, 800, 600]

       ┌──────────────────────────┐  ← 窗口 left=0, top=0
       │ NC 12px (左侧 resize 带) │
       │  ┌────────────────────┐  │  ← 客户区 left=12, top=0
       │  │                    │  │     DComp visual 也从 (12, 0) 开始
       │  │   客户区 776×588   │  │
       │  │                    │  │
       │  │                    │  │  NC 12px (右侧)
       │  └────────────────────┘  │  ← 客户区 right=788, bottom=588
       │         NC 12px (底部)   │
       └──────────────────────────┘  ← 窗口 right=800, bottom=600
```

**关键规则**：

1. **窗口矩形不变**，只是客户区缩小了
2. **NC 带位于窗口矩形的内侧**，不在窗口外
3. **顶部不保留 NC 带**，避免 DWM 绘制系统标题栏
4. **客户区顶部与窗口顶部对齐**（都是 y=0），DComp 内容无需偏移

### 3.4 通过 `WM_NCHITTEST` 定义 resize 命中带

**实现**（`FluentUI/window/NativeWindowHost.cpp:1588`）：

```cpp
LRESULT NativeWindowHost::HitTestNca(POINT ptScreen)
{
    RECT rc;
    GetWindowRect(hwnd_, &rc);  // rc 是窗口矩形（屏幕坐标）
    
    // NC 带宽度：与 WM_NCCALCSIZE 使用的值完全相同
    const int insetPx = ResizeGripOuterPx(dpi_);
    constexpr int tolerancePx = 4;  // 向外延伸 4px，容错
    
    bool left = false, right = false, top = false, bottom = false;
    
    // 关键：NC 带在窗口矩形的 INSIDE
    // 左侧：[rc.left, rc.left+inset) + 外侧容差
    if (ptScreen.x >= rc.left - tolerancePx && ptScreen.x < rc.left + insetPx)
        left = true;
    
    // 右侧：[rc.right-inset, rc.right) + 外侧容差
    if (ptScreen.x >= rc.right - insetPx && ptScreen.x < rc.right + tolerancePx)
        right = true;
    
    // 顶部：无 NC 带，这 inset 像素在客户区内
    if (ptScreen.y >= rc.top && ptScreen.y < rc.top + insetPx)
        top = true;
    
    // 底部：[rc.bottom-inset, rc.bottom) + 外侧容差
    if (ptScreen.y >= rc.bottom - insetPx && ptScreen.y < rc.bottom + tolerancePx)
        bottom = true;
    
    // 角优先于边
    if (top && left) return HTTOPLEFT;
    if (top && right) return HTTOPRIGHT;
    if (bottom && left) return HTBOTTOMLEFT;
    if (bottom && right) return HTBOTTOMRIGHT;
    
    if (left) return HTLEFT;
    if (right) return HTRIGHT;
    if (top) return HTTOP;
    if (bottom) return HTBOTTOM;
    
    // 标题栏拖动、按钮等其他判断...
    // （见完整实现）
    
    return HTCLIENT;
}
```

**NC 带在窗口矩形内侧的推导**：

`WM_NCCALCSIZE` 只缩小客户区，窗口矩形不变：

```
窗口矩形:  [rc.left,          rc.right)        ← GetWindowRect 返回
客户区:    [rc.left+inset,    rc.right-inset)  ← WM_NCCALCSIZE 定义
右侧NC带:  [rc.right-inset,   rc.right)        ← 在窗口矩形内部
```

所以右边判定必须是 `ptScreen.x >= rc.right - insetPx && ptScreen.x < rc.right + tolerancePx`，
覆盖整条 NC 带（内侧 12px）+ 外侧容差（4px）。

**常见错误**：误写成 `ptScreen.x >= rc.right - 1 && ptScreen.x < rc.right + outerPx`，
只覆盖了 NC 带最内侧 1px，剩下的 11px 落到函数末尾返回 `HTCLIENT`，导致"侧边很窄"。

---

## 四、ResizeGripOuterPx 的唯一真理来源

### 4.1 全局唯一定义

**实现**（`FluentUI/window/WindowState.h`）：

```cpp
// 8 DIP = 设备无关像素，确保不同 DPI 下感知尺寸一致
constexpr float kResizeGripOuterDip = 8.0f;

inline int ResizeGripOuterPx(UINT dpi)
{
    if (dpi == 0) dpi = 96;  // 防御性编程，避免除零
    // DIP → 物理像素：DIP × (当前DPI / 96.0)
    return static_cast<int>(kResizeGripOuterDip * static_cast<float>(dpi) / 96.0f + 0.5f);
}
```

**两处调用**，共享同一个值：

1. `WM_NCCALCSIZE`（line 1050）：定义三边 NC 带宽度
2. `HitTestNca`（line 1608）：判定三边 resize 命中带

### 4.2 一致性的重要性

**如果两处不一致**：

| `WM_NCCALCSIZE` | `HitTestNca` | 结果 |
|---:|---:|---|
| 8 DIP | 8 DIP | ✅ NC 带完全可 resize |
| 8 DIP | 4 DIP | ❌ 剩余 4 DIP 的 NC 带返回 `HTCLIENT`，无法 resize |
| 4 DIP | 8 DIP | ❌ 4 DIP 客户区被判为 resize 带，内容点不到 |

### 4.3 DIP 与 DPI

**为什么用 DIP（设备无关像素）**：

确保 resize 带在不同 DPI 显示器上的**感知尺寸**一致。

| DPI | 缩放 | 8 DIP 对应像素 | 实际物理尺寸 |
|---:|---:|---:|---|
| 96  | 100% | 8 px | ≈2.1 mm @ 96 DPI |
| 144 | 150% | 12 px | ≈2.1 mm @ 144 DPI |
| 192 | 200% | 16 px | ≈2.1 mm @ 192 DPI |

如果写死 `8px`（物理像素），在 4K 显示器（200% 缩放）上 resize 带宽度减半，难以触碰。

**转换公式**：

```cpp
物理像素 = DIP × (当前DPI / 96.0) + 0.5  // +0.5 四舍五入
```

96 DPI 是 Windows 的 100% 缩放基准。

### 4.4 DPI 更新

`dpi_` 缓存必须及时更新：

```cpp
// 窗口创建后
dpi_ = GetDpiForWindow(hwnd_);

// WM_DPICHANGED（跨显示器拖拽）
case WM_DPICHANGED:
    dpi_ = HIWORD(wParam);  // 新 DPI
    // 重新计算所有 DIP→px 转换
    // 使用 lParam 提供的建议窗口矩形（已按新 DPI 调整）
    const RECT* suggested = reinterpret_cast<RECT*>(lParam);
    SetWindowPos(hwnd_, nullptr, 
                 suggested->left, suggested->top,
                 suggested->right - suggested->left,
                 suggested->bottom - suggested->top,
                 SWP_NOZORDER | SWP_NOACTIVATE);
    break;
```

**Per-Monitor V2 DPI 感知**：

- 声明在 manifest 中（见 `CLAUDE.md` 全局规则）
- 每个窗口有独立的 DPI：`GetDpiForWindow(hwnd)`
- 跨显示器拖拽时自动收到 `WM_DPICHANGED`
- 不要缓存"全局 DPI"

---

## 五、外侧容差带（tolerancePx）

`HitTestNca` 在 NC 带外侧额外延伸 4px，作为容错：

```cpp
const int insetPx = ResizeGripOuterPx(dpi_);  // 8 DIP @ 150% = 12px
constexpr int tolerancePx = 4;                 // 固定 4px，不随 DPI 缩放

if (ptScreen.x >= rc.right - insetPx && ptScreen.x < rc.right + tolerancePx)
    right = true;  // [rc.right-12, rc.right+4)，总宽度 16px
```

**作用**：鼠标稍微滑出窗口边缘时，仍然识别为 resize 带。

**为什么是 4px 而不是 DIP**：这是一个固定的容错余量，在所有 DPI 下保持相同的物理宽度（约 1mm @ 96 DPI）。如果也按 DPI 缩放，高 DPI 下容差会过宽。

**三边对称**：左、右、底三边都有 `+ tolerancePx` 外侧延伸；顶部无 NC 带，但有 `insetPx` 内侧判定（在客户区内）。

---

## 六、完整消息流

```
用户操作: 鼠标移到窗口右边缘
    ↓
Windows: WM_NCHITTEST (ptScreen = 鼠标位置)
    ↓
HitTestNca 判断:
    ptScreen.x >= rc.right - 12 && ptScreen.x < rc.right + 4
    → 在右侧 NC 带 + 容差内
    ↓
返回: HTRIGHT
    ↓
Windows 自动:
    1. 改变光标为 ↔
    2. 等待鼠标按下
    3. 进入 resize 拖动模式
    ↓
用户拖动:
    ↓
Windows 自动:
    1. 实时计算新窗口宽度
    2. 调用内部 SetWindowPos
    3. 发送 WM_WINDOWPOSCHANGING → WM_NCCALCSIZE → WM_SIZE
    ↓
NativeWindowHost 处理 WM_SIZE:
    1. 重新 Measure/Arrange 布局
    2. 重绘内容到 DirectComposition
    3. Commit
```

**应用只需要**：
- `WM_NCCALCSIZE`：定义客户区
- `WM_NCHITTEST`：返回命中语义
- `WM_SIZE`：响应尺寸变化

**Windows 自动处理**：
- 光标形状
- 拖动逻辑
- 尺寸计算
- 多显示器
- 屏幕边缘吸附
- 最小/最大尺寸（配合 `WM_GETMINMAXINFO`）

---

## 七、最大化窗口

### 7.1 当前实现：不区分最大化状态

`WM_NCCALCSIZE` 和 `WM_NCHITTEST` 两个处理器都**没有** `IsZoomed` 判断（`NativeWindowHost.cpp:1040` / `1059`）：
无论最大化还是还原，都保留三边 NC 带和边缘 resize 命中。

代码里 `IsZoomed` 只出现在四个与本文无关的地方：
- `line 1318`：双击标题栏时决定 `SC_RESTORE` 还是 `SC_MAXIMIZE`
- `line 1880`：标题栏按钮的图标状态（还原 vs 最大化图标）
- `line 2167`：保存窗口位置时跳过非普通状态
- `line 2194`：`CaptureWindowState` 记录 `maximized` 标记

### 7.2 为什么这样也能工作

最大化时 Windows 把窗口矩形设为**大于**工作区（超出的部分正好是 NC 带的宽度），
所以客户区仍然精确铺满工作区，不会出现黑边。这是 `WS_THICKFRAME` 窗口的标准行为，
DWM 会为它补偿框架尺寸。

边缘此时仍返回 `HTLEFT` / `HTRIGHT` 等语义，但拖动不产生效果 —— 窗口已达工作区上限，
Windows 自己拒绝了尺寸变化。用户能看到 resize 光标，这是与系统窗口一致的行为
（在最大化的资源管理器边缘也能看到）。

### 7.3 若要改为最大化时禁用边缘 resize

需要在两处加判断：

```cpp
case WM_NCCALCSIZE:
    if (wp == TRUE) {
        auto* params = reinterpret_cast<NCCALCSIZE_PARAMS*>(lp);
        if (!IsZoomed(hwnd_)) {                    // 仅还原状态保留 NC 带
            const int insetPx = ResizeGripOuterPx(dpi_);
            params->rgrc[0].left   += insetPx;
            params->rgrc[0].right  -= insetPx;
            params->rgrc[0].bottom -= insetPx;
        }
        return 0;
    }
    break;

case WM_NCHITTEST: {
    POINT pt = {GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
    if (IsZoomed(hwnd_)) {
        // 最大化：跳过边缘判定，但标题栏拖动/按钮仍需工作
        return HitTestCaptionOnly(pt);
    }
    return HitTestNca(pt);
}
```

**注意**：`HitTestNca` 里的标题栏和按钮判定不能一起跳过，否则最大化后按钮点不动。
所以需要拆出一个只做标题栏判定的路径，这也是当前实现没有这么做的原因 ——
收益（光标形状更"正确"）小于拆分带来的复杂度。

**尚未验证**：以上代码是设计草案，没有在真机上跑过。

---

## 八、与控件的交互

右侧 NC 带 12px（@ 150%）位于客户区外，ScrollBar 的 16 DIP hover 区位于客户区内，两者不重叠。这是三边 NC 带设计的优势：

| 设计 | 右边 NC 带 | 滚动条可用区 | 冲突？ |
|---|---|---|---|
| 早期（纯客户区内侧）| 0 NC + 8 DIP 客户区 | 客户区右侧 16 DIP | ✅ 重叠 8 DIP |
| 当前（三边 NC）| 8 DIP NC | 客户区右侧 16 DIP | ❌ 无重叠 |

**顶部的特殊性**：顶部无 NC 带，8 DIP resize 区在客户区内，与标题栏按钮共享空间。`HitTestNca` 必须先判断按钮：

```cpp
int titleBarPx = static_cast<int>(TitleBarHeightDip() * DpiScale() + 0.5f);
if (ptScreen.y >= rc.top && ptScreen.y < rc.top + titleBarPx) {
    float dipX = (ptScreen.x - rc.left) * 96.0f / dpi_;
    float dipY = (ptScreen.y - rc.top) * 96.0f / dpi_;
    if (CaptionButtonAt(dipX, dipY)) return HTCLIENT;  // 按钮优先
    if (input_.HitTest(Point{dipX, dipY})) return HTCLIENT;
    return HTCAPTION;  // 标题栏拖动
}
```

---

## 九、最小尺寸补偿

`WM_GETMINMAXINFO` 给出的 `ptMinTrackSize` 是窗口矩形尺寸，但应用的 `MinWidth` / `MinHeight` 描述的是客户区：

```cpp
case WM_GETMINMAXINFO:
{
    auto* info = reinterpret_cast<MINMAXINFO*>(lParam);
    POINT p = {0, 0};
    if (MinTrackSizePx(MinWidth(), MinHeight(), dpi_, p)) {
        info->ptMinTrackSize = p;
    }
    return 0;
}
```

```cpp
// window/WindowState.h
bool MinTrackSizePx(float minWidthDip, float minHeightDip, UINT dpi, POINT& out)
{
    if (minWidthDip <= 0 && minHeightDip <= 0) return false;
    
    const int insetPx = ResizeGripOuterPx(dpi);
    
    if (minWidthDip > 0) {
        const int clientW = static_cast<int>(minWidthDip * dpi / 96.0f + 0.999f);
        out.x = clientW + 2 * insetPx;  // 左 + 右两条 NC 带
    }
    
    if (minHeightDip > 0) {
        const int clientH = static_cast<int>(minHeightDip * dpi / 96.0f + 0.999f);
        out.y = clientH + insetPx;      // 底部一条 NC 带，顶部无
    }
    
    return true;
}
```

**宽度补偿两条带，高度补偿一条**——与 `WM_NCCALCSIZE` 的非对称设计完全对应。

---

## 十、调整 resize 带宽度

修改唯一一处：

```cpp
// window/WindowState.h
constexpr float kResizeGripOuterDip = 8.0f;  // ← 改这里
```

`WM_NCCALCSIZE`、`HitTestNca`、`MinTrackSizePx` 三处自动同步。

**建议值**：

| 值 | @ 100% | @ 150% | @ 200% | 手感 |
|---:|---:|---:|---:|---|
| 6 DIP | 6px | 9px | 12px | 偏窄，精细操作 |
| 8 DIP | 8px | 12px | 16px | 当前值，平衡 |
| 10 DIP | 10px | 15px | 20px | 更宽容 |

修改后必须验证：
- 四条边和四个角的 resize
- 100% / 150% / 200% DPI
- 滚动条、标题栏按钮交互
- 最大化 / 还原

---

## 十一、测试清单

### 基本交互
- [ ] 左/右/上/下边缘 resize
- [ ] 四个角对角 resize
- [ ] 光标形状正确（↔ ↕ ↖↘ ↗↙）

### 客户内容
- [ ] 右侧滚动条可 hover、点击、拖动
- [ ] 标题栏按钮不被顶部 resize 带遮挡
- [ ] 文本选择不被边缘拦截

### 窗口状态
- [ ] 最大化不覆盖任务栏
- [ ] 最大化时无多余边距
- [ ] 最大化时边缘不响应 resize
- [ ] 还原后 resize 恢复
- [ ] 最小尺寸约束生效

### DPI 与多显示器
- [ ] 96 / 144 / 192 DPI 下 resize 带易于触碰
- [ ] 跨显示器后 `dpi_` 已更新
- [ ] 多显示器负坐标下命中正确

---

## 十二、代码位置

| 文件 | 位置 | 作用 |
|---|---|---|
| `window/WindowState.h` | `kResizeGripOuterDip` | 唯一真理来源 |
| `window/WindowState.h` | `ResizeGripOuterPx` | DIP→px 转换 |
| `window/WindowState.h` | `MinTrackSizePx` | 最小尺寸补偿 NC 带 |
| `NativeWindowHost.cpp` | `CreateWindowExW` | 创建 `WS_THICKFRAME` 窗口 |
| `WindowAppearance.cpp` | `EnableShadow` | DWM 框架扩展 |
| `NativeWindowHost.cpp` | `WM_NCCALCSIZE` | 定义三边 NC 带 |
| `NativeWindowHost.cpp` | `HitTestNca` | 命中测试，覆盖整条 NC 带 |
| `NativeWindowHost.cpp` | `WM_GETMINMAXINFO` | 最小尺寸 |

---

## 十三、常见问题

### Q1: `DwmExtendFrameIntoClientArea` 是否隐藏边框？

不直接隐藏。边框的移除由 `WM_NCCALCSIZE` 完成（客户区扩展到窗口边缘）。`DwmExtendFrameIntoClientArea` 负责框架扩展和合成外观。

### Q2: 为什么顶部没有 NC 带？

顶部 NC 带会触发 DWM 绘制系统标题栏，出现"双标题栏"。这是 DWM 的固有行为，无框窗口不能在顶部保留 NC 区。

### Q3: NC 带在窗口内还是窗口外？

**窗口矩形内侧**。`WM_NCCALCSIZE` 只缩小客户区，窗口矩形不变。右侧 NC 带位于 `[rc.right - inset, rc.right)` 区间，完全在窗口内。

### Q4: 早期"左右很窄"bug 的原因？

误判 NC 带在窗口外侧，写成 `ptScreen.x >= rc.right - 1 && ptScreen.x < rc.right + outerPx`，只覆盖了 NC 带最内侧 1px。实际 NC 带有 12px 宽（@ 150%），剩下 11px 返回 `HTCLIENT`，无法 resize。

### Q5: 为什么不用 `WS_POPUP`？

`WS_POPUP` 不提供 `WS_THICKFRAME` 的标准 resize 语义，需要手动实现光标、拖动、多显示器等全部逻辑。保留 `WS_THICKFRAME` 可以免费获得 Windows 的完整 resize 实现。

### Q6: `ResizeGripOuterPx` 的测试覆盖？

测试文件：`tests/unit/ResizeGripOuterTests.cpp`

验证内容：
- 各 DPI 下的像素转换（96/120/144/192 DPI）
- 零 DPI 防御（回退到 96）
- 四舍五入规则与 `WM_NCCALCSIZE` 一致
- 非零保证（任何 DPI 都不会产生 0 宽度）

该测试的注释记录了两次历史 bug：
1. `WM_NCCALCSIZE` 保留 9px 底边，`HitTestNca` 只判定 1px
2. 错判 NC 带在窗口外侧，大部分带宽度无法触发

测试锁定了 `kResizeGripOuterDip` 作为唯一真理来源的契约。

---

## 十四、相关 Win32 API

实现涉及的 API，可在 MSDN 查阅：

| API / 消息 | 用途 |
|---|---|
| `WM_NCCALCSIZE` | 定义客户区范围，决定 NC 带位置 |
| `NCCALCSIZE_PARAMS` | `WM_NCCALCSIZE` 的参数结构，`rgrc[0]` 是窗口/客户区矩形 |
| `WM_NCHITTEST` | 返回命中语义（`HTLEFT` / `HTCAPTION` / `HTCLIENT` 等） |
| `WM_GETMINMAXINFO` | 设置最小/最大窗口尺寸 |
| `WM_DPICHANGED` | 跨显示器拖拽时的 DPI 变化通知 |
| `DwmExtendFrameIntoClientArea` | DWM 框架扩展，参与阴影和合成 |
| `DwmSetWindowAttribute` | 设置 DWM 渲染策略、圆角、暗色模式等 |
| `GetDpiForWindow` | 查询窗口的 Per-Monitor DPI |
| `WS_THICKFRAME` | 窗口样式，提供标准 resize 边缘 |
| `WS_EX_NOREDIRECTIONBITMAP` | 扩展样式，DirectComposition 必需 |

---

## 十五、总结

**核心机制**：
1. `WS_THICKFRAME` 保留系统 resize 能力
2. `WM_NCCALCSIZE` 定义三边 NC 带（顶部除外，避免系统标题栏）
3. `WM_NCHITTEST` 覆盖整条 NC 带（在窗口矩形内侧），返回 `HT*`
4. `DefWindowProc` 执行标准 resize 交互
5. DWM + DirectComposition 负责视觉合成

**设计优势**：
- NC 带在客户区外，与滚动条不冲突
- 客户区左上角与窗口对齐，DirectComposition 无需额外偏移
- 代码量小，Windows 自动处理所有细节
- 与 Chrome / VS Code / Electron 方案一致

**唯一真理来源**：`kResizeGripOuterDip` 一处定义，三处调用（`WM_NCCALCSIZE` / `HitTestNca` / `MinTrackSizePx`），测试自动验证一致性。
