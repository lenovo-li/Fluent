// TreeView.cpp

#include "TreeView.h"
#include "../styling/ThemeTokens.h"
#include "../core/ScrollMath.h"
#include "../input/InputManager.h"
#include "../window/WindowServices.h"
#include "../graphics/PixelSnapping.h"
#include <algorithm>
#include <cmath>

namespace fluent {

namespace {

void DrawIcon(const DrawingContext& dc, TreeViewIcon icon, float x, float y,
              const ColorTokens& pal)
{
    D2D1_COLOR_F c = pal.textSecondary;
    switch (icon) {
    case TreeViewIcon::Computer: c = pal.accent; break;
    case TreeViewIcon::Folder: c = D2D1::ColorF(0.86f, 0.60f, 0.18f, 1.0f); break;
    case TreeViewIcon::Warning: c = D2D1::ColorF(0.90f, 0.38f, 0.16f, 1.0f); break;
    case TreeViewIcon::Bluetooth: c = D2D1::ColorF(0.18f, 0.47f, 0.86f, 1.0f); break;
    case TreeViewIcon::Disk: c = D2D1::ColorF(0.25f, 0.55f, 0.78f, 1.0f); break;
    default: c = pal.textSecondary; break;
    }

    // Snap all icon coordinates to the pixel grid so geometric shapes render crisp at
    // any DPI. Without this, fractional coordinates cause D2D to antialias the edges,
    // making small icons (14×14 DIP) look blurry. The base (x, y) is already snapped
    // by the caller, but we must also snap every offset point (x+1, x+7, etc.) because
    // at non-integer DPI (125%, 150%) those offsets yield half-pixels too.
    const float dpiScale = dc.DpiScale();
    auto snap = [dpiScale](float dip) { return SnapToPixel(dip, dpiScale); };

    if (icon == TreeViewIcon::Folder) {
        dc.FillRoundedRect(D2D1::RoundedRect(D2D1::RectF(snap(x + 1), snap(y + 5), snap(x + 13), snap(y + 12)), 1.5f, 1.5f), c);
        dc.FillRoundedRect(D2D1::RoundedRect(D2D1::RectF(snap(x + 2), snap(y + 3), snap(x + 8), snap(y + 6)), 1.0f, 1.0f), c);
        return;
    }
    if (icon == TreeViewIcon::Warning) {
        dc.DrawLine(D2D1::Point2F(snap(x + 7), snap(y + 2)), D2D1::Point2F(snap(x + 13), snap(y + 12)), c, 1.3f);
        dc.DrawLine(D2D1::Point2F(snap(x + 13), snap(y + 12)), D2D1::Point2F(snap(x + 1), snap(y + 12)), c, 1.3f);
        dc.DrawLine(D2D1::Point2F(snap(x + 1), snap(y + 12)), D2D1::Point2F(snap(x + 7), snap(y + 2)), c, 1.3f);
        dc.DrawLine(D2D1::Point2F(snap(x + 7), snap(y + 5)), D2D1::Point2F(snap(x + 7), snap(y + 8)), c, 1.2f);
        dc.FillEllipse(D2D1::Ellipse(D2D1::Point2F(snap(x + 7), snap(y + 10.5f)), 0.8f, 0.8f), c);
        return;
    }
    if (icon == TreeViewIcon::Bluetooth) {
        dc.DrawLine(D2D1::Point2F(snap(x + 7), snap(y + 2)), D2D1::Point2F(snap(x + 7), snap(y + 12)), c, 1.3f);
        dc.DrawLine(D2D1::Point2F(snap(x + 7), snap(y + 2)), D2D1::Point2F(snap(x + 11), snap(y + 5)), c, 1.3f);
        dc.DrawLine(D2D1::Point2F(snap(x + 11), snap(y + 5)), D2D1::Point2F(snap(x + 4), snap(y + 10)), c, 1.3f);
        dc.DrawLine(D2D1::Point2F(snap(x + 7), snap(y + 12)), D2D1::Point2F(snap(x + 11), snap(y + 9)), c, 1.3f);
        dc.DrawLine(D2D1::Point2F(snap(x + 11), snap(y + 9)), D2D1::Point2F(snap(x + 4), snap(y + 4)), c, 1.3f);
        return;
    }
    if (icon == TreeViewIcon::Disk) {
        dc.FillRoundedRect(D2D1::RoundedRect(D2D1::RectF(snap(x + 2), snap(y + 3), snap(x + 12), snap(y + 11)), 2.0f, 2.0f), c);
        dc.FillEllipse(D2D1::Ellipse(D2D1::Point2F(snap(x + 7), snap(y + 7)), 2.0f, 2.0f), pal.cardFill);
        return;
    }

    if (icon == TreeViewIcon::Computer) {
        // Monitor body
        dc.DrawRoundedRect(D2D1::RoundedRect(D2D1::RectF(snap(x + 1), snap(y + 2), snap(x + 13), snap(y + 10)), 1.5f, 1.5f), c, 1.2f);
        // Stand foot
        dc.DrawLine(D2D1::Point2F(snap(x + 4), snap(y + 10)), D2D1::Point2F(snap(x + 4), snap(y + 12)), c, 1.2f);
        dc.DrawLine(D2D1::Point2F(snap(x + 10), snap(y + 10)), D2D1::Point2F(snap(x + 10), snap(y + 12)), c, 1.2f);
        dc.DrawLine(D2D1::Point2F(snap(x + 3), snap(y + 12)), D2D1::Point2F(snap(x + 11), snap(y + 12)), c, 1.2f);
    } else {
        // Generic device — IC chip: filled body with pin lines on both sides.
        dc.FillRoundedRect(D2D1::RoundedRect(D2D1::RectF(snap(x + 4), snap(y + 3), snap(x + 10), snap(y + 11)), 1.5f, 1.5f), c);
        // Left pins
        dc.DrawLine(D2D1::Point2F(snap(x + 2), snap(y + 5)), D2D1::Point2F(snap(x + 4), snap(y + 5)), c, 1.2f);
        dc.DrawLine(D2D1::Point2F(snap(x + 2), snap(y + 7)), D2D1::Point2F(snap(x + 4), snap(y + 7)), c, 1.2f);
        dc.DrawLine(D2D1::Point2F(snap(x + 2), snap(y + 9)), D2D1::Point2F(snap(x + 4), snap(y + 9)), c, 1.2f);
        // Right pins
        dc.DrawLine(D2D1::Point2F(snap(x + 10), snap(y + 5)), D2D1::Point2F(snap(x + 12), snap(y + 5)), c, 1.2f);
        dc.DrawLine(D2D1::Point2F(snap(x + 10), snap(y + 7)), D2D1::Point2F(snap(x + 12), snap(y + 7)), c, 1.2f);
        dc.DrawLine(D2D1::Point2F(snap(x + 10), snap(y + 9)), D2D1::Point2F(snap(x + 12), snap(y + 9)), c, 1.2f);
    }
}

} // namespace

float TreeView::RowTextWidth(int rowIndex, IDWriteTextFormat* fmt)
{
    if (rowIndex < 0 || rowIndex >= static_cast<int>(items_.size())) return 0.0f;
    if (rowTextWidth_.empty() || rowIndex >= static_cast<int>(rowTextWidth_.size()))
        return 0.0f;
    if (rowTextWidth_[rowIndex] >= 0.0f)
        return rowTextWidth_[rowIndex];  // cached

    const TreeViewRow& row = items_[rowIndex];
    if (row.text.empty()) {
        rowTextWidth_[rowIndex] = 0.0f;
        return 0.0f;
    }

    // Measure via DirectWrite layout. One CreateTextLayout per row, the first time
    // it scrolls into view; steady-state frames build zero layouts.
    ComPtr<IDWriteTextLayout> layout;
    if (FAILED(Dwrite()->Factory()->CreateTextLayout(
            row.text.c_str(), static_cast<UINT32>(row.text.size()), fmt,
            100000.0f, 100.0f, layout.GetAddressOf()))) {
        rowTextWidth_[rowIndex] = 0.0f;
        return 0.0f;
    }
    DWRITE_TEXT_METRICS m{};
    if (FAILED(layout->GetMetrics(&m))) {
        rowTextWidth_[rowIndex] = 0.0f;
        return 0.0f;
    }
    rowTextWidth_[rowIndex] = m.widthIncludingTrailingWhitespace;
    return rowTextWidth_[rowIndex];
}

void TreeView::UpdateVerticalExtent()
{
    // The horizontal rail, when live, covers the bottom 16 DIP of content; inflate
    // the vertical extent by that strip so the last row scrolls fully above the
    // rail instead of resting under it (same rule as ScrollPanel::UpdateScrollExtent).
    const float hRail = (scroll_.MaxOffsetX() > 0.0f) ? 16.0f : 0.0f;
    scroll_.SetContentHeight(rowHeight_ * visibleRows_.size() + hRail);
}

void TreeView::SetRows(std::vector<TreeViewRow> rows)
{
    // Capture the selected row's id so it can be restored after the store is
    // replaced (OnItemsChanged reads pendingSelectedId_). Then route through the
    // ItemsControl base, which stores items_ and calls OnItemsChanged.
    pendingSelectedId_ = (selectedIndex_ >= 0 && selectedIndex_ < ItemCount())
                             ? items_[selectedIndex_].id
                             : -1;
    SetItems(std::move(rows));
}

// --- Incremental mutation API ------------------------------------------------
// Each mutator captures the selected id, edits items_ in place, then routes
// through OnItemsChanged (which rebuilds idToIndex_/parentIndex_/visibleRows_ and
// restores selection by id). The mutators do NOT call SetItems: items_ is already
// the store, so a move-assign of itself would be wasteful. They call
// OnItemsChanged directly, which is the same hook SetItems fires.

void TreeView::CaptureSelectedId()
{
    pendingSelectedId_ = (selectedIndex_ >= 0 && selectedIndex_ < ItemCount())
                             ? items_[selectedIndex_].id
                             : -1;
}

TreeViewRow* TreeView::FindNode(int nodeId)
{
    int idx = RowIndexById(nodeId);
    return idx < 0 ? nullptr : &items_[idx];
}

const TreeViewRow* TreeView::FindNode(int nodeId) const
{
    int idx = RowIndexById(nodeId);
    return idx < 0 ? nullptr : &items_[idx];
}

void TreeView::AddRoot(TreeViewRow row)
{
    row.parentId = -1;
    row.depth = 0;
    CaptureSelectedId();
    items_.push_back(std::move(row));
    OnItemsChanged();
}

void TreeView::AddChild(int parentId, TreeViewRow row)
{
    const int parentIdx = RowIndexById(parentId);
    if (parentIdx < 0) return;  // unknown parent: no-op rather than silent root add

    row.parentId = parentId;
    row.depth = items_[parentIdx].depth + 1;

    // The parent now has at least one child, so it needs the expander glyph. Do NOT
    // force expanded=true here: a caller populating a collapsed subtree would be
    // fighting the control. Only hasChildren is a derived fact.
    items_[parentIdx].hasChildren = true;

    // Insert AFTER the parent's whole existing subtree, not immediately after the
    // parent row. Inserting at parentIdx+1 would place the new node BEFORE its
    // older siblings' descendants, so RebuildVisible (which walks items_ in order)
    // would render it in the wrong place — a child appearing above its own nephews.
    int insertAt = parentIdx + 1;
    while (insertAt < ItemCount() && IsDescendantOf(insertAt, parentIdx))
        ++insertAt;

    CaptureSelectedId();
    items_.insert(items_.begin() + insertAt, std::move(row));
    OnItemsChanged();
}

void TreeView::RemoveNode(int nodeId)
{
    const int idx = RowIndexById(nodeId);
    if (idx < 0) return;

    // Collect the node plus every descendant. parentIndex_ is still valid here (it
    // is rebuilt only by OnItemsChanged), so ancestry is a pointer chase per row
    // rather than an id lookup per level.
    std::vector<bool> doomed(items_.size(), false);
    doomed[idx] = true;
    for (int i = idx + 1; i < ItemCount(); ++i)
        if (IsDescendantOf(i, idx)) doomed[i] = true;

    CaptureSelectedId();
    // If the selection is inside the doomed subtree it cannot be restored by id;
    // drop it so OnItemsChanged falls back to the first visible row instead of
    // resurrecting a stale index.
    if (selectedIndex_ >= 0 && selectedIndex_ < static_cast<int>(doomed.size()) &&
        doomed[selectedIndex_])
        pendingSelectedId_ = -1;

    int write = 0;
    for (int read = 0; read < ItemCount(); ++read)
        if (!doomed[read]) items_[write++] = std::move(items_[read]);
    items_.resize(write);

    // The parent may have just lost its last child.
    RefreshHasChildren();
    OnItemsChanged();
}

void TreeView::SetNodeExpanded(int nodeId, bool expanded)
{
    TreeViewRow* row = FindNode(nodeId);
    if (!row || !row->hasChildren) return;
    if (row->expanded == expanded) return;
    row->expanded = expanded;
    CaptureSelectedId();
    OnItemsChanged();
}

void TreeView::ExpandAll() { SetAllExpanded(true); }
void TreeView::CollapseAll() { SetAllExpanded(false); }

void TreeView::SetAllExpanded(bool expanded)
{
    bool changed = false;
    for (auto& row : items_) {
        if (row.hasChildren && row.expanded != expanded) {
            row.expanded = expanded;
            changed = true;
        }
    }
    if (!changed) return;
    CaptureSelectedId();
    OnItemsChanged();
}

bool TreeView::IsDescendantOf(int rowIndex, int ancestorIndex) const
{
    if (rowIndex < 0 || rowIndex >= static_cast<int>(parentIndex_.size())) return false;
    int p = parentIndex_[rowIndex];
    while (p >= 0) {
        if (p == ancestorIndex) return true;
        if (p >= static_cast<int>(parentIndex_.size())) break;
        p = parentIndex_[p];
    }
    return false;
}

void TreeView::RefreshHasChildren()
{
    // items_ was just mutated, so idToIndex_ is stale — derive the child counts from
    // the parentId fields directly instead of going through the index.
    std::unordered_set<int> parentsWithChildren;
    parentsWithChildren.reserve(items_.size());
    for (const auto& row : items_)
        if (row.parentId >= 0) parentsWithChildren.insert(row.parentId);

    for (auto& row : items_) {
        const bool has = parentsWithChildren.count(row.id) > 0;
        row.hasChildren = has;
        // A leaf must not keep a stale collapsed flag: if it ever gains a child
        // again, AddChild sets hasChildren and the caller decides expansion.
        if (!has) row.expanded = true;
    }
}

void TreeView::OnItemsChanged()
{
    RebuildIndex();
    RebuildVisible();

    // Restore selection by id (falls back to the first visible row). Set the base
    // storage directly — this is not a user-driven selection change, so it does
    // not raise SelectionChanged (matches the historical SetRows behavior).
    int restored = RowIndexById(pendingSelectedId_);
    if (restored < 0 && !visibleRows_.empty())
        restored = visibleRows_[0];
    selectedIndex_ = restored;
    pendingSelectedId_ = -1;

    // Re-seed the multi-select set from the restored single selection. The old set
    // held row indices into the PREVIOUS items_, where the same index now means a
    // different row (or none) — see Selector::ReseedSelectionSet for why this
    // re-seeds rather than only clearing.
    ReseedSelectionSet();

    // Clear hover: the row set changed, so the old index no longer points to the
    // same row (or may now be out of range).
    hoverRowIndex_ = -1;

    // Reset horizontal extent: drop the width cache and mark stale. The next Render
    // measures every visible row from scratch. Clearing the cache means the FIRST
    // frame after SetRows does N CreateTextLayout calls (N = visible capacity), but
    // that is still much cheaper than doing it for the entire tree in SetRows.
    rowTextWidth_.assign(items_.size(), -1.0f);
    maxSeenContentWidth_ = 0.0f;
    extentStale_ = true;

    UpdateVerticalExtent();
    Invalidate();
}

void TreeView::SetSelectedIndex(int visibleIndex)
{
    // The public API takes a VISIBLE index; map it to a row index, then delegate
    // to the Selector base for the dedup/store + OnSelectionChanged hook.
    // -1 means "no selection" and passes through unchanged.
    int rowIndex = -1;
    if (visibleIndex >= 0 && !visibleRows_.empty()) {
        int vi = std::clamp(visibleIndex, 0, static_cast<int>(visibleRows_.size()) - 1);
        rowIndex = visibleRows_[vi];
    }
    Selector<TreeViewRow>::SetSelectedIndex(rowIndex);
}

void TreeView::OnSelectionChanged(int /*oldIndex*/, int newIndex)
{
    // The set collapse for a plain (non-modifier) selection now happens in
    // Selector::SetSelectedIndex, before this hook runs — it is the one place
    // every such change funnels through, so no subclass has to remember it.
    EnsureSelectedVisible();
    if (newIndex >= 0 && newIndex < ItemCount()) {
        TreeSelection sel{VisibleIndexByRow(newIndex), &items_[newIndex]};
        selectionChanged_.Raise(*this, sel);
    }
}

// --- Multi-select side effects ---------------------------------------------
// The state and the four gesture methods live in Selector now. What stays here
// is the tree-specific consequence of a set change: invalidation and typed event.
void TreeView::OnSelectionSetChanged()
{
    // Nobody listening: skip expanding the ranges into one int per selected row.
    // The set itself is range-based, so this vector<int> payload is the only
    // O(selected) step left; a cost-sensitive subscriber can read SelectedRanges().
    if (!selectionSetChanged_.HasSubscribers()) return;
    // Event::Raise takes Args& (non-const), so the vector must be an lvalue.
    std::vector<int> sel = SelectedIndices();
    selectionSetChanged_.Raise(*this, sel);
}

void TreeView::AddRangeToSelectionSet(int lo, int hi)
{
    // visibleRows_ holds the visible row indices in ascending (pre-order) order, so
    // the visible rows in [lo, hi] are one contiguous slice of it, found by binary
    // search. The ends themselves are normally visible (a click or arrow target),
    // but the anchor may have been collapsed away since; it is then simply not in
    // the slice, which is the honest answer — it is no longer on screen.
    auto first = std::lower_bound(visibleRows_.begin(), visibleRows_.end(), lo);
    auto last = std::upper_bound(first, visibleRows_.end(), hi);
    // Coalesce the slice into runs of consecutive row indices: a fully expanded
    // stretch is ONE range however long it is, and each collapsed node in the range
    // only costs a break between two runs. The runs arrive in ascending order, so
    // every AddRange is an append and the set never shifts. The scan is still
    // O(visible rows in the range) — the visible list is materialized anyway — but it
    // is integer compares in a contiguous array, not one hash-node allocation per row.
    auto it = first;
    while (it != last) {
        const int runLo = *it;
        int runHi = runLo;
        for (++it; it != last && *it == runHi + 1; ++it) runHi = *it;
        selectedSet_.AddRange(runLo, runHi);
    }
}

void TreeView::OnBoundsChanged()
{
    LayoutCostProbe::Scope probe(LayoutCostKey::TreeViewBoundsChanged);
    scroll_.SetBounds(bounds_);
    // SetBounds re-clamps both offsets against the new viewport; whether the
    // horizontal rail is live may have flipped with the width, so the vertical
    // extent (which reserves the rail's strip) is recomputed after it.
    UpdateVerticalExtent();
}

int TreeView::VisibleCapacity() const
{
    // Use ceil to ensure we render enough rows to cover the viewport. At the worst
    // scroll position (top row almost fully scrolled out, bottom row almost fully
    // scrolled in), we need: 1 (top partial) + N (full rows) + 1 (bottom partial).
    // Without ceil, bounds_.h=200 / rowHeight_=28 = 7.14 truncates to 7, giving
    // 7+1=8 rows when we actually need ceil(7.14)+1=9. The missing row causes the
    // bottom edge to flicker blank during smooth scrolling. Matches DataGrid pattern.
    return std::max(0, static_cast<int>(std::ceil(bounds_.h / rowHeight_)) + 1);
}

int TreeView::PageStep() const
{
    return std::max(1, static_cast<int>(bounds_.h / rowHeight_) - 1);
}

void TreeView::RebuildIndex()
{
    idToIndex_.clear();
    idToIndex_.reserve(items_.size());
    for (int i = 0; i < static_cast<int>(items_.size()); ++i) {
        // Ids are the node identity (parent links, expand/collapse, selection restore all
        // go through this map), so a duplicate silently re-points every lookup at the
        // LAST row carrying it: the Gallery once listed a group root twice, and clicking
        // the first copy toggled the second. Report it; keep last-wins so behaviour is
        // unchanged for callers that never hit this.
        auto [it, inserted] = idToIndex_.emplace(items_[i].id, i);
        if (!inserted) {
            FL_TRACEF("TreeView", "duplicate node id %d at rows %d and %d; lookups use row %d",
                      items_[i].id, it->second, i, i);
            it->second = i;
        }
    }

    parentIndex_.assign(items_.size(), -1);
    for (int i = 0; i < static_cast<int>(items_.size()); ++i) {
        auto it = idToIndex_.find(items_[i].parentId);
        if (it != idToIndex_.end()) parentIndex_[i] = it->second;
    }
}

void TreeView::RebuildVisible()
{
    visibleRows_.clear();
    for (int i = 0; i < static_cast<int>(items_.size()); ++i) {
        bool isHidden = false;
        int parentIndex = (i < static_cast<int>(parentIndex_.size())) ? parentIndex_[i] : -1;
        while (parentIndex >= 0) {
            if (!items_[parentIndex].expanded) { isHidden = true; break; }
            parentIndex = (parentIndex < static_cast<int>(parentIndex_.size())) ? parentIndex_[parentIndex] : -1;
        }
        if (!isHidden) visibleRows_.push_back(i);
    }
    visiblePos_.assign(items_.size(), -1);
    for (int v = 0; v < static_cast<int>(visibleRows_.size()); ++v) visiblePos_[visibleRows_[v]] = v;
}

int TreeView::RowIndexById(int id) const
{
    if (id < 0) return -1;
    auto it = idToIndex_.find(id);
    return it == idToIndex_.end() ? -1 : it->second;
}

int TreeView::VisibleIndexByRow(int rowIndex) const
{
    return rowIndex >= 0 && rowIndex < static_cast<int>(visiblePos_.size()) ? visiblePos_[rowIndex] : -1;
}

int TreeView::VisibleIndexOfId(int id) const { return VisibleIndexByRow(RowIndexById(id)); }

RectDip TreeView::VisibleRowRect(int v) const
{
    if (v < 0 || v >= VisibleRowCount()) return {};
    const float top = bounds_.y + v * rowHeight_ - CurrentOffset();
    const float y0 = std::max(top, bounds_.y);
    const float y1 = std::min(top + rowHeight_, bounds_.bottom());
    if (y1 <= y0) return {};
    return {bounds_.x, y0, bounds_.w, y1 - y0};
}

int TreeView::VisibleRowAt(float dipY) const
{
    const int v = static_cast<int>((dipY - bounds_.y + CurrentOffset()) / rowHeight_);
    return v >= 0 && v < VisibleRowCount() ? v : -1;
}

void TreeView::ScrollVisibleRowIntoView(int v)
{
    if (v < 0 || v >= VisibleRowCount()) return;
    const float target = EnsureVisibleOffset(v * rowHeight_, rowHeight_, CurrentOffset(), bounds_.h);
    if (target != scroll_.Offset()) {
        scroll_.SetOffset(target);
        Invalidate();
    }
}

int TreeView::HitRow(float dipY) const
{
    // Use the EFFECTIVE offset (the in-flight position, mid-tween) so a click during a
    // smooth scroll hits the row actually under the cursor.
    float y = dipY - bounds_.y + CurrentOffset();
    int visibleIndex = static_cast<int>(y / rowHeight_);
    return (visibleIndex >= 0 && visibleIndex < static_cast<int>(visibleRows_.size()))
        ? visibleRows_[visibleIndex]
        : -1;
}

void TreeView::ToggleRow(int rowIndex)
{
    if (rowIndex < 0 || rowIndex >= static_cast<int>(items_.size())) return;
    if (!items_[rowIndex].hasChildren) return;
    items_[rowIndex].expanded = !items_[rowIndex].expanded;
    int selectedId = (selectedIndex_ >= 0) ? items_[selectedIndex_].id : -1;
    RebuildVisible();
    selectedIndex_ = RowIndexById(selectedId);
    if (selectedIndex_ >= 0 && VisibleIndexByRow(selectedIndex_) < 0)
        selectedIndex_ = rowIndex;
    UpdateVerticalExtent();
    Invalidate();
}

void TreeView::OnPointerMoved(PointerEventArgs& e)
{
    float dipX = e.position.x, dipY = e.position.y;
    lastPointerPos_ = e.position;
    pointerInside_ = true;

    // Vertical scrollbar drag.
    if (scroll_.IsDragging()) {
        scroll_.DragTo(dipY);
        Invalidate(); e.handled = true; return;
    }
    // Horizontal scrollbar drag.
    if (scroll_.IsHDragging()) {
        scroll_.HDragTo(dipX);
        Invalidate(); e.handled = true; return;
    }

    // Any move over the control keeps the thin rail visible; nearing the right
    // edge expands the vertical rail, nearing the bottom expands the horizontal rail.
    scroll_.Wake();
    scroll_.SetBarHover(scroll_.HitBarRegion(dipX, dipY));
    scroll_.SetHBarHover(scroll_.HitHBarRegion(dipX, dipY));

    // Row hover: update hoverRowIndex_ for visual feedback (subtle fill on the row).
    int newHover = HitRow(dipY);
    if (newHover != hoverRowIndex_) {
        hoverRowIndex_ = newHover;
        Invalidate();
    }
}

void TreeView::OnPointerPressed(PointerEventArgs& e)
{
    if (e.button != PointerButton::Left) return;
    float dipX = e.position.x, dipY = e.position.y;

    // Vertical scrollbar drag: press anywhere in the hover strip (16 DIP), not just the
    // visible thumb (3-7 DIP). A rail that lights up on hover but ignores clicks
    // outside the narrow thumb reads as broken, and 3 DIP is not a real target.
    if (scroll_.HitBarRegion(dipX, dipY)) {
        // Off-thumb presses first center the thumb on the pointer, then drag from
        // there. Without the jump the thumb would stay put and leap on the first
        // move, which feels like lag. With it the list jumps to roughly where the
        // pointer landed — consistent with how page up/down behave (they jump too).
        if (!scroll_.HitThumb(dipX, dipY)) {
            const RectDip thumb = scroll_.ThumbRect();
            const float trackRange = std::max(1.0f, bounds_.h - thumb.h);
            const float wantTop = (dipY - thumb.h * 0.5f) - bounds_.y;
            scroll_.SetOffset(wantTop / trackRange * scroll_.MaxOffset());
            Invalidate();
        }
        scroll_.BeginDrag(dipY);
        if (Context().input) Context().input->CapturePointer(this);
        Invalidate();
        e.handled = true;
        return;
    }

    // Horizontal scrollbar drag: same logic as vertical, but on the X axis.
    if (scroll_.HitHBarRegion(dipX, dipY)) {
        if (!scroll_.HitHThumb(dipX, dipY)) {
            const RectDip thumb = scroll_.HThumbRect();
            const float trackRange = std::max(1.0f, scroll_.HTrackLength() - thumb.w);
            const float wantLeft = (dipX - thumb.w * 0.5f) - bounds_.x;
            scroll_.SetOffsetX(wantLeft / trackRange * scroll_.MaxOffsetX());
            Invalidate();
        }
        scroll_.BeginHDrag(dipX);
        if (Context().input) Context().input->CapturePointer(this);
        Invalidate();
        e.handled = true;
        return;
    }

    // Row press: capture so the matching release routes back to us even if the
    // pointer drifts, then act on release (chevron toggle or row select).
    if (Context().input) Context().input->CapturePointer(this);
    e.handled = true;
}

void TreeView::OnPointerReleased(PointerEventArgs& e)
{
    if (e.button != PointerButton::Left) return;
    bool wasDragging = scroll_.IsDragging();
    bool wasDraggingX = scroll_.IsHDragging();
    if (wasDragging) { scroll_.EndDrag(); Invalidate(); }
    if (wasDraggingX) { scroll_.EndHDrag(); Invalidate(); }
    if (Context().input && Context().input->Captured() == this)
        Context().input->ReleaseCapture(this);
    e.handled = true;
    if (wasDragging || wasDraggingX) return;

    // A row click only counts when released inside the control (drag-off cancels).
    float dipX = e.position.x, dipY = e.position.y;
    if (!bounds_.contains(dipX, dipY)) return;
    if (scroll_.HitBarRegion(dipX, dipY) || scroll_.HitHBarRegion(dipX, dipY)) return;

    int rowIndex = HitRow(dipY);
    if (rowIndex < 0) return;

    // Chevron hit-test: the kChevronColumn strip the expander triangle is centered
    // in, shifted by the horizontal scroll. Reads the same TreeViewRowLayout as
    // DrawRow so the glyph and its hit box cannot drift apart.
    const TreeViewRowLayout l = TreeViewRowLayout::For(items_[rowIndex].depth);
    float scrollX = scroll_.OffsetX();
    float chevronLeft = bounds_.x + l.chevronLeft - scrollX;
    float chevronRight = chevronLeft + TreeViewRowLayout::kChevronColumn;
    if (items_[rowIndex].hasChildren && dipX >= chevronLeft && dipX < chevronRight) {
        ToggleRow(rowIndex);
        return;
    }

    // Modifier-aware selection (Multiple mode only; Single mode ignores modifiers
    // so a Ctrl+click there behaves exactly as before).
    //
    // Ctrl wins over Shift when both are held: that is what Explorer and the VS
    // solution explorer do, and the alternative (Ctrl+Shift extending without
    // clearing) needs a second anchor concept to be well-defined.
    if (selectionMode_ == SelectionMode::Multiple) {
        const bool ctrl = (e.modifiers & ModifierKeys::Ctrl) != ModifierKeys::None;
        const bool shift = (e.modifiers & ModifierKeys::Shift) != ModifierKeys::None;
        if (ctrl) {
            ToggleRowSelection(rowIndex);
            return;
        }
        if (shift) {
            RangeSelectTo(rowIndex);
            return;
        }
        // Plain click falls through to SetSelectedIndex, whose OnSelectionChanged
        // hook collapses the set to this row and re-anchors — so no set handling is
        // needed here. Doing it here as well would raise SelectionSetChanged twice.
    }

    int visibleIndex = VisibleIndexByRow(rowIndex);
    if (visibleIndex >= 0) SetSelectedIndex(visibleIndex);

    // Double-click detection: check if this click is within the double-click
    // window and on the same row as the last click.
    DWORD now = GetTickCount();
    DWORD doubleClickTime = GetDoubleClickTime();  // System setting (usually 500ms)
    bool isDoubleClick = (now - lastClickTime_ <= doubleClickTime) &&
                         (lastClickRow_ == rowIndex);

    if (isDoubleClick) {
        // Raise ItemDoubleClick event
        TreeViewItemDoubleClickArgs args;
        args.visibleIndex = visibleIndex;
        args.row = (rowIndex >= 0 && rowIndex < static_cast<int>(items_.size()))
                       ? &items_[rowIndex]
                       : nullptr;
        args.handled = false;

        if (itemDoubleClick_.HasSubscribers()) {
            itemDoubleClick_.Raise(*this, args);
        }

        // Default behavior: toggle expansion if row has children, not handled by subscriber,
        // and doubleClickTogglesExpansion_ is true
        if (!args.handled && doubleClickTogglesExpansion_ && items_[rowIndex].hasChildren) {
            ToggleRow(rowIndex);
        }

        // Reset double-click state to prevent triple-click from being detected as double-click
        lastClickTime_ = 0;
        lastClickRow_ = -1;
    } else {
        // Record this click for double-click detection
        lastClickTime_ = now;
        lastClickRow_ = rowIndex;
    }
}

void TreeView::OnPointerLeft()
{
    pointerInside_ = false;
    // Pointer left the control entirely: drop bar hover so both rails shrink + fade.
    scroll_.SetBarHover(false);
    scroll_.SetHBarHover(false);
    // Clear row hover.
    if (hoverRowIndex_ != -1) {
        hoverRowIndex_ = -1;
        Invalidate();
    }
}

void TreeView::EnsureSelectedVisible()
{
    int visibleIndex = VisibleIndexByRow(selectedIndex_);
    if (visibleIndex < 0) return;
    float target = EnsureVisibleOffset(visibleIndex * rowHeight_, rowHeight_,
                                       CurrentOffset(), bounds_.h);
    if (target != scroll_.Offset()) {
        scroll_.SetOffset(target);
    }
}

void TreeView::OnPointerWheelChanged(PointerEventArgs& e)
{
    // Shift+Wheel scrolls horizontally (OS-wide convention); plain wheel is vertical.
    const bool shift = (e.modifiers & ModifierKeys::Shift) != ModifierKeys::None;
    if (shift) {
        if (scroll_.MaxOffsetX() <= 0.0f) return;  // nothing to scroll horizontally
        float lines = e.WheelScrollLines();
        // Same sign as ScrollPanel's Shift+wheel, so every horizontally scrolling
        // surface in the framework moves the same way for the same gesture. No tween
        // on the X axis (by design, see ScrollViewer's horizontal-axis comment), so
        // the offset applies immediately.
        scroll_.SetOffsetX(scroll_.OffsetX() - lines * rowHeight_);
        Invalidate();
        e.handled = true;
        return;
    }

    if (scroll_.MaxOffset() <= 0.0f) return;  // nothing to scroll (bubbles up)
    // One wheel notch (WHEEL_DELTA = 120) scrolls WheelScrollLines() rows, read from
    // the system setting (Settings → Bluetooth & devices → Mouse → scroll lines).
    // Wheel-up (positive delta) scrolls content up (offset decreases).
    // AnimateBy accumulates so spinning the wheel builds momentum; the window's
    // animation timer drives scroll_.Tick() until it settles.
    float lines = e.WheelScrollLines();
    scroll_.AnimateBy(lines * rowHeight_);
    Invalidate();
    e.handled = true;
}

void TreeView::OnKeyDownRouted(KeyEventArgs& e)
{
    if (visibleRows_.empty()) return;
    int visibleIndex = VisibleIndexByRow(selectedIndex_);
    if (visibleIndex < 0) visibleIndex = 0;

    // --- Multi-select keyboard gestures (Multiple mode only) ----------------
    // Handled before the plain switch so Shift+Down extends the range instead of
    // moving the single selection. Ctrl+Space toggles the active row in place —
    // the one gesture that changes selection without moving the active row.
    if (selectionMode_ == SelectionMode::Multiple) {
        const bool ctrl = (e.modifiers & ModifierKeys::Ctrl) != ModifierKeys::None;
        const bool shift = (e.modifiers & ModifierKeys::Shift) != ModifierKeys::None;

        if (ctrl && e.vk == VK_SPACE) {
            ToggleRowSelection(selectedIndex_);
            e.handled = true;
            return;
        }
        if (shift) {
            // Compute the target visible index for the navigation keys that extend
            // a range; anything else falls through to the normal switch.
            int targetVisible = -1;
            switch (e.vk) {
            case VK_UP:    targetVisible = std::max(0, visibleIndex - 1); break;
            case VK_DOWN:  targetVisible = std::min(static_cast<int>(visibleRows_.size()) - 1, visibleIndex + 1); break;
            case VK_HOME:  targetVisible = 0; break;
            case VK_END:   targetVisible = static_cast<int>(visibleRows_.size()) - 1; break;
            case VK_PRIOR: targetVisible = std::max(0, visibleIndex - PageStep()); break;
            case VK_NEXT:  targetVisible = std::min(static_cast<int>(visibleRows_.size()) - 1, visibleIndex + PageStep()); break;
            default: break;
            }
            if (targetVisible >= 0) {
                RangeSelectTo(visibleRows_[targetVisible]);
                EnsureSelectedVisible();  // RangeSelectTo does not scroll on its own
                e.handled = true;
                return;
            }
        }
    }

    switch (e.vk) {
    case VK_UP:
        SetSelectedIndex(std::max(0, visibleIndex - 1));
        e.handled = true; break;
    case VK_DOWN:
        SetSelectedIndex(std::min(static_cast<int>(visibleRows_.size()) - 1, visibleIndex + 1));
        e.handled = true; break;
    case VK_LEFT:
        if (selectedIndex_ >= 0 && items_[selectedIndex_].hasChildren && items_[selectedIndex_].expanded) {
            ToggleRow(selectedIndex_);
        } else if (selectedIndex_ >= 0 && items_[selectedIndex_].parentId >= 0) {
            int parent = RowIndexById(items_[selectedIndex_].parentId);
            int parentVisible = VisibleIndexByRow(parent);
            if (parentVisible >= 0) SetSelectedIndex(parentVisible);
        }
        e.handled = true; break;
    case VK_RIGHT:
        if (selectedIndex_ >= 0 && items_[selectedIndex_].hasChildren && !items_[selectedIndex_].expanded)
            ToggleRow(selectedIndex_);
        e.handled = true; break;
    case VK_HOME:
        SetSelectedIndex(0);
        e.handled = true; break;
    case VK_END:
        SetSelectedIndex(static_cast<int>(visibleRows_.size()) - 1);
        e.handled = true; break;
    case VK_PRIOR:
        SetSelectedIndex(std::max(0, visibleIndex - PageStep()));
        e.handled = true; break;
    case VK_NEXT:
        SetSelectedIndex(std::min(static_cast<int>(visibleRows_.size()) - 1, visibleIndex + PageStep()));
        e.handled = true; break;
    default:
        break;
    }
}

bool TreeView::MatchPrefix(const std::wstring& text, const std::wstring& prefix) const
{
    if (prefix.empty() || text.size() < prefix.size()) return false;
    return _wcsnicmp(text.c_str(), prefix.c_str(), prefix.size()) == 0;
}

bool TreeView::SelectByPrefix(const std::wstring& prefix)
{
    if (prefix.empty()) return false;
    int start = std::max(0, VisibleIndexByRow(selectedIndex_) + 1);
    for (int pass = 0; pass < 2; ++pass) {
        int begin = (pass == 0) ? start : 0;
        int end = (pass == 0) ? static_cast<int>(visibleRows_.size()) : start;
        for (int i = begin; i < end; ++i) {
            if (MatchPrefix(items_[visibleRows_[i]].text, prefix)) {
                SetSelectedIndex(i);
                return true;
            }
        }
    }
    return false;
}

void TreeView::OnTextInput(wchar_t ch)
{
    if (visibleRows_.empty() || ch < 0x20) return;
    DWORD now = GetTickCount();
    if (now - lastTypeTick_ > 1000) typeSearch_.clear();
    lastTypeTick_ = now;
    typeSearch_ += ch;

    // Search on the accumulated prefix. If nothing matches and we had more than
    // one character buffered, restart the search from just this key (a common
    // type-ahead behavior: typing a new letter that doesn't extend the current
    // match jumps to items starting with that letter). This is a direct retry —
    // NOT a recursive OnTextInput call, which previously re-appended ch and
    // looped forever (stack overflow).
    if (SelectByPrefix(typeSearch_)) return;
    if (typeSearch_.size() > 1) {
        typeSearch_.assign(1, ch);
        SelectByPrefix(typeSearch_);
    }
}

bool TreeView::WantsAnimationTick() const
{
    if (!IsEffectivelyVisible()) return false;
    return scroll_.NeedsTick();
}

void TreeView::OnAnimationTick(float dtSec)
{
    if (!IsEffectivelyVisible()) return;
    scroll_.Tick(dtSec);
    
    // Update hover during smooth scrolling: the content moves under the pointer,
    // which didn't move. Without this, the old hoverRowIndex_ points to a row that
    // scrolled away, leaving the actual row under the pointer without highlight.
    if (pointerInside_) {
        int newHover = HitRow(lastPointerPos_.y);
        if (newHover != hoverRowIndex_) {
            hoverRowIndex_ = newHover;
        }
    }
    
    Invalidate();
}

void TreeView::OnAttachedToTree()
{
    // Forward the UI context to scroll_ so Theme() reads the correct palette
    scroll_.AttachToContext(Context());
    scroll_.SetKeepVisibleWhenOverflow(true);
    Invalidate();
}

void TreeView::OnDetachedFromTree()
{
    scroll_.DetachFromContext();
}

void TreeView::OnFocusChanged()
{
    Invalidate();
}

void TreeView::OnThemeChanged()
{
    Invalidate();
}

void TreeView::OnDpiChanged(float /*dpiScale*/)
{
    Invalidate();
}

void TreeView::DrawRow(const DrawingContext& dc, IDWriteTextFormat* fmt,
                       int rowIndex, float rowTopY, float leftX, float rightX)
{
    const ColorTokens& pal = Theme().colors;
    const TreeViewRow& row = items_[rowIndex];
    const float y = rowTopY;

    // Apply horizontal scroll offset: all horizontal coordinates shift left by OffsetX().
    const float scrollX = scroll_.OffsetX();
    const TreeViewRowLayout l = TreeViewRowLayout::For(row.depth);

    // Selection/hover highlight fills the viewport width, not content width, so it
    // stays fixed when scrolling horizontally (matches Explorer / Device Manager).
    D2D1_RECT_F rr = D2D1::RectF(leftX + 6.0f, y + 1.0f, rightX - 8.0f, y + rowHeight_ - 2.0f);
    const float itemCorner = Theme().spacing.cornerRadiusSmall;

    // Every selected row gets the fill; only the ACTIVE row (selectedIndex_) gets
    // the left accent bar. In Single mode the two coincide, so this renders exactly
    // as before. In Multiple mode the bar is what tells the user which row the next
    // Shift+arrow will extend from — without it a multi-row selection gives no clue
    // where the keyboard is.
    const bool rowIsSelected = IsRowSelected(rowIndex);
    if (rowIsSelected) {
        dc.FillRoundedRect(D2D1::RoundedRect(rr, itemCorner, itemCorner),
                           D2D1::ColorF(pal.accent.r, pal.accent.g, pal.accent.b, 0.18f));
    }
    // Hover highlight: subtle fill, only when not selected.
    else if (rowIndex == hoverRowIndex_) {
        dc.FillRoundedRect(D2D1::RoundedRect(rr, itemCorner, itemCorner),
                           D2D1::ColorF(pal.textPrimary.r, pal.textPrimary.g, pal.textPrimary.b, 0.05f));
    }
    if (rowIndex == selectedIndex_) {
        dc.FillRoundedRect(D2D1::RoundedRect(D2D1::RectF(rr.left, rr.top + 5, rr.left + 3, rr.bottom - 5), 2.0f, 2.0f),
                           pal.accent);
    }

    // Chevron (expander glyph): a triangle centered horizontally in the
    // kChevronColumn strip. The downward triangle used to span cy-2..cy+3, so its
    // flat top edge (the visually heavy part) sat above the row's center line and
    // the glyph read as riding high next to the text. It now spans cy-1.5..cy+3.5,
    // putting the shape's centroid just below center, where the eye expects it.
    if (row.hasChildren) {
        D2D1_COLOR_F chevron = EffectiveForeground(
            row.dimmed ? pal.textSecondary : pal.textPrimary);
        D2D1_POINT_2F p1, p2, p3;
        float cx = leftX + l.chevronLeft + TreeViewRowLayout::kChevronColumn * 0.5f - scrollX;
        float cy = y + rowHeight_ * 0.5f;
        if (row.expanded) {
            // Down: baseline at cy - 1.5, point at cy + 3.5 (height 5, center at cy + 1.0)
            p1 = D2D1::Point2F(cx - 4.0f, cy - 1.5f);
            p2 = D2D1::Point2F(cx + 4.0f, cy - 1.5f);
            p3 = D2D1::Point2F(cx, cy + 3.5f);
        } else {
            // Right: left edge at cx - 2, point at cx + 3 (width 5, center at cx + 0.5)
            p1 = D2D1::Point2F(cx - 2.0f, cy - 4.0f);
            p2 = D2D1::Point2F(cx - 2.0f, cy + 4.0f);
            p3 = D2D1::Point2F(cx + 3.0f, cy);
        }
        dc.DrawLine(p1, p2, chevron, 1.2f);
        dc.DrawLine(p2, p3, chevron, 1.2f);
        dc.DrawLine(p3, p1, chevron, 1.2f);
    }

    // Draw custom bitmap icon if present, otherwise fall back to geometric icon.
    // The bitmap is loaded at physical-pixel size for the current DPI, so drawing
    // it into a 16-DIP rect gives a 1:1 pixel mapping (crisp) rather than an
    // upscale from a fixed 16px source (blurry). High-quality cubic sampling keeps
    // it clean if the DPI changed since the bitmap was cached.
    if (row.customIcon) {
        const float iconSize = TreeViewRowLayout::kIconSize;
        // Vertically center the icon within the 28px row: y + (28 - 16) / 2 = y + 6.
        D2D1_RECT_F iconRect = D2D1::RectF(leftX + l.iconLeft - scrollX, y + 6.0f,
                                           leftX + l.iconLeft - scrollX + iconSize, y + 6.0f + iconSize);
        // Raw-context draw, so the element opacity is ours to apply: a hard-coded
        // 1.0 left the icon fully opaque inside a TreeView faded with SetOpacity.
        if (auto* dc2 = dc.Dc()) {
            dc2->DrawBitmap(row.customIcon, iconRect, dc.Opacity(),
                            D2D1_INTERPOLATION_MODE_HIGH_QUALITY_CUBIC,
                            nullptr, nullptr);
        }
    } else {
        // Snap icon position to the pixel grid so geometric shapes render crisp.
        // Without this, fractional coordinates cause D2D to antialias the edges,
        // making small icons (14×14 DIP) look blurry, especially at 100% DPI.
        // The icon is a 14×14 shape inside the 16px square; center it: +1 left, +7 top.
        const float iconX = SnapToPixel(leftX + l.iconLeft + 1.0f - scrollX, dc.DpiScale());
        const float iconY = SnapToPixel(y + 7.0f, dc.DpiScale());
        DrawIcon(dc, row.icon, iconX, iconY, pal);
    }

    // Row text is the neutral text color, not the icon's accent. Selected rows use
    // the text color for clear contrast on the selection fill.
    D2D1_COLOR_F textColor = EffectiveForeground(
        row.dimmed ? pal.textSecondary : pal.textPrimary);
    if (rowIsSelected) textColor = EffectiveForeground(pal.textPrimary);

    // Text rect: anchored at textLeft with no right bound tied to the viewport. It
    // used to end at rightX - 12; once the control was narrower than textLeft + 12
    // that rect turned inverted (right < left) and DirectWrite laid the text out
    // from the wrong edge, so the text slid sideways while the pane shrank. The
    // Render clip and the horizontal scroll now handle overflow instead.
    float textLeft = leftX + l.textLeft - scrollX;
    dc.DrawText(row.text.c_str(), static_cast<UINT32>(row.text.size()), fmt,
                D2D1::RectF(textLeft, y, textLeft + 10000.0f, y + rowHeight_),
                textColor, D2D1_DRAW_TEXT_OPTIONS_CLIP);
}

void TreeView::Render(const DrawingContext& dc)
{
    if (!Dwrite()) return;

    const ColorTokens& pal = Theme().colors;

    IDWriteTextFormat* fmt = Dwrite()->Format(
        12.0f, DWRITE_FONT_WEIGHT_NORMAL, DWRITE_TEXT_ALIGNMENT_LEADING,
        DWRITE_PARAGRAPH_ALIGNMENT_CENTER, DWRITE_WORD_WRAPPING_NO_WRAP);
    if (!fmt) return;

    // Scoped to a block so the clip pops before the scrollbar / focus ring draw.
    {
        ClipGuard clip = dc.PushClip(
            D2D1::RectF(bounds_.x, bounds_.y, bounds_.right(), bounds_.bottom()));

        // Snap the offset to the pixel grid before it reaches a glyph position. The
        // smooth-scroll tween yields fractional offsets, and DirectWrite weights
        // ClearType differently per subpixel position, so unsnapped row text shimmers
        // while scrolling. Same fix as TextArea / RichTextBox / LogViewer
        // (graphics/PixelSnapping.h). The row INDEX still comes from the raw offset:
        // which rows are visible is a content question, not a pixel one.
        const float snappedOffset = SnapToPixel(scroll_.Offset(), dc.DpiScale());
        int first = static_cast<int>(scroll_.Offset() / rowHeight_);
        int visible = VisibleCapacity();
        float y = bounds_.y - (snappedOffset - first * rowHeight_);

        // Learn the horizontal extent from the rows drawn this frame (see the
        // rowTextWidth_ comment in TreeView.h). The width cache makes this a lookup
        // for any row already seen, so steady-state frames build no text layouts.
        float frameMaxWidth = extentStale_ ? 0.0f : maxSeenContentWidth_;

        for (int n = 0; n < visible && first + n < static_cast<int>(visibleRows_.size()); ++n) {
            const int rowIndex = visibleRows_[first + n];
            frameMaxWidth = std::max(frameMaxWidth,
                TreeViewRowLayout::ContentWidth(items_[rowIndex].depth,
                                                RowTextWidth(rowIndex, fmt)));
            DrawRow(dc, fmt, rowIndex, y, bounds_.x, bounds_.right());
            y += rowHeight_;
        }

        // Push the extent when it grew, or unconditionally after a row-set change
        // (where it may also have shrunk). Growing mid-frame is safe for this frame's
        // pixels: SetContentWidth only re-clamps the offset, and a wider extent never
        // makes the current offset illegal.
        if (extentStale_ || frameMaxWidth > maxSeenContentWidth_) {
            const bool hadHRail = scroll_.MaxOffsetX() > 0.0f;
            maxSeenContentWidth_ = frameMaxWidth;
            extentStale_ = false;
            scroll_.SetContentWidth(maxSeenContentWidth_);
            // The rail appearing or disappearing changes the vertical extent (the
            // 16 DIP strip). scroll_.Render runs after this block, so the vertical
            // thumb drawn this frame already reflects it; no extra frame is needed.
            if (hadHRail != (scroll_.MaxOffsetX() > 0.0f))
                UpdateVerticalExtent();
        }
    }

    scroll_.Render(dc);

    // Focus ring: a subtle accent outline so keyboard focus is visible.
    if (IsFocused()) {
        const auto accent = EffectiveAccentColor(pal.accent);
        dc.DrawRoundedRect(
            D2D1::RoundedRect(D2D1::RectF(bounds_.x + 0.5f, bounds_.y + 0.5f,
                                          bounds_.right() - 0.5f, bounds_.bottom() - 0.5f),
                              4.0f, 4.0f),
            D2D1::ColorF(accent.r, accent.g, accent.b, 0.7f), 1.0f);
    }
}

} // namespace fluent
