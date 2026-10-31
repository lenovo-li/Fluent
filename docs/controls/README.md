# 控件 API 参考

本目录包含 FluentUI 所有控件的详细 API 文档。

> **文档覆盖状态（诚实说明）**：本目录目前有 **34 份控件文档**（另有 `TEMPLATE.md` 作模板）。
> 下面清单里标注「文档待写」的条目**没有对应文件** —— 它们此前是失效链接，点了会 404，
> 现在改成纯文本以免误导。控件本身的权威说明始终在各自的头文件注释里。
>
> 已有文档中的代码示例都对照真实头文件核对过；命名空间全库只有 `namespace fluent`。
> 扩展控件（继承现有控件或从 `Control` 自建）见 [inheritance-guide.md](inheritance-guide.md)。


## 按类别浏览

### 基础输入控件

- [Button](Button.md) — 标准按钮
- RepeatButton（文档待写） — 按住时重复触发的按钮
- [CheckBox](CheckBox.md) — 复选框
- [RadioButton](RadioButton.md) — 单选按钮
- [ToggleSwitch](ToggleSwitch.md) — 开关
- [Slider](Slider.md) — 滑动条
- [TextBox](TextBox.md) — 单行文本输入
- [PasswordBox](PasswordBox.md) — 密码输入
- [NumericUpDown](NumericUpDown.md) — 数字微调器
- [Rating](Rating.md) — 星级评分

### 选择控件

- [ComboBox](ComboBox.md) — 组合框（下拉选择）
- [ListBox](ListBox.md) — 列表框（虚拟化支持）
- [TreeView](TreeView.md) — 树形视图（虚拟化 + 平滑滚动）
- [MenuBar](MenuBar.md) — 菜单栏

### 文本控件

- [TextBlock](TextBlock.md) — 只读文本显示
- [TextArea](TextArea.md) — 多行文本编辑（虚拟化 + 合成器滚动）
- [Hyperlink](Hyperlink.md) — 超链接

### 数据展示控件

- [DataGrid](DataGrid.md) — 虚拟化只读表格（行列双向虚拟化）
- [Chart](Chart.md) — 折线 / 柱状 / K 线图
- [Metric](Metric.md) — 带涨跌方向的指标卡
- [InfoBar](InfoBar.md) — 内联状态消息（四个严重级别）

### 布局容器

- [StackPanel](StackPanel.md) — 栈式布局（水平/垂直）
- [Grid](Grid.md) — 网格布局（行列定义，auto/star/pixel 尺寸）
- Canvas（文档待写） — 画布布局（绝对定位 + ZIndex）
- DockPanel（文档待写） — 停靠布局
- WrapPanel（文档待写） — 自动换行布局
- [Border](Border.md) — 边框容器
- [ScrollPanel](ScrollPanel.md) — 通用滚动容器（双轴，不虚拟化）
- Separator（文档待写） — 分隔线
- Viewbox（文档待写） — 缩放容器（Uniform/Fill/None）
- GroupBox（文档待写） — 分组框
- UniformGrid（文档待写） — 均匀网格

### 显示控件

- [ProgressBar](ProgressBar.md) — 进度条
- [Image](Image.md) — 图像显示
- [MenuFlyout](MenuFlyout.md) — 弹出菜单

### 对话框

- ContentDialog（文档待写） — 内容对话框
- MessageDialog（文档待写） — 消息框

### 导航与工具栏

- [TabControl](TabControl.md) — 选项卡控件
- ToolBar（文档待写） — 工具栏（CommandBar 风格，溢出进 flyout）
- StatusBar（文档待写） — 状态栏

### 日期与时间

- [DatePicker](DatePicker.md) — 日期选择器
- [Calendar](Calendar.md) — 日历

### 其他

- [GridSplitter](GridSplitter.md) — 可拖动分隔条
- Expander（文档待写） — 可展开/折叠容器

### 富文本（独立分类）

- [RichTextBox](RichTextBox.md) — 富文本编辑器（样式、段落格式、撤销栈）

### 布局容器（补充）

- [ScrollViewer](ScrollViewer.md) — 滚动条模型（偏移、淡出、拖动），被各滚动控件内嵌

### 专用工具（补充）

- [LogViewer](LogViewer.md) — 高吞吐日志查看器（虚拟化 + 自动尾随）

---

## 文档约定

每个控件文档包含以下部分：

1. **概述** — 控件用途与典型使用场景
2. **继承层次** — 类继承关系
3. **公开 API** — 方法、属性、事件
4. **代码示例** — 最小可运行示例
5. **备注** — 性能特征、使用注意事项
6. **相关控件** — 类似或互补的控件

---

## 快速链接

以下四份通用指南尚未编写（头文件和源码注释始终是权威参考）：

- 元素模型概述 — Visual/UIElement/FrameworkElement/Control 四层
- 布局指南 — Measure/Arrange 与各 Panel 使用场景
- 主题定制指南 — ThemeTokens 与自定义品牌色
- 性能最佳实践 — 虚拟化、脏标记、compositor 动画

---

## 贡献

控件文档模板见 [TEMPLATE.md](TEMPLATE.md)。

添加新控件文档时：
1. 复制 `TEMPLATE.md` 为 `YourControl.md`
2. 填写所有章节
3. 在本文件的相应类别下添加链接
4. 提交 PR 前确保代码示例可编译运行
