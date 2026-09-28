# ToggleSwitch 控件

## 概述

一个开/关拨动开关：40×20 DIP 的轨道加一个滑动旋钮，右侧带标签。语义是「立即生效的
二元设置」——拨动就是提交，没有确认步骤。

**和 CheckBox 的区别不是外观而是语义**：CheckBox 表达「这一项被选中」，通常配合一个
提交动作（表单、勾选列表）；ToggleSwitch 表达「这个功能现在开着」，拨动即刻生效。
需要「确定/取消」的场景应当用 CheckBox。

**典型使用场景**:
- 设置页的功能开关（Wi-Fi、自动更新、深色模式）
- 立即生效的偏好项
- 启用/禁用一组相关控件的总开关

---

## 继承层次

```
Visual
  └─ UIElement
      └─ FrameworkElement
          └─ Control
              └─ ButtonBase
                  └─ ToggleButton
                      └─ ToggleSwitch
```

`ToggleButton` 持有那个 bool 与 `SetChecked`/`Toggle()` 状态机，`ToggleSwitch` 只加
旋钮滑动动画与轨道绘制。RadioButton **不是** ToggleButton —— 它的「选中」派生自共享的
组状态而不是自己拥有的 bool。

**命名空间**: `fluent`
**头文件**: `FluentUI/controls/ToggleSwitch.h`

---

## 公开 API

### 构造函数

```cpp
ToggleSwitch();
```

默认关闭、可聚焦、可点击、`HAlign::Stretch`。

### 属性

#### On / Checked

```cpp
void SetOn(bool on);
bool IsOn() const;
```

`SetOn` / `IsOn` 是继承来的 `SetChecked` / `IsChecked` 的**同义别名**,存在的理由是
调用点的可读性:`wifi->SetOn(true)` 比 `wifi->SetChecked(true)` 更贴合这个控件的语义。
两组名字操作同一个 bool,可以混用。

`SetChecked` 在值未变时是静默的(不触发 `Toggled`、不重绘、不排帧)。

**脏标记等级:Render**(旋钮位置变了,尺寸没变)。

#### Text

```cpp
void SetText(std::wstring text);
```

轨道右侧的标签。参与控件的 desired 宽度。

**脏标记等级:Measure**。

### 事件

#### Toggled

```cpp
Event<ToggleSwitch, bool>& Toggled();
```

状态**因用户操作**而改变时触发(点击 / Space / Enter)。程序调用 `SetOn` 不触发 ——
这个区分是刻意的:恢复保存的设置时不应该跑一遍「用户刚刚打开了它」的副作用。

处理器签名注意:`Args` 是**引用**。

```cpp
sub_ = toggle->Toggled().Subscribe(this,
    [](void* owner, ToggleSwitch&, bool& on) {     // 注意 bool&，不是 bool
        static_cast<MyPage*>(owner)->advanced_->SetEnabled(on);
    });
```

**返回的 `Subscription` 必须持有**。它是 RAII 的,丢掉返回值等于立刻退订。

轨道尺寸是**固定的** 40×20 DIP,不随字号变化。字号变大只增加控件的 desired 高度,
轨道与命中区保持一致 —— 开关的可点区域不该因为标签排版而变。
(`TrackW()` / `TrackH()` 是 private 方法，`ToggleSwitch.h:62-63`)

---

## 代码示例

### 最小示例

```cpp
#include "FluentUI/controls/ToggleSwitch.h"

auto* toggle = panel->Add(std::make_unique<ToggleSwitch>());
toggle->SetText(L"Wi-Fi");
toggle->SetOn(true);
toggle->SetHAlign(HAlign::Left);      // 默认 Stretch 会占满整行
```

### 设置页的对齐形状

```cpp
// 左列说明用 Star 列，右列开关用 Auto 列并右对齐，多行开关就会纵向对齐成一条线
auto* grid = card->Add(std::make_unique<Grid>());
grid->AddColumn(GridLength::Star(1.0f));
grid->AddColumn(GridLength::Auto());

for (int i = 0; i < 3; ++i) {
    grid->AddRow(GridLength::Auto());      // Auto：不要写死行高
    auto* label = grid->Add(std::make_unique<TextBlock>());
    grid->SetCell(label, i, 0);
    label->SetVAlign(VAlign::Center);

    auto* t = grid->Add(std::make_unique<ToggleSwitch>());
    grid->SetCell(t, i, 1);
    t->SetHAlign(HAlign::Right);
    t->SetVAlign(VAlign::Center);
}
```

---

## 备注

### 性能特征

- **Render**:两个圆角矩形(轨道 + 旋钮)+ 一次 DrawText。
- **动画**:旋钮滑动是 O(1) 的 Render 级动画,`AnimatedValue` + `motion.fadeTau`。
  **不是** Measure 级 —— 滑动不改变任何尺寸,所以不会触发重新布局。
- **首帧不动画**:`SnapAnimationsToSettledState()` 在第一次 Arrange 时把旋钮直接放到
  位。这条修过一个 bug:此前 `SetOn(true)` 建出来的开关第一次显示时旋钮会从左滑到右。
- 动画结束后 `WantsAnimationTick()` 返回 false,回到零成本空闲。

### 使用注意事项

- ⚠️ **`SetForeground` 是旋钮色,不是标签文字色。** 对整个 toggle 家族
  (CheckBox 的勾、RadioButton 的点、ToggleSwitch 的旋钮),Foreground 指的是「画在
  accent 填充上的那个记号」。把它和 `SetAccentColor` 设成同一个颜色会让旋钮消失。
  想改标签颜色请改主题的 `textPrimary`,或者外面放独立的 `TextBlock`。
- **默认 Stretch**:在宽页面上会占满整行(文字左对齐、右边留白)。要收缩到内容宽度用
  `SetHAlign(HAlign::Left)`。
- **容器过窄时标签被裁**,不会溢出也不会警告 —— 这是框架的 Arrange 契约。
- 焦点环画在 `bounds_` 之外,`VisualOverflowDip()` 无条件上报该外扩量。

### 键盘导航

| 按键 | 行为 |
| --- | --- |
| Tab | 进入焦点顺序(禁用时跳过) |
| Space / Enter | 拨动并触发 `Toggled` |

### 主题 Token

| Token | 用途 |
| --- | --- |
| `accent` / `accentHover` / `accentPressed` | 开启态轨道与焦点环（自绘，不走 DrawFocusRing，`:135`） |
| `controlFillDefault` / `controlFillHover` | 关闭态轨道 |
| `onAccent` | 旋钮(未设 Foreground 时) |
| `textPrimary` | 标签 |
| `motion.fadeTau` | 滑动时间常数 |

**禁用态**:填充与旋钮保留色相、alpha ×0.4,和禁用的 Button 一致。显式设的
`AccentColor` 会被淡化(它是语义角色),显式设的 `Background` **不会**(它是字面值)——
这条优先级规则和 `Button` 相同。

### 演示页

demo 导航 → 基础控件 → ToggleSwitch。含四种状态、对齐与窄容器行为、设置组 Grid 形状、
五种 AccentColor、字号、Foreground 陷阱的对错对照,以及联动禁用一组控件。
