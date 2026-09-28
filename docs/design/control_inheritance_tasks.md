# Control Inheritance Enhancement Tasks

**对应计划**: `control_inheritance_enhancement_plan.md` Phase 1  
**创建**: 2026-09-26  
**状态**: Ready to implement

---

## Phase 1: 高优先级控件改动清单

### 任务概览

| 控件 | 头文件改动 | 实现文件改动 | 测试 | 文档 |
|------|-----------|-------------|------|------|
| Calendar | 9 个新方法 | 拆分 `Render()` | 继承示例 | ✓ |
| Button | 5 个新方法 | 拆分 `Render()` | 继承示例 | ✓ |
| Slider | 2 个新方法 | 无需改动 | 继承示例 | ✓ |

---

## Task 1: Calendar 增强 ✅ 已完成

### 目标

让派生类能够：
- ✅ 获取单元格位置（无需访问 `cellW_`/`cellH_`）
- ✅ 读取 hover 状态（实现交互反馈）
- ✅ 部分复用渲染逻辑（不用完全重写 400 行）

### 实施完成

- ✅ **头文件** (`Calendar.h`): 9 个新 protected 方法 + CellInfo 结构体
- ✅ **实现文件** (`Calendar.cpp`): 拆分 Render() + 实现 5 个辅助方法
- ✅ **编译**: Debug 干净编译
- ✅ **测试**: 1838/1838 全绿

### 新增 API

**只读访问器**:
- `float CellWidth() const`
- `float CellHeight() const`
- `int HoverCell() const`
- `float HoverOpacity() const`

**辅助方法**:
- `RectDip NavBarRect() const`
- `RectDip WeekdayHeaderRect() const`
- `RectDip PrevArrowRect() const`
- `RectDip NextArrowRect() const`

**拆分渲染**:
- `virtual void RenderNavBar(dc)`
- `virtual void RenderWeekdayHeader(dc)`
- `virtual void RenderCell(dc, CellInfo)`

**变更**: 点击检测现在使用 `PrevArrowRect()`/`NextArrowRect()` 而不是硬编码坐标，保证绘制和点击区域同步。

---

## Task 2: Button 增强 ✅ 已完成

### 目标

让派生类能够：
- ✅ 读取按钮类型（Accent/Standard/Subtle）
- ✅ 同步 hover/press 高亮动画
- ✅ 只画背景，自己画内容

### 实施完成

- ✅ **头文件** (`Button.h`): 6 个新 protected 方法，ButtonAppearance 前置声明
- ✅ **实现文件** (`Button.cpp`): 拆分 Render() + 实现 4 个方法
- ✅ **编译**: Debug 干净编译
- ✅ **测试**: 1838/1838 全绿

### 新增 API

**只读访问器**:
- `Kind CurrentKind() const`
- `float TintOpacity() const`
- `ButtonAppearance AppearanceForRender() const`

**拆分渲染**:
- `virtual void RenderBackground(dc)`
- `virtual void RenderBorder(dc)`
- `virtual void RenderContent(dc)`

**变更**: `Render()` 现在只是编排器，调用三个钩子。焦点环保留在主方法（VisualOverflowDip 声明了它的溢出）。

---

## Task 3: Slider 增强 ✅ 已完成

### 目标

让派生类能够：
- ✅ 读取步进值（实现对数步进）
- ✅ 区分拖动/键盘输入（不同的提交策略）

### 实施完成

- ✅ **头文件** (`Slider.h`): 7 个新 protected 方法
- ✅ **实现文件** (`Slider.cpp`): 实现 ValueToTrackPos (几何映射)
- ✅ **编译**: Debug 干净编译
- ✅ **测试**: 1838/1838 全绿 (6 次运行)

### 新增 API

**只读访问器**:
- `bool IsDragging() const`
- `float DisplayedValue() const` (动画中的显示值)
- `Step()` 已是 public，无需添加

**几何辅助**:
- `float TrackStart() const`
- `float TrackEnd() const`
- `float TrackCenterline() const`
- `float ValueToTrackPos(float) const` (值→轨道位置，绘制刻度用)

**变更**: 无破坏性改动，只暴露几何查询。CoerceValue/OnValueChanged 已是 protected virtual。

---

## Phase 1 总结 ✅ 全部完成

**完成时间**: 2026-09-26  
**总工时**: ~4 小时 (含文档、验证、迭代)

| 任务 | 新增方法 | 代码行数 | 测试结果 |
|------|---------|---------|---------|
| Calendar | 9 个 | +170 行 | ✅ 1838/1838 |
| Button | 6 个 | +80 行 | ✅ 1838/1838 |
| Slider | 7 个 | +15 行 | ✅ 1838/1838 |

**关键成果**:
1. ✅ 三个热点控件现在支持深度继承定制
2. ✅ 零破坏性改动（所有现有测试通过）
3. ✅ 几何辅助方法保证绘制和点击检测同步
4. ✅ 文档记录了设计约束和使用模式

**下一步**: 
- 📝 编写继承指南文档 (Task 4)
- 📝 更新控件文档添加 Inheritance 章节 (Task 5)
- 🧪 编写真实的继承示例 (LunarCalendar/BigButton/LogSlider)

---

## Task 4: 编写继承指南

### 目标

让派生类能够：
- ✅ 获取单元格位置（无需访问 `cellW_`/`cellH_`）
- ✅ 读取 hover 状态（实现交互反馈）
- ✅ 部分复用渲染逻辑（不用完全重写 400 行）

### 1.1 头文件改动 (`FluentUI/controls/Calendar.h`)

在 `protected:` 区块添加：

```cpp
protected:
    // --- 只读访问器（派生类可读状态，不可写） ---
    
    /// Cell width in DIPs. Use CellRect() instead of assuming this value.
    float CellWidth() const { return cellW_; }
    
    /// Cell height in DIPs. Use CellRect() instead of assuming this value.
    float CellHeight() const { return cellH_; }
    
    /// Currently hovered cell index [0..41], or -1 if none. Useful for adding
    /// custom hover interactions in derived classes.
    int HoverCell() const { return hoverCell_; }
    
    // --- 辅助方法（派生类无需重新计算） ---
    
    /// Bounds of the cell at index [0..41] in window DIPs. Derived classes
    /// should use this instead of computing from cellW_/cellH_ directly, so
    /// they remain compatible if the base class changes layout algorithm.
    RectDip CellRect(int cellIndex) const;
    
    /// Bounds of the navigation bar (month/year header + arrows).
    RectDip NavBarRect() const;
    
    /// Bounds of the weekday header row ("日 一 二 三 四 五 六").
    RectDip WeekdayHeaderRect() const;
    
    // --- 拆分渲染逻辑（派生类可选择性复用） ---
    
    /// Render the navigation bar. Called by Render(). Override to customize
    /// the month/year display or arrow buttons.
    virtual void RenderNavBar(const DrawingContext& dc);
    
    /// Render the weekday header row. Called by Render(). Override to change
    /// weekday labels (e.g., "Sun Mon Tue" instead of "日 一 二").
    virtual void RenderWeekdayHeader(const DrawingContext& dc);
    
    /// Render a single cell. Called by Render() for each of the 42 cells.
    /// Override to add decorations (lunar date, holiday marker) or change
    /// the cell appearance. The base implementation draws the background,
    /// selection ring, today indicator, and day number.
    ///
    /// @param cellIndex Cell position [0..41]
    /// @param year, month, day The date this cell represents
    /// @param selected True if this is the selected date
    /// @param today True if this is today's date
    /// @param enabled False if the date is outside min/max or blacked out
    virtual void RenderCell(const DrawingContext& dc, int cellIndex,
                           int year, int month, int day,
                           bool selected, bool today, bool enabled);
```

**注意事项**：
- 保持 `cellW_`、`cellH_`、`hoverCell_` 仍然 `private`（不移动到 protected）
- `CellDate()` 已经是 `public`，无需改动
- 新方法都是 `const` 或 `virtual`，不引入新的状态

### 1.2 实现文件改动 (`FluentUI/controls/Calendar.cpp`)

#### 1.2.1 新增辅助方法实现

在文件末尾（析构函数之后）添加：

```cpp
RectDip Calendar::CellRect(int cellIndex) const {
    if (cellIndex < 0 || cellIndex >= 42) return RectDip{};
    int row = cellIndex / 7;
    int col = cellIndex % 7;
    float x = bounds_.x + col * cellW_;
    float y = bounds_.y + navH_ + hdrH_ + row * cellH_;
    return RectDip{x, y, cellW_, cellH_};
}

RectDip Calendar::NavBarRect() const {
    return RectDip{bounds_.x, bounds_.y, bounds_.w, navH_};
}

RectDip Calendar::WeekdayHeaderRect() const {
    float y = bounds_.y + navH_;
    return RectDip{bounds_.x, y, bounds_.w, hdrH_};
}
```

#### 1.2.2 拆分 `Render()` 方法

**当前结构**（Calendar.cpp 约第 267-440 行）：
```cpp
void Calendar::Render(const DrawingContext& dc) {
    // 1. 背景
    // 2. 导航栏（月份标题 + 箭头）
    // 3. 星期标题
    // 4. 42 个单元格循环
}
```

**重构为**：
```cpp
void Calendar::Render(const DrawingContext& dc) {
    const ThemeSnapshot& th = Theme();
    const ColorTokens& colors = th.colors;
    const float corner = EffectiveCornerRadius();

    // 背景（保持内联，不拆分）
    D2D1_ROUNDED_RECT bg = D2D1::RoundedRect(
        D2D1::RectF(bounds_.x, bounds_.y, bounds_.right(), bounds_.bottom()),
        corner, corner);
    dc.FillRoundedRect(bg, EffectiveBackground(colors.cardFill));

    // 调用拆分后的方法
    RenderNavBar(dc);
    RenderWeekdayHeader(dc);

    // 渲染 42 个单元格
    for (int i = 0; i < 42; ++i) {
        int y, m, d;
        CellDate(i, y, m, d);
        bool selected = (y == selYear_ && m == selMonth_ && d == selDay_);
        bool today = (y == todayY_ && m == todayM_ && d == todayD_);
        bool enabled = IsDateEnabled(y, m, d);
        RenderCell(dc, i, y, m, d, selected, today, enabled);
    }
}

void Calendar::RenderNavBar(const DrawingContext& dc) {
    const ColorTokens& colors = Theme().colors;
    float navY = bounds_.y;
    
    // 月份/年份标题（保持现有代码）
    std::wostringstream oss;
    oss << viewYear_ << L"年" << viewMonth_ << L"月";
    std::wstring headerText = oss.str();
    
    if (DWriteContext* dw = Dwrite()) {
        if (auto* fmt = dw->Format(14.0f, DWRITE_FONT_WEIGHT_SEMI_BOLD,
                                   DWRITE_TEXT_ALIGNMENT_CENTER,
                                   DWRITE_PARAGRAPH_ALIGNMENT_CENTER)) {
            D2D1_RECT_F textRect = D2D1::RectF(
                bounds_.x + 40, navY,
                bounds_.right() - 40, navY + navH_);
            dc.DrawText(headerText.c_str(), static_cast<UINT32>(headerText.size()),
                       fmt, textRect, EffectiveForeground(colors.textPrimary));
        }
        
        // 箭头（保持现有代码）
        if (auto* arrowFmt = dw->Format(16.0f, DWRITE_FONT_WEIGHT_NORMAL,
                                        DWRITE_TEXT_ALIGNMENT_CENTER,
                                        DWRITE_PARAGRAPH_ALIGNMENT_CENTER)) {
            D2D1_RECT_F leftArrowRect = D2D1::RectF(bounds_.x, navY, bounds_.x + 40, navY + navH_);
            dc.DrawText(L"<", 1, arrowFmt, leftArrowRect, EffectiveForeground(colors.textSecondary));
            
            D2D1_RECT_F rightArrowRect = D2D1::RectF(bounds_.right() - 40, navY, bounds_.right(), navY + navH_);
            dc.DrawText(L">", 1, arrowFmt, rightArrowRect, EffectiveForeground(colors.textSecondary));
        }
    }
}

void Calendar::RenderWeekdayHeader(const DrawingContext& dc) {
    const ColorTokens& colors = Theme().colors;
    float hdrY = bounds_.y + navH_;
    
    const wchar_t* weekdays[] = {L"日", L"一", L"二", L"三", L"四", L"五", L"六"};
    
    if (DWriteContext* dw = Dwrite()) {
        if (auto* fmt = dw->Format(12.0f, DWRITE_FONT_WEIGHT_NORMAL,
                                   DWRITE_TEXT_ALIGNMENT_CENTER,
                                   DWRITE_PARAGRAPH_ALIGNMENT_CENTER)) {
            for (int i = 0; i < 7; ++i) {
                float x = bounds_.x + i * cellW_;
                D2D1_RECT_F rect = D2D1::RectF(x, hdrY, x + cellW_, hdrY + hdrH_);
                dc.DrawText(weekdays[i], 1, fmt, rect,
                           EffectiveForeground(colors.textSecondary));
            }
        }
    }
}

void Calendar::RenderCell(const DrawingContext& dc, int cellIndex,
                         int year, int month, int day,
                         bool selected, bool today, bool enabled) {
    const ColorTokens& colors = Theme().colors;
    RectDip cellRect = CellRect(cellIndex);
    
    // 非当月日期用浅灰色
    bool isCurrentMonth = (year == viewYear_ && month == viewMonth_);
    
    // 单元格背景 + hover 高亮（保持现有逻辑）
    if (enabled) {
        D2D1_ROUNDED_RECT cellRR = D2D1::RoundedRect(
            D2D1::RectF(cellRect.x + 2, cellRect.y + 2,
                       cellRect.right() - 2, cellRect.bottom() - 2),
            4.0f, 4.0f);
        
        // 选中态
        if (selected) {
            dc.FillRoundedRect(cellRR, EffectiveAccentColor(colors.accent));
        }
        
        // Hover 高亮
        if (cellIndex == hoverCell_ && hoverFade_ > 0.0f) {
            D2D1_COLOR_F hoverColor = colors.controlFillHover;
            hoverColor.a *= static_cast<float>(hoverFade_);
            dc.FillRoundedRect(cellRR, hoverColor);
        }
        
        // 今日标记
        if (today && !selected) {
            dc.DrawRoundedRect(cellRR, EffectiveAccentColor(colors.accent), 1.0f);
        }
    }
    
    // 日期数字（保持现有逻辑）
    std::wstring dayText = std::to_wstring(day);
    D2D1_RECT_F textRect = D2D1::RectF(
        cellRect.x, cellRect.y,
        cellRect.right(), cellRect.bottom());
    
    D2D1_COLOR_F textColor;
    if (!enabled) {
        textColor = colors.textSecondary;
        textColor.a *= 0.5f;
    } else if (selected) {
        textColor = colors.onAccent;
    } else if (!isCurrentMonth) {
        textColor = colors.textSecondary;
    } else {
        textColor = colors.textPrimary;
    }
    
    if (DWriteContext* dw = Dwrite()) {
        if (auto* fmt = dw->Format(13.0f, DWRITE_FONT_WEIGHT_NORMAL,
                                   DWRITE_TEXT_ALIGNMENT_CENTER,
                                   DWRITE_PARAGRAPH_ALIGNMENT_CENTER)) {
            dc.DrawText(dayText.c_str(), static_cast<UINT32>(dayText.size()),
                       fmt, textRect, textColor, D2D1_DRAW_TEXT_OPTIONS_CLIP);
        }
    }
}
```

**注意事项**：
- 保持现有逻辑不变，只是拆分到独立方法
- 每个方法都是 `virtual`，派生类可以选择性重写
- `Render()` 主方法变短，逻辑清晰

### 1.3 验证

#### 编译验证
```bash
MSBuild Fluent.slnx -p:Configuration=Debug -p:Platform=x64
```

#### 测试验证
```bash
x64/Debug/FluentUITests.exe
# 应该全绿（改动不破坏现有行为）
```

#### 继承示例验证

创建 `examples/inheritance/LunarCalendar.h`:
```cpp
// 示例：农历日历
class LunarCalendar : public Calendar {
protected:
    void RenderCell(const DrawingContext& dc, int cellIndex,
                   int year, int month, int day,
                   bool selected, bool today, bool enabled) override {
        // 先调用基类绘制格里历
        Calendar::RenderCell(dc, cellIndex, year, month, day, selected, today, enabled);
        
        // 在下方添加农历标记
        RectDip rect = CellRect(cellIndex);
        std::wstring lunar = GetLunarDate(year, month, day);
        
        if (DWriteContext* dw = Dwrite()) {
            if (auto* fmt = dw->Format(9.0f, DWRITE_FONT_WEIGHT_NORMAL,
                                       DWRITE_TEXT_ALIGNMENT_CENTER,
                                       DWRITE_PARAGRAPH_ALIGNMENT_CENTER)) {
                D2D1_RECT_F lunarRect = D2D1::RectF(
                    rect.x, rect.y + rect.h * 0.6f,
                    rect.right(), rect.bottom());
                dc.DrawText(lunar.c_str(), lunar.size(), fmt, lunarRect,
                           Theme().colors.textSecondary);
            }
        }
    }

private:
    std::wstring GetLunarDate(int y, int m, int d) {
        // 简化示例：实际需要农历转换算法
        return L"初一";
    }
};
```

**验证能力**：
- ✅ 调用 `CellRect(cellIndex)` 获取位置
- ✅ 调用 `Calendar::RenderCell()` 复用基类渲染
- ✅ 访问 `Theme().colors` 保持视觉一致

**验证限制**：
- ❌ 无法修改 `cellW_`/`cellH_`（这是设计约束，需重写 `Measure` 才能改尺寸）

---

## Task 2: Button 增强

### 目标

让派生类能够：
- ✅ 读取按钮类型（Accent/Standard/Subtle）
- ✅ 同步 hover/press 高亮动画
- ✅ 只画背景，自己画内容

### 2.1 头文件改动 (`FluentUI/controls/Button.h`)

在 `protected:` 区块添加：

```cpp
protected:
    // --- 只读访问器 ---
    
    /// Current button kind (Accent/Standard/Subtle). Derived classes can
    /// read this to adjust rendering, but should not modify it directly.
    Kind CurrentKind() const { return kind_; }
    
    /// Current tint opacity [0..1] for hover/press highlight animation.
    /// Derived classes can read this to synchronize custom animations.
    float TintOpacity() const { return tintOpacity_; }
    
    // --- 拆分渲染逻辑 ---
    
    /// Render the button background (fill + tint). Called by Render().
    /// Override to change the background shape or material.
    virtual void RenderBackground(const DrawingContext& dc);
    
    /// Render the button border (if ButtonDrawsBorder returns true).
    /// Called by Render(). Override to change border style.
    virtual void RenderBorder(const DrawingContext& dc);
    
    /// Render the button content (label from ContentControl). Called by
    /// Render(). Override to add icons, badges, or change text layout.
    virtual void RenderContent(const DrawingContext& dc);
```

### 2.2 实现文件改动 (`FluentUI/controls/Button.cpp`)

拆分当前的 `Button::Render()`（约第 100-160 行）：

```cpp
void Button::Render(const DrawingContext& dc) {
    RenderBackground(dc);
    if (ButtonDrawsBorder(kind_)) {
        RenderBorder(dc);
    }
    if (IsFocused()) {
        // 焦点环（保持内联）
        const ColorTokens& colors = Theme().colors;
        float corner = EffectiveCornerRadius();
        D2D1_ROUNDED_RECT focusRect = D2D1::RoundedRect(
            D2D1::RectF(bounds_.x + 1, bounds_.y + 1,
                       bounds_.right() - 1, bounds_.bottom() - 1),
            corner, corner);
        dc.DrawRoundedRect(focusRect, colors.focusStroke, 2.0f);
    }
    RenderContent(dc);
}

void Button::RenderBackground(const DrawingContext& dc) {
    const ColorTokens& colors = Theme().colors;
    const ButtonAppearance& app = Appearance();
    const float corner = EffectiveCornerRadius();
    
    // 基础填充
    D2D1_COLOR_F fill = ButtonFillColor(kind_, State(), app, colors);
    if (fill.a > 0) {
        D2D1_ROUNDED_RECT rr = D2D1::RoundedRect(
            D2D1::RectF(bounds_.x, bounds_.y, bounds_.right(), bounds_.bottom()),
            corner, corner);
        dc.FillRoundedRect(rr, fill);
    }
    
    // Tint 高亮
    if (tintOpacity_ > 0.0f) {
        D2D1_COLOR_F tint = ButtonTintColor(kind_, app, colors);
        tint.a *= static_cast<float>(tintOpacity_);
        if (tint.a > 0) {
            D2D1_ROUNDED_RECT rr = D2D1::RoundedRect(
                D2D1::RectF(bounds_.x, bounds_.y, bounds_.right(), bounds_.bottom()),
                corner, corner);
            dc.FillRoundedRect(rr, tint);
        }
    }
}

void Button::RenderBorder(const DrawingContext& dc) {
    const ColorTokens& colors = Theme().colors;
    const ButtonAppearance& app = Appearance();
    const float corner = EffectiveCornerRadius();
    
    D2D1_ROUNDED_RECT rr = D2D1::RoundedRect(
        D2D1::RectF(bounds_.x + 0.5f, bounds_.y + 0.5f,
                   bounds_.right() - 0.5f, bounds_.bottom() - 0.5f),
        corner, corner);
    D2D1_COLOR_F stroke = EffectiveBorderBrush(colors.controlStrokeDefault);
    dc.DrawRoundedRect(rr, stroke, 1.0f);
}

void Button::RenderContent(const DrawingContext& dc) {
    const ColorTokens& colors = Theme().colors;
    const ButtonAppearance& app = Appearance();
    
    std::wstring label = Content();
    if (label.empty()) return;
    
    if (DWriteContext* dw = Dwrite()) {
        float fontSize = app.FontSize().value_or(Theme().typography.body);
        DWRITE_FONT_WEIGHT weight = app.FontWeight().value_or(DWRITE_FONT_WEIGHT_NORMAL);
        
        if (auto* fmt = dw->Format(fontSize, weight,
                                   DWRITE_TEXT_ALIGNMENT_CENTER,
                                   DWRITE_PARAGRAPH_ALIGNMENT_CENTER)) {
            D2D1_RECT_F textRect = D2D1::RectF(
                bounds_.x, bounds_.y,
                bounds_.right(), bounds_.bottom());
            D2D1_COLOR_F textColor = ButtonTextColor(kind_, State(), app, colors);
            dc.DrawText(label.c_str(), static_cast<UINT32>(label.size()),
                       fmt, textRect, textColor, D2D1_DRAW_TEXT_OPTIONS_CLIP);
        }
    }
}
```

### 2.3 验证

继承示例 `examples/inheritance/BigButton.h`:
```cpp
class BigButton : public Button {
public:
    void SetSubtitle(std::wstring s) { subtitle_ = s; Invalidate(); }
    void SetIcon(std::wstring icon) { icon_ = icon; Invalidate(); }

protected:
    void Measure(float availW, float availH) override {
        SetDesired(SizeDip(std::min(availW, 200.0f), 64.0f));
    }
    
    void RenderContent(const DrawingContext& dc) override {
        // 绘制图标
        if (!icon_.empty() && Dwrite()) {
            if (auto* iconFmt = Dwrite()->Format(24.0f, DWRITE_FONT_WEIGHT_NORMAL,
                                                 DWRITE_TEXT_ALIGNMENT_CENTER,
                                                 DWRITE_PARAGRAPH_ALIGNMENT_CENTER)) {
                D2D1_RECT_F iconRect = D2D1::RectF(
                    bounds_.x, bounds_.y + 8,
                    bounds_.right(), bounds_.y + 32);
                dc.DrawText(icon_.c_str(), icon_.size(), iconFmt, iconRect,
                           ButtonTextColor(CurrentKind(), State(), Appearance(), Theme().colors));
            }
        }
        
        // 主标题
        if (!Content().empty() && Dwrite()) {
            if (auto* titleFmt = Dwrite()->Format(14.0f, DWRITE_FONT_WEIGHT_SEMI_BOLD,
                                                  DWRITE_TEXT_ALIGNMENT_CENTER,
                                                  DWRITE_PARAGRAPH_ALIGNMENT_CENTER)) {
                D2D1_RECT_F titleRect = D2D1::RectF(
                    bounds_.x, bounds_.y + 28,
                    bounds_.right(), bounds_.y + 44);
                dc.DrawText(Content().c_str(), Content().size(), titleFmt, titleRect,
                           ButtonTextColor(CurrentKind(), State(), Appearance(), Theme().colors));
            }
        }
        
        // 副标题
        if (!subtitle_.empty() && Dwrite()) {
            if (auto* subFmt = Dwrite()->Format(11.0f, DWRITE_FONT_WEIGHT_NORMAL,
                                                DWRITE_TEXT_ALIGNMENT_CENTER,
                                                DWRITE_PARAGRAPH_ALIGNMENT_CENTER)) {
                D2D1_RECT_F subRect = D2D1::RectF(
                    bounds_.x, bounds_.y + 44,
                    bounds_.right(), bounds_.y + 60);
                D2D1_COLOR_F subColor = Theme().colors.textSecondary;
                dc.DrawText(subtitle_.c_str(), subtitle_.size(), subFmt, subRect, subColor);
            }
        }
    }

private:
    std::wstring subtitle_;
    std::wstring icon_;
};
```

**验证能力**：
- ✅ 重写 `Measure()` 改尺寸为 64dp
- ✅ 调用 `RenderBackground(dc)` 和 `RenderBorder(dc)` 复用基类
- ✅ 读取 `CurrentKind()` 和 `TintOpacity()` 保持文本颜色一致

---

## Task 3: Slider 增强

### 目标

让派生类能够：
- ✅ 读取步进值（实现对数步进）
- ✅ 区分拖动/键盘输入（不同的提交策略）

### 3.1 头文件改动 (`FluentUI/controls/Slider.h`)

在 `protected:` 区块添加：

```cpp
protected:
    // --- 只读访问器 ---
    
    /// Current step value. Derived classes can read this in CoerceValue()
    /// to implement custom stepping (e.g., logarithmic).
    double Step() const { return step_; }
    
    /// True while the thumb is being dragged. Derived classes can read this
    /// to distinguish drag (commit on release) from keyboard (commit immediately).
    bool IsDragging() const { return dragging_; }
```

### 3.2 实现文件改动

**无需改动** `Slider.cpp`。只是暴露只读访问器，不拆分渲染。

### 3.3 验证

继承示例 `examples/inheritance/LogSlider.h`:
```cpp
// 对数滑块：线性拖动 → 对数值输出
class LogSlider : public Slider {
public:
    LogSlider() {
        SetMinimum(1.0);
        SetMaximum(10000.0);
        SetValue(100.0);
    }

protected:
    double CoerceValue(double v) override {
        // 对数约束：值只能是 1, 10, 100, 1000, 10000
        double logVal = std::log10(std::max(1.0, v));
        double rounded = std::round(logVal);
        return std::pow(10.0, rounded);
    }
    
    void OnValueChanged() override {
        Slider::OnValueChanged();
        
        // 拖动时实时反馈，键盘输入时延迟提交
        if (IsDragging()) {
            // 实时更新 UI
            UpdateLabel();
        } else {
            // 键盘输入完成后提交到设置
            CommitToSettings();
        }
    }

private:
    void UpdateLabel() { /* ... */ }
    void CommitToSettings() { /* ... */ }
};
```

**验证能力**：
- ✅ 重写 `CoerceValue()` 实现对数步进
- ✅ 读取 `IsDragging()` 区分输入方式

---

## 文档任务

### Task 4: 编写继承指南

**文件**: `docs/controls/inheritance-guide.md`

**内容大纲**：
1. **概述**：FluentUI 的继承策略（抽象基类 vs 具体控件）
2. **何时继承**：
   - ✅ 继承抽象基类（ButtonBase、RangeBase、Panel）深度定制
   - ✅ 继承具体控件（Calendar、Button、Slider）视觉/行为微调
   - ❌ 不要继承复杂控件（DataGrid、TreeView）
3. **可继承控件清单**：
   - Calendar: 拆分渲染方法 + 辅助方法 + 只读访问器
   - Button: 拆分渲染方法 + 只读访问器
   - Slider: 只读访问器
4. **完整示例**：
   - `LunarCalendar`: 农历日历
   - `BigButton`: 大按钮
   - `LogSlider`: 对数滑块
5. **设计约束**：
   - 为什么 `cellW_` 保持 private
   - 如何通过 `CellRect()` 间接访问
   - 什么情况下应该继承基类而非具体控件

---

### Task 5: 更新控件文档

为每个改动的控件更新文档，添加"Inheritance"章节。

#### 5.1 `docs/controls/Calendar.md`

在文档末尾添加：

```markdown
## Inheritance

Calendar is **partially inheritable** for visual and behavioral customization.

### What you can do

✅ **Add decorations**: Override `RenderCell()` to draw lunar dates, holiday markers, or event dots below the day number.

✅ **Change layout**: Override `Measure()` to change the overall size. Use `CellRect()` to query cell positions instead of assuming fixed dimensions.

✅ **Customize navigation**: Override `RenderNavBar()` to change the month/year display or add custom navigation buttons.

✅ **Read hover state**: Call `HoverCell()` in your `OnPointerMoved()` override to add custom hover interactions.

### What you cannot do

❌ **Modify cell dimensions directly**: `cellW_` and `cellH_` are private. Change them indirectly by overriding `Measure()` and setting different desired dimensions.

❌ **Access animation state**: `hoverFade_` is a private implementation detail. Derive from `Control` if you need full control over animations.

### Example: Lunar Calendar

See `examples/inheritance/LunarCalendar.h` for a complete example that adds lunar date display below each day.

```cpp
class LunarCalendar : public Calendar {
protected:
    void RenderCell(const DrawingContext& dc, int cellIndex, ...) override {
        Calendar::RenderCell(dc, cellIndex, ...);  // Base rendering
        RectDip rect = CellRect(cellIndex);
        DrawLunarDate(dc, rect, year, month, day);
    }
};
```

### Protected API Reference

| Method | Description |
|--------|-------------|
| `float CellWidth() const` | Cell width in DIPs |
| `float CellHeight() const` | Cell height in DIPs |
| `int HoverCell() const` | Currently hovered cell [0..41] or -1 |
| `RectDip CellRect(int) const` | Bounds of cell at index |
| `RectDip NavBarRect() const` | Navigation bar bounds |
| `RectDip WeekdayHeaderRect() const` | Weekday header bounds |
| `virtual void RenderNavBar(dc)` | Render navigation bar |
| `virtual void RenderWeekdayHeader(dc)` | Render weekday labels |
| `virtual void RenderCell(dc, ...)` | Render a single cell |
```

#### 5.2 `docs/controls/Button.md`

类似地添加 Inheritance 章节，参考 Calendar 的模式。

#### 5.3 `docs/controls/Slider.md`

类似地添加 Inheritance 章节，参考 Calendar 的模式。

---

## 验证清单

每个任务完成后检查：

- [ ] **编译验证**: Debug + Release 干净编译，无警告
- [ ] **测试验证**: `FluentUITests.exe` 全绿
- [ ] **继承示例验证**: 写一个实际的派生类，验证能做什么、不能做什么
- [ ] **文档验证**: 继承示例的代码能编译并运行
- [ ] **视觉验证**: （由用户在真机验证）派生类的视觉效果是否符合预期

---

## 时间线

| 任务 | 预估时间 | 依赖 |
|------|---------|------|
| Task 1: Calendar | 2 小时 | - |
| Task 2: Button | 1.5 小时 | - |
| Task 3: Slider | 0.5 小时 | - |
| Task 4: 继承指南 | 2 小时 | Task 1-3 |
| Task 5: 控件文档 | 1 小时 | Task 1-3 |
| **总计** | **7 小时** | |

**注**：实际时间可能因验证迭代而延长。

---

## 风险与注意事项

### 风险

1. **拆分 Render() 可能遗漏逻辑**  
   - **缓解**: 对比重构前后的渲染输出，确保像素级一致

2. **派生类可能误用只读访问器**  
   - **缓解**: 文档明确说明"使用 `CellRect()` 而不是假设 `CellWidth()`"

3. **维护负担增加**  
   - **缓解**: Phase 1 只做 3 个控件，Phase 2/3 等用户需求再考虑

### 注意事项

- **保持 API 稳定**: 新增的 protected 方法是 API 契约，不能随意改签名
- **文档同步**: 每次改动 protected 方法都要更新文档
- **示例可编译**: 继承示例必须能实际编译运行，不能是伪代码

---

## 下一步行动

1. ✅ **文档评审**（当前文档）  
   - 用户确认任务清单的详细程度  
   - 调整任务优先级或范围

2. ⏭ **实施 Task 1-3**  
   - 按顺序逐个实施（Calendar → Button → Slider）  
   - 每个控件完成后立即验证

3. ⏭ **编写文档 Task 4-5**  
   - 在所有代码改动验证通过后编写文档  
   - 确保示例代码可编译

4. ⏭ **用户真机验证**  
   - 用户验证继承示例的视觉效果  
   - 根据反馈调整 Phase 2 范围
