# NumericUpDown 控件

## 概述

带上下箭头的数值输入框:可以直接键入,也可以点箭头或按方向键按步长调整。

**和 `Slider` 的区别是精度 vs 概览**:Slider 适合「大概调一下」并能一眼看出当前值在区间里
的位置;NumericUpDown 适合需要**精确值**、且区间可能很大或无直观比例的场合(份数、
超时秒数、价格)。

**典型使用场景**:
- 数量、份数、页码
- 超时/间隔参数
- 需要键入精确数字的表单字段

---

## 继承层次

```
Visual
  └─ UIElement
      └─ FrameworkElement
          └─ Control
              └─ RangeBase
                  └─ NumericUpDown
```

和 `Slider`、`ProgressBar` 共用 `RangeBase` 的 min/max/value 三元组。

**命名空间**: `fluent`
**头文件**: `FluentUI/controls/NumericUpDown.h`

---

## 公开 API

### 属性

#### Min / Max / Value(继承自 `RangeBase`)

```cpp
void  SetMinimum(float v);   void SetMin(float v);   // SetMin 是别名
void  SetMaximum(float v);   void SetMax(float v);   // SetMax 是别名
void  SetValue(float v);
float Value() const;
```

`SetMin`/`SetMax` 是 WPF 风格的同义别名,和 `Slider` 保持一致。

#### Step

```cpp
void  SetStep(float v);
float Step() const;
```

一次箭头点击或一次方向键的增量。**PageUp/PageDown 是 10 倍**。

⚠️ **步长网格是从 Min 量起的,不是从 0。** `CoerceValue` 做的是
`round((v - min) / step) * step + min`。所以 `min = 1, step = 5` 时,合法值是
1 / 6 / 11 / 16…… 而不是 0 / 5 / 10。这个细节在「起始值不是 0 的区间」上很容易造成意外。

⚠️ 和 `Slider` 一样,`SetStep` 把参数夹到 **≥ 0.0001**,没有「关闭吸附」这一档。

Step 定义在这里而不是 `RangeBase` 上,理由和 Slider 相同:`ProgressBar` 没有「一步」的概念。

#### DecimalPlaces

```cpp
void SetDecimalPlaces(int places);   // 默认 0（整数）
int  DecimalPlaces() const;
```

只影响**显示格式**,不影响存储的 float,也不改变 step 网格。设成 2 而 step 是 1.0 时,
显示会是 `3.00`、`4.00`。

### 事件

```cpp
Event<NumericUpDown, float>& ValueChanged();
```

箭头点击、键盘步进、**文本提交**三种途径都会触发。

`Args` 是**引用**:

```cpp
sub_ = spin->ValueChanged().Subscribe(this,
    [](void* owner, NumericUpDown&, float& v) {      // 注意 float&
        static_cast<MyPage*>(owner)->SetCopies(static_cast<int>(v));
    });
```

⚠️ 返回的 `Subscription` 必须持有(RAII)。

---

## 代码示例

### 整数份数

```cpp
#include "FluentUI/controls/NumericUpDown.h"

auto* spin = panel->Add(std::make_unique<NumericUpDown>());
spin->SetMin(1.0f);
spin->SetMax(99.0f);
spin->SetValue(1.0f);
spin->SetStep(1.0f);
spin->SetDecimalPlaces(0);
```

### 两位小数的比例

```cpp
spin->SetMin(0.0f);
spin->SetMax(1.0f);
spin->SetStep(0.05f);
spin->SetDecimalPlaces(2);      // 显示 0.35，而非 0.349999
```

### 表单一行

```cpp
auto* form = card->Add(std::make_unique<Grid>());
form->AddRow(GridLength::Auto());              // Auto：不要写死行高
form->AddColumn(GridLength::Pixels(96.0f));    // 标签
form->AddColumn(GridLength::Star(1.0f));       // 输入框
```

---

## UI Automation

NumericUpDown 的 peer：

| 项 | 值 |
|---|---|
| ControlType | Spinner |
| 模式 | RangeValue，SmallChange = `Step()` |
| AutomationId | `SetAutomationId` |
| HelpText | `Tooltip()` |

惰性创建：只有 UIA 客户端访问到这个控件时才创建 peer，没有客户端时零开销。模式操作经投递消息执行，执行前重新检查控件仍在树上且已启用。属性变化只在真正变化且有客户端监听时发送。自定义方式见 inheritance-guide.md 的“UI Automation”一节。

验证：无头测试 `tests/unit/AutomationControlPeerTests.cpp` / `AutomationSelectorPeerTests.cpp`，真实 UIA 客户端自测 `FluentUITests.exe --uia-smoke`；第 2 阶段 14 处变异全部被测试抓到。

## 备注

### 性能特征

- **Render**:输入区圆角矩形 + 两个箭头字形 + 一次 DrawText。
- 文本布局走和 `TextBox` 同一套缓存。
- **不请求动画帧**。

### 使用注意事项

- **箭头按钮固定 20 DIP 宽**(`kButtonWidth`),从控件右侧扣除。所以控件太窄时先被挤掉的是
  文字区,而不是按钮。
- **写死宽度要留够**:实测宽度低于约 70 DIP 时数字会与箭头重叠 —— 框架的 Arrange 契约是
  压缩而非溢出,但这个控件内部是按「总宽减去按钮宽」分配文字区的,过窄就没有余量了。
  demo 里有一个刻意做窄的对照。
- **焦点环外扩 3.75 DIP**(`VisualOverflowDip()` 固定返回这个值),父容器裁剪过紧会切掉它。
- **step 网格从 Min 量起**,见上。

### 键盘导航

| 按键 | 行为 |
| --- | --- |
| Tab | 进入焦点顺序 |
| ↑ / ↓ | ± 一个 step |
| PageUp / PageDown | ± 10 个 step |
| 数字键 / Backspace | 直接编辑文本 |
| Enter / 失焦 | 提交文本(触发 `ValueChanged`) |

### 主题 Token

| Token | 用途 |
| --- | --- |
| `controlFillDefault` | 输入区底色 |
| `controlFillHover` / `controlFillPressed` | 箭头按钮填充（悬停 / 按下） |
| `controlStrokeDefault` | 边框 |
| `accent` | 聚焦边框 |
| `focusStroke` | 焦点环（DrawFocusRing） |
| `textPrimary` | 数字与箭头（正常态） |
| `textSecondary` | 禁用态箭头与文字 |

### 演示页

demo 导航 → 输入控件 → NumericUpDown。含状态、Stretch vs 固定宽度、Min/Max 夹逼、
step 网格从 Min 量起的演示、小数位数,以及刻意做窄导致重叠的对照。
