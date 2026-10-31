// TreeView.h — Lightweight Fluent tree/list control for value-owned row data.
//
// TreeView is a Selector<TreeViewRow>: the row vector lives in
// the ItemsControl base as items_, and the selection index lives in the Selector
// base as selectedIndex_ (a *row* index into items_). TreeView keeps its own
// tree-specific derived state (visible rows, id→index maps, expand/collapse,
// type-ahead) and overrides SetSelectedIndex so its public API still takes a
// *visible* index (mapped onto a row index before the base dedup/store), while
// SelectedIndex() continues to return the row index — the historical asymmetry is
// preserved deliberately.
#pragma once

#include "primitives/Selector.h"
#include "../base/Event.h"
#include "../graphics/DWriteContext.h"
#include "../layout/ScrollViewer.h"
#include <memory>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

struct ID2D1DeviceContext;
struct IDWriteTextFormat;

namespace fluent {

enum class TreeViewIcon {
    None,
    Computer,
    Folder,
    Device,
    Warning,
    Bluetooth,
    Disk,
};

struct TreeViewRow {
    int id = 0;
    int parentId = -1;
    std::wstring text;
    int depth = 0;
    bool dimmed = false;
    bool hasChildren = false;
    bool expanded = true;
    TreeViewIcon icon = TreeViewIcon::Device;
    void* user = nullptr;
    ID2D1Bitmap* customIcon = nullptr;  // Custom bitmap icon (takes priority over icon enum)
};

// Payload for TreeView::SelectionChanged: the visible index of the newly selected
// row and the row data itself.
struct TreeSelection {
    int visibleIndex = -1;
    const TreeViewRow* row = nullptr;
};

// Payload for TreeView::ItemDoubleClick: the double-clicked row with a handled
// flag. Setting handled=true prevents the default behavior (expand/collapse).
struct TreeViewItemDoubleClickArgs {
    int visibleIndex = -1;
    const TreeViewRow* row = nullptr;
    bool handled = false;
};

// SelectionMode now lives in Selector (primitives/Selector.h) — the enum moved
// there when the multi-select state was hoisted so ListBox could share it. It is
// still reachable as fluent::SelectionMode, so existing call sites are unchanged.

class TreeView : public Selector<TreeViewRow> {
public:
    TreeView() {
        SetFocusable(true);
    }

    // Replace the row set. Routes through the ItemsControl base (items_) so the
    // shared store owns the vector; OnItemsChanged rebuilds the tree indices and
    // restores the selection by id. Kept as SetRows for API compatibility.
    void SetRows(std::vector<TreeViewRow> rows);

    // --- Incremental mutation API -------------------------------------------
    // Add/remove/modify individual nodes without reading/writing the whole vector.
    // All methods preserve the selection by id and trigger OnItemsChanged (which
    // rebuilds indices + visible rows). The caller still supplies id/parentId, but
    // depth/hasChildren are computed automatically.

    // Add a root node (parentId = -1, depth = 0 auto-filled).
    void AddRoot(TreeViewRow row);
    // Add a child under parentId. Automatically fills row.parentId and row.depth,
    // and marks the parent as hasChildren=true. Inserts immediately after the parent
    // in the items_ vector so it appears in tree order.
    void AddChild(int parentId, TreeViewRow row);
    // Remove nodeId plus ALL its descendants (recursive). No-op if id not found.
    void RemoveNode(int nodeId);
    // Set expanded state of a single node. No-op if id not found or !hasChildren.
    void SetNodeExpanded(int nodeId, bool expanded);
    // Expand/collapse all nodes that have children.
    void ExpandAll();
    void CollapseAll();
    // Find a mutable row by id. Returns nullptr if not found.
    TreeViewRow* FindNode(int nodeId);
    const TreeViewRow* FindNode(int nodeId) const;

    // Selection: SetSelectedIndex takes a VISIBLE index (mapped to a row index);
    // SelectedIndex() returns the ROW index (inherited from Selector). SelectedRow
    // is an alias for SelectedItem() with the historical name.
    void SetSelectedIndex(int visibleIndex) override;
    const TreeViewRow* SelectedRow() const { return SelectedItem(); }

    // Selection mode + the multi-select API live in Selector now
    // (SetSelectionMode / GetSelectionMode / SelectedIndices / IsSelected /
    // ToggleSelection / RangeSelectTo). The four names below are kept as thin
    // row-flavoured aliases: they read better at a tree call site, and they are
    // what the existing tests and demo call. The indices are ROW indices (into
    // items_), not visible indices — same as before the hoist.
    std::vector<int> SelectedRowIndices() const { return SelectedIndices(); }
    bool IsRowSelected(int rowIndex) const { return IsSelected(rowIndex); }
    void ToggleRowSelection(int rowIndex) { ToggleSelection(rowIndex); }

    // Fired when the selected row changes (arrow keys, type-ahead, or a row
    // click). Replaces SetOnSelectionChanged(std::function<...>).
    Event<TreeView, TreeSelection>& SelectionChanged() { return selectionChanged_; }

    // Fired when the set of selected rows changes in Multiple mode (Ctrl+click,
    // Shift+click, or programmatic ToggleRowSelection / RangeSelectTo).
    Event<TreeView, std::vector<int>>& SelectionSetChanged() { return selectionSetChanged_; }

    // Fired when a row is double-clicked. The default behavior (when
    // args.handled is false) toggles expansion for rows with children. Set
    // handled=true to prevent the toggle and implement custom behavior (e.g., open
    // a file). Control the default behavior via SetDoubleClickTogglesExpansion.
    Event<TreeView, TreeViewItemDoubleClickArgs>& ItemDoubleClick() { return itemDoubleClick_; }
    
    // Whether double-clicking a row with children toggles its expansion (default: true).
    // Has no effect when the ItemDoubleClick event has a subscriber that sets handled=true.
    void SetDoubleClickTogglesExpansion(bool enable) { doubleClickTogglesExpansion_ = enable; }
    bool DoubleClickTogglesExpansion() const { return doubleClickTogglesExpansion_; }

    // The ROW index the pointer is currently over, or -1 when the pointer is
    // outside the control / not over a row. Read-only: hover is owned by the
    // pointer handlers. Exposed because the hover fill is otherwise unobservable
    // from a headless test (same reason as TabControl::IsContentAttached).
    int HoveredRowIndex() const { return hoverRowIndex_; }

    void ScrollBy(float deltaDip) { scroll_.ScrollBy(deltaDip); Invalidate(); }
    void OnKeyDownRouted(KeyEventArgs& e) override;
    void OnTextInput(wchar_t ch) override;
    void OnPointerWheelChanged(PointerEventArgs& e) override;
    bool WantsAnimationTick() const override;
    void OnAnimationTick(float dtSec) override;

    void Render(const DrawingContext& dc) override;

    // Routed pointer input: scrollbar drag (with capture) + row hover / click.
    void OnPointerMoved(PointerEventArgs& e) override;
    void OnPointerPressed(PointerEventArgs& e) override;
    void OnPointerReleased(PointerEventArgs& e) override;

protected:
    void OnAttachedToTree() override;
    void OnDetachedFromTree() override;
    void OnBoundsChanged() override;
    void OnFocusChanged() override;
    void OnThemeChanged() override;
    void OnDpiChanged(float dpiScale) override;
    void OnPointerLeft() override;

    // ItemsControl hook: rebuild derived indices + restore selection by id.
    void OnItemsChanged() override;
    // Selector hook: ensure the new row is visible + raise the rich TreeSelection.
    void OnSelectionChanged(int oldIndex, int newIndex) override;
    // Selector hook: the multi-select set changed via ToggleSelection /
    // RangeSelectTo. Raise SelectionSetChanged.
    void OnSelectionSetChanged() override;
    // Selector hook: a Shift range selects the VISIBLE rows between its ends, not
    // every row index — rows inside a collapsed node are skipped (see Selector).
    void AddRangeToSelectionSet(int lo, int hi) override;

private:
    // The scroll offset currently shown. Kept as a named accessor (rather than
    // inlining scroll_.Offset()) because hit-testing and EnsureVisible both need
    // "the offset on screen right now" and reading it through one function keeps the
    // two consistent.
    float CurrentOffset() const { return scroll_.Offset(); }
    // Shared single-row draw. `rowTopY` is the row's top in the target's DIP space.
    void DrawRow(const DrawingContext& dc, IDWriteTextFormat* fmt, int rowIndex,
                 float rowTopY, float leftX, float rightX);

    // --- Incremental mutation helpers ----------------------------------------
    // Snapshot the selected row's id into pendingSelectedId_ so OnItemsChanged can
    // restore the selection after items_ moves. Every mutator calls this BEFORE
    // touching items_ (an index captured after the edit means a different row).
    void CaptureSelectedId();
    // Is items_[rowIndex] a descendant of items_[ancestorIndex]? Walks parentIndex_,
    // so it is only valid while that array matches items_ (i.e. before an edit).
    bool IsDescendantOf(int rowIndex, int ancestorIndex) const;
    // Recompute every row's hasChildren from the parentId links, and clear the
    // collapsed flag on rows that are now leaves. Called after a removal, which is
    // the one edit that can orphan an expander glyph.
    void RefreshHasChildren();
    // Shared body of ExpandAll / CollapseAll.
    void SetAllExpanded(bool expanded);

    int HitRow(float dipY) const;
    int VisibleCapacity() const;
    int PageStep() const;
    void RebuildIndex();
    void RebuildVisible();
    int RowIndexById(int id) const;
    int VisibleIndexByRow(int rowIndex) const;
    void ToggleRow(int rowIndex);
    void EnsureSelectedVisible();
    bool MatchPrefix(const std::wstring& text, const std::wstring& prefix) const;
    // Select the first visible row (searching from just after the current
    // selection, then wrapping) whose text starts with `prefix`. Returns true if
    // a row was selected. Does NOT touch typeSearch_ — the caller owns that state
    // — so it can be reused for the single-character fallback without recursion.
    bool SelectByPrefix(const std::wstring& prefix);

    // Derived tree state (items_ + selectedIndex_ live in the Selector base).
    std::vector<int> visibleRows_;
    std::unordered_map<int, int> idToIndex_;
    std::vector<int> parentIndex_;
    int pendingSelectedId_ = -1;   // captured across a SetRows to restore selection
    float rowHeight_ = 28.0f;
    ScrollViewer scroll_;  // scrollbar visual model + UI-thread scroll offset
    std::wstring typeSearch_;
    DWORD lastTypeTick_ = 0;
    Event<TreeView, TreeSelection> selectionChanged_;
    Event<TreeView, std::vector<int>> selectionSetChanged_;
    Event<TreeView, TreeViewItemDoubleClickArgs> itemDoubleClick_;
    
    // Double-click detection state
    DWORD lastClickTime_ = 0;
    int lastClickRow_ = -1;
    bool doubleClickTogglesExpansion_ = true;
    
    // Multi-select state (selectionMode_ / selectedIndices_ / selectionAnchor_)
    // lives in the Selector base — see primitives/Selector.h for the invariants.
    
    // Hover tracking: which row the pointer is currently over, or -1.
    int hoverRowIndex_ = -1;
    // Last known pointer position and whether it's inside the control, for updating
    // hover during smooth scrolling (the content moves under a stationary pointer).
    Point lastPointerPos_;
    bool pointerInside_ = false;
};

} // namespace fluent
