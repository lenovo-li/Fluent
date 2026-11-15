# Image 控件

## 概述

通过 WIC 解码位图、用 Direct2D 绘制。支持 PNG / JPG / BMP / ICO / GIF / TIFF，四种拉伸
模式与 WPF/WinUI 语义一致。

**加载发生在 attach，不在 `SetSource`。** 解码需要 D2D 设备，而设备随 `UIContext` 在
元素加入活动树时才到达。常见的使用顺序是 `SetSource(...)` 然后 `panel->Add(...)` ——
所以 `SetSource` 只记录路径并标记待加载，真正的位图在 `OnAttachedToTree` 里构建
（首次 `Measure` 作为兜底）。

**典型使用场景**:
- 图标、头像、缩略图
- 内容配图（等比适配容器）
- 从内存/资源解码后直接喂 WIC 源

---

## 继承层次

```
Visual
  └─ UIElement
      └─ FrameworkElement
          └─ Image
```

注意它继承 `FrameworkElement` 而**不是** `Control` —— 没有 Background/Padding/主题化外框，
它只画一张图。

**命名空间**: `fluent`
**头文件**: `FluentUI/controls/Image.h`

---

## 公开 API

### 构造函数

```cpp
Image();
```

默认 `ImageStretch::Uniform`，无源，`NativeSize()` 为 `{0, 0}`。

### 嵌套类型

```cpp
enum class ImageStretch {
    None,           // 原始尺寸居中，超出边界则裁剪
    Uniform,        // 等比缩放适配，整图可见（留白）
    UniformToFill,  // 等比缩放填充，保持宽高比（裁剪溢出）
    Fill,           // 双轴拉伸到边界，不保持宽高比
};
```

### 属性

#### Source

```cpp
void SetSource(std::wstring path);
const std::wstring& Source() const;

void SetSource(IWICBitmapSource* bitmap);
```

两个重载：文件路径，或已解码的 WIC 源（应用从内存/资源自己解码时用）。

路径版本只记录路径；WIC 版本在已 attach 时立即生效，否则同样等到 attach。

**脏标记等级**: Measure（新图的原生尺寸会改变 desired size）。

#### FailureSource

```cpp
void SetFailureSource(std::wstring path);
const std::wstring& FailureSource() const;
```

**说明**: 主源加载失败时的回退图（占位图）。默认为空 —— 失败就什么都不画。

回退图**自己没有回退**：占位图也加载失败时控件画空。

#### Stretch

```cpp
void SetStretch(ImageStretch s);
ImageStretch Stretch() const;
```

**默认值**: `ImageStretch::Uniform`
**脏标记等级**: Render（只改变绘制的两个矩形，不改变 desired size）。

#### NativeSize / IsLoaded / LoadAttempted

```cpp
SizeDip NativeSize() const;   // 图自身的 DIP 尺寸（像素 × 内嵌 DPI 元数据缩放）
bool IsLoaded() const;        // 位图是否存在
bool LoadAttempted() const;   // 是否已尝试加载（无论成败）
```

`NativeSize()` 在加载成功前是 `{0, 0}`，这也是 `Measure` 的依据。

`LoadAttempted()` 用来区分「还没试过」和「试过但失败了」—— 单看 `IsLoaded()` 这两种
情况一样都是 false。

---

### 事件

#### ImageOpened

```cpp
struct ImageOpenedArgs {
    std::wstring path;         // 文件路径，WIC 源时为空
    SizeDip size{0.0f, 0.0f};  // 加载后的 DIP 尺寸（== NativeSize()）
};
Event<Image, ImageOpenedArgs>& ImageOpened();
```

**触发时机**: 加载**成功**后，两种源都触发。

**不为回退图触发。** 回退图是「应用请求失败后的替代品」，把它报告成 opened 会告诉应用
「你的图到了」，而事实相反 —— 那条路径上 `ImageFailed` 已经响过了，那才是诚实的信号。

**用途**: 隐藏占位图、启动淡入、或者在拿到 `NativeSize()` 后调整容器尺寸。

#### ImageFailed

```cpp
struct ImageFailedArgs { std::wstring path; };
Event<Image, ImageFailedArgs>& ImageFailed();
```

**触发时机**: 主源加载失败（文件不存在、解码错误、设备丢失恢复失败）。

**不为回退图的失败触发** —— 只有主源是应用关心的信号。

```cpp
sub_ = image->ImageFailed().Subscribe(this,
    [](void* owner, Image&, Image::ImageFailedArgs& args) {
        static_cast<MyPage*>(owner)->LogMissingAsset(args.path);
    });
```

⚠️ **返回的 `Subscription` 必须持有**（RAII，丢掉即退订）。

---

### 自由函数

```cpp
struct ImageDrawRects {
    RectDip dest;   // 画到窗口的哪里（窗口 DIP）
    RectDip src;    // 取图的哪一部分（图像 DIP）
    bool isEmpty() const;
};

ImageDrawRects ComputeImageDrawRects(SizeDip native, RectDip bounds, ImageStretch mode);
```

**这是 Image 唯一有意思的逻辑，刻意抽成了纯函数** —— 无设备、无元素状态，可以 headless
测试。`Render` 除了把两个矩形交给 D2D 什么都不做，所以测这个函数就等于测控件行为。

**源矩形是裁剪得以实现的关键。** `None`（图比控件大时）和 `UniformToFill` 都必须裁剪，
而只钳制目标矩形会把图**压扁** —— D2D 会把你给的任何源拉伸进你给的任何目标。这两个模式
因此收窄**源**矩形，保持宽高比不变。

---

## 代码示例

### 最小示例

```cpp
#include "FluentUI/controls/Image.h"

auto* image = panel->Add(std::make_unique<Image>());
image->SetSource(L"photo.png");
image->SetStretch(ImageStretch::Uniform);
image->SetWidth(200.0f);
image->SetHeight(200.0f);
```

### 占位图 + 失败回退

```cpp
auto* image = panel->Add(std::make_unique<Image>());
image->SetSource(L"avatar.png");
image->SetFailureSource(L"default_avatar.png");   // 主图失败时显示

sub_ = image->ImageFailed().Subscribe(this,
    [](void* owner, Image&, Image::ImageFailedArgs& args) {
        // 记录哪张图缺失（回退图此时已经在显示了）
        static_cast<MyPage*>(owner)->ReportMissing(args.path);
    });
```

### 拿到真实尺寸后调整布局

```cpp
sub_ = image->ImageOpened().Subscribe(this,
    [](void* owner, Image&, Image::ImageOpenedArgs& args) {
        // args.size 是图的原生 DIP 尺寸，此时才知道
        static_cast<MyPage*>(owner)->FitContainerTo(args.size);
    });
image->SetSource(L"unknown_size.jpg");
```

### 从内存解码

```cpp
// 应用自己用 WIC 解码（资源、网络流、内存缓冲）
ComPtr<IWICBitmapSource> decoded = MyDecodeFromMemory(buffer);
image->SetSource(decoded.Get());   // 已 attach 则立即生效
```

---

## 备注

### 性能特征

- **Render 是一次 `DrawBitmap`**，加上 `ComputeImageDrawRects` 的几个浮点运算。
- **解码只发生一次**（attach 时），之后每帧只是绘制已有的 `ID2D1Bitmap`。
- **无内置缓存**：两个 Image 指向同一路径会解码两次。需要共享请自己解码成 WIC 源后
  喂给两个控件。
- **idle 零成本**：没有动画，没有 tick。

### 设备丢失

`OnDeviceLost` 丢弃位图，`OnDeviceRestored` 重新解码。应用不需要做任何事，但要注意
**重新解码会再次触发 `ImageOpened`**（或失败时的 `ImageFailed`）—— 订阅者应当能承受
同一张图报告多次。

### 使用注意事项

- **`SetSource` 之后立即读 `IsLoaded()` 会得到 false**（还没 attach）。要判断加载结果请
  订阅事件，或在 attach 之后再查。
- **没有给尺寸时 Measure 用 `NativeSize()`** —— 加载失败的图 desired size 是 `{0, 0}`，
  在 StackPanel 里会完全不占位。需要固定占位请显式设 Width/Height。
- **失败不是错误路径**：控件画空，原因通过 `[FluentUI.Image]` 打到调试器。这是刻意的 ——
  一张图加载不出来不该让应用崩。
- **GIF 只显示第一帧**，没有动画支持。

### 键盘导航

**不参与。** Image 不可聚焦，不接受键盘输入。

### 主题 Token

**不使用任何主题 token。** Image 只画位图本身，没有主题化的外框或背景。需要边框/圆角
请用 `Border` 包一层。

### 演示页

demo 导航 → 基础输入 → Image。当前只有四种拉伸模式的文字说明与代码示例，**未嵌入实际
演示图像**（Gallery 里没有随附的图片资源）。

---

## UI Automation

Image 的 peer：

| 项 | 值 |
|---|---|
| ControlType | Image |
| 模式 | 无；Name 只来自 `SetAutomationName`，没有名字时视为装饰，只出现在 raw view |
| AutomationId | `SetAutomationId` |
| HelpText | `Tooltip()` |

惰性创建：只有 UIA 客户端访问到这个控件时才创建 peer，没有客户端时零开销。模式操作经投递消息执行，执行前重新检查控件仍在树上且已启用。属性变化只在真正变化且有客户端监听时发送。自定义方式见 inheritance-guide.md 的“UI Automation”一节。

验证：无头测试 `tests/unit/AutomationControlPeerTests.cpp` / `AutomationSelectorPeerTests.cpp`，真实 UIA 客户端自测 `FluentUITests.exe --uia-smoke`；14 处变异（如把 UIA 操作换回不发事件的 setter、去掉项缓存上限）全部被测试抓到。

## 相关控件

- [Border](Border.md) — 需要给图加边框/圆角/背景时包在外层
