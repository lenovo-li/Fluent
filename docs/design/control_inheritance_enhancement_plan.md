# Control Inheritance Enhancement Plan

**创建**: 2026-09-26  
**状态**: Draft  
**目标**: 在不破坏封装的前提下，增强具体控件的可继承性

---

## 背景与动机

### 当前状态

FluentUI 采用**两层继承架构**：

1. **抽象基类**（ButtonBase、TextEditBase、RangeBase、Panel 等）  
   - `protected` 状态成员  
   - `protected virtual` 钩子方法  
   - ✅ 完全可继承，设计良好

2. **具体控件**（Button、Calendar、Slider、ComboBox 等）  
   - 关键状态 `private`（如 `Button::kind_`、`Calendar::cellW_`）  
   - 虚函数 `public override`（如 `Render`、`Measure`）  
   - ⚠️ 只能重写渲染，无法访问内部状态深度定制

### 用户需求

用户希望通过**继承具体控件**实现定制，而不是回调接口：

- **LunarCalendar** : 继承 Calendar，添加农历显示
- **BigButton** : 继承 Button，双行文本 + 图标 + 64dp 高度
- **LogSlider** : 继承 Slider，线性拖动 → 对数值输出

当前这些场景**部分受阻**：无法访问 `cellW_`/`hoverCell_`、`kind_`/`tintOpacity_`、`step_`/`dragging_` 等状态。

### 行业对比

主流框架的策略（已验证）：

| 框架 | 具体控件状态可见性 | 策略 |
|------|------------------|------|
| **Qt** | ❌ private（d-pointer 彻底隐藏） | 只能继承抽象基类 |
| **WPF** | ⚠️ 模板驱动（不鼓励继承具体控件） | 改模板而非继承 |
| **Android View** | ✅ protected 成员丰富 | 鼓励继承（但维护成本高） |

**结论**：具体控件封闭实现是行业共识。FluentUI 的现状**已是最佳实践**，只需微调。

---

## 设计原则

### ✅ 保持的（不改）

1. **关键状态仍然 `private`**  
   - `Button::kind_`、`Calendar::cellW_`、`Slider::step_` 等保持 `private`  
   - 理由：维护不变量（如 `kind_` 和 `tintOpacity_` 必须同步），避免派生类破坏

2. **两层继承架构不变**  
   - 抽象基类（ButtonBase/RangeBase/Panel）仍是深度定制的主要扩展点  
   - 具体控件是"最终产品"，不是"可插拔引擎"

3. **不添加回调接口**  
   - 不引入 `SetCellRenderer`、`SetCellDecorator` 等 `std::function<>` 成员  
   - 理由：库体积膨胀（每个钩子 +12 字节），用户需求可通过继承满足

### 🔧 改进的（增强）

1. **暴露 `protected` 辅助方法**  
   - 新增：`Calendar::CellRect(int)` 让派生类获取单元格位置  
   - 新增：`Button::RenderBackground()` / `RenderContent()` 拆分渲染逻辑  
   - 理由：派生类通过方法间接访问状态，基类改算法不破坏派生类

2. **暴露只读访问器（可选）**  
   - 新增：`Calendar::CellWidth() const` / `CellHeight() const`  
   - 新增：`Button::CurrentKind() const`  
   - 理由：允许派生类**读取**状态用于条件分支，但不能修改

3. **拆分大型 `Render()` 为小方法**  
   - 当前：`Calendar::Render()` 400+ 行，一锅炖  
   - 改为：`RenderNavBar()` / `RenderWeekdayHeader()` / `RenderCell()` 各自 `protected virtual`  
   - 理由：派生类可以选择性复用部分逻辑

---

## 分阶段实施计划

### Phase 0: 修复已知 Bug（立即）

**范围**：无争议的析构函数问题

| 文件 | 当前问题 | 修复 |
|------|---------|------|
| ~~`Canvas.h`~~ | ~~public non-virtual 析构~~ | **误报**：基类 `Visual::~Visual()` 是 virtual，派生类自动 virtual |
| ~~`DataGrid.h`~~ | ~~显式 non-virtual 析构~~ | **误报**：同上 |
| ~~`MenuBar.h`~~ | ~~显式 non-virtual 析构~~ | **误报**：同上 |

**结论**：Phase 0 不需要任何改动。子代理的析构函数"bug"是误判。

---

### Phase 1: 高优先级控件（用户明确需求）

**范围**：用户已提出定制场景的控件

#### 1.1 Calendar（农历、节假日标记）

**当前限制**：
- ❌ 无法访问 `cellW_`、`cellH_`（硬编码布局时会出错）
- ❌ 无法访问 `hoverCell_`（无法基于 hover 状态添加交互）
- ❌ `Render()` 400+ 行，无法部分复用

**增强计划**：
```cpp
// Calendar.h
class Calendar : public Control {
protected:
    // 新增：只读访问器（派生类可读，不可写）
    float CellWidth() const { return cellW_; }
    float CellHeight() const { return cellH_; }
    int HoverCell() const { return hoverCell_; }
    
    // 新增：辅助方法（派生类无需重新计算）
    RectDip CellRect(int cellIndex) const;
    RectDip NavBarRect() const;
    RectDip WeekdayHeaderRect() const;
    
    // 新增：拆分渲染逻辑（派生类可选择性复用）
    virtual void RenderNavBar(const DrawingContext& dc);
    virtual void RenderWeekdayHeader(const DrawingContext& dc);
    virtual void RenderCell(const DrawingContext& dc, int cellIndex,
                           int year, int month, int day,
                           bool selected, bool today, bool enabled);
    
    // 保持：CellDate() 已是 public，很好
    
private:
    // 保持：状态仍然 private
    float cellW_ = 36.0f, cellH_ = 32.0f;
    int hoverCell_ = -1;
    // ...
};
```

**验证示例**：`LunarCalendar` 能够：
- ✅ 调用 `CellRect(i)` 获取单元格位置
- ✅ 重写 `RenderCell()` 在日期下方绘制农历
- ✅ 读取 `HoverCell()` 添加 hover 时的农历提示

**不能做的**（设计约束）：
- ❌ 修改 `cellW_`/`cellH_` 改变布局（这需要重写 `Measure`，已经可以）
- ❌ 访问 `hoverFade_` 动画状态（这是实现细节）

---

#### 1.2 Button（大按钮、图标按钮）

**当前限制**：
- ❌ 无法访问 `kind_`（不知道是 Accent/Standard/Subtle）
- ❌ 无法访问 `tintOpacity_`（无法同步 hover/press 高亮）
- ❌ `Render()` 一锅炖，无法只画背景

**增强计划**：
```cpp
// Button.h
class Button : public ButtonBase {
protected:
    // 新增：只读访问器
    Kind CurrentKind() const { return kind_; }
    float TintOpacity() const { return tintOpacity_; }
    
    // 新增：拆分渲染
    virtual void RenderBackground(const DrawingContext& dc);
    virtual void RenderBorder(const DrawingContext& dc);
    virtual void RenderContent(const DrawingContext& dc);
    
private:
    Kind kind_ = Kind::Standard;
    AnimatedValue tintOpacity_{0.0f};
    // ...
};
```

**验证示例**：`BigButton` 能够：
- ✅ 重写 `Measure()` 返回 64dp 高度
- ✅ 调用 `RenderBackground(dc)` 复用基类背景
- ✅ 读取 `TintOpacity()` 同步高亮到自定义图标
- ✅ 重写 `RenderContent()` 绘制双行文本 + 图标

---

#### 1.3 Slider（对数滑块、步进定制）

**当前限制**：
- ❌ 无法访问 `step_`（无法基于现有步进调整）
- ❌ 无法访问 `dragging_`（无法区分拖动/键盘/程序设值）

**增强计划**：
```cpp
// Slider.h
class Slider : public RangeBase {
protected:
    // 新增：只读访问器
    double Step() const { return step_; }
    bool IsDragging() const { return dragging_; }
    
    // 保持：CoerceValue() 已是 protected virtual，很好
    
private:
    double step_ = 1.0;
    bool dragging_ = false;
    // ...
};
```

**验证示例**：`LogSlider` 能够：
- ✅ 重写 `CoerceValue()` 实现对数步进
- ✅ 读取 `IsDragging()` 区分输入来源（拖动时实时反馈，键盘时延迟提交）

---

### Phase 2: 中优先级控件（常见定制场景）

**范围**：文档里常见的自定义需求

#### 2.1 ComboBox（自定义下拉内容）

**当前限制**：
- ❌ 无法访问 `popup_`、`listView_`（无法替换下拉逻辑）

**增强计划**：
```cpp
// ComboBox.h
class ComboBox : public Selector<std::wstring> {
protected:
    // 新增：只读访问器
    PopupHost* Popup() const { return popup_.get(); }
    UIElement* ListView() const { return listView_.get(); }
    
private:
    std::unique_ptr<PopupHost> popup_;
    std::shared_ptr<UIElement> listView_;
    // ...
};
```

**争议点**：暴露 `popup_` 和 `listView_` 的指针可能让派生类破坏所有权。需评估风险。

---

#### 2.2 TextBox / TextArea（自定义输入过滤、高亮）

**当前限制**：
- ❌ TextBox 的 `password_`、`scrollX_` private
- ❌ TextArea 的虚拟化状态 private

**增强计划**：
```cpp
// TextBox.h
class TextBox : public TextEditBase {
protected:
    // 新增：只读访问器
    bool IsPasswordMode() const { return password_; }
    float ScrollX() const { return scrollX_; }
    
    // 保持：SanitizeInput() 已是 protected virtual
    
private:
    bool password_ = false;
    float scrollX_ = 0.0f;
    // ...
};
```

---

### Phase 3: 低优先级控件（罕见定制）

**范围**：大多数用户直接用，极少继承的控件

- **CheckBox** / **RadioButton** / **ToggleSwitch**：动画状态暴露价值低
- **ProgressBar** / **Rating**：通常只改颜色（已通过 Control 的属性支持）
- **DataGrid** / **TreeView**：复杂控件，继承成本高，建议组合而非继承

**策略**：Phase 3 **暂不实施**，等用户明确需求再考虑。

---

## 文档计划

### 新增文档

1. **`docs/controls/inheritance-guide.md`**  
   - 哪些控件可以继承、哪些不该继承  
   - 每个可继承控件的扩展点说明  
   - 3-5 个完整的继承示例（LunarCalendar、BigButton、LogSlider）

2. **每个控件的文档增补**（如 `docs/controls/Calendar.md`）  
   - 新增"Inheritance"章节  
   - 列出 protected 方法和只读访问器  
   - 说明设计约束（如"cellW_/cellH_ 保持 private 的原因"）

### 更新现有文档

- **`docs/api-surface.md`**：补充继承约定（protected 方法命名、只读访问器模式）

---

## 验证计划

### 每个控件完成后的验证

1. **编译验证**：Debug + Release 干净编译
2. **测试验证**：`FluentUITests.exe` 全绿（增强不破坏现有行为）
3. **继承示例验证**：  
   - 写一个实际的派生类（如 `LunarCalendar`）  
   - 验证能做什么、不能做什么符合设计预期  
   - 将示例代码放入 `examples/inheritance/` 或文档

### 不验证的（需用户确认）

- ❌ 视觉效果（农历显示是否好看、BigButton 布局是否美观）  
- ❌ 性能（派生类自己的实现可能慢，这是用户责任）

---

## 风险与权衡

### 风险

1. **维护负担增加**  
   - 每个 protected 方法都成为 API 契约，改动需考虑派生类兼容性  
   - **缓解**：只为明确需求的控件添加，Phase 3 不实施

2. **用户可能误用**  
   - 派生类可能假设 `cellW_` 永远是 36，基类改算法后破坏  
   - **缓解**：文档明确说明"使用 `CellRect()` 而不是假设尺寸"

3. **暴露内部实现**  
   - `CurrentKind()`、`HoverCell()` 让派生类依赖内部状态表示  
   - **缓解**：只暴露逻辑必需的，避免暴露纯实现细节（如 `hoverFade_`）

### 权衡

| 选择 | 优点 | 缺点 |
|------|------|------|
| **暴露只读访问器** | 派生类可读状态做条件分支 | 派生类依赖内部表示 |
| **不暴露，只提供方法** | 基类可改实现不破坏派生类 | 派生类能力受限 |

**决策**：Phase 1 **两者都做**：
- 高频需求的状态暴露只读访问器（如 `CellWidth()`、`CurrentKind()`）
- 低频需求的用方法间接提供（如 `CellRect()`）

---

## 替代方案（已拒绝）

### 方案 A：全面改 private → protected

**描述**：将 `cellW_`、`kind_`、`step_` 等全部改为 protected

**拒绝理由**：
- ❌ 维护负担爆炸（每次改字段都要考虑派生类）
- ❌ 破坏封装（派生类可能破坏不变量）
- ❌ 违背行业共识（Qt/WPF 都不这样做）

### 方案 B：回调接口

**描述**：添加 `SetCellDecorator(std::function<...>)` 等回调

**拒绝理由**：
- ❌ 库体积膨胀（每个钩子 +12 字节）
- ❌ 灵活性不如继承（只能在预定义点插入代码）
- ❌ 用户已明确要求"继承而非回调"

### 方案 C：模板驱动（WPF 模式）

**描述**：分离逻辑和视觉，用户替换"模板"

**拒绝理由**：
- ❌ C++ 没有 XAML，手写模板系统工程量巨大
- ❌ 与现有架构不兼容（已有 4 年代码库）

---

## 时间线估算

| 阶段 | 工作量 | 时间 |
|------|--------|------|
| Phase 0: 修复 Bug | ~~0 控件~~ | ~~0 天~~（无 bug） |
| Phase 1: 高优先级 | 3 控件 × 2 小时 | 1 工作日 |
| Phase 2: 中优先级 | 3 控件 × 1 小时 | 0.5 工作日 |
| 文档编写 | 主文档 + 6 个控件文档 | 1 工作日 |
| 验证与示例 | 3 个示例 + 测试 | 0.5 工作日 |
| **总计** | | **3 工作日** |

**注**：实际时间取决于用户反馈和迭代轮次。

---

## 下一步行动

1. ✅ **计划文档评审**（当前文档）  
   - 用户确认方向和优先级  
   - 调整 Phase 1/2/3 的范围

2. ⏭ **编写任务文档**  
   - `control_inheritance_tasks.md`：Phase 1 的 3 个控件的详细改动清单

3. ⏭ **实施 Phase 1**  
   - 按任务文档逐个控件实施  
   - 每个控件完成后立即验证

4. ⏭ **编写继承指南**  
   - `docs/controls/inheritance-guide.md`  
   - 每个改动的控件补充文档

5. ⏭ **用户验证**  
   - 真实硬件验证视觉效果  
   - 根据反馈调整 Phase 2/3 范围
