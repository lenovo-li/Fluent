# Calendar 控件

## 概述

自绘的月视图日历：顶部导航栏（标题 + 上一月 / 下一月箭头）、星期表头、6 周 × 7 天共 42 个
日期格。全部在 `Render()` 里用 D2D 画出，没有子元素，所以命中测试简单、控件很轻。

支持日期范围（min / max）和黑名单日期；不可选的日期淡化并加删除线，点击和键盘都跳过它们。
`DatePicker` 的弹窗里就是一个 Calendar。

**典型使用场景**:
- 常驻显示的日期选择（预约、日程）
- 需要禁用部分日期的选择（节假日、已满的日期）
- 继承后加装饰：农历、节日标记、事件圆点（见 `extensions/CustomControls` 的 `LunarCalendar`）

---

## 继承层次

```
Visual
  └─ UIElement
      └─ FrameworkElement
          └─ Control
              └─ Calendar
```

**命名空间**: `fluent`
**头文件**: `FluentUI/controls/Calendar.h`

---

## 公开 API

### 构造函数

```cpp
Calendar();
```

默认选中并显示本地今天，可聚焦，无日期约束。

### 选中日期与显示月份

```cpp
void SetSelectedDate(int year, int month, int day);
void GetSelectedDate(int& year, int& month, int& day) const;
int SelectedYear() const;  int SelectedMonth() const;  int SelectedDay() const;

int ViewedYear() const;    int ViewedMonth() const;
void SetViewedMonth(int year, int month);
void PreviousMonth();
void NextMonth();
```

- **`SetSelectedDate` 拒绝被约束禁止的日期**（超出 min / max 或在黑名单里），不管是不是程序调用。
  选中别的月份的日期会把视图翻到那个月。不触发 `SelectionChanged`。
- **`SetViewedMonth` 只翻页，不改选中日期。**

### 日期约束

```cpp
void SetMinDate(int year, int month, int day);   void ClearMinDate();
void SetMaxDate(int year, int month, int day);   void ClearMaxDate();
bool HasMinDate() const;  bool HasMaxDate() const;
void GetMinDate(int& y, int& m, int& d) const;
void GetMaxDate(int& y, int& m, int& d) const;

void SetBlackoutDates(const std::vector<std::tuple<int,int,int>>& dates);
void ClearBlackoutDates();
bool IsDateBlackedOut(int y, int m, int d) const;   // 排序数组上二分查找
bool IsDateEnabled(int y, int m, int d) const;      // 在 [min, max] 内且不在黑名单
```

### 几何与命中测试（公开给测试）

```cpp
void CellDate(int i, int& y, int& m, int& d) const;   // 第 i 格（0..41，行优先，周日起）
int  CellIndexOf(int y, int m, int d) const;         // 不在视图内返回 -1
RectDip CellRect(int cellIndex) const;
int  HitTestCell(float dipX, float dipY) const;      // 不在格子上返回 -1
```

日期运算是头文件里的纯函数：`CalIsLeapYear`、`CalDaysInMonth`、`CalDayOfWeek`、
`CalAddMonths`（日期夹到新月份的天数，用于翻月）、`CalAddDays`（精确的逐日运算，跨月跨年进位）。
**不要用 `CalAddMonths` 做逐日运算**：它会把溢出的日期夹到月末，所有格子都会报告同一天。

### 事件

```cpp
Event<Calendar, RoutedEventArgs>& SelectionChanged();
struct DisplayMonthChangedArgs { int year; int month; };
Event<Calendar, DisplayMonthChangedArgs>& DisplayMonthChanged();
```

- **`SelectionChanged`**：用户点选日期或按 Enter。方向键移动选中日期但**不**触发它（Enter 才算提交）。
- **`DisplayMonthChanged`**：显示的月份变了（箭头、`SetViewedMonth`、选中日期翻页）。落在当前月份上的
  调用不触发，所以处理器不会被重复设置同一个月驱动成循环。

`Subscription` 必须作为成员持有，丢掉即退订。

---

## 代码示例

```cpp
#include "FluentUI/controls/Calendar.h"

auto* cal = panel->Add(std::make_unique<Calendar>());
cal->SetMinDate(2026, 1, 1);
cal->SetMaxDate(2026, 12, 31);
cal->SetBlackoutDates({{2026, 10, 1}, {2026, 10, 2}});

sub_ = cal->SelectionChanged().Subscribe(this,
    [](void* owner, Calendar& c, RoutedEventArgs&) {
        static_cast<MyPage*>(owner)->OnPick(c.SelectedYear(), c.SelectedMonth(), c.SelectedDay());
    });
```

---

## UI Automation

Calendar 的 peer（`automation/DataPeers.h`）：

| 项 | 值 |
|---|---|
| ControlType | Calendar |
| 模式 | Selection（单选，必须有选中） |
| 子节点 | 42 个日期格，DataItem，Name = 用户区域设置的长日期（`GetDateFormatEx(DATE_LONGDATE)`，例如 “2026年11月1日”），超出 min/max 或在 blackout 中的日期 IsEnabled = false |
| SelectionItem | Select 走新的公开 `PerformSelectDate`（与点击日期格相同：拒绝禁用日期，触发 `SelectionChanged`）；`SetSelectedDate` 是程序设置，不触发 |

日期格按日期（yyyymmdd）作 key：翻到下个月后，不再显示的日期节点被释放并发 ChildrenInvalidated，两个月共有的溢出日期保留原节点。命中测试先把点按 Viewbox 反算，再用 `HitTestCell`。

惰性创建：只有 UIA 客户端访问到这个控件时才创建 peer，没有客户端时零开销。行（项）不是元素，客户端访问到才创建节点。模式操作经投递消息执行，执行前重新检查控件仍在树上且已启用。自定义方式见 inheritance-guide.md 的“UI Automation”一节。

验证：无头测试 `tests/unit/AutomationDataPeerTests.cpp`，真实 UIA 客户端自测 `FluentUITests.exe --uia-smoke`；12 处变异（把操作换回不发事件的 setter、行名只取一列、日期按下标作 key、日志行序号不前进等）全部被测试抓到。讲述人实际朗读的效果需要在真机上听。

## 备注

### 修过的 bug

- **绘制裁剪到 `bounds_`。** `Arrange` 把格子下限定在自然尺寸（36 × 32 DIP），所以槽位比自然
  尺寸窄或矮时，网格会画出控件边界：盖到相邻元素上，而且落在脏区之外（Calendar 不声明
  `VisualOverflowDip`），画在那里的悬停或选中高亮永远不会被清掉，留成残影。现在 `Render`
  一开始就 `PushClip(bounds_)`，三个子类绘制钩子也在这个裁剪里执行。
- **「今天」的描边圈内缩半个线宽。** 最后一列和最后一行的格子贴着 `bounds_`，裁剪后居中在
  格子边上的 1 DIP 描边会丢掉外侧一半；内缩 0.5 DIP 让整条描边都在格子里。

未验证：两处都是像素层面的修复，headless 测试没有设备，覆盖不到。请在真机上看：把 Calendar
放进一个比 252 × 252 DIP 更小的固定槽位（例如 `SetWidth(200)`），在右侧和底部的格子上来回悬停，
确认相邻控件上没有残留的高亮；再看「今天」落在最后一列或最后一行时圆圈是否完整。

### 性能特征

- **每帧固定成本**：42 格 + 导航栏 + 表头，无布局对象。
- **悬停高亮淡入淡出**走 `AnimatedValue`，收敛后 `WantsAnimationTick()` 返回 false，idle 零成本。
- **自然尺寸 252 × 252 DIP**（7 × 36 宽；36 导航栏 + 24 表头 + 6 × 32 高）。给的空间更大时格子按比例放大；
  更小时格子保持自然尺寸并被裁剪。

### 使用注意事项

- **黑名单查找是 O(log n)**，`SetBlackoutDates` 会排序输入。
- **方向键跳过禁用日期**，最多连续跳 365 天；找不到可选日期就不动。
- **自定义前景色**（`SetForeground`）会覆盖所有可选日期的文字色，包括选中格。

### 键盘导航

| 按键 | 行为 |
| --- | --- |
| ← / → | 前一天 / 后一天（跨月时翻页） |
| ↑ / ↓ | 前一周 / 后一周 |
| Enter | 提交当前选中日期，触发 `SelectionChanged` |

### 主题 Token

| Token | 用途 |
| --- | --- |
| `cardFill` | 背景（`EffectiveBackground` 回退值） |
| `accent` | 选中格填充、「今天」圆圈（`EffectiveAccentColor`） |
| `controlFillHover` | 悬停格 |
| `textPrimary` | 本月日期、标题、箭头 |
| `textSecondary` | 星期表头、相邻月份日期、禁用日期（@ 0.5 alpha）与删除线 |

选中格的文字是硬编码白色，不读 `onAccent`。

### 继承扩展点

`Calendar` 是可继承的具体控件：`protected` 的 `RenderNavBar` / `RenderWeekdayHeader` / `RenderCell`
钩子、`CellInfo` 结构和只读几何访问器见 [inheritance-guide.md](inheritance-guide.md)。

### 演示页

demo 导航 → 输入控件 → Calendar（nav id 205）。

---

## 相关控件

- [DatePicker](DatePicker.md) — 按钮 + 弹出的 Calendar
- `LunarCalendar`（`extensions/CustomControls`） — 继承 Calendar 加农历的示例
