// ListBox.h — Virtualized single-selection list control.
//
// ListBox is a simpler variant of TreeView: a flat list (no tree hierarchy),
// O(visible) rendering, single selection, keyboard navigation (Up/Down/Home/End/
// PageUp/PageDown), and mouse click selection. Supports two modes:
//
// 1. Direct items mode: SetItems(vector<wstring>) — the control owns the strings.
// 2. Virtualized mode: SetItemCount(n) + ItemTextProvider callback — for large
//    lists (100k+ items) where building the full vector up front would stall.
//
// Inherits from Selector<wstring> so selectedIndex_ and the selection protocol
// are shared. Does NOT use compositor scrolling (that can be added later if
// needed); uses the UI-thread ScrollViewer path only.
#pragma once

#include "primitives/Selector.h"
#include "../base/Event.h"
#include "../layout/ScrollViewer.h"
#include <functional>
#include <string>
#include <vector>

struct IDWriteTextFormat;

namespace fluent {

class ListBox : public Selector<std::wstring> {
public:
    ListBox() {
        SetFocusable(true);
        scroll_.SetKeepVisibleWhenOverflow(true);
    }

    // Direct items mode: the control owns the string vector.
    using Selector<std::wstring>::SetItems;

    // Virtualized mode: set the item count and a text provider callback.
    // ItemTextProvider is called on-demand for visible items only.
    void SetItemCount(size_t count);
    void SetItemHeight(float dip);
    float ItemHeight() const { return itemHeight_; }

    // Callback for virtualized mode: given an item index, return its display text.
    // If null, falls back to items_[index] (direct mode).
    std::function<std::wstring(size_t)> ItemTextProvider;

    // The effective item count: virtualItemCount_ when in virtualized mode, else
    // items_.size(). Shadows ItemsControl::ItemCount() deliberately — every caller
    // (hit-test, key nav, scroll extent, selection clamp) must see the virtual
    // count, and leaving the base version visible would make "which count" depend
    // on the static type at the call site.
    int ItemCount() const override;

    // The text for item i: via ItemTextProvider when set, else items_[i]. Public so
    // a test can verify the provider is actually consulted — the render loop is the
    // only other caller and a headless test cannot reach it (null dc).
    //
    // This value-returning form copies, and Render calls it once per VISIBLE ROW PER
    // FRAME. Virtualization already bounds that to O(visible), so the copy is not a
    // scaling bug — but a screenful of rows is tens of heap allocations per frame
    // during a scroll, all of them immediately discarded, and in direct-items mode
    // every one of them duplicates a string the control already owns. Prefer the
    // scratch overload below on any per-frame path; this form stays for callers that
    // genuinely want to keep the value (and for the test that pins the provider).
    std::wstring GetItemText(int i) const;

    // Zero-allocation form, same contract as TextEditBase::DisplayText(scratch).
    //
    // Direct-items mode returns a reference straight to items_[i] and touches
    // `scratch` not at all — no copy exists to make. Virtualized mode has to
    // materialize something (the provider returns by value, so there is no stable
    // object to point at), and moves it into the caller's `scratch`, which the
    // render loop reuses across every row of the frame: one buffer that grows to
    // the longest visible row and then stops reallocating, instead of one
    // allocation per row per frame.
    //
    // Lifetime: valid until the next mutation of the item source, or the next call
    // reusing the same `scratch`. Both existing call sites use it inside a single
    // block, which is the lifetime this is designed for. Do not stash the reference.
    const std::wstring& GetItemText(int i, std::wstring& scratch) const;

    // --- Row geometry and scrolling, for UI Automation ------------------------
    // Item i's rect in this control's coordinate space (host-client DIPs), clipped
    // to the viewport; empty when the row is scrolled out. O(1).
    RectDip ItemRect(int i) const;
    // The item index at a host-DIP y, or -1 (same arithmetic as the click path).
    int ItemIndexAt(float dipY) const { return HitItem(dipY); }
    // Scroll so item i is fully visible (the keyboard's minimal scroll).
    void ScrollItemIntoView(int i);
    // The list's own scroller, read-only (UIA's Scroll pattern reports it).
    const ScrollViewer& Scroller() const { return scroll_; }
    // Moves the list's own scroller and repaints. scroll_ is a member, not an
    // element, so it cannot schedule a frame itself (same as TreeView).
    void SetScrollOffset(float dip) {
        const float before = scroll_.Offset();
        scroll_.SetOffset(dip);
        if (scroll_.Offset() != before) Invalidate();
    }

    // Selection: inherited from Selector. SelectionChanged event is fired when
    // the ACTIVE index changes via keyboard, mouse, or SetSelectedIndex.
    Event<ListBox, int>& SelectionChanged() { return selectionChanged_; }

    // Multi-select comes from Selector: SetSelectionMode(SelectionMode::Multiple)
    // then Ctrl+click / Shift+click (and Ctrl+Space / Shift+arrows from the
    // keyboard) build the set, read back via SelectedIndices() / IsSelected().
    // Fired whenever that set changes without the active index moving — i.e. from
    // a Ctrl or Shift gesture. A plain selection change raises SelectionChanged
    // instead, so a subscriber to both sees exactly one event per gesture.
    Event<ListBox, std::vector<int>>& SelectionSetChanged() { return selectionSetChanged_; }

    // Keyboard navigation.
    void OnKeyDownRouted(KeyEventArgs& e) override;
    void OnPointerWheelChanged(PointerEventArgs& e) override;
    bool WantsAnimationTick() const override { return scroll_.NeedsTick(); }
    void OnAnimationTick(float dtSec) override {
        scroll_.Tick(dtSec);
        
        // Update hover during smooth scrolling: the content moves under the pointer,
        // which didn't move. Without this, the old hoverIndex_ points to an item that
        // scrolled away, leaving the actual item under the pointer without highlight.
        if (pointerInside_) {
            int newHover = HitItem(lastPointerPos_.y);
            if (newHover != hoverIndex_) {
                hoverIndex_ = newHover;
            }
        }
        
        // scroll_ is a MEMBER, not a tree node: the Invalidate() inside its Tick has
        // no path to the frame scheduler, so fade/expand steps would only appear when
        // some other element happened to invalidate that frame ("segmented" animation).
        Invalidate();
    }

    void Render(const DrawingContext& dc) override;

    // Mouse input: scrollbar drag + item click selection.
    void OnPointerMoved(PointerEventArgs& e) override;
    void OnPointerPressed(PointerEventArgs& e) override;
    void OnPointerReleased(PointerEventArgs& e) override;
    void OnPointerLeft() override;

protected:
    // UI Automation peer (automation/SelectorPeers.h).
    std::unique_ptr<automation::AutomationPeer> CreateAutomationPeer() override;
    void OnBoundsChanged() override;
    void OnFocusChanged() override { Invalidate(); }
    void OnThemeChanged() override { Invalidate(); }
    void OnAttachedToTree() override;
    void OnDetachedFromTree() override;

    // ItemsControl hook: update scroll extent when items change.
    void OnItemsChanged() override;
    // Selector hook: ensure the new selection is visible.
    void OnSelectionChanged(int oldIndex, int newIndex) override;
    // Selector hook: the multi-select set changed via a Ctrl/Shift gesture.
    void OnSelectionSetChanged() override;
    // The SelectionSetChanged event without the gesture's scroll-into-view; used when
    // a data change (SetItemCount shrink, SetItems replace) altered the set.
    void RaiseSelectionSetChanged();

private:
    // Hit-test: return the item index at the given Y coordinate, or -1.
    int HitItem(float dipY) const;
    // Visible capacity: how many items fit in the viewport.
    int VisibleCapacity() const;
    // Page step: items to scroll per PageUp/Down.
    int PageStep() const;
    // Ensure the selected item is visible.
    void EnsureSelectedVisible();
    // Update scroll content height based on item count.
    void UpdateScrollExtent();

    float itemHeight_ = 32.0f;
    size_t virtualItemCount_ = 0;  // 0 = use items_.size()
    ScrollViewer scroll_;
    int hoverIndex_ = -1;
    // Last known pointer position and whether it's inside the control, for updating
    // hover during smooth scrolling (the content moves under a stationary pointer).
    Point lastPointerPos_;
    bool pointerInside_ = false;
    Event<ListBox, int> selectionChanged_;
    Event<ListBox, std::vector<int>> selectionSetChanged_;
};

} // namespace fluent
