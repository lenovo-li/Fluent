# InfoBar 控件

## 概述

内联状态消息：一条带严重级别配色的通知，长在页面布局里而不是弹窗里。四个级别
（信息 / 成功 / 警告 / 错误）各自对应一对主题 token，所以「这条消息是警告」这件事
由主题决定颜色，控件里没有任何硬编码色值。

高度由内容决定：消息在可用宽度处换行，控件上报换行后需要的高度，因此窄列里会变高
而不是被裁掉，调用者不需要猜高度。

**典型使用场景**:
- 表单校验反馈（提交失败的原因）
- 长任务的结果通知（「回测完成，37 笔交易」）
- 数据质量提示（「科创板查不到行业分类，相对估值会缺失」）
- 拒绝执行的说明（「自由现金流为负，无法估值」）

**它不是对话框。** `ContentDialog` 打断用户并要求一个决定；InfoBar 待在布局里陈述
事实。这个区别决定了它派生自 `Control`、参与正常布局，而不像
`MessageDialog` 那样持有一个 popup 窗口。

---

## 继承层次

```
Visual
  └─ UIElement
      └─ FrameworkElement
          └─ Control
              └─ InfoBar
```

**命名空间**: `fluent`
**头文件**: `FluentUI/controls/InfoBar.h`

---

## 公开 API

### 构造函数

```cpp
InfoBar();
```

默认：`Severity::Informational`、不可关闭、不可获得焦点、`HAlign::Stretch`。

不可获得焦点是刻意的：一条不可关闭的 InfoBar 是只读文字，把它放进 Tab 顺序会让
键盘用户在没有任何可激活目标的文本间穿行 —— 那是噪音，不是可访问性。调用
`SetClosable(true)` 后它才变得可聚焦。

---

### 属性

#### Severity

```cpp
enum class Severity { Informational, Success, Warning, Error };
void SetSeverity(Severity s);
Severity GetSeverity() const;
```

严重级别，决定填充色、边框色和左侧标记的颜色。

**脏标记等级：Render。** 级别只换颜色，不改变任何度量（标记框对每个级别都是固定
尺寸），所以重新测量是纯浪费。对比 `Button::SetKind` —— 它看起来类似但**是**
Measure 级，因为它选择字重从而改变 desired size。

#### Title

```cpp
void SetTitle(std::wstring text);
const std::wstring& Title() const;
```

加粗的首行。**可选**：只有消息、没有标题是合法且常见的形态（一行通知），此时不会
为标题预留任何纵向空间。

**脏标记等级：Measure**（多一行改变高度）。

#### Message

```cpp
void SetMessage(std::wstring text);
const std::wstring& Message() const;
```

正文。在可用宽度处换行，是驱动控件高度的主要因素。

**脏标记等级：Measure**。

#### Closable

```cpp
void SetClosable(bool closable);
bool IsClosable() const;
```

是否显示关闭按钮。同时切换 `SetFocusable`。

**脏标记等级：Measure** —— 关闭按钮占用横向空间，改变消息可用的宽度，因此改变
它换行的行数。

---

### 方法

#### CloseButtonRect

```cpp
RectDip CloseButtonRect() const;
```

关闭按钮的命中区域（窗口绝对 DIP）。不可关闭时返回空矩形，而不是一个幽灵命中区。

公开的理由和 `TabControl::HeaderRect` 相同：headless 测试没有别的办法验证这块
几何。按钮顶部对齐而非垂直居中 —— 在多行的高条上，居中的关闭按钮会飘离它所属的
标题。

---

### 事件

#### Closed

```cpp
Event<InfoBar, RoutedEventArgs>& Closed();
```

关闭按钮被点击（或聚焦时按 Space / Enter）时触发。

**控件不会自己隐藏或移除自己。** 这是刻意的：一个静默消失的控件会在父级仍在为它
计算尺寸的布局里留下一个洞，而且「关掉之后怎么办」（移除、折叠、记住选择）每个
应用不一样。隐藏它是调用者的决定：

```cpp
sub_ = bar->Closed().Subscribe(this, [](void* o, InfoBar& b, RoutedEventArgs&) {
    b.SetVisible(false);      // 这一行是应用的选择
});
```

⚠️ **返回的 `Subscription` 必须持有。** 它是 RAII 的，丢掉返回值等于立刻退订，
处理器再也不会被调用。

---

## 代码示例

### 最小示例

```cpp
#include "FluentUI/controls/InfoBar.h"

auto* bar = panel->Add(std::make_unique<InfoBar>());
bar->SetSeverity(InfoBar::Severity::Warning);
bar->SetMessage(L"数据覆盖不全，部分标的缺少行业分类。");
```

### 表单校验

```cpp
auto* feedback = card->Add(std::make_unique<InfoBar>());
feedback->SetSeverity(InfoBar::Severity::Informational);
feedback->SetMessage(L"填入代码后点「查询」。");

submitSub_ = submit->Click().Subscribe(this,
    [](void* o, Button&, RoutedEventArgs&) {
        auto* self = static_cast<MyPage*>(o);
        if (self->input_->Text().empty()) {
            self->feedback_->SetSeverity(InfoBar::Severity::Error);
            self->feedback_->SetTitle(L"代码不能为空");
            self->feedback_->SetMessage(L"请输入 6 位股票代码。");
        } else {
            self->feedback_->SetSeverity(InfoBar::Severity::Success);
            self->feedback_->SetTitle(L"已提交");
            self->feedback_->SetMessage(L"正在查询…");
        }
    });
```

---

## 备注

### 性能特征

- **Measure 成本**：每次 Measure 构建 1~2 个 DirectWrite 布局（标题不换行、正文
  换行）。文字或宽度不变时由 `MeasureCached` 挡住，不会每帧重建。
- **Render 成本**：一个圆角矩形填充 + 描边、一个圆环 + 圆点（几何绘制，不依赖
  图标字体）、1~2 次 DrawText、可选的关闭按钮两条线。无动画、无每帧 tick。
- **不请求动画帧**：`WantsAnimationTick()` 未被重写，所以它对空闲成本零贡献。

### 使用注意事项

- **不要写死高度。** 高度取决于换行结果，写死一个值在窗口变窄时会切掉文字。这个
  框架的 Arrange 契约是「子元素永不超出容器」，超出部分静默裁剪 —— 没有警告。
单子元素容器（`Viewbox`、`Expander`）忽略子元素的 `Margin`——但 `Border` 与 `GroupBox`
其实尊重 Margin（两者都通过 `FrameworkElement::ComputeArrangeRect` 应用它）。Panel 的
子元素 Margin 始终生效。给 InfoBar 自己设 Margin 在 Panel 里有效。
- 焦点环画在 `bounds_` 之外，`VisualOverflowDip()` **无条件**上报该外扩量（不按
  `IsFocused()` 门控）—— 失焦那一帧仍需重绘环占据的像素，而那时 `IsFocused()`
  已经是 false 了。

### 键盘导航

| 按键 | 行为 |
| --- | --- |
| Tab | 仅当 `IsClosable()` 时进入 Tab 顺序 |
| Space / Enter | 触发 `Closed` |

### 主题 Token

| Token | 用途 |
| --- | --- |
| `severityInfoFill` / `severityInfoStroke` | Informational 的底色 / 边框与标记 |
| `severitySuccessFill` / `severitySuccessStroke` | Success |
| `severityWarningFill` / `severityWarningStroke` | Warning |
| `severityErrorFill` / `severityErrorStroke` | Error |
| `textPrimary` | 标题与正文 |
| `controlFillHover` | 关闭按钮悬停底色 |
| `spacing.borderWidth` | 边框宽度 |

浅色主题的 fill 是淡色调，深色主题的 fill 是低透明度的同色调覆盖（浅色调在深色
背景上会刺眼），而 stroke 在深色主题下**被提亮**而不是复用浅色值 ——
`0x0E700E` 画在 `0x202020` 上几乎看不见，那正是这些 token 存在的理由。

**severityInfo 不是 accent 的别名。** accent 是用户可个性化的颜色（可以被设成
红色），复用它会让信息提示在某些机器上和错误提示无法区分。

### 尚未实现

- **动作按钮**：WinUI 的 InfoBar 有一个。加它意味着命中测试、焦点顺序和第二个
  点击目标，目前没有消费者需要。把 `Button` 放在父面板里 InfoBar 旁边即可。
- **开关动画**：错误消息瞬间出现才是对的，而高度动画是本框架最贵的形状
  （Measure 级，每帧重排整棵子树）。
- **图标字体**：严重级标记用几何图形绘制，无字体依赖 —— headless 像素测试也依赖
  这一点。

### 演示页

demo 导航 → 数据控件 → InfoBar（nav id 406）。含四级别对照、三种形态、
宽度-高度联动（拖滑块）、以及与 TextBox/Button 组合的表单校验。
