# 用 FluentUI 复刻 `D:\project\Game` 界面 —— 可行性与缺口

结论先说：**能做，但缺四个控件，已补齐并落地为一个可运行的复刻页面。**

复刻页面在 `FluentUIDemo/pages/StockAnalyzerPage.cpp`，demo 里导航到
「📈 真实应用复刻 → A股分析工具」（nav id 901）。

## 先纠正一个前提

`D:\project\Game` 不是游戏项目。它是一个 **A 股数据分析工具**：Python +
Streamlit + akshare + Plotly，六个功能页（个股分析 / 选股筛选 / 策略回测 /
基本面 / 内在价值 / 市场概览），打包成 exe 给没装 Python 的机器用。目录名
是历史遗留。

这个差别决定了工作量：一个游戏 UI 大概需要自绘和动画，而一个数据分析
应用需要的是**表格、图表、指标卡**——恰好是本框架此前完全没有的三类。

## 控件映射：28 个 Streamlit 调用点

统计了 `pages_impl/` 与 `ui/` 下所有 `st.*` 调用（按出现次数排序），
逐个对到本框架：

| Streamlit | 用量 | FluentUI | 状态 |
| --- | --- | --- | --- |
| `st.caption` | 42 | `TextBlock` + `SetDimmed` | 已有 |
| `st.columns` | 36 | `Grid` + Star 列 | 已有（见下方坑） |
| `st.warning` | 30 | `InfoBar` | **新增** |
| `st.info` | 29 | `InfoBar` | **新增** |
| `st.subheader` | 24 | `TextBlock` + 字号 | 已有 |
| `st.plotly_chart` | 17 | `Chart` | **新增（部分）** |
| `st.markdown` | 17 | `TextBlock` | 已有（降级为纯文本） |
| `st.dataframe` | 16 | `DataGrid` | **新增** |
| `st.selectbox` | 12 | `ComboBox` | 已有 |
| `st.expander` | 11 | `Expander` | 已有 |
| `st.number_input` | 9 | `NumericUpDown` | 已有 |
| `st.divider` | 9 | `Separator` | 已有 |
| `st.checkbox` | 9 | `CheckBox` | 已有 |
| `st.error` | 8 | `InfoBar` | **新增** |
| `st.success` | 7 | `InfoBar` | **新增** |
| `st.slider` | 7 | `Slider` | 已有 |
| `st.metric` | 6 | `Metric` | **新增** |
| `st.header` | 6 | `TextBlock` | 已有 |
| `st.download_button` | 4 | — | **缺失**（无文件对话框） |
| `st.tabs` | 3 | `TabControl` | 已有 |
| `st.spinner` | 3 | `ProgressBar`（不确定态） | 已有 |
| `st.radio` | 3 | `RadioButton` | 已有 |
| `st.multiselect` | 3 | — | **缺失** |
| `st.date_input` | 2 | `DatePicker` | 已有 |
| `st.button` | 2 | `Button` | 已有 |
| `st.title` | 1 | `TextBlock` | 已有 |
| `st.text_input` | 1 | `TextBox` | 已有 |
| `st.progress` | 1 | `ProgressBar` | 已有 |

**20 / 28 直接命中现有控件。** 缺口集中在数据可视化，这是意料之中的：
框架此前的定位是通用桌面控件，没碰过定量展示。

## 新增的四个控件

### `InfoBar` —— 内联状态消息
`st.info/success/warning/error` 共 **74 次调用**，是缺口里用量最大的一项。

四个严重级别，各自一对 fill/stroke token。做成控件而不是让每个页面拼
`Border` + `TextBlock` 的理由是**颜色**：硬编码 `0xFFF4CE` 当警告底色在
深色模式下不可见。严重级别→颜色的映射必须在主题里，只有一份。

高度随内容走：长消息在窄列里换行变高，所以放进 `ScrollPanel` 页面不用
调用者猜高度。

**刻意不做**：动作按钮（WinUI 有，但要引入命中测试和焦点顺序，把 Button
放旁边即可）、开关动画（错误消息瞬间出现才对，且高度动画是本框架最贵
的形状）、图标字体依赖（严重级标记用几何图形画，无字体也能跑，headless
测试也依赖这点）。

### `Metric` —— 带涨跌方向的指标卡
`st.metric`。三行：caption、大号数值、带符号的变化量。

**核心是「符号」与「颜色」是两件事**：符号来自数值，颜色来自另一个问题
——「涨是好事吗？」。股价上涨是收益，但错误率、回撤、负债率上涨是坏事。
没有任何对数值的检查能判断这一点，所以有 `SetInverted()`。复刻页面的
「资产负债率」用的就是它。

值传字符串不传 double：格式化（货币、千分位、百分比、有效位、locale）
是应用已经要统一决定的事，控件自己格式化会和应用的表格打架。

### `DataGrid` —— 虚拟化表格
`st.dataframe`。这是最大的一块。

**双向虚拟化是重点**，行和列都要：财务报表宽表有 80+ 期列，只做行虚拟化
的话在小测试表上看不出问题，真实数据下光绘制屏幕外的列就能吃掉整帧。
实测 41 列 × 60 行放进 820×220 的 tab，只有 10 列 × 7 行（70 格）是活的。

数据是**拉取式**：调用者给 `(row, col) -> wstring`，百万行表「加载」成本
为零，排序和格式化留在应用侧。

滚动是内部的（自己持有一个 `ScrollViewer`），因为表头必须钉住而表体滚动，
外层滚动容器会把表头滚出视野。

**刻意不做**：编辑（需要一整套焦点/提交/校验模型，是另一个控件）、排序
（只上报表头点击，应用自己排）、单元格里放控件（会毁掉虚拟化）、单元格
换行（行高会变成数据相关，破坏 O(1) 行索引）。

### `Chart` —— 折线 / 柱状 / K 线
`st.plotly_chart` 的一部分。三种图共用一个控件，因为难的部分是共享的：
数据→像素的映射、坐标轴刻度取整、裁剪。分成三个控件就是三份 scale 代码，
以及其中某一份的 scale bug。

只画屏幕上的点：10 万点的序列显示 200 点窗口，成本和 200 点序列相同。

**不是绘图库**：没有图例交互、缩放平移、标注、双轴、堆叠柱。

## 为此新增的主题 token

| token | 用途 | 为什么必须是 token |
| --- | --- | --- |
| `severity{Info,Success,Warning,Error}{Fill,Stroke}` | InfoBar | 硬编码颜色在另一个模式下不可见 |
| `dataPositive` / `dataNegative` | 涨跌色 | **地区习惯不同**：中日韩红涨绿跌，欧美相反 |
| `dataNeutral` | 平盘 | 同上 |
| `gridLine` | 图表网格线、表格分隔线 | 需要比 `controlStrokeDefault` 更弱 |
| `rowFillAlternate` | 斑马纹 | 同上 |

`dataPositive/Negative` 开放为 `ColorOverrides`，因为涨跌配色不是审美
偏好而是地区规范，应用必须能改而不用碰控件或维护调色板分支。

`severityInfo` **刻意不复用 `accent`**：accent 是用户可personalise的颜色
（可以被设成红色），复用会让信息提示在某些机器上和错误提示无法区分。

## 复刻页面覆盖了什么

`StockAnalyzerPage.cpp` 用到：`Chart`（K线/折线/柱）、`DataGrid`（480 行
选股结果 + 41 列财务宽表）、`Metric`（KPI 条）、`InfoBar`（三种严重级）、
`TabControl`（基本面五个 tab）、`ComboBox`、`Slider`、`NumericUpDown`、
`CheckBox`、`ProgressBar`、`Expander`（动画展开）、`Grid`/`Border`。

**页面数据全部是进程内合成的**（固定种子的 LCG 随机漫步）。原项目通过
akshare 联网取数，本框架没有 HTTP 客户端。页面顶部有 InfoBar 明确写了这
一点——复刻一个金融界面时，把免责声明一起复刻是应该的。

## 仍然做不到的

1. **联网取数**：无 HTTP 客户端。这是复刻与移植的根本差别。
2. **雷达图**：五维评分改用柱状图。
3. **热力图**：DCF 敏感性分析（折现率 × 永续增长率）无法表达。
4. **散点图**：PB-ROE 四象限图无法表达。
5. **瀑布图**：DCF 价值构成图无法表达。
6. **`st.multiselect`**：没有多选控件，示例用 ComboBox 单选替代。
7. **Markdown 渲染**：`TextBlock` 是纯文本，富文本降级。
8. **CSV 导出**：没有文件保存对话框。

前五项都是 `Chart` 的扩展（Radar / Heatmap / Scatter / Waterfall 四种
新 Kind），是明确的后续工作而非架构障碍。

## 过程中发现的既有框架问题

**`UniformGrid` 不是 `st.columns`。** 它把每个 cell 的尺寸定为**最大子元素
的 desired 尺寸**（先无约束测量子元素，取 max 作为 `cellW_`），且从不把
cell 撑开填满 arrange 宽度。所以四个 Metric 放进 800 DIP 的 `UniformGrid`
每个只得到约 63 DIP（文字宽度），右边 550 DIP 空着。

这对它自己声明的用途（日历那种等大、内容定尺寸的格子）是合理的，但**不是**
`st.columns` 的语义。`st.columns` 是把可用宽度均分——那是 `Grid` + Star 轨道。
测试与复刻页面都已改用 `Grid`，并把这个区别写进注释。未改 `UniformGrid`
本身：它的行为有其道理，改会影响既有使用者。

**`Metric` 自身的 bug（本轮引入并修掉）**：`Measure` 原本上报文字宽度，
在 stretch 容器里会少报。改为「有界测量填满 offer，无界测量上报内容宽度」
——`UniformGrid` 正是用无穷宽度测量子元素，直接取 offered 会上报 `inf`。

## 验证

- Debug + Release x64 双配置干净编译
- 全套 **1538 / 1538 通过**
- `DataGridTests.cpp`（13 条）：虚拟化契约、双向滚动、表头钉住、选择钳制
- `ChartTests.cpp`（13 条）：scale 锚点、刻度取整、自动缩放跟随窗口、窗口钳制
- `InfoBarMetricTests.cpp`（14 条）：严重级别去重、深浅色差异、trend 反转、
  DirtyFlags 分级
- `StockAnalyzerShapeTests.cpp`（6 条）：复刻页面用到的组合形状真实布局

变异测试：把 `DataGrid::VisibleRowRange` 改成「全部可见」→ 4 条失败，
数字直接暴露问题（10 万行全部可见、40 格变 40 万格）。

**未验证**：页面在真机上「好不好看」。headless 能证明几何正确、虚拟化生效、
不崩不 NaN，但配色、间距、字号是否协调只能人看。这需要你在 demo 里
导航到 nav id 901 亲自判断。
