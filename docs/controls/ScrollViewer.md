# ScrollViewer 控件

## 概述

最小化的 Fluent 滚动容器状态与滚动条渲染器。管理垂直/水平滚动偏移、拇指拖动、平滑滚动
动画，以及 WinUI 11 风格的自动淡出 + 悬停展开滚动条。

**两条独立轴而非两个实例**，因为两个滚动条必须共享淡出状态——双向滚动的编辑器不能一条
轨道在淡出而另一条还亮着。共享的 idle 倒计时让它们一起出现和消失。**只有展开**
（细轨→胶囊）是分轴的，因为那跟随指针实际悬停的那条轨。

**典型使用场景**:
- TreeView、列表控件的垂直滚动
- TextArea 的无换行模式双向滚动
- 长表单、日志查看器

---

## 继承层次

```
Visual
  └─ UIElement
      └─ FrameworkElement
          └─ Control
              └─ ScrollViewer
```

**命名空间**: `fluent`
**头文件**: `FluentUI/layout/ScrollViewer.h`

---

## 公开 API

### 默认状态

无显式构造函数（隐式默认构造）。默认 offset 为 0、内容高度 0（无滚动）、滚动条完全淡出。

### 垂直滚动

#### 内容与偏移

```cpp
void SetContentHeight(float heightDip);
void SetOffset(float offsetDip);       // 立即跳转（取消动画）
void ScrollBy(float deltaDip);
float Offset() const;
float MaxOffset() const;               // 可滚动的最大偏移
```

**`SetOffset` 立即生效、无动画**，取消任何飞行中的平滑滚动并将动画目标捕捉到新偏移。
用于拖动、键盘导航和 EnsureVisible —— 这些场景下移动应当是瞬时的。

#### 拇指拖动

```cpp
RectDip ThumbRect() const;
RectDip TrackRect() const;
bool HitThumb(float dipX, float dipY) const;
void BeginDrag(float dipY);
void DragTo(float dipY);
void EndDrag();
bool IsDragging() const;
```

拖动协议：`HitThumb` 测试 → `BeginDrag` 捕获 → `DragTo` 每帧 → `EndDrag` 释放。

#### 平滑（动画）滚动

```cpp
void AnimateTo(float offsetDip);
void AnimateBy(float deltaDip);        // 累加到当前目标
bool IsAnimating() const;
float TargetOffset() const;            // 动画目的地；settled 时 == Offset()
bool Tick(float dtSec);                // 推进动画，返回是否仍需 tick
bool NeedsTick() const;                // 任何动画/淡出/倒计时进行中
```

**`AnimateBy` 连续调用会累加** —— 滚轮连续滚动时积累动量而非从当前位置重启。

**`TargetOffset()` 是「朝向哪里」而非「当前在哪里」**：滚轮到达的那一帧 `Offset()` 还
没动，所以「用户是否滚离了底部」判断必须基于 target。

**收尾有最小速度**：`Tick` 用指数缓动（τ = 0.045s）逼近目标，但每帧至少走
`120 DIP/s × dt`，剩余距离不超过一步时直接吸附到目标。原因见备注「平滑滚动收尾」。

### 水平滚动

```cpp
void SetContentWidth(float widthDip);
void SetOffsetX(float offsetDip);
float OffsetX() const;
float MaxOffsetX() const;
bool HorizontallyScrollable() const;    // MaxOffsetX() > 0
RectDip HThumbRect() const;
float HTrackLength() const;
RectDip HTrackRect() const;
bool HitHThumb(float dipX, float dipY) const;
bool HitHBarRegion(float dipX, float dipY) const;
void SetHBarHover(bool over);
void BeginHDrag(float dipX);
void HDragTo(float dipX);
void EndHDrag();
bool IsHDragging() const;
float HExpandFactor() const;            // 水平展开系数 [0..1]
```

**内容不超过视口时整个水平轴惰性**：`MaxOffsetX()` 为 0、`HThumbRect()` 为空、
`Render` 不画水平部分。

**`HTrackLength()` 在两条轨道都活跃时会缩短**，避免在角落重叠。

**没有水平平滑滚动**：水平移动来自 Shift+滚轮、拇指拖动和插入符跟随，这些都不需要惯性。
动画会迫使命中测试问「水平滚动此刻在哪里」——垂直轴曾为此引入过 `EffectiveOffset()`
（该 API 已随合成器滚动一起移除）。水平轴不做动画，这个问题就不存在。

### 自动淡出 / 悬停展开

```cpp
void Wake();                            // 标记滚动活动，淡入并重启倒计时
void SetBarHover(bool over);            // 指针悬停→淡入+展开成胶囊
bool HitBarRegion(float dipX, float dipY) const;
float Visibility() const;               // 当前不透明度 [0..1]
float ExpandFactor() const;             // 当前展开系数 [0..1]
bool IsBarHovered() const;              // 指针在任一轨道的悬停条带上
void HideBarsNow();                     // 立即结束淡出周期
```

`HitBarRegion` 是右边缘附近的**更宽命中条**（可发现性）。

`IsBarHovered()` 为 true 时空闲倒计时被挂起，所以一个过期的 true 会让滚动条一直显示。

`HideBarsNow()` 把滚动条直接置为隐藏、细轨、无悬停、倒计时结束，于是除非还有滚动在进行，
`NeedsTick()` 立即为 false。给内容在滚动条醒着时停止溢出的宿主用：没有轨道可画了（不溢出时
`Render` 直接返回），把剩下约 1.5 秒的淡出缓动跑完只是在浪费帧。之后内容再次溢出时 `Wake` 从隐藏
状态开始，和正常淡出后一样。`ScrollPanel` 就是这样用的。

`Visibility` 和 `ExpandFactor` 是只读状态查询，拥有者控件可据此判断滚动条是否仍在
淡入淡出、是否需要继续请求重绘。

### 定制

```cpp
void SetKeepVisibleWhenOverflow(bool on);
void SetIdleHideDelay(float seconds);   // 默认 1.2s
void SetFadeSpeed(float tauSeconds);    // 时间常数，默认 0.06s（更小=更快）
void SetScrollbarColor(D2D1::ColorF color);
```

**`SetScrollbarColor`** 用于控件背景不跟随主题的情况（比如浅色主题下 LogViewer 的
终端黑背景），否则自动颜色（主题的 `textPrimary`）会看不见。

### 事件

#### ScrollChanged

```cpp
struct ScrollChangedArgs {
    float offsetOld = 0.0f;     // 垂直：滚动前
    float offsetNew = 0.0f;     // 垂直：滚动后
    float offsetXOld = 0.0f;    // 水平：滚动前
    float offsetXNew = 0.0f;    // 水平：滚动后
};
Event<ScrollViewer, ScrollChangedArgs>& ScrollChanged();
```

**触发时机**: 滚动偏移改变（`SetOffset`、`ScrollBy`、`DragTo`、`HDragTo` 或动画完成）。
**两条轴的旧值和新值都在 args 里**，所以一个事件就能处理双向滚动。

**用途**:
- 滚动到底部时触发「加载更多」
- 同步两个滚动器
- 更新虚拟化列表的可见范围

```cpp
sub_ = scrollViewer->ScrollChanged().Subscribe(this,
    [](void* owner, ScrollViewer& sv, ScrollChangedArgs& args) {
        if (args.offsetNew >= sv.MaxOffset() - 10.0f) {
            static_cast<MyList*>(owner)->LoadMore();
        }
    });
```

⚠️ **返回的 `Subscription` 必须持有**（RAII，丢掉即退订）。

---

## 代码示例

### 最小示例（手动滚动）

```cpp
#include "FluentUI/layout/ScrollViewer.h"

// ScrollViewer 通常是拥有者控件的成员（不是子元素），拥有者在自己的 bounds
// 变化时把视口同步给它——ListBox::OnBoundsChanged 就是这么做的。
void MyList::OnBoundsChanged() {
    scroll_.SetBounds(bounds_);        // 视口 = 控件自身区域
    scroll_.SetContentHeight(1000.0f); // 1000 DIP 内容
}

scroll_.SetOffset(100.0f);             // 跳到 100 DIP 处（首次 Arrange 之后才有效）
```

### 监听滚动到底部

```cpp
sub_ = sv->ScrollChanged().Subscribe(this,
    [](void* owner, ScrollViewer& scroller, ScrollChangedArgs& args) {
        auto* self = static_cast<MyLogView*>(owner);
        const float threshold = scroller.MaxOffset() - 20.0f;
        if (args.offsetNew >= threshold && args.offsetOld < threshold) {
            // 刚滚到底部 20 DIP 以内，触发「加载更多」
            self->LoadNextChunk();
        }
    });
```

### 滚轮驱动平滑滚动

```cpp
// 元素级滚轮入口是 UIElement::OnPointerWheelChanged。
// （NativeWindowHost::OnMouseWheel 是窗口级虚函数，与控件无关。）
void MyList::OnPointerWheelChanged(PointerEventArgs& e) {
    const float lines = e.WheelScrollLines();   // 系统「每次滚动行数」，已带方向
    if (HasModifier(e.modifiers, ModifierKeys::Shift)) {
        // 水平轴没有 tween，直接瞬时设置
        scroll_.SetOffsetX(scroll_.OffsetX() + lines * 16.0f);
    } else {
        scroll_.AnimateBy(lines * 16.0f);       // 垂直：平滑滚动
    }
    e.handled = true;
}
```

### 自定义滚动条颜色

```cpp
// 黑底白字的日志查看器，浅色主题下默认滚动条是深色（看不见）
sv->SetScrollbarColor(D2D1::ColorF(D2D1::ColorF::White));
```

---

## 备注

### 性能特征

- **Render**: 两个圆角矩形（垂直+水平拇指），alpha 混合。
- **只在需要时 tick**：`NeedsTick()` 为 false 时零成本（无动画、已淡出、倒计时结束）。
- **Tick 只推进浮点值**（偏移、淡出、展开），无布局。
- **淡出帧该报多大的脏区由宿主决定**:ScrollViewer 是成员不是树节点,它自己的 `Invalidate()` 到不了帧调度器。
  宿主在只推进了淡出/展开的帧上应只报轨道条,`ScrollPanel` 的做法和证明见 [ScrollPanel.md](ScrollPanel.md)。

### 平滑滚动收尾

**纯指数缓动在非整数 DPI 下会在终点前卡住再跳一下。** 指数逼近的步长越来越小，到最后
一两个 DIP 时每帧只走零点几个物理像素。行内容按像素对齐绘制（`SnapToPixel`），所以显示
位置会连续多帧停在同一个像素上。旧代码在剩余距离 ≤ 0.5 DIP 时才吸附，150% DPI 下这等于
0.75 个物理像素：画面先停住几帧，然后跳 1px 到终点。用户看到的是滚轮停下那一刻图标和文字
上下抖一下（TreeView 上最明显，因为行里有图标）。

按 `Tick` 的公式模拟（DPI 1.0–2.0、60/120/144Hz、多种滚动距离），旧代码最多连续 7 帧停在
同一像素（150% DPI、120Hz）；加最小速度后最多 1 帧，单帧停留是正常的取整误差。

**最小速度只影响最后约 5 DIP**（120 DIP/s 下约 45ms），大距离滚动仍由指数缓动决定，手感
不变。所有用 `ScrollViewer::Tick` 平滑滚动的控件都走这条路径。

`tests/unit/ScrollViewerSettleTests.cpp` 在无头环境锁定「物理像素最多连续停 1 帧」和
「收尾只触发一次 `ScrollChanged`」。这只证明数值，画面是否顺滑仍需在真机上看。

### 淡出时机

**默认 idle 1.2 秒后完全淡出。** 滚动活动（`Wake`）、拖动、悬停（`SetBarHover`）都会
重启倒计时。

**`SetKeepVisibleWhenOverflow(true)` 让滚动条在有内容溢出时不完全隐藏** —— 沉降到
最小可见度，让用户始终知道可以滚动。默认 false，匹配经典覆盖滚动条行为。

### 两条轨道共享淡出，展开分离

**淡出/淡入一起发生**（`visibility_` 是共享的），所以双向滚动时两条轨道同时出现和消失。

**展开独立**（`expand_` 和 `expandX_`），因为那跟随指针实际悬停的轨道 —— 悬停垂直轨
时只有垂直轨变胖，水平轨保持细条。

### 使用注意事项

- **首次 Arrange 之前偏移无效**：视口范围还不存在，`MaxOffset()` 还是 0。
- **`OnBoundsChanged` 重新钳制两条轴**：窗口缩放改变视口尺寸，之前合法的偏移现在可能
  指向内容末尾之外。
- **`SetOffset` 取消动画、`AnimateTo` 覆盖动画目标**。两者都立即生效，没有「排队」
  语义。
- **水平滚动没有惯性**：来自 Shift+wheel 的移动直接调 `SetOffsetX`（瞬时）而非
  `AnimateBy`。

### 键盘导航

**ScrollViewer 本身不处理键盘**（无 `SetFocusable`）。键盘滚动由拥有者控件实现
（TreeView 的上下箭头、TextArea 的翻页），它们直接调 `SetOffset` 或 `AnimateTo`。

### 主题 Token

| Token | 用途 |
| --- | --- |
| `textPrimary` | 滚动条墨水（默认，可用 `SetScrollbarColor` 覆盖）|

拇指 alpha = 基础值 × `visibility_`。基础值 idle 为 0.32（亮色）/ 0.40（暗色），悬停
0.55，拖动 0.65。开启 `keepVisibleWhenOverflow_` 时，内容溢出的情况下
`visibility_` 不会低于 0.35，否则淡出到 0。

### 演示页

demo 中 **ScrollViewer 本身没有单独的演示页**，但它被 TreeView、TextArea、ListBox
等需要滚动的控件内部使用。

---

## 相关控件

- [TreeView](TreeView.md) — 使用 ScrollViewer 管理垂直滚动
- [ScrollPanel](ScrollPanel.md) — 内嵌 ScrollViewer 的通用滚动容器
- [TextArea](TextArea.md) — 在无换行模式下双向滚动
