# Control Inheritance Guide

**Last Updated**: 2026-09-27  
**Status**: Complete (Phase 1-3, all evaluated)

---

## Overview

FluentUI adopts a **two-tier inheritance strategy**:

1. **Abstract base classes** (`ButtonBase`, `RangeBase`, `TextEditBase`, `Panel`) expose `protected` state and virtual hooks, designed for deep customization.
2. **Concrete controls** (`Button`, `Slider`, `Calendar`) seal internal state as `private` but expose `protected` accessors and split rendering hooks for visual/behavioral extension.

This guide explains **when to inherit**, **what you can do**, and **what constraints remain by design**.

---

## Architecture: How to Extend FluentUI

**✅ Recommended**: Create a **separate library** for your custom controls, don't modify FluentUI source.

```
FluentUI.lib (framework, unchanged)
    ↓ links against
CustomControls.lib (your extensions: LunarCalendar, BigButton, LogSlider)
    ↓ links against
YourApp.exe (links both libraries)
```

**Why?**
- ✅ Clear boundary: FluentUI = stable framework, CustomControls = your code
- ✅ FluentUI.lib doesn't grow with every custom control
- ✅ You can fork/modify CustomControls without touching FluentUI
- ✅ Demonstrates the real integration path to end users

**Example**: See `extensions/CustomControls/` for a working separate-library template with LunarCalendar, BigButton, and LogSlider.

---

---

## When to Inherit

### ✅ **Inherit Abstract Base Classes** — Deep Customization

When you need **fundamentally different behavior** (e.g., a three-state toggle, a radial slider), inherit the base class:

| Base Class | Purpose | Examples |
|------------|---------|----------|
| `ButtonBase` | Click/focus/keyboard activation | Button, RadioButton, ToggleButton, DatePicker |
| `RangeBase` | Min/max/value + coercion | Slider, ProgressBar, NumericUpDown |
| `TextEditBase` | Text buffer + caret + selection | TextBox, TextArea |
| `Panel` | Layout container | StackPanel, Grid, Canvas, WrapPanel |
| `ItemsControl` | Items collection + virtualization | ListBox, ComboBox, TreeView |

**What you get**:
- Full access to `protected` state (`min_`, `max_`, `value_` in RangeBase; `checked_` in ToggleButton)
- Rich virtual hooks (`CoerceValue`, `OnToggle`, `OnItemsChanged`)
- No pre-existing visual: you define the entire appearance

---

### ✅ **Inherit Concrete Controls** — Visual/Behavioral Tweaks

When you want to **add decorations or conditional rendering** without reimplementing the control from scratch:

| Control | Use Case | What's Exposed |
|---------|----------|----------------|
| **Calendar** | Lunar calendar, holiday markers, event dots | Cell geometry, hover state, split rendering hooks |
| **Button** | Big button (icon + two-line text), themed buttons | Kind, tint opacity, split rendering (background/border/content) |
| **Slider** | Logarithmic slider, tick marks, custom scale | Drag state, track geometry, value-to-position mapping |

**What you get**:
- Read-only accessors to internal state (`CellWidth()`, `CurrentKind()`, `IsDragging()`)
- Geometry helpers (`CellRect()`, `TrackStart()`, `ValueToTrackPos()`)
- Split rendering hooks (`RenderCell()`, `RenderBackground()`, `RenderContent()`)

**What you cannot do**:
- ❌ Modify private state directly (`cellW_`, `kind_`, `dragging_` remain `private`)
- ❌ Change layout算法 without overriding `Measure()` (geometry helpers compute from private state)

---

### ❌ **Do Not Inherit** — Use Composition Instead

Avoid inheriting **complex controls** where composition is simpler:

- `DataGrid`, `TreeView`: Wrap them and configure, don't subclass
- `ComboBox`, `ListBox`: Use `ItemTemplate` (if available) or style overrides
- `MessageDialog`, `GridSplitter`: Marked `final`, cannot inherit

---

## Working Examples

**Location**: `extensions/CustomControls/` — a separate library demonstrating how users extend FluentUI.

| Control | Source | What It Does |
|---------|--------|--------------|
| **LunarCalendar** | `LunarCalendar.cpp` | Adds lunar date display below Gregorian days |
| **BigButton** | `BigButton.cpp` | 64dp tall button with icon + two-line text |
| **LogSlider** | `LogSlider.cpp` | Logarithmic snapping (1, 10, 100, 1000) |

These are **real, compilable implementations** that link against `FluentUI.lib`. See `extensions/CustomControls/README.md` for build instructions.

---

## Phase 1 Controls (Inheritance-Ready)

### Calendar

**Scenario**: Add lunar dates, holiday badges, or event dots below each day number.

**Protected API**:

```cpp
// Read-only state
float CellWidth() const;
float CellHeight() const;
int HoverCell() const;              // [0..41] or -1
float HoverOpacity() const;         // [0..1], syncs with base animation

// Geometry helpers
RectDip CellRect(int cellIndex) const;
RectDip NavBarRect() const;
RectDip WeekdayHeaderRect() const;
RectDip PrevArrowRect() const;
RectDip NextArrowRect() const;

// Split rendering hooks
virtual void RenderNavBar(const DrawingContext& dc);
virtual void RenderWeekdayHeader(const DrawingContext& dc);
virtual void RenderCell(const DrawingContext& dc, const CellInfo& cell);
```

**CellInfo struct**:
```cpp
struct CellInfo {
    int index;              // 0..41, row-major, Sunday-first
    int year, month, day;
    RectDip rect;           // == CellRect(index)
    bool inViewedMonth;     // false for leading/trailing days
    bool isToday;
    bool isSelected;
    bool isEnabled;         // within min/max, not blacked out
    bool isHovered;         // index == HoverCell()
};
```

**Example: Lunar Calendar**

```cpp
class LunarCalendar : public Calendar {
protected:
    void RenderCell(const DrawingContext& dc, const CellInfo& cell) override {
        // Call base to draw day number + selection/today ring
        Calendar::RenderCell(dc, cell);
        
        // Add lunar date below
        if (cell.inViewedMonth && cell.isEnabled) {
            std::wstring lunar = GetLunarDate(cell.year, cell.month, cell.day);
            if (DWriteContext* dw = Dwrite()) {
                if (auto* fmt = dw->Format(9.0f, DWRITE_FONT_WEIGHT_NORMAL,
                                           DWRITE_TEXT_ALIGNMENT_CENTER,
                                           DWRITE_PARAGRAPH_ALIGNMENT_CENTER)) {
                    D2D1_RECT_F lunarRect = D2D1::RectF(
                        cell.rect.x, cell.rect.y + cell.rect.h * 0.6f,
                        cell.rect.right(), cell.rect.bottom());
                    dc.DrawText(lunar.c_str(), lunar.size(), fmt, lunarRect,
                               Theme().colors.textSecondary);
                }
            }
        }
    }

private:
    std::wstring GetLunarDate(int y, int m, int d) {
        // Lunar calendar conversion (simplified)
        return L"初一";
    }
};
```

**Design Constraints**:
- ✅ Can read `CellWidth()`/`CellHeight()` but **cannot write** them (layout algorithm stays in base)
- ✅ Use `CellRect(index)` instead of assuming cell dimensions (future-proof if base changes layout)
- ❌ Cannot access `hoverFade_` animation state directly (private implementation detail)

---

### Button

**Scenario**: Big button with icon + two-line text + 64dp height.

**Protected API**:

```cpp
// Read-only state
Kind CurrentKind() const;           // Accent/Standard/Subtle
float TintOpacity() const;          // [0..1], hover/press animation
ButtonAppearance AppearanceForRender() const;  // resolved overrides + theme

// Split rendering hooks
virtual void RenderBackground(const DrawingContext& dc);
virtual void RenderBorder(const DrawingContext& dc);
virtual void RenderContent(const DrawingContext& dc);
```

**Example: Big Button**

```cpp
class BigButton : public Button {
public:
    void SetSubtitle(std::wstring s) { subtitle_ = s; Invalidate(); }
    void SetIcon(std::wstring icon) { icon_ = icon; Invalidate(); }

protected:
    void Measure(float availW, float availH) override {
        // Override size to 64dp height
        SetDesired(SizeDip(std::min(availW, 200.0f), 64.0f));
    }
    
    void RenderContent(const DrawingContext& dc) override {
        // Icon (top third)
        if (!icon_.empty() && Dwrite()) {
            if (auto* iconFmt = Dwrite()->Format(24.0f, DWRITE_FONT_WEIGHT_NORMAL,
                                                 DWRITE_TEXT_ALIGNMENT_CENTER,
                                                 DWRITE_PARAGRAPH_ALIGNMENT_CENTER)) {
                D2D1_RECT_F iconRect = D2D1::RectF(
                    bounds_.x, bounds_.y + 8,
                    bounds_.right(), bounds_.y + 32);
                // Sync color with base: onAccent for Accent, textPrimary otherwise
                D2D1_COLOR_F textColor = ButtonTextColor(CurrentKind(), State(),
                                                         AppearanceForRender(),
                                                         Theme().colors);
                dc.DrawText(icon_.c_str(), icon_.size(), iconFmt, iconRect, textColor);
            }
        }
        
        // Main title (middle)
        if (!Content().empty() && Dwrite()) {
            if (auto* titleFmt = Dwrite()->Format(14.0f, DWRITE_FONT_WEIGHT_SEMI_BOLD,
                                                  DWRITE_TEXT_ALIGNMENT_CENTER,
                                                  DWRITE_PARAGRAPH_ALIGNMENT_CENTER)) {
                D2D1_RECT_F titleRect = D2D1::RectF(
                    bounds_.x, bounds_.y + 28,
                    bounds_.right(), bounds_.y + 44);
                D2D1_COLOR_F textColor = ButtonTextColor(CurrentKind(), State(),
                                                         AppearanceForRender(),
                                                         Theme().colors);
                dc.DrawText(Content().c_str(), Content().size(), titleFmt,
                           titleRect, textColor);
            }
        }
        
        // Subtitle (bottom)
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

**Design Constraints**:
- ✅ Can override `Measure()` to change size
- ✅ Can read `CurrentKind()` to branch on Accent vs Standard
- ✅ Can call `RenderBackground(dc)` to reuse base fill + tint
- ❌ Cannot modify `kind_` or `tintOpacity_` directly (they drive color resolution in sync)

---

### Slider

**Scenario**: Logarithmic slider (drag linearly → output 1, 10, 100, 1000).

**Protected API**:

```cpp
// Read-only state
bool IsDragging() const;            // true during pointer press-to-release
float DisplayedValue() const;       // eased value (lags value_ while animating)

// Geometry helpers (window DIPs along the track axis)
float TrackStart() const;           // left for Horizontal, top for Vertical
float TrackEnd() const;
float TrackCenterline() const;      // Y for Horizontal, X for Vertical
float ValueToTrackPos(float v) const;  // map value to DIP position (for tick marks)

// RangeBase hooks (already protected)
float CoerceValue(float v) const override;
void OnValueChanged(float old, float newVal) override;
```

**Example: Logarithmic Slider**

```cpp
class LogSlider : public Slider {
public:
    LogSlider() {
        SetMinimum(1.0f);
        SetMaximum(10000.0f);
        SetValue(100.0f);
    }

protected:
    float CoerceValue(float v) const override {
        // Snap to nearest power of 10
        float logVal = std::log10(std::max(1.0f, v));
        float rounded = std::round(logVal);
        return std::pow(10.0f, rounded);
    }
    
    void OnValueChanged(float old, float newVal) override {
        // Call base first so ValueChanged/ValueChanging events fire
        Slider::OnValueChanged(old, newVal);
        
        // Distinguish drag (preview) from keyboard/programmatic (commit)
        if (IsDragging()) {
            // Update live preview UI
            UpdateVolumeDisplay(newVal);
        } else {
            // Write to settings
            settings_.volume = static_cast<int>(newVal);
            SaveSettings();
        }
    }

private:
    void UpdateVolumeDisplay(float v) { /* ... */ }
    void SaveSettings() { /* ... */ }
    SettingsStore settings_;
};
```

**Design Constraints**:
- ✅ `CoerceValue()` is called during drag, keyboard, and `SetValue()` — perfect for snapping
- ✅ `IsDragging()` distinguishes real-time preview from final commit
- ✅ `ValueToTrackPos(v)` maps snapped values to screen coords (for drawing tick marks at 1, 10, 100, 1000)
- ❌ Cannot access `step_` to modify it at runtime (use `SetStep()` public API)

---

## Common Patterns

### Pattern 1: Call Base Then Decorate

Most inheritance needs **augment**, not **replace**:

```cpp
void RenderCell(const DrawingContext& dc, const CellInfo& cell) override {
    Calendar::RenderCell(dc, cell);  // ✅ base selection ring, today indicator, day number
    DrawMyDecoration(dc, cell);       // ✅ add-on: lunar date, event dot
}
```

### Pattern 2: Read State, Don't Write

Use read-only accessors to **branch**, but let the base **mutate**:

```cpp
void RenderContent(const DrawingContext& dc) override {
    if (CurrentKind() == Kind::Accent) {
        // Custom rendering for accent buttons
    } else {
        Button::RenderContent(dc);  // fall back to base for Standard/Subtle
    }
}
```

### Pattern 3: Geometry Helpers Prevent Drift

Never hardcode dimensions — **query** them:

```cpp
// ❌ BAD: assumes cellW_ == 36
float x = bounds_.x + (col * 36.0f);

// ✅ GOOD: base controls the layout
RectDip rect = CellRect(cellIndex);
float x = rect.x;
```

---

## Design Rationale

### Why Keep State Private?

**Example**: `Button::kind_` and `Button::tintOpacity_` must stay in sync:
- `kind_ == Accent` → fill color is `colors.accent`, text is `colors.onAccent`
- Letting a subclass write `kind_` alone could break the color resolution

**Solution**: Expose `CurrentKind()` (read) + `SetKind()` (public write that enforces invariants).

### Why Split Rendering Into Hooks?

**Before** (monolithic):
```cpp
void Render(dc) {
    // 400 lines: background, nav bar, weekday header, 42 cells, ...
}
// To add lunar dates: copy-paste all 400 lines, add 2 lines, maintain forever
```

**After** (split):
```cpp
void Render(dc) {
    RenderNavBar(dc);           // ← subclass can override
    RenderWeekdayHeader(dc);    // ← subclass can override
    for (...) RenderCell(dc, cell);  // ← subclass can override
}
// To add lunar dates: override RenderCell (10 lines), call base, add 2 lines
```

### Why Geometry Helpers?

**Without**:
```cpp
// Subclass hardcodes layout assumptions
float x = bounds_.x + (col % 7) * 36.0f;  // ← breaks if base changes cellW_
```

**With**:
```cpp
RectDip rect = CellRect(index);  // ← base computes, subclass queries
```
Now Calendar can change its layout algorithm (variable cell width, padding) without breaking subclasses.

---

## Phase 2 (Evaluated - No Code Changes Needed)

The following controls were **evaluated for Phase 2** and found to **already have sufficient APIs** for customization:

### ComboBox
**Status**: ✅ No changes needed

**Why**: 
- Public API already sufficient: `Text()`, `SetText()`, `SetEditable(bool)`
- Events: `DropDownOpened`, `DropDownClosed`, `SelectionChanged`
- `popup_` and `listView_` are `unique_ptr` - exposing raw pointers has lifetime risks
- Custom dropdown scenarios are rare; if truly needed, use **composition** (PopupHost + custom Panel) instead of inheritance

### TextBox / TextArea
**Status**: ✅ No changes needed (base class already inheritable)

**Why**:
- **TextEditBase** (their base class) is already fully inheritable:
  - `protected` members: `text_`, `caret_`, `selAnchor_`, `composition_`
  - `protected virtual` hooks: `OnTextLayoutDirty`, `OnSelectionChanged`, `EnsureCaretVisible`, `OnNavigationKey`, `SanitizeInput`
- TextBox's `password_` has public API `SetPassword(bool)` / `IsPassword()`
- TextBox's protected `ReservedRightDip()` (right-edge width the text must not run under),
  `SetMaskRevealed(bool)` (show a password in plain text without leaving password mode) and
  `ContentLeft()` / `ContentWidth()` (the text viewport) exist for in-box buttons;
  PasswordBox's reveal button is built on them
- TextEditBase's protected `AllowCopyToClipboard()` / `AllowImeComposition()` policy virtuals
  gate Ctrl+C/X + menu Cut/Copy and IME composition; a subclass whose answer changes while
  focused calls `SyncImeAssociation()` after the change
- TextBox's `scrollX_` is internal scroll state (no external access needed)
- TextArea's virtualization state is complex implementation detail (exposing it couples subclasses to internals)

**Example**: Syntax highlighting is already possible
```cpp
class SyntaxTextArea : public TextArea {
protected:
    void Render(const DrawingContext& dc) override {
        TextArea::Render(dc);  // base rendering
        HighlightKeywords(dc, text_);  // text_ is protected in TextEditBase
    }
};
```

---

## Phase 3 (Evaluated - Composition Preferred)

Low-priority controls were evaluated and found to prefer **composition over inheritance**:

### CheckBox / RadioButton / ToggleSwitch
**Status**: ✅ Base class (ToggleButton) already sufficient

- `ToggleButton` exposes `protected checked_`
- Animation states (`checkAnim_`, `slide_`) are visual-only, low value for subclasses
- Custom rendering: override `Render()` and read `IsChecked()`

### ProgressBar / Rating
**Status**: ✅ Base class (RangeBase) already sufficient

- `RangeBase` exposes `protected value_`, `min_`, `max_`
- Public APIs: `Value()`, `Minimum()`, `Maximum()` getters; ProgressBar adds `Fraction()`
- Custom rendering: override `Render()` and read state

### DataGrid / TreeView
**Status**: ⏸️ Explicitly out of scope

- Complex controls with large internal state (columns, sorting, tree nodes)
- Inheritance cost >> benefit
- Prefer **composition**: configure via public APIs (templates, renderers) or build from `Panel`/`ItemsControl`

---

## Summary

| Inheritance Level | When | Examples | Freedom | Constraints |
|-------------------|------|----------|---------|-------------|
| **Abstract Base** | New control type | ButtonBase → MyCustomButton | Full | Must implement pure virtuals |
| **Concrete Control** | Visual tweak | Calendar → LunarCalendar | High | Cannot mutate private state |
| **Composition** | Config-driven | DataGrid in a Panel | Medium | No code, only property setting |

**Golden Rule**: If you need to change **behavior**, inherit the base class. If you need to change **appearance**, inherit the concrete control.

---

**Related Documents**:
- Individual control docs: `Calendar.md`, `Button.md`, `Slider.md` (Inheritance sections)
- Separate-library template: `extensions/CustomControls/README.md`
