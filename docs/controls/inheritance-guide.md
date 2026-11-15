# Control Inheritance Guide

**Last Updated**: 2026-10-31  
**Status**: Complete (inheritance-ready controls, then the controls evaluated and left as they are)

---

## Overview

FluentUI adopts a **two-tier inheritance strategy**:

1. **Abstract base classes** (`ButtonBase`, `RangeBase`, `TextEditBase`, `Panel`) expose `protected` state and virtual hooks, designed for deep customization.
2. **Concrete controls** (`Button`, `Slider`) seal internal state as `private` but expose `protected` accessors and split rendering hooks for visual/behavioral extension.

This guide explains **when to inherit**, **what you can do**, and **what constraints remain by design**.

---

## Architecture: How to Extend FluentUI

**✅ Recommended**: Create a **separate library** for your custom controls, don't modify FluentUI source.

```
FluentUI.lib (framework, unchanged)
    ↓ links against
    ↓ links against
YourApp.exe (links both libraries)
```

**Why?**
- ✅ Clear boundary: FluentUI = stable framework, CustomControls = your code
- ✅ FluentUI.lib doesn't grow with every custom control
- ✅ You can fork/modify CustomControls without touching FluentUI
- ✅ Demonstrates the real integration path to end users


---

---

## When to Inherit

### ✅ **Inherit Abstract Base Classes** — Deep Customization

When you need **fundamentally different behavior** (e.g., a three-state toggle, a radial slider), inherit the base class:

| Base Class | Purpose | Examples |
|------------|---------|----------|
| `ButtonBase` | Click/focus/keyboard activation | Button, RadioButton, ToggleButton |
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
| **BigButton** | `BigButton.cpp` | 64dp tall button with icon + two-line text |
| **LogSlider** | `LogSlider.cpp` | Logarithmic snapping (1, 10, 100, 1000) |

These are **real, compilable implementations** that link against `FluentUI.lib`. See `extensions/CustomControls/README.md` for build instructions.

---

## Inheritance-Ready Controls

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

// Per-frame dirty walks. The PUBLIC entry points CollectDirtyBounds (UIElement),
// AnyDirtyInSubtree and ClearDirtySubtree (Visual) are non-virtual: each first
// tests the subtree dirty mark (Visual::SubtreeDirty) and returns at once for a
// subtree no invalidation has touched. The overridable bodies are these protected
// hooks, called only when the mark is set. Override them only if the container
// paints a child set that differs from children_ (TabControl reports the selected
// page only) or keeps dirty state outside dirty_ (ScrollPanel's rail strips).
void CollectDirtyBoundsOverride(std::vector<RectDip>& out) override;
bool AnyDirtyInSubtreeOverride(DirtyFlags flags) const override;
void ClearDirtySubtreeOverride() override;
```

**Dirty-walk contract** (applies to any container, Panel-derived or not, such as
a single-child wrapper like `Border`):

- Recurse by calling the child's **public** entry point
  (`child->CollectDirtyBounds(out)`, `child->ClearDirtySubtree()`), never the
  child's hook. The pruning test happens inside the public entry point, so this
  is what makes the walk cost the dirty path and not the whole subtree.
- `ClearDirtySubtreeOverride` must reach every child that `CollectDirtyBoundsOverride`
  or `AnyDirtyInSubtreeOverride` can visit. A child left out keeps its mark set:
  that is safe, but it is walked again every frame.
- Dirty state kept in a member instead of `dirty_` (a flag your
  `CollectDirtyBoundsOverride` reports from) must be raised together with
  `InvalidateDirty(DirtyFlags::None)`. That call sets `Render` in the mark of this
  element and every ancestor without dirtying anything, which is what brings the
  pruned walk to the element. `ScrollPanel::InvalidateRails` does this.
- A child that joins the tree already dirty is handled by `SetParent`, which
  carries the child's mark up the new ancestor chain. A container that owns a
  child outside `Panel`'s API must still call `SetParent(this)` on it.
- **Migration from the previous API:** `CollectDirtyBounds`, `AnyDirtyInSubtree`
  and `ClearDirtySubtree` used to be public virtuals. An existing
  `... override` of one of them no longer compiles, which is intentional. Rename it
  to the `...Override` hook and make it `protected`. Inside it, call the base hook
  (for example `Panel::ClearDirtySubtreeOverride()`) where you used to call the
  base public function. Every in-repo container was migrated this way: Panel,
  ScrollPanel, TabControl, Border, GroupBox, Viewbox and Expander.
  `SubtreeDirtyPruning.*` in `tests/unit/SubtreeDirtyTests.cpp` covers the
  contract.

**Design Constraints**:
- ✅ Measure children with `MeasureCached()`, not `Measure()`: it short-circuits when the child is clean and the constraint is unchanged
- ⚠️ **Per-child state keyed by the child's address must be dropped in `OnChildRemoved`.** The allocator hands that address to the next child, which then silently inherits the dead child's entry. `Grid`'s cell map had exactly this bug (a recycled address placed a new child in the old child's row/column); `Grid.RemovedChildCellEntryIsDropped` covers the fix
- ✅ A container that clips its children to a viewport should paint them through `dc.WithClipHint(viewport)` so nested panels cull to what is visible (without it, a 10 000-row StackPanel inside a scroller draws every row to have it clipped away). A container that draws content under a transform (Viewbox) uses `dc.WithMappedClipHint(rectInContentSpace)` instead, because the window-space hint must be mapped, not intersected. Both are `DrawingContext` methods (Extension tier, see `docs/api-surface.md`)
- ✅ A scrolling container with compositor-backed descendants also overrides `GetViewportClipForDescendants`; a D2D clip cannot reach a composition visual (`ScrollPanel` is the worked example)

---

### Selector (collection controls)

`Selector<T>` (`controls/primitives/Selector.h`) holds the selected index, the multi-select set and the anchor. Protected hooks and state a collection control relies on:

```cpp
virtual void OnSelectionChanged(int oldIndex, int newIndex);  // raise your typed event, sync views
virtual void OnSelectionSetChanged();                         // multi-select set changed (Toggle / Range)
virtual void AddRangeToSelectionSet(int lo, int hi);          // fill the set for RangeSelectTo
bool ReseedSelectionSet();                                    // items replaced wholesale: set = {selectedIndex_}
bool ClampSelectionToCount(int count);                        // count shrank, surviving indices keep identity
// Both return whether the SET changed and do NOT raise SelectionSetChanged: the caller
// raises it (a subscriber's last payload is stale), without the scroll-into-view a
// Ctrl/Shift gesture's OnSelectionSetChanged does.

int selectedIndex_;                // active index (both modes)
SelectionMode selectionMode_;
IndexRangeSet selectedSet_;        // authoritative selection in Multiple mode
int selectionAnchor_;              // Shift-range origin
```

**The set is ranges, not indices.** `selectedSet_` is an `IndexRangeSet`: sorted, disjoint, non-touching inclusive `IndexRange{lo, hi}` runs, with one canonical form for any index set (touching runs are always merged). It replaced a protected `std::unordered_set<int> selectedIndices_`, which turned Shift+End over a virtualized 1,000,000-item ListBox into a million hash inserts on the key-press path (about 80 ms per press in Release). The member was renamed on purpose, so a subclass written against the old one fails to compile instead of calling a different API. Migration:

| Old (`selectedIndices_`) | New (`selectedSet_`) | Cost (r = range count) |
|---|---|---|
| `insert(i)` | `Add(i)`; for a run `AddRange(lo, hi)` | O(log r), O(1) when appending ascending |
| `erase(i)` | `Remove(i)`; `RemoveRange(lo, hi)` | O(log r) + one insert when it splits a run |
| `count(i)` | `Contains(i)` | O(log r) |
| `size()` | `Count()` (items) / `RangeCount()` (runs) | O(r) / O(1) |
| `clear()` | `Clear()`; `AssignRange(lo, hi)` to replace with one run | O(1) |
| range-for over indices | `for (const IndexRange& r : selectedSet_.Ranges())` | O(r) |
| — | `ClampTo(count)`, `InsertAt(index, n)`, `EraseAt(index, n)` | O(log r + runs shifted) |

`InsertAt` / `EraseAt` keep a selection on the same *items* across an incremental insert or remove in the item source: indices above the edit shift, a run straddling an insert is split around the new (unselected) items, and two runs that touch after an erase are merged. Use them if your control mutates its items in place; replace-everything controls use `ReseedSelectionSet` instead (TreeView re-seeds because its mutators rebuild by id).

`AddRangeToSelectionSet` receives the inclusive index range of a Shift gesture with the set already cleared. The default is one `selectedSet_.AddRange(lo, hi)`, O(1) whatever the length, which is right for a flat list. Override it when some indices in the range are not on screen: `TreeView` does, because a collapsed node's rows sit numerically inside the range, and selecting them meant a delete over the selection removed nodes the user never saw. Add the runs you want in ascending order (each add is then an append); `TreeView` coalesces consecutive visible rows into runs, so a fully expanded stretch is one range and each collapsed node only splits it.

Public reads, same Single-mode rule for all of them (the single selection as one item, or empty): `IsSelected(i)` O(log r), `SelectedRanges()` and `SelectedCount()` O(r), `SelectedIndices()` O(selected) because its `vector<int>` signature expands every index. Draw and hit-test with `IsSelected`; count with `SelectedCount`, never `SelectedIndices().size()`.

In `OnSelectionSetChanged`, check `HasSubscribers()` on your event before building a `vector<int>` payload: with range storage that expansion is the only O(selected) step left in a large Shift gesture (ListBox and TreeView both skip it when nobody listens).

---

### UI Automation (any element)

Screen readers (Narrator, NVDA) and UIA test tools see an element through its **automation peer** (`automation/AutomationPeer.h`). The extension point is one protected virtual on `UIElement`:

```cpp
// Called lazily: only while the element is attached to a window, and only when a
// UIA client actually reaches it (navigation, hit-test, or focus while listening).
virtual std::unique_ptr<automation::AutomationPeer> CreateAutomationPeer();
```

The default peer reports `Custom` for a focusable element and a raw-view-only `Pane` otherwise, named by `SetAutomationName()`. Built-in peers live in `automation/ControlPeers.h` (`ButtonPeer`, `TogglePeer`, `RadioButtonPeer`, `TextBlockPeer`, `TextEditPeer`, `GroupBoxPeer`). A control derived from `Button` keeps `ButtonPeer` without doing anything; override only to change what UIA sees:

```cpp
class StarRating : public FrameworkElement {
protected:
    std::unique_ptr<automation::AutomationPeer> CreateAutomationPeer() override;
};

class StarRatingPeer : public automation::AutomationPeer, public automation::IValuePeer {
public:
    explicit StarRatingPeer(StarRating& o) : AutomationPeer(o) {}
    automation::ControlType GetControlType() const override { return automation::ControlType::Slider; }
    bool IsControlElement() const override { return true; }
    automation::IValuePeer* AsValue() override { return this; }
    std::wstring GetValue() const override;              // read the control's state now
    bool IsReadOnly() const override { return false; }
    void SetValue(const std::wstring& v) override;       // runs from a posted message
};
```

Pattern interfaces: `IInvokePeer`, `ITogglePeer`, `IValuePeer`, `ISelectionItemPeer`, exposed through `AsInvoke()`/`AsToggle()`/`AsValue()`/`AsSelectionItem()` (return `this`, or null when unsupported).

Rules a peer must follow, and why:

- **Getters read live state, O(1).** No caching, no layout, no tree walk per property: UIA can ask hundreds of properties per second while Narrator is on, and every call is on the UI thread.
- **Actions may destroy the element.** `Invoke()`/`Toggle()`/`Select()`/`SetValue()` run app code (a Click handler can remove its own button). The framework calls them from a posted message after the COM call returned and re-checks that the element is still attached and enabled. Do not touch `this` after the call that raises app events (see `ButtonPeer::Invoke`).
- **The peer never outlives the attach period.** `DetachFromContext` and `~UIElement` release it; the provider UIA still holds is frozen and answers `UIA_E_ELEMENTNOTAVAILABLE`. Do not keep a pointer to a peer.
- **No UIA headers.** Peers see only `automation/AutomationTypes.h`; `<UIAutomation.h>` stays inside `automation/Uia*.cpp`.
- **Children** come from `UIElement::LogicalChildCount()/LogicalChildAt()`. A container that owns children outside `Panel` overrides those two (only attached, visible children are exposed); one that transforms its children overrides `MapChildRectToSelf` (Viewbox does) so UIA's bounding rectangle matches the pixels.
- **Property changes need no code.** While a peer exists, any invalidation of its element queues one re-check per flush; Name, IsEnabled, ToggleState and Value changes are raised only if they really changed and only while a client listens.

`ButtonBase::PerformActivate()` is the public programmatic activation (same effect as a click; ignored when disabled) that `ButtonPeer`/`TogglePeer`/`RadioButtonPeer` call.

More patterns are available to a peer: `IRangeValuePeer` (Slider, NumericUpDown, ProgressBar), `IExpandCollapsePeer` (Expander, ComboBox, TreeView rows, NavRail), `IScrollPeer` (ScrollPanel, ListBox, TreeView; percent-based, `-1` = axis cannot scroll), `ISelectionPeer` (containers; answered by index, `GetSelection` capped at 256), and `CanScrollIntoView()`/`ScrollIntoView()` for ScrollItem. Built-in peers: `automation/RangeAndContainerPeers.h`, `automation/SelectorPeers.h`.

**Items that are not elements.** A control that draws its own rows (a list, a tree, tab headers) derives its peer from `ItemsContainerPeer` (`automation/ItemPeers.h`), or from `SelectableContainerPeer` when the rows are selectable, and supplies `ItemCount()`, `ItemName(i)`, `ItemRect(i)` (host DIPs, empty when scrolled out), and the selection hooks. Row peers are created only when a client reaches them, keyed by index by default; override `KeyAt`/`IndexOfKey` to key by a stable id (TreeView does), so a row that only moved keeps its UIA identity. Rules:

- Never release item peers synchronously. `ItemAt` may run inside a client call made on behalf of another cached item; vanished rows and the bounded cache (`kMaxCachedItems` = 1024) are pruned in `OnFlushRecheck()`, which the host calls from its flush.
- A gone item (its key no longer maps to a row) stays connected until that flush but reports `IsEnabled() == false`, so an action on it is refused instead of handing an out-of-range index to the control.
- A container whose items can be many must override `VisibleItemRange(first, last)` (O(1), always returning true; `first = SIZE_MAX` when nothing is visible). Child navigation then walks only the visible rows plus a page on each side (at most `kMaxExposedItems` = 256), and a scroll that moves that window raises ChildrenInvalidated. Such a container also gets the ItemContainer pattern (`FindItemByProperty` by Name / IsSelected / next item, scanning `ItemNameAt(i)` and `ItemSelectedAt(i)` by index; override `ItemNameAt` when the default, which creates the item's peer, is too costly) and its items get VirtualizedItem (`Realize` = posted ScrollIntoView, offered when the item `CanScrollIntoView`). Without it every row is a sibling, and a client that lists siblings (Inspect does, for the focused element) walks all of them: 50,000 rows took ~22 s through a real client. The default exposes every item, which is right only for a handful.
- Hit-testing over many rows must be arithmetic: override `ItemAtPoint` (ListBox, TreeView) instead of the default scan. The point arrives in host DIPs; call `HostToOwner(x, y)` first so a scaled Viewbox maps it the inverse way `BoundsInHostDip` maps rects.
- Index keys (the default) are released wholesale whenever the item count changes, because index 5 may now be another row. Return `true` from `KeysAreStable()` only when `KeyAt` is a real row id (TreeView).
- Item property changes (IsSelected, an item's ExpandCollapseState) need no code: after the container's element invalidates, the flush re-checks up to `kMaxRecheckedItems` cached items (focused first, then visible ones).
- **Act on the user's path.** A pattern action is a user acting through an assistive tool. Call the entry point a click or keystroke uses, not a silent programmatic setter, or the app's event never fires: `NavRail::PerformSelect`, `Expander::PerformSetExpanded`, `NavRail::PerformSetPaneExpanded`, `ComboBox::ReplaceTextAsUserEdit`, `TextEditBase::ReplaceAllAsUserEdit`, `ButtonBase::PerformActivate`. A control whose user path is private needs a public `Perform...` added for its peer.


**Popup windows.** Content shown in a `PopupHost` is in another HWND, so it is not under the owner window's UIA root. A popup whose content should be reachable calls `PopupHost::EnableAutomation()` before `SetContent`; the popup then answers WM_GETOBJECT itself, its content's peer is the popup root's child, and a content peer that overrides `PopupOpenedEventId()` / `PopupClosedEventId()` gets that event raised when the popup is shown / hidden (only while a client listens). The content must be detached or destroyed before the PopupHost. MenuFlyout's levels are the example (`controls/MenuFlyout.cpp`, `MenuListPeer`).

**Live regions.** A peer whose element reports status (InfoBar) overrides `GetLiveSetting()` (Polite or Assertive) and raises LiveRegionChanged itself when its text changes: compare against a baseline in `OnFlushRecheck()` and call `IAutomationSite::QueueEvent(Provider(), 20024)`, which drops the event when nobody listens. `InfoBarPeer` (`automation/DataPeers.cpp`) is the example.

**Per-item enabled state and item kinds.** `SelectableContainerPeer::ItemEnabled(i)` makes an item report IsEnabled false (a disabled date, a NavRail section header), so UIA refuses Select on it. An item that is not selectable (InfoBar's close button, a log line) derives from `ItemPeer` directly and maps its control-space rect with `OwnerRectToHost`. Data controls key their items so content changes stay correct: DataGrid by row index, LogViewer by line serial (`automation/DataPeers.h` explains each choice).

**Text.** A peer that returns an `ITextPeer` from `AsText()` gets the UIA TextPattern (`automation/UiaTextPattern.cpp`): `Text()`, `SelectionStart()/SelectionLength()`, `SelectText(start, len)` (called from the flush, never inside the client's call), `OffsetAtPoint(x, y)` (host DIPs) and `IsTextReadOnly()`. Ranges are plain offset pairs that are clamped to the current text on every use, so a client holding a range across an edit reads the shortened text instead of reading out of bounds. Units: Character, Word (whitespace-separated; Format behaves as Word, since no formatting is exposed), Line (`
`; Paragraph behaves as Line), Document (Page behaves as Document). `TextEditPeer` raises `Text_TextChanged` / `Text_TextSelectionChanged` from the control's own `TextChanged` / `SelectionChanged` events through `IAutomationSite::QueueEvent`, which coalesces per flush and sends nothing without a listener. Password mode returns `nullptr` from `AsText()`: the text must not be readable. Known limit: `GetBoundingRectangles` returns the control's rectangle, not one per line, so a screen magnifier following the caret line does not move within a multi-line TextArea.

Programmatic entry points added for UIA, all public: `Hyperlink::PerformActivate()`, `ComboBox::IsDropDownOpen()`/`SetDropDownOpen()`, `ListBox::ItemRect()`/`ItemIndexAt()`/`ScrollItemIntoView()`/`Scroller()`, `TreeView::VisibleRowCount()`/`RowIndexAtVisible()`/`VisibleIndexOfId()`/`VisibleRowRect()`/`VisibleRowAt()`/`ScrollVisibleRowIntoView()`/`Scroller()`, `TextEditBase::ReplaceAllAsUserEdit()`, `TextEditBase::OffsetAtPoint()`, `Expander::PerformSetExpanded()`, `ComboBox::ReplaceTextAsUserEdit()`, `ListBox::SetScrollOffset()` (repaints), and in NavigationControls `NavRail::PerformSelect()`/`IsItemSelectable()`/`PerformSetPaneExpanded()`. Expander's peer is a Group (Name = `Header()`) whose ExpandCollapse goes through `PerformSetExpanded`: the app's `SetUserToggleTransition` and `ExpandedChanged`, exactly like a header click.

## Common Patterns

### Pattern 1: Call Base Then Decorate

Most inheritance needs **augment**, not **replace**:

```cpp
void RenderCell(const DrawingContext& dc, const CellInfo& cell) override {
    BaseControl::RenderOverride(dc);  // ✅ call base rendering first
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
This pattern keeps base layout changes compatible with subclasses.

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

### ProgressBar
**Status**: ✅ Existing APIs already sufficient

- ProgressBar derives `RangeBase`, which exposes `protected value_`, `min_`, `max_`; public `Value()`, `Minimum()`, `Maximum()`, plus `Fraction()`
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
