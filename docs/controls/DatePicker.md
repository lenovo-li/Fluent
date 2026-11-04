# DatePicker 控件

## 概述

一个显示所选日期（`2026/08/15`）的按钮，点击后弹出一个 `Calendar` 供选择。选中日期后弹窗
关闭、按钮文字更新。聚焦时按 ↑ / ↓ 直接把日期加减一天，不必打开日历。

它是 `Calendar` 的薄外壳：绘制、翻月、日期运算都由 Calendar 完成，DatePicker 只负责按钮外观
和弹出窗口（与 ComboBox 同一套 `PopupHost`）。

**典型使用场景**:
- 表单里的单个日期字段（生日、截止日）
- 空间有限、不能常驻一个整月日历的地方

---

## 继承层次

```
Visual
  └─ UIElement
      └─ FrameworkElement
          └─ Control
              └─ ButtonBase
                  └─ DatePicker
```

`ButtonBase` 提供点击手势（指针 + Space/Enter）和状态动画；DatePicker 在 `OnActivate` 里打开日历。

**命名空间**: `fluent`
**头文件**: `FluentUI/controls/DatePicker.h`

---

## 公开 API

### 构造函数

```cpp
DatePicker();
```

默认日期是构造时的本地今天。

### 属性

```cpp
void SetSelectedDate(int year, int month, int day);
int SelectedYear() const;
int SelectedMonth() const;
int SelectedDay() const;
```

**脏标记**: Render（日期相同时直接返回）。`SetSelectedDate` **不触发** `DateChanged`，也**不做**
范围校验——DatePicker 没有 min/max；需要约束时在 `DateChanged` 里自己检查。

### 事件

```cpp
Event<DatePicker, RoutedEventArgs>& DateChanged();
Event<DatePicker, RoutedEventArgs>& CalendarOpened();
Event<DatePicker, RoutedEventArgs>& CalendarClosed();
```

- **`DateChanged`**：用户在日历里选了日期，或按 ↑ / ↓ 调整了日期。新值从 `SelectedYear()` 等访问器读。
  从日历选日期时，`DateChanged` 先触发，然后弹窗关闭、`CalendarClosed` 触发。
- **`CalendarOpened` / `CalendarClosed`**：弹窗打开后 / 关闭后，**任何路径**都触发——点击、
  Space/Enter、选中日期、轻触关闭（点外部、点在弹窗卡片的留白上）、detach 导致的关闭。
  每次状态转换恰好一次。
- **处理器里可以重新打开日历**：`CalendarClosed` 在弹窗已经完全关闭之后才触发，重新打开不会被拒绝，
  重新打开的日历照样能被轻触关闭。
- ⚠️ **不要在处理器里同步销毁这个 DatePicker**（也不要同步把它从树上移走再销毁）。`PopupHost::Close`
  本身扛得住，但 `Event::Raise` 在每个处理器返回后还要读自己的订阅列表；而由弹窗内点击触发的关闭，
  调用栈上还有弹窗的 `InputManager` 和被点击的元素。要让控件消失，就推迟到之后（`PostMessage`、下一帧）。

```cpp
sub_ = picker->DateChanged().Subscribe(this,
    [](void* owner, DatePicker& p, RoutedEventArgs&) {
        static_cast<MyPage*>(owner)->OnDate(p.SelectedYear(), p.SelectedMonth(), p.SelectedDay());
    });
Subscription sub_;   // 必须作为成员持有，丢掉即退订
```

---

## 代码示例

```cpp
#include "FluentUI/controls/DatePicker.h"

auto* picker = panel->Add(std::make_unique<DatePicker>());
picker->SetSelectedDate(2026, 8, 15);
```

---

## 备注

### 修过的 bug

- **弹窗自己关闭时也报告关闭。** 点在日历卡片的留白上时，`PopupHost` 走自己的轻触关闭。
  以前那条路径只清掉打开标志：`CalendarClosed` 不触发，窗口的解散钩子一直挂到 detach。
  现在所有关闭路径都经过同一个收尾函数（清标志、释放解散钩子、触发 `CalendarClosed`），
  且它是幂等的。
- **解散钩子只在打开期间持有。** 以前用的是上下文订阅，每打开一次就多一个，直到 detach 才
  一起释放；现在打开时注册、关闭时释放。
- **关闭事件在弹窗完全关闭后才触发。** `PopupHost::Close` 现在先进入 Closed 状态、最后才调
  所有者的关闭回调，回调之后不再访问 `PopupHost` 的成员。以前回调在 Closing 状态里运行，处理器里
  重新打开会被拒绝。
- **在关闭事件里重新打开后，轻触关闭仍然有效。** 窗口调用解散钩子时用的是钩子的本地副本，并且只有
  调用期间没有人注册新钩子时才清空钩子槽位（`NativeWindowHost::DispatchPopupDismiss`）。以前钩子返回后
  槽位被无条件清空，重新打开时刚注册的新钩子也被抹掉，点外部、移动窗口、失去激活都关不掉它。
  钩子槽位的规则是纯逻辑，由 `PopupDismiss.HookThatReopensKeepsTheNewHook`、
  `PopupDismiss.HandledHookClearsSlotAndRetiresItsToken`、`PopupDismiss.DecliningHookStaysRegistered` 锁定。

未验证：其余几处需要真实的弹出 HWND，headless 测试覆盖不到（`PopupHost` 在测试里不创建窗口）。
请在真机上看 demo 的 DatePicker 页：打开日历后点卡片边缘的留白，确认日历关闭且
`CalendarClosed` 计数加一；反复开关多次后，点窗口其他位置不应再有异常的关闭行为。

### 性能特征

- **按钮固定 120 × 32 DIP**，`Measure` 不读文字宽度。
- **弹窗只创建一次**，第一次打开时建，之后复用；detach 时只关闭不销毁（detach 可能发生在
  日历自己的事件分发里，那时销毁会释放正在执行的对象），再 attach 时继续可用。
- **idle 零成本**：没有动画 tick，日历打开时它自己的悬停动画走父窗口的动画时钟。

### 使用注意事项

- **没有日期范围约束**：DatePicker 不转发 Calendar 的 `SetMinDate` / `SetBlackoutDates`。
- **显式宽高会被忽略**：`Measure` 直接报告固定尺寸。
- **弹窗是独立顶层 HWND**，可以溢出父窗口；靠近屏幕底部时翻到按钮上方。

### 键盘导航

| 按键 | 行为 |
| --- | --- |
| Tab | 进入焦点顺序 |
| Space / Enter | 打开日历 |
| ↑ / ↓ | 日期加 / 减一天，触发 `DateChanged` |
| 日历打开时 | 键盘仍送到 DatePicker 本身（弹窗不取得焦点，也没有注册按键转发），所以 ↑ / ↓ 仍改按钮上的日期，没有 Esc 关闭。日历只响应鼠标 |

轻触关闭的触发条件是点在弹窗外、父窗口失去激活、移动或改变尺寸。

### 主题 Token

| Token | 用途 |
| --- | --- |
| `controlFillDefault` / `controlFillHover` / `controlFillPressed` | 按钮底色（按状态） |
| `controlStrokeDefault` | 按钮描边（`EffectiveBorderBrush` 回退值） |
| `focusStroke` | 焦点环（自绘，2 DIP） |
| `textPrimary` | 日期文字（`EffectiveForeground` 回退值） |

### 演示页

demo 导航 → 输入控件 → DatePicker（nav id 204）。

---

## 相关控件

- [Calendar](Calendar.md) — 弹窗里的月历；常驻显示时直接用它
- [ComboBox](ComboBox.md) — 同一套弹出窗口机制和关闭顺序
