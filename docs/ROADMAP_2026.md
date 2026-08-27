# FluentUI 发展路线图 (2026)

**最后更新**: 2026-08-21  
**当前状态**: feat/async-layout 分支完成，29 个控件实现并展示

本文档整合了控件补全、性能优化、功能增强、文档完善四个方向的计划。

---

## 目录

1. [控件补全计划](#1-控件补全计划)
2. [性能优化机会](#2-性能优化机会)
3. [功能增强方向](#3-功能增强方向)
4. [文档与工具](#4-文档与工具)
5. [已知限制与长期规划](#5-已知限制与长期规划)

---

## 1. 控件补全计划

### 当前状态 (29 个控件已实现)

**基础输入** (10): Button, RepeatButton, CheckBox, RadioButton, ToggleSwitch, 
Slider, TextBox, PasswordBox, NumericUpDown, Rating

**选择控件** (4): ComboBox, ListBox, TreeView, MenuBar

**文本控件** (3): TextBlock, TextArea, Hyperlink

**布局容器** (7): StackPanel, Grid, Canvas, ScrollPanel, Border, Separator, Viewbox

**显示控件** (5): ProgressBar, Image, ToolTip, MenuFlyout, ColorPicker

### 1.1 立即可做（零/极小成本）

#### ✅ ToggleButton（可直接实例化）
**现状**: `controls/primitives/ToggleButton.h` 是完整基类，但无具体可用类  
**用途**: ToolBar 的"按下态"按钮（加粗/斜体/对齐方式）  
**成本**: 近零（继承 ToggleButton + 复用 Button 绘制路径，约 80 行）  
**优先级**: 高（ToolBar 增强的前置需求）

#### InfoBar / InfoBadge
**组成**: Border + 图标绘制 + TextBlock + 关闭 Button  
**成本**: 极小（无新能力，配色取 styling/ThemeTokens）  
**价值**: 性价比最高——几乎所有应用都要用它替代打断用户的 MessageDialog  
**优先级**: 高

#### Shape 三件套: Rectangle / Ellipse / Line
**现状**: graphics/DrawingContext 已有 FillEllipse/DrawRoundedRect/DrawLine  
**成本**: 小（薄 UIElement 包装，每个约 60 行）  
**价值**: Canvas 布局中绘制装饰图形的基础件  
**优先级**: 中  
**备注**: Path 暂不做（需 ID2D1PathGeometry + 路径语法解析器）

#### ProgressRing
**复用**: controls/ProgressSweepVisual 已提供合成器扫掠视觉  
**成本**: 小（环形几何 + 独立控件封装，约 150 行）  
**价值**: 不确定进度指示，补充 ProgressBar  
**优先级**: 中

### 1.2 近期可做（小成本，组合已有件）

#### Flyout（通用弹出容器）
**现状**: services/PopupHost 已有全部底层能力（SetContent/LightDismiss/翻转钳制）  
**缺失**: "锚定到 UIElement + Placement 枚举"的薄封装  
**阻塞项**: DropDownButton、SplitButton、AutoSuggestBox、TimePicker、TeachingTip  
**成本**: 小（约 200 行）  
**价值**: **最高杠杆的一步——解锁 6+ 个控件**  
**优先级**: 高

#### DropDownButton
**依赖**: Flyout  
**组成**: Button + chevron 绘制 + MenuFlyout::ShowBelow 调用  
**成本**: 小（约 150 行）  
**优先级**: 高

#### SplitButton
**依赖**: Flyout  
**组成**: 左半执行默认命令，右半弹出选项  
**成本**: 中（约 250 行）  
**优先级**: 中（价值不如 DropDownButton 普遍）

#### AutoSuggestBox（搜索框）
**依赖**: Flyout + ListBox（都已存在）  
**新增**: 输入节流 + 键盘上下选中转发  
**成本**: 中（约 350 行）  
**优先级**: 高（搜索场景必备）

#### TimePicker
**复用**: DatePicker/Calendar 的弹出与本地化模式  
**成本**: 中（约 300 行）  
**价值**: 与 DatePicker 成对出现，缺它会让日期时间输入不完整  
**优先级**: 中

#### SplitView（可折叠侧边栏）
**组成**: Panel/Grid + 宽度动画（animation/ 已有）  
**阻塞项**: NavigationView  
**成本**: 中（约 400 行）  
**价值**: 本身有用，同时为 NavigationView 铺路  
**优先级**: 中

### 1.3 可选/打磨类（非阻塞）

- **ScrollBar（独立控件）**: 内部已有等价能力，抽出来能消除重复绘制逻辑
- **TeachingTip**: 带尾巴的引导式提示卡，依赖 Flyout，属打磨类
- **PersonPicture / Avatar**: 小成本装饰类
- **CommandBar**: ToolBar 已缓解需求，等 DropDownButton 后再增强
- **Pivot**: 与 TabControl 高度重叠，低优先级
- **BreadcrumbBar**: 使用场景窄（文件/层级浏览器）

### 1.4 重型项（被架构阻塞，需专项决策）

#### ItemsRepeater / ListView / GridView
**阻塞原因**: 强依赖"数据模板 + 元素回收"两项框架能力，都不存在  
**现状**: 现有虚拟化是控件内自绘式（ListBox 的 ItemTextProvider 文本回调、
TreeView::DrawRowsToSurface），都是"画像素"而非"回收 UIElement"  
**决策需求**: 是否引入轻量 DataTemplate 抽象？否则会出现多个控件各造回收池的重复  
**优先级**: 需架构决策

#### RichTextBox
**需求**: 富文本文档模型 + DWrite typography range + 撤销栈扩展  
**现状**: TextEditBase 目前是纯文本模型  
**成本**: 最高，通用性最低  
**优先级**: 低

#### NavigationView
**依赖**: SplitView + 选中指示动画 + 导航项列表  
**阻塞**: 需页面切换概念（框架目前无 Frame/Page 导航）  
**价值**: 对"看起来像 Fluent 应用"影响最大  
**策略**: 应拆成 SplitView 先行  
**优先级**: 中（需先完成 SplitView）

#### Frame / Page（导航宿主）
**需求**: 页面栈与前进/后退导航，需页面生命周期与导航栈概念  
**现状**: 框架目前完全没有  
**性质**: 架构级决策，不属控件补齐范畴  
**优先级**: 需架构决策

---

## 2. 性能优化机会

### 2.1 已知性能特征（来自 FluentUIBench）

#### ✅ 已优化的路径
- **布局成本**: 1000 个堆叠控件 × 200 趟 = 2.957 ms（0.0148 ms/趟）
- **动画活跃集**: 5000 元素树中 200 个活跃动画 = 0.195 ms（0.19 µs/tick）
- **Idle 成本**: 30 秒 idle = 0.000 ms（消息循环 INFINITE 阻塞，零 CPU）

#### 🔍 待测量的场景（需真机 + GPU）
- **TextStress**: CJK/Latin 混合换行文本（IDWriteTextLayout 构建 + 绘制）
- **ResizeStress**: 连续窗口 resize（swap chain 复用，无闪烁）
- **PopupStress**: 级联菜单开关 10,000 次（GDI/USER 句柄 + 私有字节不增长）
- **DpiLab**: 跨显示器 / 多 DPI（96/120/144/192 DPI，无缓存资源复用，无文本裁剪）
- **ThemeLab**: Light/Dark/Mica/Acrylic/高对比度视觉回归
- **InputLatency**: 指针按下到 Present（用 frame-latency waitable object 测量）

### 2.2 具体优化方向

#### 文本布局缓存
**现状**: TextArea 的 LineLayoutCache 已为可见行缓存布局  
**机会**: 
- 监控 `textLayoutsNew` / `textLayoutCacheHits` 比率（F12 HUD）
- CJK/Latin 混合文本的 IDWriteTextLayout 构建成本需真机测量  
**优先级**: 中（需先完成 TextStress 场景测量）

#### TreeView 虚拟化
**现状**: FluentUIBench 注释标记"Virtualization not implemented yet"  
**目标**: 100k 数据行不创建 100k UIElement——视觉元素数量应跟踪可见行  
**当前实现**: TreeView::DrawRowsToSurface 已是 O(visible) 绘制  
**改进空间**: 元素池复用？（需评估收益 vs 复杂度）  
**优先级**: 低（当前实现已满足性能要求）

---

## 3. 功能增强方向

### 3.1 无障碍功能（P0 优先级）

**现状**: README.md 明确标记"无障碍功能未实现"  
**需求**: UI Automation 支持，屏幕阅读器识别  
**优先级**: **高（合规要求）**  
**工作量**: 大（需为每个控件实现 UIA provider）

#### 初步实施路径
1. **基础设施**: 实现 IRawElementProviderSimple 接口
2. **控件 provider**: Button/TextBox/CheckBox/ComboBox 等常用控件
3. **导航 provider**: Panel 的子元素导航
4. **属性支持**: Name/Role/Value/State
5. **事件支持**: Focus/ValueChanged/SelectionChanged

### 3.2 输入与交互

#### 键盘导航增强
**现状**: Tab/Shift+Tab 焦点导航已实现  
**改进**:
- 方向键导航（Grid 内二维导航）
- Ctrl+Tab（TabControl 切换页）
- 加速键（Alt+字母）

#### 触摸与手势
**现状**: 仅鼠标 + 键盘  
**改进**:
- WM_POINTER 消息处理
- 捏合缩放、滑动手势
- 触摸滚动惯性

### 3.3 国际化

#### RTL 布局
**现状**: README.md 标记"无 RTL 布局"  
**需求**: 从右到左语言（阿拉伯语、希伯来语）  
**工作量**: 中（需在布局系统中添加 FlowDirection 支持）

---

## 4. 文档与工具

### 4.1 新增文档

✅ **已创建**:
- `docs/ROADMAP_2026.md` — 本路线图
- `docs/GETTING_STARTED.md` — 入门教程
- `docs/controls/README.md` — 控件文档索引
- `docs/controls/TEMPLATE.md` — 控件文档模板
- `docs/controls/Button.md` — Button 控件完整文档（示例）

❌ **待创建**:
- 其余 28 个控件的 API 文档
- `docs/LAYOUT_GUIDE.md` — 布局系统指南
- `docs/THEMING_GUIDE.md` — 主题定制指南
- `docs/PERFORMANCE_BEST_PRACTICES.md` — 性能最佳实践
- `docs/ELEMENT_MODEL.md` — 元素模型（Visual/UIElement/FrameworkElement/Control）
- `docs/MIGRATION_FROM_WPF.md` — WPF 迁移指南

---

## 5. 优先级决策矩阵

| 项目 | 成本 | 价值 | 阻塞性 | 优先级 |
|------|------|------|--------|--------|
| **Flyout** | 小 | 高 | 解锁 6+ 控件 | **P0** |
| **InfoBar** | 极小 | 高 | 无 | **P0** |
| **无障碍功能** | 大 | 极高 | 合规要求 | **P0** |
| **控件 API 文档** | 中 | 高 | 无 | **P1** |
| **入门教程** | 小 | 高 | 无 | ✅ **已完成** |
| **ToggleButton** | 近零 | 中 | ToolBar 增强 | P1 |
| **DropDownButton** | 小 | 高 | 依赖 Flyout | P1 |
| **AutoSuggestBox** | 中 | 高 | 依赖 Flyout | P1 |
| **Shape 三件套** | 小 | 中 | 无 | P2 |
| **SplitView** | 中 | 中 | NavigationView 前置 | P2 |
| **TimePicker** | 中 | 中 | 与 DatePicker 成对 | P2 |
| **ProgressRing** | 小 | 中 | 无 | P2 |
| **TextStress 测量** | 小 | 中 | 性能基线 | P2 |

---

## 下一步行动（由用户决定优先级）

1. **P0 项目**: Flyout + InfoBar + 无障碍功能基础设施
2. **P1 文档**: 补全 28 个控件的 API 参考文档
3. **真机性能测量**: TextStress / ResizeStress / DpiLab
4. **架构决策**: 模板系统 / 导航框架是否引入？
