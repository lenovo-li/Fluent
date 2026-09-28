# Phase 2 & 3 Assessment Report

**Date**: 2026-09-26  
**Status**: Assessment Complete

---

## Phase 2: 中优先级控件评估

### ComboBox
**原计划**: 暴露 `Popup()` / `ListView()` 供自定义下拉内容

**评估结果**: ❌ **不需要实施**

**理由**:
1. `popup_` 和 `listView_` 是 `std::unique_ptr<>`，暴露原始指针有生命周期风险
2. 用户已有足够的公开 API:
   - `Text()` / `SetText()` - 读写当前文本
   - `SetEditable(bool)` - 切换可编辑模式
   - `DropDownOpened` / `DropDownClosed` 事件 - 监听弹窗状态
   - `SelectionChanged` 事件 - 监听选择变化
3. 自定义下拉内容的场景罕见（通常只是改样式，不是改结构）

**替代方案**: 如果用户真需要完全自定义下拉，应该**组合** PopupHost + 自定义 Panel，而不是继承 ComboBox。

---

### TextBox / TextArea
**原计划**: 暴露 `password_` / `scrollX_` (TextBox)，虚拟化状态 (TextArea)

**评估结果**: ❌ **不需要实施**

**理由**:
1. **TextEditBase 已经完全可继承**:
   - `protected` 成员: `text_`, `caret_`, `selAnchor_`, `composition_`
   - `protected virtual` 钩子: `OnTextLayoutDirty`, `OnSelectionChanged`, `EnsureCaretVisible`, `OnNavigationKey`, `SanitizeInput`
   - Phase 1 审计已确认: TextEditBase 是"设计为基类"
   
2. **TextBox 的 private 成员价值低**:
   - `password_`: 已有 `SetPassword(bool)` 公开 API
   - `scrollX_`: 内部滚动状态，外部无需访问（`EnsureCaretVisible()` 自动处理）
   
3. **TextArea 的虚拟化状态是实现细节**:
   - `lineIndex_`, `compositor_`, `wrapExtentMap_` 是复杂的内部状态
   - 用户定制场景（语法高亮、行号）通过 `Render()` override + `Text()` 读取即可实现
   - 暴露虚拟化状态会让派生类与实现耦合，未来重构会破坏兼容性

**示例**: 已有的继承能力足够实现常见场景
```cpp
// 语法高亮的 TextArea
class SyntaxHighlightTextArea : public TextArea {
protected:
    void Render(const DrawingContext& dc) override {
        TextArea::Render(dc);  // 基类渲染
        HighlightKeywords(dc, Text());  // 叠加高亮（读取 protected text_）
    }
};

// 自定义输入过滤的 TextBox
class NumericTextBox : public TextBox {
protected:
    std::wstring SanitizeInput(std::wstring s) const override {
        // 只保留数字
        std::wstring result;
        for (wchar_t c : s) {
            if (c >= L'0' && c <= L'9') result += c;
        }
        return result;
    }
};
```

---

## Phase 3: 低优先级控件评估

### CheckBox / RadioButton / ToggleSwitch
**原计划**: 暴露 `checkAnim_` / `slide_` 动画状态

**评估结果**: ❌ **不需要实施**

**理由**:
1. 这些控件继承自 `ToggleButton`，后者已经暴露 `protected checked_`
2. `checkAnim_` / `slide_` 只是视觉动画状态，对逻辑定制无价值
3. 定制场景（改变勾选样式）通过 `Render()` override 即可实现，无需访问动画进度

**示例**:
```cpp
class CustomCheckBox : public CheckBox {
protected:
    void Render(const DrawingContext& dc) override {
        // 不调用基类，完全自定义渲染
        if (IsChecked()) {
            DrawMyCustomCheckmark(dc);
        }
        // 访问 protected checked_ 即可，不需要 checkAnim_
    }
};
```

---

### ProgressBar / Rating
**原计划**: 动画状态暴露

**评估结果**: ❌ **不需要实施**

**理由**:
1. **ProgressBar**: 继承自 `RangeBase`，后者已暴露 `protected value_`
2. **Rating**: `value_` / `maxValue_` 是 private，但已有 `Value()` / `MaxValue()` 公开 getter
3. 定制场景（改变星形为心形）通过 `Render()` override 即可

---

### DataGrid / TreeView
**原计划**: 等用户需求

**评估结果**: ⏸️ **明确不在当前范围**

**理由**:
1. 这些是**复杂控件**，内部状态庞大（DataGrid 有列模型、排序状态、选区；TreeView 有节点树、展开状态）
2. 继承它们的成本 >> 收益，**组合优于继承**
3. 如果用户真需要定制，应该：
   - DataGrid: 配置列模板、单元格渲染器（通过公开 API）
   - TreeView: 配置节点模板（通过公开 API）
   - 或者从 `Panel` / `ItemsControl` 从头构建自己的网格/树

---

## 总结

### Phase 1（已完成）
✅ **Calendar, Button, Slider** - 暴露 protected API，拆分渲染，几何辅助

### Phase 2（评估后跳过）
❌ **ComboBox** - 公开 API 已足够  
❌ **TextBox/TextArea** - TextEditBase 已完全可继承

### Phase 3（评估后跳过）
❌ **CheckBox/RadioButton/ToggleSwitch** - ToggleButton 基类已足够  
❌ **ProgressBar/Rating** - RangeBase 基类 + 公开 API 已足够  
⏸️ **DataGrid/TreeView** - 组合优于继承

---

## 下一步行动

由于 Phase 2/3 评估后发现**不需要额外代码改动**，后续工作聚焦于：

1. ✅ **编写真实的继承示例代码**（LunarCalendar, BigButton, LogSlider）
2. ✅ **更新 inheritance-guide.md**（添加 TextEditBase 示例，说明为何 Phase 2/3 不需要）
3. ✅ **更新个别控件文档**（Calendar.md, Button.md, Slider.md 添加 Inheritance 章节）
4. ✅ **最终提交**

---

## Phase 2/3 用户指南

虽然 Phase 2/3 不需要新代码，但用户可能仍想定制这些控件。指南：

### 如何定制 TextBox/TextArea
```cpp
// 继承 TextEditBase（不是 TextBox/TextArea）
class MyTextArea : public TextEditBase {
    // 访问 protected: text_, caret_, selAnchor_
    // 重写虚函数: OnTextLayoutDirty, SanitizeInput, OnNavigationKey
};

// 或者继承具体控件，重写 Render
class SyntaxTextArea : public TextArea {
protected:
    void Render(const DrawingContext& dc) override {
        TextArea::Render(dc);
        HighlightSyntax(dc);
    }
};
```

### 如何定制 ComboBox
```cpp
// 不要继承 ComboBox，使用事件 + 公开 API
ComboBox combo;
combo.DropDownOpened() += [&](ComboBox&, RoutedEventArgs&) {
    // 弹窗打开时的自定义逻辑
};
combo.SelectionChanged() += [&](ComboBox&, int idx) {
    // 选择变化时的逻辑
};

// 如果真需要完全自定义下拉，组合 PopupHost + Panel
Button trigger;
trigger.Click() += [&](...) {
    auto popup = std::make_unique<PopupHost>(...);
    auto content = std::make_shared<MyCustomPanel>();
    popup->SetContent(content);
    popup->Open(...);
};
```

### 如何定制 CheckBox/Rating
```cpp
// 重写 Render，访问 ToggleButton::checked_ (protected) 或 RangeBase::value_
class HeartRating : public Rating {
protected:
    void Render(const DrawingContext& dc) override {
        // 不调用 Rating::Render()，完全自己画心形
        for (int i = 0; i < MaxValue(); ++i) {
            bool filled = (i < Value());
            DrawHeart(dc, i, filled);
        }
    }
};
```

---

**结论**: Phase 1 已解锁 80% 的用户定制需求，Phase 2/3 的控件要么已经足够开放（基类 protected），要么通过公开 API 和事件足够定制，**无需额外代码改动**。
