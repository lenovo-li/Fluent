# TabControl 控件

## 概述

选项卡条 + 单个内容区。选项卡头由 TabControl 自己绘制（不是一排 Button），整条选项卡条是
**一个**焦点停靠点，←/→ 切换选中项，和 WinUI / WPF 一致。

**只有选中页的内容挂在活动树上**：未选中页是脱离上下文的子树，不测量、不排列、不命中测试、
不绘制，也不持有订阅或动画注册。20 个选项卡的对话框只付出一页的成本。

**典型使用场景**:
- 设置对话框的分组页
- 多文档界面（配合关闭按钮）
- 数据视图的不同维度（表格 / 图表）

---

## 继承层次

```
Visual
  └─ UIElement
      └─ FrameworkElement
          └─ Panel
              └─ TabControl
```

内容元素由 `Panel` 基类持有（`children_`），但 `Add` / `Emplace` 在 TabControl 上被隐藏，必须用 `AddTab`。

**命名空间**: `fluent`
**头文件**: `FluentUI/controls/TabControl.h`

---

## 公开 API

### 构造函数

```cpp
TabControl();
```

可聚焦，无选项卡，选中索引 -1，选项卡条在顶部，内容淡入开启，关闭按钮隐藏。

### 选项卡

```cpp
template <typename T> T* AddTab(std::wstring header, std::unique_ptr<T> content);
void RemoveTab(int index);
int TabCount() const;
const std::wstring& HeaderAt(int index) const;
FrameworkElement* SelectedContent() const;
bool IsContentAttached(int index) const;   // 只有选中页为 true
```

- `AddTab` 返回借用指针（选项卡被移除或控件销毁前有效）。**第一个添加的选项卡自动选中。**
- `RemoveTab` 移除选中页时，选中项移到前一页（移除的是第 0 页则留在 0，没有页了则为 -1）。

### 选中项

```cpp
int SelectedIndex() const;
void SetSelectedIndex(int index);   // 越界视为 -1
```

**脏标记**: Measure（新页从未在当前尺寸下测量过）。选中项变化时触发 `SelectionChanged`。

### 外观

```cpp
enum class TabStripPlacement { Top, Bottom, Left, Right };   // namespace fluent 级
void SetTabStripPlacement(TabStripPlacement placement);     // 脏标记 Measure
TabStripPlacement StripPlacement() const;
bool IsVerticalStrip() const;

void SetCloseButtonVisible(bool visible);                   // 脏标记 Measure，默认 false
bool CloseButtonVisible() const;

void SetContentFadeEnabled(bool enabled);                   // 默认 true
bool ContentFadeEnabled() const;
void SetContentFadeDuration(float tauSeconds);              // 时间常数，默认 0.1 s，下限 0.01
float ContentFadeDuration() const;
```

竖向选项卡条（Left / Right）上每个头一样宽（最宽的头 + 内边距）。键盘在所有方位都是 ←/→，
↑/↓ 不绑定。

### 事件

```cpp
Event<TabControl, int>& SelectionChanged();                  // 载荷是新索引
struct TabCloseRequestedArgs { int index; };
Event<TabControl, TabCloseRequestedArgs>& TabCloseRequested();
```

**点关闭按钮不会自动移除选项卡**：所有者在处理器里决定是否 `RemoveTab(e.index)`（例如先提示保存）。
关闭按钮的命中优先于选中：点第 3 页的 × 关闭第 3 页，不会先选中它。

`Subscription` 必须作为成员持有，丢掉即退订。

### 几何（公开给测试）

```cpp
float StripHeightDip() const;
float HeaderWidth(int index) const;
RectDip HeaderRect(int index) const;
int HeaderIndexAt(float dipX, float dipY) const;
RectDip CloseButtonRect(int index) const;
int CloseButtonIndexAt(float dipX, float dipY) const;
```

---

## 代码示例

```cpp
#include "FluentUI/controls/TabControl.h"

auto* tabs = panel->Add(std::make_unique<TabControl>());
tabs->AddTab(L"常规", BuildGeneralPage());
tabs->AddTab(L"高级", BuildAdvancedPage());
tabs->SetCloseButtonVisible(true);

closeSub_ = tabs->TabCloseRequested().Subscribe(this,
    [](void*, TabControl& t, TabControl::TabCloseRequestedArgs& e) {
        t.RemoveTab(e.index);
    });
```

---

## 备注

### 修过的 bug：切换后不再空转 tick

选中指示条一直是直接画在 `HeaderRect(selectedIndex_)` 上的，但控件里还留着一对没人读的
`indicatorX_` / `indicatorW_` 缓动值。每次切换后它们让 `WantsAnimationTick()` 保持 true
约 0.3 秒（约 20 帧），每帧整块重绘控件和选中页的内容，屏幕上没有一个像素变化，关掉内容
淡入也一样。现在这对缓动值删掉了，唯一的动画是内容淡入：关掉淡入时，切换不请求任何 tick。

由 `TabControl.SwitchWithFadeOffRequestsNoTickAtAll` 锁定：它在切换后、任何 tick 之前检查
`WantsAnimationTick()`。原有的 `TabControl.ContentFadeEnabledRequestsTickAfterSwitch` 先把 tick
耗尽再断言，所以看不到这些多余的帧。

### 性能特征

- **只有选中页是活的**：命中测试、脏区收集、绘制都只走选中页（`HitTestDeep` 和
  `CollectDirtyBounds` 都重写过；否则基类 Panel 仍会绘制和命中未选中但 bounds 还有效的页）。
- **头宽只在文字变化时重新测量**（`AddTab` / `RemoveTab`），缩放窗口时不重建文字布局。
- **idle 零成本**：淡入收敛后 `WantsAnimationTick()` 为 false。
- **没有指示条滑动动画**：指示条在切换的那一帧直接跳到新位置。

### 使用注意事项

- **未选中页里的控件取消选中时会 detach**：它在 `OnAttachedToTree` 里拿到的东西（窗口订阅、
  弹出窗口注册）都会失去。需要跨切换保留的状态要放在控件自己的成员里。
- **未选中页够不着**：命中测试和 Tab 焦点顺序都跳过它。
- 选项卡头的字号是常量 13 DIP，不读排版 token。

### 键盘导航

| 按键 | 行为 |
| --- | --- |
| Tab | 整条选项卡条是一个焦点停靠点 |
| ← / → | 上一个 / 下一个选项卡（所有方位相同） |
| Home / End | 第一个 / 最后一个选项卡 |

### 主题 Token

| Token | 用途 |
| --- | --- |
| `layerFill` | 选项卡条底色 |
| `controlFillDefault` / `controlFillHover` | 选中头 / 悬停头的圆角底 |
| `controlStrokeDefault` | 选中或悬停头的细描边 |
| `accent` | 选中指示条 |
| `textPrimary` / `textSecondary` | 选中 / 未选中头文字；关闭按钮 × 的悬停 / 常态 |
| `focusStroke` | 焦点环 |

### 演示页

demo 导航 → 窗口 & 对话框 → TabControl（nav id 601）。

---

## 相关控件

- [ScrollPanel](ScrollPanel.md) — 选项卡内容超出高度时放进它
- `PageHost`（`extensions/NavigationControls`） — 多页容器，所有页保持挂载、带过渡动画
