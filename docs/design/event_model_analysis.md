# FluentUI 控件事件模型全面分析

**作者**: AI Assistant  
**日期**: 2026-09-20  
**目的**: 全面审计所有控件的事件暴露情况，识别缺失的用户交互事件

---

## 摘要

当前 FluentUI 库中 **45 个控件**，已实现的事件大多是**状态变更事件**（如 `ValueChanged`、`SelectionChanged`），但**缺少大量底层用户交互事件**（如 `DoubleClick`、`RightClick`、`MouseEnter`/`Leave`、`DragStart`等）。

**核心问题**: 用户无法自定义控件对这些交互的响应行为，必须通过继承并重写虚函数来实现，这**违反了 WPF/WinUI 的事件驱动设计哲学**。

---

## 一、当前事件模型架构

### 1.1 基类层次结构

```
Visual (最底层，只有渲染)
  └─ UIElement (输入/布局/生命周期)
      ├─ FrameworkElement (Width/Margin/Alignment)
      │   ├─ Control (Background/Foreground/主题)
      │   └─ Panel (容器布局)
      └─ ContentControl (单子元素容器)
```

### 1.2 当前事件暴露层级

#### UIElement 层（虚函数，未暴露为 Event<>）
```cpp
// 鼠标事件
virtual void OnPreviewPointerPressed(PointerEventArgs& e);
virtual void OnPointerPressed(PointerEventArgs& e);
virtual void OnPointerReleased(PointerEventArgs& e);
virtual void OnPointerMoved(PointerEventArgs& e);
virtual void OnPointerWheelChanged(PointerEventArgs& e);
virtual void OnPointerLeft();
virtual void OnClickRouted(PointerEventArgs& e);

// 键盘事件
virtual void OnPreviewKeyDown(KeyEventArgs& e);
virtual void OnKeyDownRouted(KeyEventArgs& e);
virtual void OnTextInput(wchar_t ch);

// 右键菜单
virtual void OnContextMenuOpening(float dipX, float dipY);

// 生命周期
virtual void OnAttachedToTree();
virtual void OnDetachedFromTree();
virtual void OnBoundsChanged();
virtual void OnFocusChanged();
virtual void OnStateChanged();  // VisualState 变化

// 动画/IME
virtual void OnAnimationTick(float dtSec);
virtual void OnBlink();  // 光标闪烁
virtual void OnImeStartComposition(HWND hwnd);
virtual void OnImeComposition(HWND hwnd, LPARAM flags);
virtual void OnImeEndComposition(HWND hwnd);
```

#### Control 层（已暴露的 Event<>）
**按控件分类：**

| 控件 | 已有事件 | 类型 |
|------|---------|------|
| **Button** | `Click()` | 用户操作 |
| **CheckBox** | `Checked()` | 状态变更 |
| **ToggleSwitch** | `Toggled()` | 状态变更 |
| **RadioButton** | `Selected()` | 状态变更 |
| **Slider** | `ValueChanged()` | 状态变更 |
| **NumericUpDown** | `ValueChanged()` | 状态变更 |
| **Rating** | `ValueChanged()` | 状态变更 |
| **TextEditBase** | `TextChanged()`, `SelectionChanged()` | 状态变更 |
| **ComboBox** | `SelectionChanged()`, `TextChanged()` | 状态变更 |
| **ListBox** | `SelectionChanged()`, `SelectionSetChanged()` | 状态变更 |
| **TreeView** | `SelectionChanged()`, `SelectionSetChanged()` | 状态变更 |
| **TabControl** | `SelectionChanged()`, `TabCloseRequested()` | 状态变更 + 操作请求 |
| **DataGrid** | `RowsReordered()`, `CellEdited()`, `SelectionChanged()`, `ColumnHeaderClicked()` | 状态变更 + 用户操作 |
| **Calendar** | `SelectionChanged()` | 状态变更 |
| **DatePicker** | `DateChanged()` | 状态变更 |
| **Expander** | `ExpandedChanged()` | 状态变更 |
| **InfoBar** | `Closed()` | 用户操作 |
| **Image** | `ImageFailed()` | 错误事件 |
| **Hyperlink** | `Click()` | 用户操作 |

---

## 二、缺失的事件类别

### 2.1 双击事件（DoubleClick）

**影响控件：**
- **TreeView**: 无法实现"双击节点展开/折叠"或"双击文件打开"
- **ListBox**: 无法实现"双击列表项执行操作"
- **DataGrid**: 无法实现"双击行编辑"或"双击列头自动列宽"
- **Button**: 特殊场景可能需要区分单击/双击
- **Image**: 无法实现"双击图片全屏查看"
- **TextArea/RichTextBox**: 双击词选择（已内部实现，但不可定制）

**WPF 对比：**
```csharp
// WPF 有 MouseDoubleClick 事件
treeView.MouseDoubleClick += (s, e) => { /* 用户代码 */ };
```

**当前变通方法：**
用户必须继承控件并重写 `OnPointerReleased`，手动检测双击时间窗口。

---

### 2.2 鼠标进入/离开事件（MouseEnter/MouseLeave）

**影响控件：**
- **所有控件**（通用需求）

**典型场景：**
1. **工具提示的自定义触发逻辑**（不依赖内置 Tooltip）
2. **悬停高亮效果**（不修改控件背景色）
3. **弹出式预览面板**（鼠标悬停图片显示大图）
4. **状态栏信息更新**（鼠标悬停控件时显示帮助文本）

**当前变通方法：**
- `OnPointerMoved` + `OnPointerLeft` 组合，但前者**每次鼠标移动都触发**（性能浪费）
- `OnPointerLeft` 是虚函数，未暴露为 `Event<>`

**WPF 对比：**
```csharp
button.MouseEnter += (s, e) => statusBar.Text = "点击保存";
button.MouseLeave += (s, e) => statusBar.Text = "";
```

---

### 2.3 右键点击事件（RightClick）

**影响控件：**
- **TreeView**, **ListBox**, **DataGrid**: 右键选中+弹出菜单
- **TextArea**, **RichTextBox**: 右键时根据点击位置调整选区
- **Button**, **Image**: 自定义右键操作（不一定是上下文菜单）

**当前状态：**
- `OnContextMenuOpening(dipX, dipY)` 虚函数存在，但：
  1. **只在有上下文菜单时调用**
  2. **未暴露为 Event<>**
  3. 用户无法阻止默认菜单弹出

**典型场景：**
- DataGrid 右键行：先选中该行，再弹菜单
- TextArea 右键：点击在选区外 → 移动光标；点击在选区内 → 保持选区

**WPF 对比：**
```csharp
treeView.MouseRightButtonDown += (s, e) => {
    var item = GetItemAt(e.GetPosition(treeView));
    if (item != null) treeView.SelectedItem = item;
    e.Handled = true;  // 阻止默认行为
};
```

---

### 2.4 拖拽相关事件（Drag Events）

**当前状态：**
- `SetDropTarget(IFluentDropTarget)` 接口存在
- 但**拖拽源端（Drag Source）完全缺失**

**缺失事件：**
```cpp
Event<Control, DragStartEventArgs> DragStart;  // 开始拖拽
Event<Control, DragEventArgs> DragOver;         // 拖拽经过
Event<Control, DragEventArgs> DragLeave;        // 拖拽离开
Event<Control, DragEventArgs> Drop;             // 松开放下
Event<Control, RoutedEventArgs> DragEnd;        // 拖拽结束（无论成功或取消）
```

**影响场景：**
1. **TreeView 节点重排**（拖动节点改变层级）
2. **TabControl 标签页拖拽重排**
3. **DataGrid 行拖拽排序**
4. **文件拖出到系统资源管理器**

**WPF 对比：**
```csharp
// WPF 有完整的拖拽事件链
listBox.DragOver += (s, e) => { /* 显示拖放指示器 */ };
listBox.Drop += (s, e) => { /* 处理放下操作 */ };
```

---

### 2.5 焦点事件（Focus Events）

**当前状态：**
- `OnFocusChanged()` 虚函数存在
- **未暴露为 Event<>**

**典型场景：**
1. **表单验证**：文本框失去焦点时触发验证
2. **自动保存**：编辑控件失去焦点时保存草稿
3. **焦点跟踪**：记录用户的操作流程

**WPF 对比：**
```csharp
textBox.GotFocus += (s, e) => textBox.SelectAll();
textBox.LostFocus += (s, e) => ValidateInput(textBox.Text);
```

---

### 2.6 按键事件（Key Events）

**当前状态：**
- `OnPreviewKeyDown`, `OnKeyDownRouted` 虚函数存在
- **未暴露为 Event<>**

**影响场景：**
1. **快捷键处理**（不继承控件直接绑定）
2. **全局按键监听**（例如 F11 全屏）
3. **自定义导航**（方向键在自定义控件中的行为）

**WPF 对比：**
```csharp
window.KeyDown += (s, e) => {
    if (e.Key == Key.F11) ToggleFullScreen();
};
```

---

### 2.7 尺寸变化事件（SizeChanged）

**当前状态：**
- `OnBoundsChanged()` 虚函数存在
- **未暴露为 Event<>**

**典型场景：**
1. **响应式布局调整**（控件大小变化时重新计算子元素）
2. **图表重绘**（Chart 控件窗口调整时重新绘制）
3. **滚动位置恢复**（窗口变大时取消滚动条）

**WPF 对比：**
```csharp
chart.SizeChanged += (s, e) => RedrawChart(e.NewSize);
```

---

## 三、缺失事件的系统性问题

### 3.1 违反事件驱动原则

**现状：** 用户必须通过**继承 + 重写虚函数**来自定义行为  
**问题：**
1. **代码耦合度高**：每个自定义行为需要一个新的派生类
2. **无法组合**：两个独立的交互逻辑无法同时应用到一个控件实例
3. **破坏封装**：用户被迫了解控件内部实现

**WPF/WinUI 模式：** 通过 **Event<> + 委托** 实现松耦合
```cpp
// 理想用法（当前不支持）
treeView.ItemDoubleClick += [](TreeView&, TreeViewItemDoubleClickArgs& e) {
    if (e.row->user) OpenFile(e.row);
    e.handled = true;
};
```

---

### 3.2 与现有架构不一致

**已有事件都暴露了 Event<>：**
- `Button::Click()`
- `TextEditBase::TextChanged()`
- `Slider::ValueChanged()`

**但底层交互事件（鼠标/键盘）都是虚函数，未暴露。**

**结果：**
- 高层语义事件（状态变更）→ 可订阅
- 低层交互事件（鼠标/键盘）→ 只能继承

这种**混合模式**增加了学习成本和使用复杂度。

---

### 3.3 与 WPF/WinUI 不兼容

**移植困难：** 从 WPF 迁移到 FluentUI 的代码必须：
1. 将所有事件订阅改为虚函数重写
2. 将组合关系改为继承关系
3. 手动管理事件优先级（e.handled）

**示例对比：**

| 需求 | WPF | FluentUI（当前） |
|------|-----|-----------------|
| 双击 TreeView 节点打开文件 | `treeView.MouseDoubleClick += ...` | 继承 `MyTreeView : TreeView`，重写 `OnPointerReleased` |
| 悬停显示工具提示 | `control.MouseEnter += ...` | 继承 `MyControl : Control`，重写 `OnPointerMoved` + `OnPointerLeft` |
| 右键菜单前调整选区 | `textBox.MouseRightButtonDown += ...` | 继承 `MyTextBox : TextBox`，重写 `OnPointerPressed` |

---

## 四、建议的事件补全方案

### 4.1 Phase 1: 暴露现有虚函数为 Event<>

在 **UIElement** 或 **Control** 层添加：

```cpp
class UIElement : public Visual {
public:
    // 鼠标事件
    Event<UIElement, PointerEventArgs>& PointerPressed() { return pointerPressed_; }
    Event<UIElement, PointerEventArgs>& PointerReleased() { return pointerReleased_; }
    Event<UIElement, PointerEventArgs>& PointerMoved() { return pointerMoved_; }
    Event<UIElement, PointerEventArgs>& PointerWheelChanged() { return pointerWheel_; }
    Event<UIElement, RoutedEventArgs>& PointerEntered() { return pointerEntered_; }  // 新增
    Event<UIElement, RoutedEventArgs>& PointerExited() { return pointerExited_; }    // 新增
    
    // 键盘事件
    Event<UIElement, KeyEventArgs>& KeyDown() { return keyDown_; }
    Event<UIElement, KeyEventArgs>& PreviewKeyDown() { return previewKeyDown_; }
    Event<UIElement, wchar_t>& TextInput() { return textInput_; }
    
    // 焦点事件
    Event<UIElement, RoutedEventArgs>& GotFocus() { return gotFocus_; }
    Event<UIElement, RoutedEventArgs>& LostFocus() { return lostFocus_; }
    
    // 布局事件
    Event<UIElement, RoutedEventArgs>& SizeChanged() { return sizeChanged_; }
    
    // 右键菜单
    Event<UIElement, ContextMenuEventArgs>& ContextMenuOpening() { return contextMenuOpening_; }
    
protected:
    // 虚函数调用事件链
    void OnPointerPressed(PointerEventArgs& e) override {
        pointerPressed_.Raise(*this, e);
        // 子类可以继续重写做额外处理
    }
    
private:
    Event<UIElement, PointerEventArgs> pointerPressed_;
    Event<UIElement, PointerEventArgs> pointerReleased_;
    // ... 其他事件成员
};
```

**优点：**
- 无需修改控件行为，只添加事件钩子
- 向后兼容：已有的虚函数重写继续工作
- 统一模式：所有交互都可订阅

---

### 4.2 Phase 2: 添加语义化复合事件

在各个控件层添加：

```cpp
// TreeView.h
struct TreeViewItemDoubleClickArgs {
    int visibleIndex;
    const TreeViewRow* row;
    bool handled = false;
};

class TreeView : public Selector<TreeViewRow> {
public:
    Event<TreeView, TreeViewItemDoubleClickArgs>& ItemDoubleClick() { 
        return itemDoubleClick_; 
    }
    
    // 控制默认行为
    void SetDoubleClickTogglesExpansion(bool enable) { 
        doubleClickTogglesExpansion_ = enable; 
    }
    
protected:
    void OnPointerReleased(PointerEventArgs& e) override {
        // ... 双击检测逻辑 ...
        if (isDoubleClick) {
            TreeViewItemDoubleClickArgs args{visIndex, &items_[rowIndex], false};
            itemDoubleClick_.Raise(*this, args);
            
            // 用户未处理 && 节点有子项 && 启用了默认行为
            if (!args.handled && doubleClickTogglesExpansion_ && 
                items_[rowIndex].hasChildren) {
                ToggleRow(rowIndex);
            }
        }
    }
    
private:
    Event<TreeView, TreeViewItemDoubleClickArgs> itemDoubleClick_;
    bool doubleClickTogglesExpansion_ = true;
    DWORD lastClickTime_ = 0;
    int lastClickRow_ = -1;
};
```

**为以下控件添加双击事件：**
- `TreeView::ItemDoubleClick`
- `ListBox::ItemDoubleClick`
- `DataGrid::CellDoubleClick`, `RowDoubleClick`, `ColumnHeaderDoubleClick`
- `TabControl::TabDoubleClick`
- `Image::DoubleClick`

---

### 4.3 Phase 3: 拖拽源端支持

```cpp
// DragDrop.h
struct DragStartEventArgs : RoutedEventArgs {
    float startX, startY;           // 拖拽起点
    IDataObject* dataObject;         // 待填充的数据对象
    DWORD allowedEffects;            // DROPEFFECT_COPY | MOVE | LINK
    bool handled = false;
};

// UIElement.h
class UIElement : public Visual {
public:
    Event<UIElement, DragStartEventArgs>& DragStart() { return dragStart_; }
    Event<UIElement, DragEventArgs>& DragOver() { return dragOver_; }
    Event<UIElement, RoutedEventArgs>& DragEnd() { return dragEnd_; }
    
    // 启用拖拽源
    void SetIsDragSource(bool enable) { isDragSource_ = enable; }
    
protected:
    void OnPointerPressed(PointerEventArgs& e) override;
    void OnPointerMoved(PointerEventArgs& e) override;
    // ... 拖拽逻辑检测：按下 → 移动超过阈值 → 触发 DragStart
    
private:
    bool isDragSource_ = false;
    Event<UIElement, DragStartEventArgs> dragStart_;
    Event<UIElement, DragEventArgs> dragOver_;
    Event<UIElement, RoutedEventArgs> dragEnd_;
};
```

---

## 五、优先级建议

### High Priority（影响大量用户场景）
1. **DoubleClick** → TreeView, ListBox, DataGrid
2. **MouseEnter/MouseLeave** → 所有控件
3. **RightClick** → TreeView, DataGrid, TextArea
4. **GotFocus/LostFocus** → 表单控件（TextBox, ComboBox）

### Medium Priority（常见但有变通方案）
5. **KeyDown/KeyUp** → 自定义快捷键
6. **SizeChanged** → Chart, 自适应布局
7. **DragStart/DragOver/Drop** → TreeView, DataGrid, TabControl

### Low Priority（特殊场景）
8. **PreviewXxx** 系列（事件隧道/冒泡）
9. **MouseWheel** 单独事件（当前只有 OnPointerWheelChanged 虚函数）
10. **IME 事件**（当前只在 TextEditBase 内部使用）

---

## 六、向后兼容性

### 6.1 保留现有虚函数
```cpp
// 现有代码继续工作
class MyTreeView : public TreeView {
protected:
    void OnPointerReleased(PointerEventArgs& e) override {
        // 自定义逻辑
        TreeView::OnPointerReleased(e);  // 调用基类（触发事件）
    }
};
```

### 6.2 事件调用链
```
OnPointerReleased 虚函数
  ├─ 触发 PointerReleased 事件（订阅者执行）
  ├─ 检查 e.handled
  └─ 未处理 → 继续默认行为（例如选中行）
```

### 6.3 迁移路径
1. **Phase 1**: 添加事件，虚函数继续生效
2. **Phase 2**: 文档标记虚函数重写为"legacy pattern"
3. **Phase 3**（可选）: 虚函数标记 `[[deprecated]]`，推荐事件订阅

---

## 七、性能考量

### 7.1 事件开销
- `Event<>` 基于观察者模式，订阅者为空时开销几乎为零
- 鼠标移动事件（MouseMove）需注意：
  - 只在有订阅者时才触发
  - 提供节流选项（例如每 16ms 最多触发一次）

### 7.2 内存占用
- 每个事件成员 = `sizeof(Event<...>)` ≈ 16-24 字节
- 45 个控件 × 平均 5 个新事件 ≈ 每个实例增加 80-120 字节
- 可接受范围（当前 Control 已有 ~200 字节成员）

---

## 八、与 WPF/WinUI 的对齐

### WPF 核心事件对照表

| WPF 事件 | FluentUI 当前状态 | 建议补充 |
|---------|------------------|---------|
| `MouseDoubleClick` | ❌ 无 | ✅ `DoubleClick` |
| `MouseEnter` | ❌ 无（只有虚函数 `OnPointerLeft`） | ✅ `PointerEntered` |
| `MouseLeave` | ❌ 无 | ✅ `PointerExited` |
| `MouseRightButtonDown` | ❌ 无 | ✅ `RightClick` 或检查 `PointerPressed` 中的 `button` |
| `GotFocus` | ❌ 虚函数 `OnFocusChanged` | ✅ `GotFocus` / `LostFocus` |
| `KeyDown` | ❌ 虚函数 `OnKeyDownRouted` | ✅ `KeyDown` |
| `SizeChanged` | ❌ 虚函数 `OnBoundsChanged` | ✅ `SizeChanged` |
| `DragEnter` | ❌ 只有 Drop 端接口 | ✅ 完整拖拽源端支持 |
| `ContextMenuOpening` | ❌ 虚函数 | ✅ 暴露为 Event<> |

---

## 九、实施计划

### Step 1: 基础事件暴露（1-2 周）
- [ ] UIElement 添加 8 个基础事件（Pointer/Key/Focus）
- [ ] 修改现有虚函数为"调用事件 + 默认行为"模式
- [ ] 编写单元测试验证事件链

### Step 2: 控件特定事件（2-3 周）
- [ ] TreeView: `ItemDoubleClick`
- [ ] ListBox: `ItemDoubleClick`
- [ ] DataGrid: `CellDoubleClick`, `RowDoubleClick`
- [ ] TabControl: `TabDoubleClick`
- [ ] 所有控件: `MouseEnter` / `MouseLeave` 触发逻辑

### Step 3: 拖拽完整支持（2 周）
- [ ] 拖拽源端 API 设计
- [ ] TreeView/DataGrid 拖拽重排示例
- [ ] 文档 + 教程

### Step 4: 文档与迁移指南（1 周）
- [ ] API 文档更新
- [ ] WPF → FluentUI 迁移指南
- [ ] 最佳实践示例

**总计时间**: 6-8 周（一个开发者全职）

---

## 十、总结

**当前问题：**
1. ❌ 底层交互事件（鼠标/键盘）只有虚函数，未暴露为 Event<>
2. ❌ 缺少关键用户交互事件（双击、悬停、右键、拖拽源）
3. ❌ 强制用户通过继承实现自定义行为，违反组合优于继承原则
4. ❌ 与 WPF/WinUI 事件模型不兼容，增加移植成本

**建议方案：**
1. ✅ 暴露 UIElement 所有交互虚函数为 Event<>
2. ✅ 为关键控件添加语义化复合事件（ItemDoubleClick 等）
3. ✅ 提供 `handled` 标志让用户阻止默认行为
4. ✅ 保持向后兼容（虚函数继续生效）

**预期收益：**
- 用户代码从**继承模式**转为**组合模式**
- 与 WPF/WinUI 对齐，降低学习曲线
- 支持更复杂的交互场景（多个处理器协作）
- 保持 FluentUI "现代 C++ UI 框架" 的定位

---

**附录 A: 完整事件清单（建议）**

见下一页...
