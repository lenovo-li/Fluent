# Chart 控件

## 概述

折线 / 柱状 / K 线三种定量图表，带坐标轴与刻度。

**三种图共用一个控件**，因为难的、值得只写一次的部分是共享的：数据坐标到像素的
映射（scale）、可读的刻度取整、绘图区裁剪。每种图型独有的只是标记的画法。拆成三个
控件等于三份 scale 代码，以及其中某一份里的 scale bug。

**只画屏幕上的点**：10 万点的序列显示 200 点窗口，成本和 200 点序列相同。

**数据是借用的**，通过索引访问器拉取而不是拷入控件 —— 行情序列有几万个点，每次
更新都拷一份会在实时刷新时持续分配。

**典型使用场景**:
- K 线图（日/周/月线）
- 净值曲线、指数走势
- 财务指标的多期柱状对比
- 盈亏分布

**它不是绘图库。** 没有图例交互、缩放平移手势、标注、双坐标轴、堆叠或分组柱状图。
需要那些请在应用层用本控件组合。

---

## 继承层次

```
Visual
  └─ UIElement
      └─ FrameworkElement
          └─ Control
              └─ Chart
```

**命名空间**: `fluent`
**头文件**: `FluentUI/controls/Chart.h`

---

## 公开 API

### 嵌套类型

```cpp
enum class Kind { Line, Bar, Candlestick };

struct Series {
    std::wstring name;
    std::function<double(int index)> value;                  // Line / Bar
    std::function<double(int index)> open, high, low, close; // Candlestick
    bool hasColor = false;
    D2D1_COLOR_F color{};
};
```

OHLC 用四个独立访问器而不是一个结构体：行情数据的常见形状是列式的，用结构体会强迫
调用者重新打包。

---

### 属性

| 成员 | 说明 | 脏标记 |
| --- | --- | --- |
| `SetKind(Kind)` | 图型 | Measure |
| `SetPointCount(int)` | x 位置总数（所有序列共享） | Measure |
| `AddSeries(Series)` / `ClearSeries()` | 序列 | Measure |
| `SetAutoScaleY(bool)` | 自动 Y 范围，默认开 | Measure |
| `SetYRange(double, double)` | 固定 Y 范围（同时关闭自动） | Measure |
| `SetVisibleRange(int first, int count)` | 可见 x 窗口，`count=0` 表示全部 | Measure |
| `SetXLabelProvider(fn)` | x 轴标签（通常是日期） | Render |
| `SetShowGridLines(bool)` | 网格线 | Render |
| `SetShowYAxis(bool)` / `SetShowXAxis(bool)` | 坐标轴 | Measure |

`SetKind` 是 Measure 级而不是 Render 级：K 线要用 high/low 定范围，所以同一批数据
在自动缩放下的 Y 范围（以及标签槽宽度）与折线图不同。

坐标轴开关是 Measure 级，因为关掉会把预留的边距还给绘图区 —— sparkline 需要这个，
否则三分之一的宽度浪费在看不见的轴槽上。

**自动缩放跟随可见窗口**，不是整个序列。这才使滚动的图表按屏幕上的内容重新缩放，
而不是被远处的极值压平。

---

### 几何与映射（公开，因为这就是契约）

```cpp
RectDip PlotRect() const;                // 绘图区（bounds 减去轴槽）
float   XToPixel(int index) const;
float   YToPixel(double value) const;
int     PixelToIndex(float dipX) const;  // 反向，用于命中测试
std::vector<double> YTicks() const;      // 取整后的刻度值
```

必须公开的理由：用户会注意到的图表错误几乎全是 scale 错误 —— Y 轴反了、序列跑出
绘图区、柱状图从错误的基线量起、K 线实体画反。这些都是算术，可以精确测试且不需要
设备，而**看截图抓不到** —— 一个看起来合理的错误图表看起来就是个图表。

`XToPixel` 把点放在**槽中心**而不是槽边缘：柱和蜡烛是围绕 x 绘制的，把索引 0 顶在
绘图区左边缘会让它的标记被裁掉一半。

`PixelToIndex` 在绘图区外返回 **-1**（「这里没有点」）而不是钳到边缘索引 —— 显示
tooltip 的调用者必须能区分「没悬停在数据上」。绘图区宽度为 0 时同样返回 -1。

`YTicks` 把原始步长向上取整到 1/2/5 × 10ⁿ 的「好数字」。没有这一步，[0, 3.7] 的
轴会显示 0.74、1.48 这类标签 —— 技术上正确，实际不可读。

---

## 代码示例

### 最小示例（折线）

```cpp
#include "FluentUI/controls/Chart.h"

auto* chart = panel->Add(std::make_unique<Chart>());
chart->SetHeight(200.0f);         // 图表没有自然高度，必须由调用者定
chart->SetKind(Chart::Kind::Line);
chart->SetPointCount(static_cast<int>(MyData().size()));

Chart::Series s;
s.name = L"收盘价";
s.value = [](int i) { return MyData()[i].close; };
chart->AddSeries(std::move(s));
```

### K 线 + 移动均线

```cpp
chart->SetKind(Chart::Kind::Candlestick);
chart->SetPointCount(500);
chart->SetVisibleRange(420, 80);      // 显示最后 80 根；滚动/缩放就是改这个

Chart::Series ohlc;
ohlc.open  = [](int i) { return Bars()[i].open;  };
ohlc.high  = [](int i) { return Bars()[i].high;  };
ohlc.low   = [](int i) { return Bars()[i].low;   };
ohlc.close = [](int i) { return Bars()[i].close; };
chart->AddSeries(std::move(ohlc));

Chart::Series ma;                     // 第二个序列，显式配色以便区分
ma.hasColor = true;
ma.color = D2D1::ColorF(0xE3A008);
ma.value = [](int i) { return Ma20(i); };
chart->AddSeries(std::move(ma));

chart->SetXLabelProvider([](int i) { return Dates()[i]; });
```

---

## 备注

### 性能特征

- **每帧成本 O(可见点 × 序列数)**，与 `PointCount()` 无关。
- **无 GPU 专用路径**：全部走 `DrawingContext` 的线/矩形绘制，所以离屏像素测试能
  像验证焦点环一样验证图表。
- **折线逐段 DrawLine** 而不是构建 geometry：窗口滚动时 geometry 需要重建，而
  DrawLine 本身已经是一个 op。
- **不请求动画帧**。

### 各图型的规则

- **Bar**：自动缩放**必定包含 0**，否则柱长没有意义（98~100 缩放到 [98,100] 会
  三根柱子一样高）。未指定颜色时负值用 `dataNegative` —— 盈亏图用单一色系不可读。
- **Candlestick**：自动缩放用 **high/low** 而不是 open/close，否则每根影线都被裁。
  十字星（open == close）实体高度为 0 会消失，强制给 1 DIP 以便价格位置仍可见。
  先画影线再画实体，实体覆盖中段。
- **Line**：不做任何范围特殊处理。

**零基线**只在 0 确实落在范围内时才绘制 —— 否则它会被钉在边缘，被误读成一条不存在
的坐标轴。

### 使用注意事项

- **必须写显式高度**（和 `DataGrid` 同理）：图表可以画成任意高，没有自然高度。
- **退化范围会被加宽**而不是除零：`SetYRange(42, 42)` 会变成 [42, 43]。常量序列
  会走到这条路。
- **窗口会被钳制**到数据范围内：滚过末尾或在陈旧窗口下缩小数据，都不会让访问器被
  越界索引调用 —— 那会读到应用自己数组的界外。
- x 轴标签有**上限**（约每 70 DIP 一个），全部绘制会重叠成一团并产生 O(可见) 次
  文本布局。

### 键盘导航

不可聚焦，无手势。缩放/滚动由调用者通过 `SetVisibleRange` 驱动（demo 页用两个
Slider 演示）。

### 主题 Token

| Token | 用途 |
| --- | --- |
| `accent` | 折线默认色 |
| `dataPositive` / `dataNegative` | 涨/跌蜡烛、正/负柱 |
| `gridLine` | 网格线 |
| `controlStrokeDefault` | 零基线 |
| `textSecondary` | 轴标签 |
| `typography.captionSize` | 轴标签字号 |

涨跌方向是**地区规范**：中日韩红涨绿跌，欧美相反。改主题即可，不用改控件。

### 尚未实现

以下都是加 `Kind` 的扩展，不是架构障碍：

- **Radar（雷达图）** —— 五维评分现在只能降级成柱状图
- **Heatmap（热力图）** —— 敏感性分析矩阵
- **Scatter（散点图）** —— PB-ROE 四象限
- **Waterfall（瀑布图）** —— DCF 价值构成

另外未实现：图例、tooltip（`PixelToIndex` 已经提供了所需的命中测试）、
缩放平移手势、双坐标轴、堆叠/分组柱状图。

### 演示页

demo 导航 → 数据控件 → Chart（nav id 405）。含三种 Kind 对照、可见窗口实况
（两个滑块滚动/缩放并打印实时范围与自动 Y 范围）、坐标轴开关、多序列，
以及与 Metric/InfoBar 组合的净值面板。
