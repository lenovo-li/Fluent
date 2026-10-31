# Control Inheritance Guide

**Last Updated**: 2026-10-31  
**Status**: Complete (inheritance-ready controls, then the controls evaluated and left as they are)

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
- `ComboBox`, `ListBox`: configure through their public API and the `Control` property layer (`SetBackground` / `SetForeground`); there is no `ItemTemplate`
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

## Inheritance-Ready Controls

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

**Example: Big Button** (the shipped version is
`extensions/CustomControls/BigButton.cpp`; this is the same shape, shortened)

```cpp
class BigButton : public Button {
public:
    void SetSubtitle(const std::wstring& s) { subtitle_ = s; Invalidate(); }
    // Render-only: the height is fixed, so an icon never changes the desired size.
    void SetIcon(const std::wstring& icon) { icon_ = icon; Invalidate(); }

protected:
    void Measure(float availW, float availH) override {
        // Override size to 64dp height
        SetDesired(SizeDip(std::min(availW, 200.0f), 64.0f));
    }

    void RenderContent(const DrawingContext& dc) override {
        DWriteContext* dw = Dwrite();
        if (!dw) return;
        // Stack the rows that have content (icon 24, title 20, subtitle 16 DIP)
        // and centre the stack vertically inside the 64 DIP button.
        const std::wstring& title = Text();   // ContentControl's label
        const float iconH = icon_.empty() ? 0.0f : 24.0f;
        const float titleH = title.empty() ? 0.0f : 20.0f;
        const float subH = subtitle_.empty() ? 0.0f : 16.0f;
        float y = bounds_.y + (bounds_.h - (iconH + titleH + subH)) * 0.5f;
        auto row = [&](float h) {
            D2D1_RECT_F r = D2D1::RectF(bounds_.x, y, bounds_.right(), y + h);
            y += h;
            return r;
        };
        const D2D1_RECT_F iconRect = row(iconH);
        const D2D1_RECT_F titleRect = row(titleH);
        const D2D1_RECT_F subRect = row(subH);

        // Sync color with base: onAccent for Accent, textPrimary otherwise
        const D2D1_COLOR_F textColor = ButtonTextColor(CurrentKind(), State(),
                                                       AppearanceForRender(),
                                                       Theme().colors);
        if (!icon_.empty())
            dc.DrawText(icon_.c_str(), static_cast<UINT32>(icon_.size()),
                        dw->Format(24.0f), iconRect, textColor);
        if (!title.empty())
            dc.DrawText(title.c_str(), static_cast<UINT32>(title.size()),
                        dw->Format(Theme().typography.bodySize, DWRITE_FONT_WEIGHT_SEMI_BOLD),
                        titleRect, textColor);
        if (!subtitle_.empty())
            dc.DrawText(subtitle_.c_str(), static_cast<UINT32>(subtitle_.size()),
                        dw->Format(Theme().typography.captionSize), subRect,
                        Theme().colors.textSecondary);
    }

private:
    std::wstring subtitle_;
    std::wstring icon_;
};
```

**Design Constraints**:
- ✅ Can override `Measure()` to change size
- ✅ Can read `CurrentKind()` to branch on Accent vs Standard
- ✅ Overriding only `RenderContent()` keeps the base fill, tint and border (`RenderBackground` / `RenderBorder`)
- ❌ Cannot modify `kind_` or `tintOpacity_` directly (they drive color resolution in sync)

---

### Slider

**Scenario**: Slider that snaps to powers of ten (1, 10, 100, 1000), one decade per arrow key.

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

// Repaint hooks (both just call Invalidate())
void OnStateChanged() override;     // hover / pressed / disabled changed
void OnFocusChanged() override;     // focus ring appears / disappears
```

If you override `OnStateChanged` or `OnFocusChanged`, call the `Slider::` version
(or `Invalidate()` yourself). Otherwise the thumb's hover size and the focus ring
stop updating.

**Example: Logarithmic Slider**

```cpp
class LogSlider : public Slider {
public:
    LogSlider() {
        SetMinimum(1.0f);
        SetMaximum(10000.0f);
        SetValue(100.0f);
    }

    // Slider's arrows add +/-Step(); CoerceValue below would round that straight
    // back to the same power of 10, so step a decade per key instead.
    void OnKeyDownRouted(KeyEventArgs& e) override {
        if (e.vk == VK_RIGHT || e.vk == VK_UP) { SetValue(Value() * 10.0f); e.handled = true; }
        else if (e.vk == VK_LEFT || e.vk == VK_DOWN) { SetValue(Value() / 10.0f); e.handled = true; }
        else Slider::OnKeyDownRouted(e);
    }

protected:
    float CoerceValue(float v) const override {
        // Run the base clamp + step snap first, then snap to the nearest power
        // of 10, and clamp again: rounding the exponent can land outside
        // [Minimum, Maximum] (e.g. max 5000 rounds up to 10000).
        v = Slider::CoerceValue(v);
        if (v <= 0.0f) return Minimum();
        float snapped = std::pow(10.0f, std::round(std::log10(v)));
        return std::clamp(snapped, Minimum(), Maximum());
    }
};

// Preview vs commit: subscribe to the two events rather than branching on
// IsDragging() inside OnValueChanged. A drag's final commit is raised from
// OnPointerReleased straight to ValueChanged and never passes through
// OnValueChanged, so an `else { Save(); }` branch there is never reached after a
// drag. ValueChanging = every drag step; ValueChanged = keyboard, SetValue, and
// once on release of a drag that moved the value.
class VolumePanel {
public:
    void Bind(LogSlider& s) {
        previewSub_ = s.ValueChanging().Subscribe(this,
            [](void* o, Slider&, float& v) { static_cast<VolumePanel*>(o)->Preview(v); });
        commitSub_ = s.ValueChanged().Subscribe(this,
            [](void* o, Slider&, float& v) { static_cast<VolumePanel*>(o)->Save(v); });
    }

private:
    void Preview(float v) { /* update the live volume display */ }
    void Save(float v) { /* write settings */ }
    Subscription previewSub_;   // RAII: unregisters with the panel
    Subscription commitSub_;
};
```

**Design Constraints**:
- ✅ `CoerceValue()` is called during drag, keyboard, and `SetValue()` — perfect for snapping
- ✅ `IsDragging()` tells a drag step from a keyboard/programmatic change inside `OnValueChanged`; for preview vs commit use `ValueChanging` / `ValueChanged` (the drag's commit on release bypasses `OnValueChanged`)
- ⚠️ The track mapping stays linear in value (it is private), so on [1, 10000] the 1/10/100 snap points share the leftmost percent of the track; a true log drag would store the exponent as the value
- ✅ `ValueToTrackPos(v)` maps snapped values to screen coords (for drawing tick marks at 1, 10, 100, 1000)
- ❌ Cannot access `step_` to modify it at runtime (use `SetStep()` public API)

### Panel (custom layout containers)

**Scenario**: A new layout container (a flow layout, a radial panel, a scroller).

**Protected API**:

```cpp
// Layout strategy. Both receive padding-deducted values: Panel::Measure subtracts
// Padding before calling MeasureOverride and adds it back to the result.
virtual SizeDip MeasureOverride(float availW, float availH) = 0;
virtual void ArrangeOverride(const RectDip& content) = 0;
static void ArrangeChild(FrameworkElement* child, const RectDip& slot);  // honors margin + alignment

// Optional fill drawn before the children (transparent by default).
virtual void OnRenderBackground(const DrawingContext& dc);

// Called by RemoveAt and Clear for each child about to be destroyed, after it was
// detached and unparented, while the pointer is still valid. Default: no-op.
virtual void OnChildRemoved(FrameworkElement* child);

// Context propagation into the owned children. Override only to filter which
// children are live (TabControl attaches the selected page only); call the base
// otherwise, or the children never receive a context.
void AttachChildren(const UIContext& ctx) override;
void DetachChildren() override;
```

**Design Constraints**:
- ✅ Measure children with `MeasureCached()`, not `Measure()`: it short-circuits when the child is clean and the constraint is unchanged
- ⚠️ **Per-child state keyed by the child's address must be dropped in `OnChildRemoved`.** The allocator hands that address to the next child, which then silently inherits the dead child's entry. `Grid`'s cell map had exactly this bug (a recycled address placed a new child in the old child's row/column); `Grid.RemovedChildCellEntryIsDropped` covers the fix
- ✅ A container that clips its children to a viewport should paint them through `dc.WithClipHint(viewport)` so nested panels cull to what is visible (without it, a 10 000-row StackPanel inside a scroller draws every row to have it clipped away). A container that draws content under a transform (Viewbox) uses `dc.WithMappedClipHint(rectInContentSpace)` instead, because the window-space hint must be mapped, not intersected. Both are `DrawingContext` methods (Extension tier, see `docs/api-surface.md`)
- ✅ A scrolling container with compositor-backed descendants also overrides `GetViewportClipForDescendants`; a D2D clip cannot reach a composition visual (`ScrollPanel` is the worked example)

---

### Selector (collection controls)

`Selector<T>` (`controls/primitives/Selector.h`) holds the selected index, the multi-select set and the anchor. Protected hooks a collection control relies on:

```cpp
virtual void OnSelectionChanged(int oldIndex, int newIndex);  // raise your typed event, sync views
virtual void OnSelectionSetChanged();                         // multi-select set changed (Toggle / Range)
virtual void AddRangeToSelectionSet(int lo, int hi);          // fill the set for RangeSelectTo
```

`AddRangeToSelectionSet` receives the inclusive index range of a Shift gesture with the set already cleared. The default inserts every index, which is right for a flat list. Override it when some indices in the range are not on screen: `TreeView` does, because a collapsed node's rows sit numerically inside the range, and selecting them meant a delete over the selection removed nodes the user never saw. Cost is O(hi - lo) inserts on the input path.

In `OnSelectionSetChanged`, check `HasSubscribers()` on your event before materializing the set into a vector: on a large Shift range that copy is most of the gesture's cost (ListBox and TreeView both skip it).

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

## Evaluated: Existing API Already Sufficient

The following controls were **evaluated** and found to **already have sufficient APIs** for customization:

### ComboBox
**Status**: ✅ No changes needed

**Why**: 
- Public API already sufficient: `Text()`, `SetText()`, `SetEditable(bool)`
- Events: `DropDownOpened`, `DropDownClosed`, `SelectionChanged`
- `popup_` and `listView_` are `unique_ptr` - exposing raw pointers has lifetime risks. They live until `~ComboBox`: detach only closes the popup, because a detach can run synchronously inside the popup's own dispatch
- Custom dropdown scenarios are rare. `PopupHost` is **Internal** tier (`docs/api-surface.md`), so a custom dropdown built on it is building against an unsupported API. If you do it anyway, copy ComboBox's three rules: size the popup as content + `2 * PopupHost::kContentPadding` (the card inset), route every close path through the `SetOnClose` callback (the popup closes itself on a click on its card padding), and in that callback raise your closed event last and touch nothing afterwards (a handler may reopen the popup)

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
- **If you override `OnSelectionChanged()`, call `TextEditBase::OnSelectionChanged()`**: the base is where the public `SelectionChanged()` event is raised. TextArea's override once only repainted, and the event never fired on TextArea (`TextEditSelection.SelectionChangedFiresOnTextArea`)
- **Delete through `DeleteSelectionRecorded()`, never the bare `DeleteSelection()`**, from any user-facing edit path: the bare one does not record undo, so the stack stops matching `text_` and the next Ctrl+Z deletes the wrong text. The context menu's Cut had this bug (`TextEditUndo.ContextMenuCutIsUndoable`). Anything that removes text outside a recorded edit must clear the history instead (`ClearUndoHistory()`), the rule `SetText` and the `SetMaxLines` trim follow
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

## Evaluated: Composition Preferred

Low-priority controls were evaluated and found to prefer **composition over inheritance**:

### CheckBox / RadioButton / ToggleSwitch
**Status**: ✅ Existing bases already sufficient

- CheckBox and ToggleSwitch derive `ToggleButton`, which exposes `protected checked_` and the `OnToggle()` / `OnToggleChanged()` hooks
- RadioButton derives `ButtonBase` directly, not ToggleButton: its selected state is the shared group value, read through `IsSelected()`, and `OnActivate()` is the protected hook
- Animation states (`checkAnim_`, `slide_`) are visual-only, low value for subclasses
- Custom rendering: override `Render()` and read `IsChecked()` / `IsSelected()`

### ProgressBar / Rating
**Status**: ✅ Existing APIs already sufficient

- ProgressBar derives `RangeBase`, which exposes `protected value_`, `min_`, `max_`; public `Value()`, `Minimum()`, `Maximum()`, plus `Fraction()`
- Rating derives `Control`, not RangeBase (a rating is a small integer count whose count is also its maximum); its value is private, read through `Value()` / `MaxValue()`
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

## Not Extension Points

Some `protected` / `public` members exist for the framework or for tests, not for subclasses:

- `DialogWindow::PlanOwnerRestore` (protected static) is the pure owner-restore rule (re-enable the owner only if this dialog disabled it; hand foreground back only when the owner will be usable), split out so tests can cover nested modal dialogs without windows. Derive from `DialogWindow` for its public API, not to change this
- `UIElement::ClearFocusOnDetach()` is for `FocusManager` only (it resets the focus bit without raising `LostFocus`, for an element mid-teardown). A control never calls it

---

**Related Documents**:
- Individual control docs: `Button.md`, `Slider.md` (Slider.md links back here for its protected extension points)
- Separate-library template: `extensions/CustomControls/README.md`
