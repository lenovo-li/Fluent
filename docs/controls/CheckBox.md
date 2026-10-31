# CheckBox 控件

## 概述

方框加勾选记号,右侧带标签。语义是「这一项被选中」,通常配合一个提交动作 —— 表单、
勾选列表、多选设置。

**和 ToggleSwitch 的区别是语义而非外观**:CheckBox 表达选中状态,常在「确定/取消」之后
才生效;ToggleSwitch 表达「功能现在开着」,拨动即刻生效。需要确认步骤的用 CheckBox。

**典型使用场景**:
- 表单里的多选项
- 「记住我」「我已阅读并同意」
- 权限/功能模块的批量勾选

---

## 继承层次

```
Visual
  └─ UIElement
      └─ FrameworkElement
          └─ Control
              └─ ButtonBase
                  └─ ToggleButton
                      └─ CheckBox
```

`ToggleButton` 持有那个 bool 与状态机;`CheckBox` 只加方框与勾的绘制和入场动画。
`RadioButton` **不是** ToggleButton —— 它的「选中」派生自共享的组状态。

**命名空间**: `fluent`
**头文件**: `FluentUI/controls/CheckBox.h`

---

## 公开 API

### 构造函数

```cpp
CheckBox();
```

默认未勾选、可聚焦、可点击、`HAlign::Stretch`。

### 属性

#### Checked

```cpp
void SetChecked(bool v);      // 继承自 ToggleButton
bool IsChecked() const;
```

**说明**: CheckBox 继承自 ToggleButton。`Toggle()` 是 **protected** 方法（由点击 / Space
触发的 `OnActivate` 调用），应用代码无法直接调用。公开的状态操作只有 `SetChecked(bool)` /
`IsChecked()`。

`SetChecked` 在值未变时是**静默的** —— 不触发事件、不重绘、不排帧,所以一次冗余的设置
不会白白安排一帧。

**脏标记等级:Render**(勾的进度变了,尺寸没变)。

#### Text

```cpp
void SetText(std::wstring text);
```

方框右侧的标签,参与 desired 宽度。

**脏标记等级:Measure**。

### 事件

#### Checked

```cpp
Event<CheckBox, bool>& Checked();
```

⚠️ **事件名是 `Checked()`,不是 `CheckedChanged()`。** 它返回事件对象本身,和
`IsChecked()` 是两回事(后者读状态)。

状态**因用户操作**改变时触发。程序调用 `SetChecked` 不触发 —— 恢复保存的设置时不该跑一遍
「用户刚刚勾了它」的副作用。

`Args` 是**引用**:

```cpp
sub_ = check->Checked().Subscribe(this,
    [](void* owner, CheckBox& cb, bool& checked) {   // 注意 bool&
        static_cast<MyPage*>(owner)->OnPermissionChanged(checked);
        (void)cb;
    });
```

⚠️ 返回的 `Subscription` 必须持有(RAII,丢掉即退订)。

---

## 代码示例

### 最小示例

```cpp
#include "FluentUI/controls/CheckBox.h"

auto* check = panel->Add(std::make_unique<CheckBox>());
check->SetText(L"我已阅读并同意服务条款");
check->SetChecked(false);
check->SetHAlign(HAlign::Left);       // 默认 Stretch 会占满整行
```

### 一组权限勾选

```cpp
// Event 的 handler 是无捕获函数指针，所以每个勾选框的状态必须挂在
// owner 指向的对象上（这里是页面自己）
struct Permissions { CheckBox* read; CheckBox* write; CheckBox* exec; };

for (auto* cb : {perms.read, perms.write, perms.exec}) {
    subs_ += cb->Checked().Subscribe(this,
        [](void* o, CheckBox&, bool&) {
            static_cast<MyPage*>(o)->RecomputeSummary();
        });
}
```

---

## UI Automation

CheckBox 的 peer（`automation/ControlPeers.h`）：

| 项 | 值 |
|---|---|
| ControlType | CheckBox |
| 模式 | Toggle（On/Off） |
| Name 来源 | `Text()` |
| AutomationId | `SetAutomationId` |
| HelpText | `Tooltip()` |

惰性创建：只有 UIA 客户端（讲述人、Inspect.exe、测试工具）访问到这个控件时才创建 peer，没有客户端时零开销。模式操作（Invoke/Toggle/Select/SetValue）经投递消息执行，执行前重新检查控件仍在树上且已启用，所以处理函数里删除控件自身是安全的。属性变化事件（Name、IsEnabled、ToggleState、Value）在控件失效后合并检查，只有真正变化且有客户端监听时才发送。自定义方式见 inheritance-guide.md 的“UI Automation”一节。

验证：`tests/unit/AutomationPeerTests.cpp`、`AutomationPeerLifetimeTests.cpp`（无头，用记录调用的假 UIA core），10 处变异全部被测试抓到；真实客户端下的表现需在真机用 Inspect.exe 检查。

## 备注

### 性能特征

- **Render**:一个圆角方框 + 勾的两段线 + 一次 DrawText。勾用几何绘制,不依赖图标字体
  (离屏像素测试也依赖这一点)。
- **入场动画是 Render 级 O(1)**(`AnimatedValue` + `motion.fadeTau`),不触发重新布局。
- **Measure 与 Render 用同一个字号**:Measure 走 `EffectiveFontSize(...)`,和 Render 一致。
  此前 Measure 读主题的 `bodySize`,`SetFontSize(24)` 后按 14 DIP 预留空间、按 24 DIP 绘制,
  标签被 CLIP 截掉右侧和下部。由 `LabelMeasureCache.CheckBoxMeasureHonoursFontSize` 锁定。
- **首帧不动画**:`SnapAnimationsToSettledState()` 在第一次 Arrange 时把勾直接放到终态。
  此前 `SetChecked(true)` 建出来的勾选框第一次显示时会「自己勾上」。

### 使用注意事项

- ⚠️ **`SetForeground` 是勾的颜色,不是标签文字色。** 对整个 toggle 家族,Foreground 指
  「画在 accent 填充上的那个记号」。把它和 `SetAccentColor` 设成同色会让勾消失 ——
  这个错误在 demo 里真的发生过(紫底画紫勾)。
- **默认 Stretch**:通常要显式 `SetHAlign(HAlign::Left)`。
- **焦点环画在方框外侧**,而方框位于控件左缘,所以左侧外扩最多。`VisualOverflowDip()`
  **无条件**上报该外扩量(不按 `IsFocused()` 门控)—— 失焦那一帧仍需重绘环占据的像素。
- 容器过窄时标签被裁,不溢出也不警告(框架的 Arrange 契约)。

### 键盘导航

| 按键 | 行为 |
| --- | --- |
| Tab | 进入焦点顺序(禁用时跳过) |
| Space / Enter | 切换并触发 `Checked` |

### 主题 Token

| Token | 用途 |
| --- | --- |
| `accent` / `accentHover` / `accentPressed` | 勾选态方框填充(未设 AccentColor 时) |
| `controlFillDefault` / `controlFillHover` | 未勾选态填充 |
| `onAccent` | 勾(未设 Foreground 时) |
| `textPrimary` | 标签 |
| `focusStroke` | 焦点环 |
| `motion.fadeTau` | 入场时间常数 |

**禁用态**:填充与勾保留色相、alpha ×0.4,和禁用的 Button 一致。显式 `AccentColor` 会被
淡化(语义角色),显式 `Background` 不会(字面值)—— 与 `Button` 同一条优先级规则。

### 尚未实现

- **三态(indeterminate)**:`ToggleButton` 只持有一个 bool。三态需要状态机与第三种绘制,
  且「部分勾选」的语义要由父子关系驱动,属于更大的改动。

### 演示页

demo 导航 → 基础控件 → CheckBox(nav 102)。原「样式定制大全」里的 CheckBox 完整演示
已并入此页 —— 一个控件分两页展示只会让人不知道该看哪页。
