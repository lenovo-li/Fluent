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
Orientation GetOrientation() const;   // 默认 Orientation::Horizontal
```

垂直时长度由 `Height` 决定,**必须显式给高度** —— 纵向没有自然长度可言。

**脏标记等级:Measure**(desired 的长短边互换)。

### 事件

#### ValueChanged

```cpp
Event<Slider, float>& ValueChanged();
```

值**提交**时触发。**键盘方向键 / Home / End**、**`SetValue`** 在值确实改变时立即触发;
拖动则在**松手**时触发一次。

拖动过程中的**中间值不触发此事件**——那些是预览（见 `ValueChanging`）,最终提交只在松手时触发一次。

⚠️ **松手只在这次拖动确实改过值时才触发。** 按下点正好落在当前值上、随后不动就松手,
`ValueChanging` 和 `ValueChanged` 都不触发 —— 没有新值可提交。

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

#### ValueChanging

```cpp
Event<Slider, float>& ValueChanging();
```

**拖动过程中的每个中间值**都触发此事件。键盘和 `SetValue` 不触发（那些已经是提交值,直接走 `ValueChanged`）。

**用法**: 拖滑块时实时预览（音量、亮度）,松手时才提交到 `ValueChanged` 做最终落盘/通知。

```cpp
slider->ValueChanging().Subscribe(preview,
    [](void* owner, Slider&, float& v) {
        // 实时预览: 更新 UI 但不写配置
        static_cast<VolumeBar*>(owner)->UpdatePreview(v);
    });

slider->ValueChanged().Subscribe(settings,
    [](void* owner, Slider&, float& v) {
        // 最终提交: 写配置文件、触发应用级 PropertyChanged
        static_cast<Settings*>(owner)->SetVolume(v);
    });
```

如果每个中间值都写配置文件,一次拖动 100 像素 = 100 次文件写入。`ValueChanging` + `ValueChanged` 分离就是为此。

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

### 实际应用示例：标签 + 滑块 + 数值(三列 Grid)

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

- **Render**:每帧固定 4~5 个图元 —— 轨道槽、已填充部分(两个圆角矩形;值在最小端时
  不画填充)、旋钮填充圆、旋钮描边圆,聚焦时再加一个焦点环圆。无文本布局。
- **填充动画是 Render 级 O(1)**,在 UI 线程上逐帧缓动,不走 compositor,也不触发重新布局。
  每个 tick 都对自身调用 `Invalidate()`(只标 Render)。`AnimatedValue` 不会自己标脏:
  曾经漏掉这一步时,单独缓动的帧脏区为空,宿主退化为整窗重绘;和别的脏元素同帧时局部
  重绘又把 Slider 裁掉,键盘步进的缓动表现为卡住再跳。由
  `SliderEvents.EaseTickMarksRenderDirty` 锁定(删掉这行 `Invalidate()` 测试即失败)。
- **idle 零成本**:缓动收敛后 `WantsAnimationTick()` 返回 false,不再占用帧。
- **拖动时不缓动**:`OnPointerPressed` / `OnPointerMoved` 都走 `animFill_.SetImmediate(value_)`,
  让旋钮 1:1 跟随光标。缓动只用于键盘/程序改值 —— 拖动时的缓动会变成橡皮筋手感。
- **首帧不动画**:第一次 Arrange 时 `FrameworkElement` 调用 `SnapAnimationsToSettledState()`,
  把显示值直接落到当前值;在此之前 `WantsAnimationTick()` 恒为 false。否则 `SetValue(50)`
  建出来的滑块第一次显示会从最小值扫到 50。

**这两项优化的证明**(headless 单元测试):

| 优化 | 测试 | 回退修复后的结果 |
| --- | --- | --- |
| 拖动时不缓动 | `SliderEvents.DragSnapsDisplayedValueWithoutEasing` | 删掉 `OnPointerPressed` 或 `OnPointerMoved` 里的 `SetImmediate`,显示值停在拖动前的位置,断言失败 |
| 首帧不动画 | `InitialStateNoAnimation.SliderBuiltAtMidRangeDoesNotSweepUp`、`SliderWithNonZeroMinimumAlsoStartsSettled` | 删掉 `SnapAnimationsToSettledState` 覆写,第一帧仍请求动画 tick |
| 按下不动就松手不提交 | `SliderEvents.PressReleaseWithoutChangeFiresNothing` | 去掉 `OnPointerReleased` 里的 `dragChangedValue_` 判断,`ValueChanged` 多触发一次 |
| (对照)程序改值仍然缓动 | `InitialStateNoAnimation.SliderStillAnimatesOnProgrammaticChange` | 防止把「不缓动」误扩到所有改值路径 |

未验证:F12 HUD 下的帧成本与拖动跟手程度需要在真机上看,headless 测试不覆盖像素与流畅度。

### 使用注意事项

- **写死宽度是正当的**(它没有自然长度)。但更推荐 `SetMaxWidth`/`SetMinWidth` 夹逼,
  这样窗口尺寸变化时仍然自适应。
- **垂直方向必须给 Height。**
- **退化区间**:`max <= min` 时 `CoerceValue` 直接返回 `min`,不会除零。
- **溢出声明 14.2 DIP**:`VisualOverflowDip()` 返回 `8 × 1.15 + 3.5 + 1.5`(拖动时放大的
  旋钮半径 + 焦点环间距 + 描边),并且无条件上报,不只在聚焦时。轨道两端也内缩了同样的
  距离,所以旋钮和焦点环在横向上不会画出 `bounds_`。
- **按下即跳值**:在轨道任意位置按下,值立刻跳到该位置(触发 `ValueChanging`),然后才开始拖动。

### 键盘导航

方向键**不区分方向**:水平和垂直滑块都同时响应四个方向键。

| 按键 | 行为 |
| --- | --- |
| Tab | 进入焦点顺序 |
| → / ↑ | 增加一个 step |
| ← / ↓ | 减少一个 step |
| Home / End | 跳到 min / max |

### 主题 Token

| Token | 用途 |
| --- | --- |
| `accent` | 已填充轨道、旋钮描边（EffectiveBorderBrush 回退值）、焦点环（自绘，不走 DrawFocusRing） |
| `accentPressed` | 按下（拖动）时的已填充轨道 |

**说明**：
- 未填充轨道槽使用硬编码回退值（dark = rgba(255,255,255,0.18), light = rgba(0,0,0,0.12)），不是主题 token
- 旋钮描边通过 `EffectiveBorderBrush(pal.accent)` 获取，回退是 accent 不是 controlStrokeDefault
- 焦点环自己画（用 accent @ 0.6 alpha），不走 `FocusVisual.h::DrawFocusRing`，所以不读 focusStroke token

### 演示页

demo 导航 → 输入控件 → Slider。含范围与状态、Stretch vs 固定宽度、Min/Max 夹逼在三种
容器宽度下的表现、垂直方向、三列表单形状、五种 AccentColor、step 网格,
以及滑块驱动 ProgressBar 的联动。

---

## 相关控件

- `ProgressBar` — 同样继承 `RangeBase`,只读、无输入
- `NumericUpDown` — 需要精确输入数值时用它
- `LogSlider`(`extensions/CustomControls`) — 继承 Slider 的对数刻度示例;Slider 留给子类的
  protected 扩展点见 [inheritance-guide.md](inheritance-guide.md)
