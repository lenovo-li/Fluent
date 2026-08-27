# Slider 控件

## 概述

在一个连续区间里拖动取值的滑块:一条轨道、已填充部分用强调色、一个可拖动的圆形旋钮。
支持水平与垂直两种方向。

**它没有内容驱动的长度。** 轨道要多长完全由布局决定 —— 这一点和 `Metric`、`TextBlock`
之类「有自然尺寸」的控件相反,所以给 Slider 写死宽度(或高度)是**正当用法**,不是坏味道。

**典型使用场景**:
- 音量、亮度、透明度
- 缩放比例、播放进度
- 任何「大概调一下」比「精确输入」更合适的数值(需要精确值请用 `NumericUpDown`)

---

## 继承层次

```
Visual
  └─ UIElement
      └─ FrameworkElement
          └─ Control
              └─ RangeBase
                  └─ Slider
```

`RangeBase` 持有 min/max/value 三元组、值的钳制与 `OnValueChanged` 钩子;`Slider` 只加
轨道绘制、拖拽输入与填充动画。`ProgressBar` 是它的同门兄弟(同样继承 `RangeBase`)。

**命名空间**: `fluent`
**头文件**: `FluentUI/controls/Slider.h`

---

## 公开 API

### 构造函数

```cpp
Slider();
```

默认水平、可聚焦、`HAlign::Stretch`、step 为 1.0。

### 嵌套类型

```cpp
enum class Orientation { Horizontal, Vertical };
```

### 属性

#### Minimum / Maximum / Value

```cpp
void  SetMinimum(float v);   void SetMin(float v);   // SetMin 是别名
void  SetMaximum(float v);   void SetMax(float v);   // SetMax 是别名
float Minimum() const;  float Maximum() const;  float Value() const;
void  SetValue(float v);
```

`SetMin`/`SetMax` 是 `SetMinimum`/`SetMaximum` 的同义别名,两组名字可以混用。

设值会先钳到 `[min, max]`,再按 step 网格取整(见下)。

**脏标记等级:Render**(旋钮位置与填充长度变了,尺寸没变)。

#### Step

```cpp
void  SetStep(float v);
float Step() const;
```

取值网格。默认 1.0。

⚠️ **没有「连续」档。** `SetStep` 会把参数夹到 **≥ 0.0001**,而 `CoerceValue` 始终执行
`round((v - min) / step) * step + min`。所以传 0 得到的是 0.0001 的细网格(视觉上等同
连续),而**不是**关闭吸附。要真正连续就把 step 设得远小于一个像素对应的值增量。

**脏标记等级:无**(只影响下一次取值,不直接改变当前绘制)。

#### Orientation

```cpp
void SetOrientation(Orientation o);
```

垂直时长度由 `Height` 决定,**必须显式给高度** —— 纵向没有自然长度可言。

**脏标记等级:Measure**(desired 的长短边互换)。

### 事件

#### ValueChanged

```cpp
Event<Slider, float>& ValueChanged();
```

值改变时触发,拖动、键盘、`SetValue` 都会触发。

处理器是**无捕获函数指针**,且 `Args` 是**引用**:

```cpp
// Event.h 刻意不支持捕获 lambda（那会要求堆分配闭包），所以显示用的指针
// 只能通过 owner 传进去
sub_ = slider->ValueChanged().Subscribe(label,
    [](void* owner, Slider&, float& v) {        // 注意 float&
        wchar_t buf[32];
        swprintf_s(buf, L"%.0f", v);
        static_cast<TextBlock*>(owner)->SetText(buf);
    });
```

⚠️ **返回的 `Subscription` 必须持有**(RAII,丢掉即退订)。

---

## 代码示例

### 最小示例

```cpp
#include "FluentUI/controls/Slider.h"

auto* slider = panel->Add(std::make_unique<Slider>());
slider->SetMinimum(0.0f);
slider->SetMaximum(100.0f);
slider->SetValue(50.0f);
```

### 标签 + 滑块 + 数值(三列 Grid)

```cpp
// 数值列用固定宽度并右对齐，否则数字位数变化时滑块会左右跳动
auto* g = card->Add(std::make_unique<Grid>());
g->AddColumn(GridLength::Pixels(80.0f));    // 标签
g->AddColumn(GridLength::Star(1.0f));       // 滑块吃掉剩余宽度
g->AddColumn(GridLength::Pixels(64.0f));    // 数值
g->AddRow(GridLength::Auto());
```

### 只夹逼而不写死宽度

```cpp
// MaxWidth 让滑块在宽屏上不会拉成一条难以精确操作的长线；
// MinWidth 保证窄容器里仍然可用。Min 优先于 Max（和 WPF 一致）。
slider->SetMaxWidth(320.0f);
slider->SetMinWidth(180.0f);
slider->SetHAlign(HAlign::Left);
```

---

## 备注

### 性能特征

- **Render**:两个圆角矩形(轨道槽 + 已填充)+ 一个圆(旋钮),无文本布局。
- **填充动画是 Render 级 O(1)**,不会触发重新布局。
- **拖动时不缓动**:`OnPointerMoved` 走 `animFill_.SetImmediate(value_)`,让旋钮 1:1
  跟随光标。缓动只用于键盘/程序改值 —— 拖动时的缓动会变成橡皮筋手感。
- **首帧不动画**:`SnapAnimationsToSettledState()` 在第一次 Arrange 时落位。此前
  `SetValue(50)` 建出来的滑块第一次显示会从最小值扫到 50。

### 使用注意事项

- **写死宽度是正当的**(它没有自然长度)。但更推荐 `SetMaxWidth`/`SetMinWidth` 夹逼,
  这样窗口尺寸变化时仍然自适应。
- **垂直方向必须给 Height。**
- **退化区间**:`max <= min` 时 `CoerceValue` 直接返回 `min`,不会除零。
- 焦点环画在 `bounds_` 之外,`VisualOverflowDip()` 上报该外扩量。

### 键盘导航

| 按键 | 行为 |
| --- | --- |
| Tab | 进入焦点顺序 |
| ← / → (水平)、↑ / ↓ (垂直) | 按 step 调整 |
| Home / End | 跳到 min / max |

### 主题 Token

| Token | 用途 |
| --- | --- |
| `accent` | 已填充轨道与旋钮描边(未设 AccentColor 时) |
| `controlFillDefault` | 未填充轨道槽 |
| `controlStrokeDefault` | 旋钮描边 |
| `focusStroke` | 焦点环 |

### 演示页

demo 导航 → 输入控件 → Slider。含范围与状态、Stretch vs 固定宽度、Min/Max 夹逼在三种
容器宽度下的表现、垂直方向、三列表单形状、五种 AccentColor、step 网格,
以及滑块驱动 ProgressBar 的联动。
