# FluentUI 控件事件矩阵

**说明**: ✅ 已实现 | ❌ 缺失 | ⚠️ 仅虚函数（未暴露 Event<>） | ➖ 不适用

---

## 基础交互事件（所有控件通用）

| 事件类型 | UIElement 基类 | 典型场景 |
|---------|---------------|---------|
| **PointerPressed** | ⚠️ 虚函数 | 检测按下开始 |
| **PointerReleased** | ⚠️ 虚函数 | 检测点击完成 |
| **PointerMoved** | ⚠️ 虚函数 | 拖拽、悬停检测 |
| **PointerWheelChanged** | ⚠️ 虚函数 | 滚轮滚动 |
| **PointerEntered** | ❌ | 鼠标进入控件 |
| **PointerExited** | ⚠️ `OnPointerLeft()` | 鼠标离开控件 |
| **DoubleClick** | ❌ | 双击操作 |
| **RightClick** | ❌ | 右键点击（非菜单） |
| **KeyDown** | ⚠️ 虚函数 | 按键检测 |
| **KeyUp** | ❌ | 按键抬起 |
| **PreviewKeyDown** | ⚠️ 虚函数 | 键盘事件预览 |
| **TextInput** | ⚠️ 虚函数 | 文本输入 |
| **GotFocus** | ⚠️ `OnFocusChanged()` | 获得焦点 |
| **LostFocus** | ⚠️ `OnFocusChanged()` | 失去焦点 |
| **SizeChanged** | ⚠️ `OnBoundsChanged()` | 控件大小变化 |
| **ContextMenuOpening** | ⚠️ 虚函数 | 右键菜单打开前 |

---

## 控件特定事件矩阵

### 1. 按钮类控件

| 控件 | Click | DoubleClick | RightClick | Checked/Toggled | 备注 |
|------|:-----:|:----------:|:----------:|:---------------:|------|
| **Button** | ✅ | ❌ | ❌ | ➖ | 单击已实现 |
| **CheckBox** | ➖ | ❌ | ❌ | ✅ `Checked()` | |
| **RadioButton** | ➖ | ❌ | ❌ | ✅ `Selected()` | |
| **ToggleSwitch** | ➖ | ❌ | ❌ | ✅ `Toggled()` | |
| **RepeatButton** | ✅ 继承 | ❌ | ❌ | ➖ | 长按重复触发 |
| **Hyperlink** | ✅ | ❌ | ❌ | ➖ | |

---

### 2. 文本输入控件

| 控件 | TextChanged | SelectionChanged | DoubleClick | RightClick | Copy/Paste | Undo/Redo |
|------|:-----------:|:----------------:|:----------:|:----------:|:----------:|:---------:|
| **TextBox** | ✅ 继承 | ✅ 继承 | ❌ | ❌ | ❌ | ❌ |
| **PasswordBox** | ✅ 继承 | ➖ | ❌ | ❌ | ❌ | ➖ |
| **TextArea** | ✅ 继承 | ✅ 继承 | ⚠️ 内部实现 | ⚠️ 内部实现 | ❌ | ❌ |
| **RichTextBox** | ❌ | ❌ | ⚠️ 内部实现 | ⚠️ 内部实现 | ❌ | ❌ |
| **NumericUpDown** | ➖ | ➖ | ❌ | ❌ | ➖ | ➖ |

**备注**:
- TextBox/TextArea/PasswordBox 继承自 `TextEditBase`，后者有 `TextChanged()` 和 `SelectionChanged()`
- RichTextBox **未继承** TextEditBase，**没有暴露任何事件**
- 双击词选择、右键菜单内部实现，用户无法定制
- 复制/粘贴/撤销/重做内部实现，无事件通知

---

### 3. 选择器控件

| 控件 | SelectionChanged | ItemDoubleClick | ItemRightClick | MultiSelect | DragReorder |
|------|:----------------:|:---------------:|:--------------:|:-----------:|:-----------:|
| **ListBox** | ✅ + `SelectionSetChanged()` | ❌ | ❌ | ✅ 已支持 | ❌ |
| **ComboBox** | ✅ + `TextChanged()` | ❌ | ❌ | ➖ | ➖ |
| **TreeView** | ✅ + `SelectionSetChanged()` | ❌ | ❌ | ✅ 已支持 | ❌ |
| **TabControl** | ✅ + `TabCloseRequested()` | ❌ | ❌ | ➖ | ❌ |

**缺失场景**:
- **ListBox**: 双击项执行操作（打开、编辑）
- **TreeView**: 双击展开/折叠、右键菜单前选中节点
- **ComboBox**: 下拉框打开/关闭事件
- **TabControl**: 标签页拖拽重排

---

### 4. 数据表格控件

| 事件 | DataGrid | 备注 |
|------|:--------:|------|
| **SelectionChanged** | ✅ | 行选中 |
| **CellEdited** | ✅ | 单元格编辑完成 |
| **ColumnHeaderClicked** | ✅ | 列头点击（排序） |
| **RowsReordered** | ✅ | 行拖拽重排后 |
| **CellDoubleClick** | ❌ | 双击单元格（常用于进入编辑模式） |
| **RowDoubleClick** | ❌ | 双击行（常用于打开详情） |
| **ColumnHeaderDoubleClick** | ❌ | 双击列头（自动列宽） |
| **CellRightClick** | ❌ | 右键单元格菜单 |
| **BeginEdit / EndEdit** | ❌ | 编辑模式进入/退出（可取消） |
| **ColumnReorder** | ❌ | 列拖拽重排 |
| **ColumnResize** | ❌ | 列宽调整 |

**当前状态**: DataGrid 已有 4 个事件，但缺少双击、右键、编辑生命周期

---

### 5. 数值输入控件

| 控件 | ValueChanged | Min/Max | Increment | Keyboard | Mouse |
|------|:------------:|:-------:|:---------:|:--------:|:-----:|
| **Slider** | ✅ | ➖ | ➖ | ⚠️ 内部 | ⚠️ 内部 |
| **NumericUpDown** | ✅ | ➖ | ➖ | ⚠️ 内部 | ⚠️ 内部 |
| **Rating** | ✅ | ➖ | ➖ | ⚠️ 内部 | ⚠️ 内部 |
| **ProgressBar** | ➖ | ➖ | ➖ | ➖ | ➖ |

**缺失事件**:
- **ValueChanging** (值变化中，可取消)
- **DragStarted / DragCompleted** (Slider 拖拽开始/结束)

---

### 6. 日期/日历控件

| 事件 | Calendar | DatePicker | 备注 |
|------|:--------:|:----------:|------|
| **SelectionChanged** | ✅ | ➖ | Calendar 日期选中 |
| **DateChanged** | ➖ | ✅ | DatePicker 值变化 |
| **DropDownOpened** | ➖ | ❌ | 下拉日历打开 |
| **DropDownClosed** | ➖ | ❌ | 下拉日历关闭 |
| **MonthChanged** | ❌ | ❌ | 切换月份 |
| **YearChanged** | ❌ | ❌ | 切换年份 |

---

### 7. 容器/布局控件

| 控件 | 事件 | 备注 |
|------|------|------|
| **Expander** | ✅ `ExpandedChanged()` | 展开/折叠 |
| **ScrollViewer** | ❌ | 缺 `ScrollChanged` 事件 |
| **Panel** | ➖ | 纯布局容器 |
| **DockPanel** | ➖ | 纯布局容器 |
| **Grid** | ➖ | 纯布局容器 |
| **StackPanel** | ➖ | 纯布局容器 |
| **Canvas** | ➖ | 纯布局容器 |
| **WrapPanel** | ➖ | 纯布局容器 |
| **UniformGrid** | ➖ | 纯布局容器 |

**缺失**: ScrollViewer 的 `ScrollChanged` 事件（滚动位置变化）

---

### 8. 内容/媒体控件

| 控件 | 事件 | 备注 |
|------|------|------|
| **Image** | ✅ `ImageFailed()` | 加载失败 |
| **Image** | ❌ `ImageOpened()` | 加载成功 |
| **Image** | ❌ `DoubleClick` | 双击全屏查看 |
| **TextBlock** | ❌ | 只读文本，无交互 |
| **Chart** | ❌ | 缺少所有交互事件（悬停、点击数据点） |
| **LogViewer** | ❌ | 缺少日志行点击、双击事件 |
| **Metric** | ❌ | 纯展示控件 |

---

### 9. 对话框/通知控件

| 控件 | 事件 | 备注 |
|------|------|------|
| **ContentDialog** | ❌ | 缺 `Opened`, `Closed`, `Closing` (可取消) |
| **MessageDialog** | ❌ | 同上 |
| **InfoBar** | ✅ `Closed()` | 关闭通知 |
| **InfoBar** | ❌ `ActionButtonClick` | 操作按钮点击 |
| **MenuBar** | ❌ | 缺 `MenuOpened`, `MenuClosed` |
| **MenuFlyout** | ❌ | 缺 `Opened`, `Closed` |
| **ToolBar** | ⚠️ `overflowOpened_` | 私有成员，未暴露 |

---

### 10. 分隔器/导航控件

| 控件 | 事件 | 备注 |
|------|------|------|
| **GridSplitter** | ❌ | 缺 `DragStarted`, `DragDelta`, `DragCompleted` |
| **Separator** | ➖ | 纯装饰控件 |
| **StatusBar** | ➖ | 纯容器 |

---

## 拖拽事件全局缺失

**所有控件都缺少拖拽源端支持**：

| 拖拽事件 | 状态 | 优先级 |
|---------|------|--------|
| **DragStart** | ❌ | 🔥 High |
| **DragOver** | ❌ (Drop 端有 `SetDropTarget` 接口) | 🔥 High |
| **DragLeave** | ❌ | Medium |
| **Drop** | ⚠️ 通过 `IFluentDropTarget` 接口 | 🔥 需补充事件形式 |
| **DragEnd** | ❌ | Medium |
| **QueryContinueDrag** | ❌ | Low |

**影响控件**:
- TreeView (节点重排)
- ListBox (项重排)
- DataGrid (行/列重排)
- TabControl (标签页重排)
- GridSplitter (拖拽调整大小)

---

## 总结统计

### 事件暴露统计
- **已暴露 Event<>**: 29 个事件（分布在 19 个控件）
- **仅虚函数未暴露**: 至少 12 个基础交互事件
- **完全缺失**: 至少 40+ 个用户场景事件

### 控件覆盖率
- **有事件的控件**: 19 / 45 (42%)
- **无任何事件的控件**: 26 / 45 (58%)

### 优先级分类

**🔥 高优先级（立即补充）**:
1. DoubleClick（TreeView, ListBox, DataGrid, Image）
2. PointerEntered / PointerExited（所有控件）
3. RightClick（TreeView, DataGrid, TextArea）
4. GotFocus / LostFocus（表单控件）
5. DragStart / Drop（TreeView, ListBox, DataGrid, TabControl）

**⚠️ 中优先级（下一迭代）**:
6. SizeChanged（Chart, 响应式布局）
7. ScrollChanged（ScrollViewer）
8. KeyDown / KeyUp（快捷键绑定）
9. ValueChanging（Slider, NumericUpDown）
10. BeginEdit / EndEdit（DataGrid）

**📋 低优先级（按需添加）**:
11. ColumnReorder / ColumnResize（DataGrid）
12. DropDownOpened / Closed（ComboBox, DatePicker）
13. MonthChanged / YearChanged（Calendar）
14. ImageOpened（Image）
15. PreviewXxx 系列（事件隧道）

---

## 下一步行动

1. **基础事件层**: 在 UIElement/Control 添加 12 个基础交互事件
2. **控件特定事件**: 为 TreeView/ListBox/DataGrid 添加 DoubleClick
3. **拖拽完整支持**: 实现拖拽源端 + 事件化 Drop
4. **文档**: WPF 迁移指南 + 事件最佳实践

**预计工作量**: 6-8 周（1 名全职开发者）
