# RadioButton 控件

## 概述

圆环加中心圆点,右侧带标签。语义是「在互斥的几项里选一个」。

**它的选中状态不是自己拥有的 bool,而是派生自一个共享的 `int`。** 这是本控件最重要的
设计点,也是它 **不** 继承 `ToggleButton` 的原因:激活的动作是「选中」而不是「切换」,
而「谁被选中」这个事实属于整个组而不属于某一个按钮。

**典型使用场景**:
- 主题选择(浅色 / 深色 / 跟随系统)
- 排序方式、付款方式
- 任何「必须选一个且只能选一个」的设置

需要多选请用 `CheckBox`;选项超过五六个时下拉框(`ComboBox`)更合适。

---

## 继承层次

```
Visual
  └─ UIElement
      └─ FrameworkElement
          └─ Control
              └─ ButtonBase
                  └─ RadioButton
```

注意它**直接继承 `ButtonBase`**,不经过 `ToggleButton`。理由见上:没有自己的 bool 可切换。

**命名空间**: `fluent`
**头文件**: `FluentUI/controls/RadioButton.h`

---

## 公开 API

### 构造函数

```cpp
RadioButton();
```

默认未加入任何组(`group_ == nullptr`,此时 `IsSelected()` 恒为 false)、可聚焦、
`HAlign::Stretch`。

### 属性

#### Group

```cpp
void SetGroup(int* groupValue, int value);
bool IsSelected() const;      // group_ && *group_ == value
```

把这个按钮接入一个组:`groupValue` 指向组共享的选中值,`value` 是本按钮代表的取值。

**指针不被持有(not owned)。** 组变量的生命期必须覆盖所有成员按钮 —— 通常是一个
`static int` 或页面/视图模型的成员。

**这个设计的取舍**:互斥性由「大家读同一个 int」天然保证,不需要按钮之间互相认识,也不
需要一个 RadioGroup 容器去广播取消选中。代价是组变量的生命期由调用者负责。

```cpp
static int themeChoice = 0;                  // 0 = 浅色
auto* light = card->Add(std::make_unique<RadioButton>());
light->SetText(L"浅色");
light->SetGroup(&themeChoice, 0);            // 建出来就是选中的
auto* dark = card->Add(std::make_unique<RadioButton>());
dark->SetText(L"深色");
dark->SetGroup(&themeChoice, 1);
```

#### Text

```cpp
void SetText(std::wstring text);
```

**脏标记等级:Measure**。

### 事件

#### Selected

```cpp
Event<RadioButton, int>& Selected();
```

本按钮被用户选中时触发,`Args` 是**该按钮的 value**(引用传递)。

⚠️ **只有被选中的那个按钮会触发。** 失去选中的按钮**不会**收到通知 —— 组里只有一个
「谁被选中」的事实,变化通过新选中者的事件表达。需要知道「切走了」的调用者应当订阅组里
所有按钮,或者在处理器里读组变量。

```cpp
sub_ = dark->Selected().Subscribe(this,
    [](void* owner, RadioButton&, int& value) {    // 注意 int&
        static_cast<MyPage*>(owner)->ApplyTheme(value);
    });
```

⚠️ 返回的 `Subscription` 必须持有(RAII)。

---

## 备注

### 性能特征

- **Render**:一个圆环 + 一个中心圆点 + 一次 DrawText。全部几何绘制,不依赖图标字体。
- **圆点入场动画是 Render 级 O(1)**,不触发重新布局。
- **首帧不动画**:第一次 Arrange 时圆点直接落到终态。此前「建出来就选中」的按钮第一次
  显示时圆点会自己长出来。

### 使用注意事项

- ⚠️ **组变量必须活得比按钮长。** 指针不被持有,悬空会在下一次绘制时读到已释放的内存。
- ⚠️ **`SetForeground` 是圆点的颜色,不是标签文字色**(整个 toggle 家族一致)。和
  `SetAccentColor` 设成同色会让圆点消失。
- **焦点环是圆形的**,画在圆环外侧且位于控件左缘。实测过一个 bug:父容器裁剪会切掉环的
  弧段。`VisualOverflowDip()` 无条件上报该外扩量。
- **默认 Stretch**,通常要显式 `SetHAlign(HAlign::Left)`。
- 未调用 `SetGroup` 的按钮永远画成未选中,且点击不会有任何视觉变化 —— 这不是 bug,是
  「没有组就没有选中概念」的直接结果。

### 键盘导航

| 按键 | 行为 |
| --- | --- |
| Tab | 进入焦点顺序 |
| Space / Enter | 选中本项并触发 `Selected` |

### 主题 Token

| Token | 用途 |
| --- | --- |
| `accent` / `accentHover` / `accentPressed` | 选中态圆环填充(未设 AccentColor 时) |
| `controlFillDefault` / `controlFillHover` | 未选中态填充 |
| `onAccent` | 中心圆点(未设 Foreground 时) |
| `textPrimary` | 标签 |
| `accent` | 焦点环（RadioButton 自绘，用 accent 不是 focusStroke，`.cpp:108-109`） |

**禁用态**:填充与圆点保留色相、alpha ×0.4。

### 演示页

demo 导航 → 基础控件 → RadioButton(nav 103)。原「样式定制大全」里的 RadioButton 完整
演示已并入此页。
