# ProgressBar 控件

## 概述

一条 4 DIP 高的进度条,两种模式:

- **确定态(determinate)**:填充长度表示进度。
- **不确定态(indeterminate)**:一段色块来回扫动,表示「在忙,但不知道还要多久」。

不确定态的扫动**跑在 DirectComposition 合成器线程上**,所以 UI 线程再忙它也照样动 ——
这正是「忙」指示器最需要的性质:如果 UI 线程卡住时进度条也跟着停,它就失去了意义。

**典型使用场景**:
- 文件传输、下载、导入(确定态)
- 等待网络响应、加载中(不确定态)
- 任务队列的完成比例

---

## 继承层次

```
Visual
  └─ UIElement
      └─ FrameworkElement
          └─ Control
              └─ RangeBase
                  └─ ProgressBar
```

和 `Slider` 是同门兄弟(共用 `RangeBase` 的 min/max/value 三元组),区别是它不接受输入、
不可聚焦。

**命名空间**: `fluent`
**头文件**: `FluentUI/controls/ProgressBar.h`

---

## 公开 API

### 构造函数

```cpp
ProgressBar();
```

默认确定态、值 0、不可聚焦、`HAlign::Stretch`、高度 4 DIP。

### 属性

#### Value

```cpp
void  SetValue(float v);      // 同时切回确定态
float Value() const;
void  SetMinimum(float v);  void SetMaximum(float v);
```

⚠️ **`SetValue` 会顺带把控件切回确定态。** 这是刻意的:给出一个具体进度的调用者显然不再
想要「不知道还要多久」的扫动,让他记得先关掉不确定态是多余的负担。

**值范围说明:**

`ProgressBar` 继承自 `RangeBase`，默认值范围是 **0.0 - 1.0** (最小值 0.0，最大值 1.0)。
这是一个归一化的比例范围，符合以下设计约定:

- **0.0** = 0% (未开始)
- **0.5** = 50% (进行到一半)
- **1.0** = 100% (完成)

这个设计选择参考了:
- iOS `UIProgressView` 的 `progress` 属性 (0.0-1.0)
- macOS `NSProgressIndicator` 的分数模式
- 数学上的归一化范围 (0-1 表示比例更自然)

**如果你习惯用百分比 (0-100):**

方式一：调用时除以 100
```cpp
bar->SetValue(progress / 100.0f);  // 68% → 0.68
```

方式二：修改值范围
```cpp
bar->SetMinimum(0.0f);
bar->SetMaximum(100.0f);
bar->SetValue(68.0f);  // 现在可以直接用百分数
```

方式三：封装辅助函数
```cpp
void SetProgressPercent(ProgressBar* bar, float percent) {
    bar->SetValue(percent / 100.0f);
}
```

**注意:** 无论用哪种方式，传给 `SetValue` 的值都会被限制在 `[Minimum, Maximum]` 范围内。
超出范围的值不会报错，而是被自动截断。

**脏标记等级:Render**。

#### Indeterminate

```cpp
void SetIndeterminate(bool on);
bool IsIndeterminate() const;
```

切换忙指示模式。切换时扫动相位归零(`sweep_.SetImmediate(0)`),这样每次进入忙状态都从
同一处开始,不会出现「接着上次的位置继续」的突兀感。

值未变时是静默的。

---

## 代码示例

### 确定态

```cpp
#include "FluentUI/controls/ProgressBar.h"

auto* bar = panel->Add(std::make_unique<ProgressBar>());
bar->SetMinimum(0.0f);
bar->SetMaximum(1.0f);
bar->SetValue(0.68f);
bar->SetWidth(420.0f);            // 没有自然长度，宽度由布局决定
bar->SetHAlign(HAlign::Left);
```

### 不确定态 → 确定态

```cpp
bar->SetIndeterminate(true);      // 开始等待，扫动跑在合成器线程上

// ...拿到总量之后：
bar->SetValue(0.0f);              // SetValue 自动切回确定态，不必显式关掉
```

### 被别的控件驱动

```cpp
// 一个值源可以有多个消费者，且都在同一帧内更新
sub_ = slider->ValueChanged().Subscribe(this,
    [](void* o, Slider&, float& v) {
        static_cast<MyPage*>(o)->bar_->SetValue(v / 100.0f);
    });
```

---

## 备注

### 性能特征

- **Render**:两个圆角矩形(轨道槽 + 已填充)。无文本布局。
- **确定态的填充变化是 Render 级 O(1)**,不触发重新布局。
- **不确定态在有合成器时不消耗 UI 线程 tick**:`WantsAnimationTick()` 在
  `indeterminate_` 分支返回 `!compositorActive_`,也就是只有回落路径(没有合成设备)才要
  每帧 tick。
- **首帧不动画**:确定态的填充在第一次 Arrange 时直接落位。此前 `SetValue(0.68f)` 建出来
  的进度条第一次显示会从 0 长到 68%。
- 不确定态**不适用**首帧落位 —— 它是连续动画,没有静止状态可落。

### 使用注意事项

- **必须给宽度**(或放进定宽容器):进度条没有自然长度,和 `Slider`、`Chart`、
  `DataGrid` 同类。这类控件写死尺寸是正当用法。
- **高度默认 4 DIP**,显式设 `Height` 可以改。
- 不可聚焦,不参与 Tab 顺序 —— 它是纯展示。
- `SetValue` 会切回确定态,见上。

### 主题 Token

| Token | 用途 |
| --- | --- |
| `accent` | 已填充部分(未设 AccentColor 时) |

**说明**：未填充轨道槽使用硬编码回退值（`ProgressBar.cpp:64`：dark = rgba(255,255,255,0.16), 
light = rgba(0,0,0,0.10)），不是 `controlFillDefault` token。

### 尚未实现

- **分段 / 缓冲进度**(像视频播放器那种「已缓冲」浅色段):需要第二个值与第三种填充。
- **文字标签**:进度百分比请自己放一个 `TextBlock`,这样格式化由应用决定。
- **错误/暂停配色**:用 `SetAccentColor` 换成 `severityError*` 的颜色即可,控件本身不带
  状态语义。

### 演示页

demo 导航 → 数据控件 → ProgressBar。另见 Slider 页的「混合使用」卡片,演示滑块驱动进度条。
