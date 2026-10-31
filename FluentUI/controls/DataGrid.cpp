// DataGrid.cpp — see DataGrid.h.

#include "DataGrid.h"
#include "TextBox.h"
#include "../graphics/DrawingContext.h"
#include "../graphics/DWriteContext.h"
#include "../graphics/PixelSnapping.h"
#include "../styling/ThemeTokens.h"
#include "../styling/FocusVisual.h"
#include "../input/InputManager.h"
#include "../input/FocusManager.h"

#include <algorithm>
#include <cmath>

namespace fluent {

// EditOverlay: a TextBox that commits the parent grid's edit on blur. Excel and WPF
// DataGrids commit when focus leaves the edit box (e.g. clicking a button while editing),
// so the typed value isn't lost. TextEditBase::OnFocusChanged is protected, so we need
// this small subclass to hook it. Defined here rather than in the header because
// DataGrid.h only forward-declares TextBox.
class DataGrid::EditOverlay : public TextBox {
public:
    explicit EditOverlay(DataGrid* owner) : owner_(owner) {}
protected:
    void OnFocusChanged() override {
        TextBox::OnFocusChanged();
        // Only commit when losing focus (not gaining), and only if editing is still active
        // (CancelEdit clears editingRow_ before moving focus, preventing re-entrancy).
        if (!IsFocused() && owner_->IsEditing()) owner_->CommitEdit();
    }
private:
    DataGrid* owner_;
};

namespace {
constexpr float kCellPadH = 8.0f;      // text inset inside a cell
constexpr float kHeaderExtra = 4.0f;   // header row is slightly taller than a data row
constexpr float kMinRowHeight = 16.0f;
constexpr float kWheelStepRows = 3.0f; // rows per wheel notch
// Divider grab zone: half-width in DIPs on each side of a column's right edge. 4 DIP
// total is the Windows convention for splitters and is wide enough to hit reliably at
// 100% DPI without swallowing header clicks near the edge.
constexpr float kDividerGrabHalf = 4.0f;
constexpr float kMinColumnWidth = 24.0f;   // narrower than this and the header is unreadable
// Sort arrow geometry, in DIPs. Drawn at the header's trailing edge.
constexpr float kSortArrowW = 8.0f;
constexpr float kSortArrowH = 4.0f;
constexpr float kSortArrowPad = 6.0f;      // gap between text and arrow
}  // namespace

DataGrid::DataGrid() {
    SetFocusable(true);      // keyboard row navigation
    SetHAlign(HAlign::Stretch);
    SetVAlign(VAlign::Stretch);
    scroll_.SetKeepVisibleWhenOverflow(true);
}

// Out of line because the unique_ptr<TextBox> member needs TextBox to be complete at the
// point the destructor is generated, and DataGrid.h only forward-declares it.
DataGrid::~DataGrid() = default;

void DataGrid::OnAttachedToTree() {
    Control::OnAttachedToTree();
    // scroll_ and editOverlay_ are members, not tree children, so the context does not
    // reach them on its own. Without this the overlay has no DWrite and renders nothing.
    scroll_.AttachToContext(Context());
    if (editOverlay_) editOverlay_->AttachToContext(Context());
}

void DataGrid::OnDetachedFromTree() {
    // Drop any in-flight edit first: it holds focus, and the FocusManager we would hand
    // focus back to is about to become unreachable.
    if (editingRow_ >= 0) CancelEdit();
    if (editOverlay_) editOverlay_->DetachFromContext();
    scroll_.DetachFromContext();
    Control::OnDetachedFromTree();
}

void DataGrid::SetColumns(std::vector<Column> columns) {
    columns_ = std::move(columns);
    InvalidateDirty(DirtyFlags::Measure);
}

void DataGrid::SetRowCount(int count) {
    const int clamped = std::max(0, count);
    if (rowCount_ == clamped) return;
    rowCount_ = clamped;
    // A shrink can leave the selection past the end, and the offset past the content.
    // Drop every out-of-range row rather than clamping them onto the last row: clamping
    // would silently select a row the user never picked, and with multi-select it would
    // collapse several dropped rows onto the same index.
    selectedRows_.erase(
        std::remove_if(selectedRows_.begin(), selectedRows_.end(),
                       [this](int r) { return r >= rowCount_; }),
        selectedRows_.end());
    if (selectionAnchor_ >= rowCount_) selectionAnchor_ = -1;
    if (activeRow_ >= rowCount_) activeRow_ = -1;
    InvalidateDirty(DirtyFlags::Measure);
}

void DataGrid::SetCellProvider(CellProvider provider) {
    cellProvider_ = std::move(provider);
    Invalidate();   // Render-level: same geometry, different text
}

void DataGrid::SetRowColorProvider(RowColorProvider provider) {
    rowColorProvider_ = std::move(provider);
    Invalidate();
}

void DataGrid::SetRowHeight(float dip) {
    const float h = std::max(kMinRowHeight, dip);
    if (rowHeight_ == h) return;
    rowHeight_ = h;
    InvalidateDirty(DirtyFlags::Measure);
}

void DataGrid::SetAlternatingRowFill(bool on) {
    if (alternating_ == on) return;
    alternating_ = on;
    Invalidate();
}

void DataGrid::SetShowGridLines(bool on) {
    if (showGridLines_ == on) return;
    showGridLines_ = on;
    Invalidate();
}

void DataGrid::SetSortColumn(int col, SortDirection dir) {
    if (col < -1 || col >= static_cast<int>(columns_.size())) col = -1;
    if (sortColumn_ == col && sortDirection_ == dir) return;
    sortColumn_ = col;
    sortDirection_ = dir;
    Invalidate();
}

void DataGrid::SetSelectionMode(SelectionMode mode) {
    if (selectionMode_ == mode) return;
    selectionMode_ = mode;
    // Collapsing Multiple -> Single must not leave several rows highlighted. Keep the
    // first (lowest index) so the visible selection does not jump to an unrelated row.
    if (selectionMode_ == SelectionMode::Single && selectedRows_.size() > 1) {
        const int keep = selectedRows_.front();
        selectedRows_.clear();
        selectedRows_.push_back(keep);
        activeRow_ = keep;
        Invalidate();
        RoutedEventArgs args{};
        args.source = this;
        selectionChanged_.Raise(*this, args);
    }
}

int DataGrid::SelectedRow() const {
    return selectedRows_.empty() ? -1 : selectedRows_.front();
}

bool DataGrid::IsRowSelected(int row) const {
    // Linear scan, not a set: a selection is a handful of rows in practice and this is
    // called once per VISIBLE row per frame (a screenful), so the constant factor of a
    // sorted vector beats a hash set's allocation.
    for (int r : selectedRows_) if (r == row) return true;
    return false;
}

void DataGrid::ClearSelection() {
    if (selectedRows_.empty()) return;
    selectedRows_.clear();
    selectionAnchor_ = -1;
    activeRow_ = -1;
    Invalidate();
    RoutedEventArgs args{};
    args.source = this;
    selectionChanged_.Raise(*this, args);
}

void DataGrid::SetSelectedRow(int row) {
    const int clamped = (row < 0 || rowCount_ == 0)
                            ? -1
                            : std::min(row, rowCount_ - 1);
    // Short-circuit only when the resulting set is identical, so switching from a
    // multi-row selection down to that same single row still fires.
    const bool same = (clamped < 0) ? selectedRows_.empty()
                                    : (selectedRows_.size() == 1 &&
                                       selectedRows_.front() == clamped);
    // Anchor and active row follow even when the set is unchanged: after Ctrl+clicking
    // row 5 off {3,5}, selecting row 3 again must make 3 the row arrows step from.
    selectionAnchor_ = clamped;
    activeRow_ = clamped;
    if (same) return;

    selectedRows_.clear();
    if (clamped >= 0) selectedRows_.push_back(clamped);
    Invalidate();   // selection is a fill, not a size
    RoutedEventArgs args{};
    args.source = this;
    selectionChanged_.Raise(*this, args);
}

void DataGrid::SelectSingleRow(int row) { SetSelectedRow(row); }

std::wstring DataGrid::CellText(int row, int col) const {
    if (!cellProvider_ || row < 0 || row >= rowCount_ || col < 0 ||
        col >= static_cast<int>(columns_.size()))
        return {};
    return cellProvider_(row, col);
}

void DataGrid::ToggleRowSelection(int row) {
    if (row < 0 || row >= rowCount_) return;
    auto it = std::find(selectedRows_.begin(), selectedRows_.end(), row);
    if (it != selectedRows_.end()) {
        selectedRows_.erase(it);
    } else {
        // Insert in sorted position rather than push_back + sort: the vector is already
        // ordered and a selection grows one row at a time.
        selectedRows_.insert(
            std::lower_bound(selectedRows_.begin(), selectedRows_.end(), row), row);
    }
    // Ctrl+click re-anchors: a following Shift+click extends from the row just touched,
    // which is what Explorer does.
    selectionAnchor_ = row;
    activeRow_ = row;
    Invalidate();
    RoutedEventArgs args{};
    args.source = this;
    selectionChanged_.Raise(*this, args);
}

void DataGrid::RangeSelectTo(int row) {
    if (row < 0 || row >= rowCount_) return;
    // No anchor yet (Shift+click as the very first gesture): degenerate to selecting the
    // clicked row, which also establishes the anchor.
    if (selectionAnchor_ < 0 || selectionAnchor_ >= rowCount_) {
        SetSelectedRow(row);
        return;
    }
    const int lo = std::min(selectionAnchor_, row);
    const int hi = std::max(selectionAnchor_, row);
    // The moving end of the range is where the next Shift+arrow steps from.
    activeRow_ = row;
    // Unchanged set (Shift+Down pressed against the last row, a repeated Shift+click
    // on the same row): no repaint and no SelectionChanged. selectedRows_ is sorted
    // and unique, so "same size, same ends" means the same contiguous range.
    if (selectedRows_.size() == static_cast<size_t>(hi - lo + 1) &&
        selectedRows_.front() == lo && selectedRows_.back() == hi)
        return;
    selectedRows_.clear();
    selectedRows_.reserve(static_cast<size_t>(hi - lo + 1));
    for (int r = lo; r <= hi; ++r) selectedRows_.push_back(r);
    // Anchor deliberately NOT moved, so shrinking the range with a second Shift+click
    // works instead of ratcheting.
    Invalidate();
    RoutedEventArgs args{};
    args.source = this;
    selectionChanged_.Raise(*this, args);
}

float DataGrid::TotalContentWidth() const {
    float w = 0.0f;
    for (const Column& c : columns_) w += c.width;
    return w;
}

float DataGrid::FrozenWidth() const {
    float w = 0.0f;
    for (const Column& c : columns_) if (c.frozen) w += c.width;
    return w;
}

RectDip DataGrid::HeaderRect() const {
    const float h = rowHeight_ + kHeaderExtra;
    return RectDip{bounds_.x, bounds_.y, bounds_.w, std::min(h, bounds_.h)};
}

RectDip DataGrid::ViewportRect() const {
    const RectDip hdr = HeaderRect();
    const float y = bounds_.y + hdr.h;
    return RectDip{bounds_.x, y, bounds_.w, std::max(0.0f, bounds_.bottom() - y)};
}

void DataGrid::VisibleRowRange(int& first, int& last) const {
    first = 0;
    last = -1;                       // the documented "nothing visible" answer
    if (rowCount_ <= 0 || rowHeight_ <= 0.0f) return;
    const RectDip vp = ViewportRect();
    if (vp.h <= 0.0f) return;

    const float top = scroll_.Offset();
    // floor for the first row and ceil for the last: a row only partly scrolled into the
    // viewport is still visible and must be drawn, otherwise scrolling shows a gap at the
    // top edge for a fraction of a row height.
    first = static_cast<int>(std::floor(top / rowHeight_));
    last = static_cast<int>(std::ceil((top + vp.h) / rowHeight_)) - 1;
    first = std::max(0, first);
    last = std::min(last, rowCount_ - 1);
    if (last < first) { first = 0; last = -1; }
}

void DataGrid::VisibleColumnRange(int& first, int& last) const {
    first = 0;
    last = -1;
    if (columns_.empty() || bounds_.w <= 0.0f) return;

    // Frozen columns are ALWAYS visible: they are pinned, so no scroll offset can move
    // them out of the viewport. Scrolling columns are tested against the scroll window,
    // which starts after the frozen band. Testing them in one loop against one offset
    // (the pre-frozen behaviour) reported frozen columns as scrolled away.
    int firstCol = -1, lastCol = -1;
    const float scrollLeft = scroll_.OffsetX();
    const float scrollRight = scrollLeft + std::max(0.0f, bounds_.w - FrozenWidth());
    float sx = 0.0f;   // x within the scrolling band's own content space

    for (size_t i = 0; i < columns_.size(); ++i) {
        const float cw = columns_[i].width;
        bool visible;
        if (columns_[i].frozen) {
            visible = true;
        } else {
            // Half-open intersection test: a column touching the edge exactly is not
            // visible, which keeps the count stable instead of flickering by one at
            // exact offsets.
            visible = (sx + cw > scrollLeft && sx < scrollRight);
            sx += cw;
        }
        if (visible) {
            if (firstCol < 0) firstCol = static_cast<int>(i);
            lastCol = static_cast<int>(i);
        }
    }
    if (firstCol < 0) { first = 0; last = -1; return; }
    first = firstCol;
    last = lastCol;
}

float DataGrid::ColumnLeft(int col) const {
    if (col < 0 || col >= static_cast<int>(columns_.size())) return bounds_.x;
    const Column& target = columns_[static_cast<size_t>(col)];

    // A frozen column sits in a band pinned to the left edge, laid out in column order
    // among the other frozen columns and NEVER offset by the horizontal scroll. That
    // "never offset" is the whole feature: everything else here follows from it.
    if (target.frozen) {
        float x = bounds_.x;
        for (int i = 0; i < col; ++i)
            if (columns_[static_cast<size_t>(i)].frozen)
                x += columns_[static_cast<size_t>(i)].width;
        return x;
    }

    // A scrolling column starts after the frozen band and is offset by the scroll. Its
    // own origin counts only the OTHER scrolling columns before it, because the frozen
    // ones are not in this coordinate space.
    float x = bounds_.x + FrozenWidth() - scroll_.OffsetX();
    for (int i = 0; i < col; ++i)
        if (!columns_[static_cast<size_t>(i)].frozen)
            x += columns_[static_cast<size_t>(i)].width;
    return x;
}

bool DataGrid::VerticalGridLineX(int boundary, float& x) const {
    if (boundary < 0 || boundary > static_cast<int>(columns_.size())) return false;
    const RectDip vp = ViewportRect();
    if (boundary == 0) {
        x = bounds_.x;
    } else {
        const int col = boundary - 1;
        x = ColumnLeft(col) + columns_[static_cast<size_t>(col)].width;
        // A scrolling column's edge left of the band's right edge is hidden by the
        // frozen columns. Strictly left: an edge exactly ON the band edge coincides
        // with the last frozen column's own line, so drawing it is harmless.
        if (!columns_[static_cast<size_t>(col)].frozen && x < bounds_.x + FrozenWidth())
            return false;
    }
    return x >= vp.x && x <= vp.right();
}

RectDip DataGrid::CellRect(int row, int col) const {
    if (col < 0 || col >= static_cast<int>(columns_.size())) return RectDip{};
    const RectDip vp = ViewportRect();
    const float y = vp.y + row * rowHeight_ - scroll_.Offset();
    return RectDip{ColumnLeft(col), y,
                   columns_[static_cast<size_t>(col)].width, rowHeight_};
}

void DataGrid::SetVerticalOffset(float dip) {
    // Scrolling only Invalidates, it does not re-Arrange, so a live edit overlay would keep
    // the screen position of a cell that has moved — a box floating over the wrong row.
    // Committing here is what every grid does when the edited cell scrolls away.
    if (editingRow_ >= 0) CommitEdit();
    scroll_.SetOffset(dip);
    Invalidate();
}

void DataGrid::SetHorizontalOffset(float dip) {
    if (editingRow_ >= 0) CommitEdit();
    scroll_.SetOffsetX(dip);
    Invalidate();
}

void DataGrid::ScrollRowIntoView(int row) {
    if (row < 0 || row >= rowCount_) return;
    const RectDip vp = ViewportRect();
    if (vp.h <= 0.0f) return;

    const float rowTop = row * rowHeight_;
    const float rowBottom = rowTop + rowHeight_;
    const float viewTop = scroll_.Offset();
    const float viewBottom = viewTop + vp.h;

    // Only move if the row is actually outside: scrolling an already-visible row to the
    // top would make arrow-key navigation jerk the view on every keypress.
    if (rowTop < viewTop) scroll_.SetOffset(rowTop);
    else if (rowBottom > viewBottom) scroll_.SetOffset(rowBottom - vp.h);
    Invalidate();
}

float DataGrid::MaxVerticalOffset() const { return scroll_.MaxOffset(); }
float DataGrid::MaxHorizontalOffset() const { return scroll_.MaxOffsetX(); }

void DataGrid::ClampScroll() {
    const RectDip vp = ViewportRect();
    scroll_.SetContentHeight(TotalContentHeight());
    // Content and viewport must be in the SAME coordinate space. Arrange already shrank the
    // ScrollViewer's bounds by the frozen band, so the content it is compared against must
    // exclude the frozen columns too — MaxOffsetX is (content - bounds) and mixing the two
    // spaces would under- or over-shoot the last column by exactly FrozenWidth().
    scroll_.SetContentWidth(TotalContentWidth() - FrozenWidth());
    // Re-applying the current offset runs it through the ScrollViewer's own clamp against
    // the extents just set, which is what pulls the view back when content shrinks.
    scroll_.SetOffset(scroll_.Offset());
    scroll_.SetOffsetX(scroll_.OffsetX());
    UNREFERENCED_PARAMETER(vp);
}

void DataGrid::Measure(float availW, float availH) {
    // Fixed column widths, so desired width is simply their sum: no auto-fit pass.
    //
    // Auto-fit is deliberately absent. Measuring the widest cell in a column means pulling
    // and laying out EVERY row's text for that column, which is O(rows) per measure — it
    // would undo the virtualization this control exists for, and on a 100k-row grid it
    // would stall the UI thread on every resize. A caller who wants fitted columns can
    // measure a sample itself and set widths.
    const float w = TotalContentWidth();
    const float h = TotalContentHeight() + HeaderRect().h;
    SetDesired({IsAuto(width_) ? (availW > 0.0f ? std::min(w, availW) : w) : width_,
                IsAuto(height_) ? (availH > 0.0f ? std::min(h, availH) : h) : height_});
}

void DataGrid::Arrange(const RectDip& finalRect) {
    bounds_ = finalRect;
    // The ScrollViewer's own bounds ARE the viewport it computes thumbs against, so they
    // must be the body area rather than the whole grid — otherwise the vertical thumb is
    // sized as if the pinned header scrolled too, and its travel is short by the header
    // height at the bottom of a long table.
    // For horizontal: frozen columns don't scroll, so the scrollable viewport width excludes
    // them. Without this, MaxOffsetX would clamp the scroll range short of the last column.
    RectDip scrollVp = ViewportRect();
    scrollVp.x += FrozenWidth();
    scrollVp.w -= FrozenWidth();
    scroll_.Arrange(scrollVp);
    ClampScroll();
    // Re-place the edit overlay every Arrange so it tracks its cell through scrolling and
    // column resizing. CellRect already accounts for both scroll offsets.
    if (editingRow_ >= 0 && editOverlay_)
        editOverlay_->Arrange(CellRect(editingRow_, editingColumn_));
}

void DataGrid::Render(const DrawingContext& dc) {
    if (!dc.Dc()) return;
    const ColorTokens& c = Theme().colors;
    const TypographyTokens& typo = Theme().typography;

    if (IsFocused())
        DrawFocusRing(dc, bounds_, c, FocusRingSpec{});

    DWriteContext* dw = Dwrite();
    const RectDip hdr = HeaderRect();
    const RectDip vp = ViewportRect();

    int firstRow = 0, lastRow = -1, firstCol = 0, lastCol = -1;
    VisibleRowRange(firstRow, lastRow);
    VisibleColumnRange(firstCol, lastCol);

    // Draw-time scroll offset, snapped to the pixel grid. The smooth-scroll tween
    // produces fractional offsets and ClearType weights each glyph by its subpixel
    // position, so unsnapped cell text shimmers while scrolling — same fix as
    // TextArea / RichTextBox / LogViewer (graphics/PixelSnapping.h). VisibleRowRange
    // above deliberately keeps using the RAW offset: which rows are visible is a
    // content question, and its floor/ceil already covers the partial row.
    const float scrollY = SnapToPixel(scroll_.Offset(), dc.DpiScale());

    // --- Body -------------------------------------------------------------
    // Clipped to the viewport so a partially-scrolled row cannot paint over the header or
    // past the bottom edge. Rows are drawn before gridlines so the lines sit on top.
    {
        ClipGuard clip = dc.PushClip(
            D2D1::RectF(vp.x, vp.y, vp.right(), vp.bottom()));

        for (int r = firstRow; r <= lastRow; ++r) {
            const float rowY = vp.y + r * rowHeight_ - scrollY;
            const D2D1_RECT_F rowRect =
                D2D1::RectF(vp.x, rowY, vp.right(), rowY + rowHeight_);

            if (IsRowSelected(r)) {
                dc.FillRect(rowRect, c.accent);
            } else if (alternating_ && (r % 2) == 1) {
                dc.FillRect(rowRect, c.rowFillAlternate);
            }

            if (!dw || !dw->Valid() || !cellProvider_) continue;

            // Row text colour: selection wins over the app's per-row tint, because a
            // tinted row on an accent fill can fall below contrast.
            D2D1_COLOR_F textColor = c.textPrimary;
            if (IsRowSelected(r)) {
                textColor = c.onAccent;
            } else if (rowColorProvider_) {
                D2D1_COLOR_F rowColor{};
                if (rowColorProvider_(r, rowColor)) textColor = rowColor;
            }

            // TWO passes over the columns, scrolling first and frozen second, so a frozen
            // cell paints OVER whatever scrolled beneath it. One pass in column order
            // would let a scrolled column's text run through the pinned band.
            auto drawCell = [&](int col) {
                const Column& column = columns_[static_cast<size_t>(col)];
                // Drawn straight from the provider's result. CellProvider returns by
                // value, so that string is one allocation per visible cell per frame
                // (beyond the small-string buffer) whatever the grid does with it. Copying
                // it into a reused scratch buffer, as this used to, avoided nothing and
                // added a memcpy per cell.
                const std::wstring text = cellProvider_(r, col);
                if (text.empty()) return;

                const RectDip cell = CellRect(r, col);
                DWRITE_TEXT_ALIGNMENT ta =
                    column.align == Align::Right  ? DWRITE_TEXT_ALIGNMENT_TRAILING
                    : column.align == Align::Center ? DWRITE_TEXT_ALIGNMENT_CENTER
                                                    : DWRITE_TEXT_ALIGNMENT_LEADING;
                IDWriteTextFormat* fmt = dw->Format(
                    typo.bodySize, DWRITE_FONT_WEIGHT_NORMAL, ta,
                    DWRITE_PARAGRAPH_ALIGNMENT_CENTER, DWRITE_WORD_WRAPPING_NO_WRAP);
                if (!fmt) return;
                dc.DrawText(text.c_str(),
                            static_cast<UINT32>(text.size()), fmt,
                            D2D1::RectF(cell.x + kCellPadH, cell.y,
                                        cell.right() - kCellPadH, cell.bottom()),
                            textColor, D2D1_DRAW_TEXT_OPTIONS_CLIP);
            };

            // Scrolling columns are clipped to the region RIGHT of the frozen band so they
            // cannot paint underneath the pinned ones. A solid background fill is NOT enough:
            // anti-aliased glyph edges survive it, which is the ghosting the frozen band shows
            // when a scrolled cell passes beneath it. ClipGuard is RAII — the inner scope is
            // what pops the clip before the frozen pass runs.
            const float frozenW = FrozenWidth();
            {
                ClipGuard scrollClip = (frozenW > 0.0f)
                    ? dc.PushClip(D2D1::RectF(bounds_.x + frozenW, vp.y, vp.right(), vp.bottom()))
                    : ClipGuard{};
                for (int col = firstCol; col <= lastCol; ++col)
                    if (!columns_[static_cast<size_t>(col)].frozen) drawCell(col);
            }

            // Frozen pass, unclipped: the band is now guaranteed clean.
            if (frozenW > 0.0f) {
                for (int col = firstCol; col <= lastCol; ++col)
                    if (columns_[static_cast<size_t>(col)].frozen) drawCell(col);
            }
        }

        if (showGridLines_) {
            // Horizontal rules only between visible rows, vertical only between visible
            // columns: drawing all of them would be O(rows) and defeat the point.
            for (int r = firstRow; r <= lastRow + 1 && r <= rowCount_; ++r) {
                const float y = vp.y + r * rowHeight_ - scrollY;
                if (y < vp.y || y > vp.bottom()) continue;
                dc.DrawLine(D2D1::Point2F(vp.x, y), D2D1::Point2F(vp.right(), y),
                            c.gridLine, 1.0f);
            }
            // VerticalGridLineX drops scrolling-column edges that slid under the frozen
            // band, which the cells' own clip already hides.
            for (int col = 0; col <= static_cast<int>(columns_.size()); ++col) {
                float x = 0.0f;
                if (VerticalGridLineX(col, x))
                    dc.DrawLine(D2D1::Point2F(x, vp.y), D2D1::Point2F(x, vp.bottom()),
                                c.gridLine, 1.0f);
            }
        }

        // Row-reorder drop indicator: a thick accent line at the boundary where the dragged
        // rows would land. Drawn last inside the body clip so it sits above the gridlines.
        // Only while an actual drag is live (dropTargetRow_ is set past the drift threshold).
        if (draggingRow_ >= 0 && dropTargetRow_ >= 0) {
            // Drop ABOVE the target when dragging upward, BELOW when dragging downward:
            // the line has to sit on the edge the rows will occupy, or the user cannot tell
            // whether the drop lands before or after the row under the pointer.
            const int edgeRow = (dropTargetRow_ > draggingRow_) ? dropTargetRow_ + 1
                                                                : dropTargetRow_;
            const float y = vp.y + edgeRow * rowHeight_ - scrollY;
            if (y >= vp.y && y <= vp.bottom())
                dc.DrawLine(D2D1::Point2F(vp.x, y), D2D1::Point2F(vp.right(), y),
                            c.accent, 2.5f);
        }
    }

    // --- Header (drawn AFTER the body, so a scrolled row cannot cover it) ---
    {
        ClipGuard clip = dc.PushClip(
            D2D1::RectF(hdr.x, hdr.y, hdr.right(), hdr.bottom()));
        dc.FillRect(D2D1::RectF(hdr.x, hdr.y, hdr.right(), hdr.bottom()),
                    c.controlFillDefault);
        dc.DrawLine(D2D1::Point2F(hdr.x, hdr.bottom()),
                    D2D1::Point2F(hdr.right(), hdr.bottom()),
                    c.controlStrokeDefault, 1.0f);

        if (dw && dw->Valid()) {
            // Lambda to draw one header cell (text + optional sort arrow). Split into TWO
            // passes, scrolling first and frozen second, so frozen headers paint OVER
            // whatever scrolled underneath (matching the body-cell rendering pattern).
            auto drawHeader = [&](size_t i) {
                const Column& column = columns_[i];
                const float x = ColumnLeft(static_cast<int>(i));
                const float cw = column.width;
                if (x + cw > hdr.x && x < hdr.right() && !column.header.empty()) {
                    const bool sorted = (static_cast<int>(i) == sortColumn_ &&
                                         sortDirection_ != SortDirection::None);
                    // The arrow eats into the text box so a long header is clipped rather
                    // than drawn under the glyph.
                    const float arrowReserve =
                        sorted ? (kSortArrowW + kSortArrowPad) : 0.0f;

                    DWRITE_TEXT_ALIGNMENT ta =
                        column.align == Align::Right  ? DWRITE_TEXT_ALIGNMENT_TRAILING
                        : column.align == Align::Center ? DWRITE_TEXT_ALIGNMENT_CENTER
                                                        : DWRITE_TEXT_ALIGNMENT_LEADING;
                    if (IDWriteTextFormat* fmt = dw->Format(
                            typo.bodySize, DWRITE_FONT_WEIGHT_SEMI_BOLD, ta,
                            DWRITE_PARAGRAPH_ALIGNMENT_CENTER,
                            DWRITE_WORD_WRAPPING_NO_WRAP)) {
                        dc.DrawText(column.header.c_str(),
                                    static_cast<UINT32>(column.header.size()), fmt,
                                    D2D1::RectF(x + kCellPadH, hdr.y,
                                                x + cw - kCellPadH - arrowReserve,
                                                hdr.bottom()),
                                    c.textSecondary, D2D1_DRAW_TEXT_OPTIONS_CLIP);
                    }

                    if (sorted) {
                        // Two lines forming a chevron, not a filled triangle: the grid has
                        // no path/geometry helper and a chevron reads correctly at 1px
                        // stroke on every DPI, where a 3-point fill would need a
                        // ID2D1PathGeometry per frame.
                        const float ax = x + cw - kCellPadH - kSortArrowW;
                        const float cy = hdr.y + hdr.h * 0.5f;
                        const float half = kSortArrowH * 0.5f;
                        const bool up = (sortDirection_ == SortDirection::Ascending);
                        // Ascending: apex at the top. Descending: apex at the bottom.
                        const float apexY = up ? cy - half : cy + half;
                        const float baseY = up ? cy + half : cy - half;
                        dc.DrawLine(D2D1::Point2F(ax, baseY),
                                    D2D1::Point2F(ax + kSortArrowW * 0.5f, apexY),
                                    c.textSecondary, 1.5f);
                        dc.DrawLine(D2D1::Point2F(ax + kSortArrowW * 0.5f, apexY),
                                    D2D1::Point2F(ax + kSortArrowW, baseY),
                                    c.textSecondary, 1.5f);
                    }
                }
            };

            // Scrolling headers clipped to the region RIGHT of the frozen band, matching
            // the body's frozen-cell rendering. Without this clip, a scrolled header's
            // anti-aliased text edges leak through the frozen header painted on top.
            const float frozenW = FrozenWidth();
            {
                ClipGuard scrollClip = (frozenW > 0.0f)
                    ? dc.PushClip(D2D1::RectF(bounds_.x + frozenW, hdr.y, hdr.right(), hdr.bottom()))
                    : ClipGuard{};
                for (size_t i = 0; i < columns_.size(); ++i)
                    if (!columns_[i].frozen) drawHeader(i);
            }

            // Frozen headers, unclipped: the band is now guaranteed clean.
            if (frozenW > 0.0f) {
                for (size_t i = 0; i < columns_.size(); ++i)
                    if (columns_[i].frozen) drawHeader(i);
            }
        }
    }

    // Scrollbars last: they overlay everything.
    scroll_.Render(dc);

    // The edit overlay sits on top of everything, including the scrollbars: it is modal
    // for the cell it covers, and a rail drawn over the caret would be unreadable.
    if (editingRow_ >= 0 && editOverlay_)
        editOverlay_->Render(dc);
}

int DataGrid::RowAtPoint(float dipY) const {
    const RectDip vp = ViewportRect();
    if (dipY < vp.y || dipY > vp.bottom() || rowHeight_ <= 0.0f) return -1;
    const int row = static_cast<int>((dipY - vp.y + scroll_.Offset()) / rowHeight_);
    return (row >= 0 && row < rowCount_) ? row : -1;
}

int DataGrid::DividerAtPoint(float dipX, float dipY) const {
    if (!columnResizeEnabled_) return -1;
    const RectDip hdr = HeaderRect();
    if (!hdr.contains(dipX, dipY)) return -1;

    for (size_t i = 0; i < columns_.size(); ++i) {
        const float rightEdge = ColumnLeft(static_cast<int>(i)) + columns_[i].width;
        // Check if pointer is within ±kDividerGrabHalf of the right edge.
        if (dipX >= rightEdge - kDividerGrabHalf && dipX <= rightEdge + kDividerGrabHalf) {
            // Only hit if this column is resizable.
            return columns_[i].resizable ? static_cast<int>(i) : -1;
        }
    }
    return -1;
}

UIElement* DataGrid::HitTestDeep(float dipX, float dipY) {
    if (!IsVisible() || !bounds_.contains(dipX, dipY)) return nullptr;
    // The edit overlay consumes hits when active: clicking the TextBox should move the
    // caret, not select a different row underneath it.
    if (editingRow_ >= 0 && editOverlay_) {
        if (auto* hit = editOverlay_->HitTestDeep(dipX, dipY))
            return hit;
    }
    return this;
}

void DataGrid::OnPointerPressed(PointerEventArgs& e) {
    if (e.button != PointerButton::Left) return;
    const float dipX = e.position.x, dipY = e.position.y;

    // Vertical scrollbar drag: press anywhere in the hover strip, not just the narrow thumb.
    // Both scrollbar branches commit a live edit first, for the reason SetVerticalOffset
    // gives: scrolling does not re-Arrange, so the editor would stay put over another row.
    if (scroll_.HitBarRegion(dipX, dipY)) {
        if (editingRow_ >= 0) CommitEdit();
        if (!scroll_.HitThumb(dipX, dipY)) {
            // Off-thumb press: center the thumb on the pointer before dragging.
            const RectDip thumb = scroll_.ThumbRect();
            const RectDip track = scroll_.TrackRect();
            const float trackRange = std::max(1.0f, track.h - thumb.h);
            const float relClick = dipY - track.y;
            const float wantTop = relClick - thumb.h * 0.5f;
            scroll_.SetOffset(std::clamp(wantTop, 0.0f, trackRange) / trackRange * scroll_.MaxOffset());
        }
        scroll_.BeginDrag(dipY);
        if (Context().input)
            Context().input->CapturePointer(this);
        Invalidate();
        e.handled = true;
        return;
    }

    // Horizontal scrollbar drag.
    if (scroll_.HitHBarRegion(dipX, dipY)) {
        if (editingRow_ >= 0) CommitEdit();
        if (!scroll_.HitHThumb(dipX, dipY)) {
            const RectDip hTrack = scroll_.HTrackRect();
            const RectDip hThumb = scroll_.HThumbRect();
            const float trackRange = std::max(1.0f, hTrack.w - hThumb.w);
            const float relClick = dipX - hTrack.x;
            const float wantLeft = relClick - hThumb.w * 0.5f;
            scroll_.SetOffsetX(std::clamp(wantLeft, 0.0f, trackRange) / trackRange * scroll_.MaxOffsetX());
        }
        scroll_.BeginHDrag(dipX);
        if (Context().input)
            Context().input->CapturePointer(this);
        Invalidate();
        e.handled = true;
        return;
    }

    // Column divider drag: takes precedence over the header click, because the grab zone
    // straddles the boundary between two headers and a click there means "resize", not
    // "sort by whichever header the pointer happens to be a pixel inside".
    const int divider = DividerAtPoint(dipX, dipY);
    if (divider >= 0) {
        resizingColumn_ = divider;
        resizeStartX_ = dipX;
        resizeStartWidth_ = columns_[static_cast<size_t>(divider)].width;
        if (Context().input)
            Context().input->CapturePointer(this);
        e.handled = true;
        return;
    }

    const RectDip hdr = HeaderRect();
    if (hdr.contains(dipX, dipY)) {
        // Header click: report the column so the app can sort. The grid does not sort.
        // Frozen columns are tested FIRST and in reverse order, because a frozen column
        // is painted on top of whatever scrolled underneath it — the topmost thing under
        // the pointer is what the user believes they clicked.
        for (size_t i = columns_.size(); i-- > 0;) {
            if (!columns_[i].frozen) continue;
            const float x = ColumnLeft(static_cast<int>(i));
            if (dipX >= x && dipX < x + columns_[i].width) {
                clickedColumn_ = static_cast<int>(i);
                RoutedEventArgs args{};
                args.source = this;
                headerClicked_.Raise(*this, args);
                e.handled = true;
                return;
            }
        }
        for (size_t i = 0; i < columns_.size(); ++i) {
            if (columns_[i].frozen) continue;
            const float x = ColumnLeft(static_cast<int>(i));
            if (dipX >= x && dipX < x + columns_[i].width) {
                clickedColumn_ = static_cast<int>(i);
                RoutedEventArgs args{};
                args.source = this;
                headerClicked_.Raise(*this, args);
                e.handled = true;
                return;
            }
        }
        return;
    }

    const int row = RowAtPoint(dipY);

    // A click anywhere in the body while an edit is live ends that edit FIRST. Without this
    // the overlay stays parented to its old cell while selection moves elsewhere, which reads
    // as a stuck edit box that cannot be dismissed. Commit (not cancel) matches every grid
    // users know: clicking away keeps what you typed.
    if (editingRow_ >= 0 && editingRow_ != row) CommitEdit();

    if (row >= 0) {
        // Focus the grid on any body click so F2/arrows work immediately. Without this, the
        // user must Tab to the grid or click twice — once to focus, again to select.
        if (Context().focus) Context().focus->SetFocus(this);

        // Double-click in the body: start editing the first editable column in that row.
        // A clickCount > 1 means the two clicks were close in both time and space (see
        // ClickCounter.h), so the user intended to activate that cell, not just select it.
        if (e.clickCount >= 2 && editingEnabled_) {
            // Find the first column; for now we don't track which column was clicked.
            const int col = 0;  // TODO: compute from dipX when multi-column editing matters
            BeginEdit(row, col);
            e.handled = true;
            return;
        }

        // Modifier-aware selection in Multiple mode only, so a Ctrl+click in Single mode
        // behaves exactly as it always did. Ctrl beats Shift when both are held — the
        // precedence ListBox and Explorer use.
        if (selectionMode_ == SelectionMode::Multiple) {
            const bool ctrl = (e.modifiers & ModifierKeys::Ctrl) != ModifierKeys::None;
            const bool shift = (e.modifiers & ModifierKeys::Shift) != ModifierKeys::None;
            if (ctrl)        ToggleRowSelection(row);
            else if (shift)  RangeSelectTo(row);
            else             SetSelectedRow(row);
        } else {
            SetSelectedRow(row);
        }

        // Row reordering drag: initiate only on a selected row (so you can select first,
        // then drag the selection as a unit). Store the start Y to distinguish a drag from
        // a click — a 2px drift should not trigger reordering, which would confuse users who
        // just want to click. The actual drag gesture starts in OnPointerMoved when drift
        // exceeds the threshold, not here.
        if (rowReorderEnabled_ && IsRowSelected(row)) {
            draggingRow_ = row;
            dragStartY_ = dipY;
        }

        e.handled = true;
        if (Context().input)
            Context().input->CapturePointer(this);
    }
}

void DataGrid::OnPointerMoved(PointerEventArgs& e) {
    const float dipX = e.position.x, dipY = e.position.y;

    // Column resize drag: live width update. Measure-level invalidation, because the
    // column width feeds TotalContentWidth() and therefore the horizontal scroll extent.
    if (resizingColumn_ >= 0) {
        const float delta = dipX - resizeStartX_;
        const float want = std::max(kMinColumnWidth, resizeStartWidth_ + delta);
        Column& col = columns_[static_cast<size_t>(resizingColumn_)];
        if (col.width != want) {
            col.width = want;
            InvalidateDirty(DirtyFlags::Measure);
            ClampScroll();
        }
        e.handled = true;
        return;
    }

    // Row reordering drag: once the pointer drifts beyond a small threshold (4 DIP),
    // convert the potential drag into an active drag and track the drop target row.
    // The threshold prevents accidental reordering from a wobbly click.
    if (draggingRow_ >= 0) {
        const float drift = std::abs(dipY - dragStartY_);
        if (drift > 4.0f) {
            // Active drag: compute which row the pointer is over and show the drop indicator.
            const int targetRow = RowAtPoint(dipY);
            if (targetRow >= 0 && targetRow != dropTargetRow_) {
                dropTargetRow_ = targetRow;
                Invalidate();
            }
        }
        e.handled = true;
        return;
    }

    // Scrollbar drag: track the thumb.
    if (scroll_.IsDragging()) {
        scroll_.DragTo(dipY);
        Invalidate();
        e.handled = true;
        return;
    }
    if (scroll_.IsHDragging()) {
        scroll_.HDragTo(dipX);
        Invalidate();
        e.handled = true;
        return;
    }

    // Divider hover: remembered so Cursor() can answer in O(1). NOT an invalidation:
    // Render draws nothing for a hovered divider — the feedback is the resize cursor,
    // which the window applies through WM_SETCURSOR — and repainting here redrew the
    // whole grid (every visible cell's text) twice per divider the pointer crossed,
    // for no visible change.
    hoverDivider_ = DividerAtPoint(dipX, dipY);

    // Scrollbar hover: wake the bar, expand on hover.
    scroll_.Wake();
    scroll_.SetBarHover(scroll_.HitBarRegion(dipX, dipY));
    scroll_.SetHBarHover(scroll_.HitHBarRegion(dipX, dipY));
}

void DataGrid::OnPointerReleased(PointerEventArgs& e) {
    if (e.button != PointerButton::Left) return;

    const bool wasResizing = (resizingColumn_ >= 0);
    if (wasResizing) {
        resizingColumn_ = -1;
    }

    // Row reordering drop: commit the reorder if we have a valid drop target that differs
    // from the dragged row(s). The grid does NOT reorder its own data — the app receives
    // RowsReordered and must update its backing storage.
    const bool wasReordering = (draggingRow_ >= 0 && dropTargetRow_ >= 0);
    if (wasReordering && dropTargetRow_ != draggingRow_) {
        // Collect all selected rows (the dragging unit) in ascending order.
        reorderedOldIndices_ = selectedRows_;
        reorderedNewIndex_ = dropTargetRow_;
        RoutedEventArgs args{};
        args.source = this;
        rowsReordered_.Raise(*this, args);
    }
    draggingRow_ = -1;
    dropTargetRow_ = -1;

    const bool wasDragging = scroll_.IsDragging() || scroll_.IsHDragging();
    if (scroll_.IsDragging()) {
        scroll_.EndDrag();
        Invalidate();
    }
    if (scroll_.IsHDragging()) {
        scroll_.EndHDrag();
        Invalidate();
    }

    if (Context().input && Context().input->Captured() == this)
        Context().input->ReleaseCapture(this);

    e.handled = true;
}

void DataGrid::OnPointerLeft() {
    hoverDivider_ = -1;
    scroll_.SetBarHover(false);
    scroll_.SetHBarHover(false);
}

void DataGrid::OnPointerWheelChanged(PointerEventArgs& e) {
    // Animated, so the wheel feels like the rest of the framework's scrolling; AnimateBy
    // accumulates against the pending target so spinning builds momentum. Commit a live
    // edit first (see SetVerticalOffset): the wheel used to scroll the rows out from
    // under an editor that stayed where it was.
    //
    // Nothing to scroll (all rows fit): leave the wheel UNHANDLED so it bubbles to an
    // enclosing ScrollPanel. Marking it handled here swallowed it, and a page holding a
    // short grid could not be scrolled while the pointer was over the grid. Same guard
    // as ListBox / TreeView / ComboBox's list.
    if (scroll_.MaxOffset() <= 0.0f) return;
    if (editingRow_ >= 0) CommitEdit();
    scroll_.AnimateBy(-static_cast<float>(e.wheelDelta) / 120.0f *
                      kWheelStepRows * rowHeight_);
    e.handled = true;
    Invalidate();
}

void DataGrid::OnKeyDownRouted(KeyEventArgs& e) {
    // While editing, Enter/Escape belong to the editor, not to the grid. Handled FIRST so
    // Enter cannot fall through to row navigation and Escape cannot reach a parent dialog
    // and close it — the usual complaint about grids that bolt editing on afterwards.
    if (editingRow_ >= 0) {
        if (e.vk == VK_RETURN) {
            // A rejected commit deliberately keeps the overlay open and still marks the key
            // handled: the user must fix or cancel, and Enter must not also move the row.
            CommitEdit();
            e.handled = true;
            return;
        }
        if (e.vk == VK_ESCAPE) {
            CancelEdit();
            e.handled = true;
            return;
        }
        // Every other key goes to the TextBox through normal focus routing.
        return;
    }

    // F2 opens the editor on the current row, matching Explorer's rename shortcut.
    if (e.vk == VK_F2 && editingEnabled_) {
        const int row = SelectedRow();
        if (row >= 0) {
            BeginEdit(row, 0);
            e.handled = true;
        }
        return;
    }

    // Ctrl+C: valid with no selection (copies nothing) and must not be mistaken for a
    // navigation key.
    if (copyEnabled_ && e.vk == 'C' &&
        (e.modifiers & ModifierKeys::Ctrl) != ModifierKeys::None) {
        if (CopySelectionToClipboard()) e.handled = true;
        return;
    }

    if (rowCount_ <= 0) return;
    const RectDip vp = ViewportRect();
    const int pageRows = std::max(1, static_cast<int>(vp.h / rowHeight_) - 1);
    // Step from the ACTIVE row (the end the last gesture moved), falling back to the
    // first selected row when there is none — see activeRow_ for why front() alone
    // stalled a downward Shift range after one row.
    const int current = (activeRow_ >= 0 && activeRow_ < rowCount_) ? activeRow_
                                                                     : SelectedRow();
    int target = current;

    switch (e.vk) {
        case VK_DOWN:  target = (current < 0) ? 0 : current + 1; break;
        case VK_UP:    target = (current < 0) ? 0 : current - 1; break;
        case VK_NEXT:  target = (current < 0) ? 0 : current + pageRows; break;
        case VK_PRIOR: target = (current < 0) ? 0 : current - pageRows; break;
        case VK_HOME:  target = 0; break;
        case VK_END:   target = rowCount_ - 1; break;
        default: return;
    }
    target = std::clamp(target, 0, rowCount_ - 1);
    if (target != current) {
        // Shift+arrow extends selection in Multiple mode; plain arrow replaces it. Both
        // step from the active row, which matches Explorer.
        const bool shift = (e.modifiers & ModifierKeys::Shift) != ModifierKeys::None;
        if (selectionMode_ == SelectionMode::Multiple && shift && current >= 0) {
            // Re-anchor at the current first, so Shift+Down then Shift+Up shrinks back down
            // instead of walking the anchor.
            if (selectionAnchor_ < 0 || selectionAnchor_ >= rowCount_)
                selectionAnchor_ = current;
            RangeSelectTo(target);
        } else {
            SetSelectedRow(target);
        }
        ScrollRowIntoView(target);
    }
    e.handled = true;
}

void DataGrid::OnAnimationTick(float dtSec) {
    scroll_.Tick(dtSec);
    // scroll_ is a member rather than a tree node, so its own Invalidate cannot reach the
    // host. Invalidate here, exactly as ListBox does.
    Invalidate();
}

HCURSOR DataGrid::Cursor() const {
    // Horizontal resize arrow while over (or actively dragging) a column divider. Loaded
    // once and cached: LoadCursor on a system cursor is cheap but this runs per mouse move.
    if (resizingColumn_ >= 0 || hoverDivider_ >= 0) {
        static HCURSOR sizeWE = LoadCursor(nullptr, IDC_SIZEWE);
        return sizeWE;
    }
    return nullptr;
}

float DataGrid::VisualOverflowDip() const {
    // Unconditional, not gated on focus: the frame where focus LEAVES still has to clear
    // the ring's pixels, and IsFocused() is already false by then.
    return FocusRingPadDip(FocusRingSpec{});
}

void DataGrid::BeginEdit(int row, int col) {
    if (!editingEnabled_) return;
    if (row < 0 || row >= rowCount_) return;
    if (col < 0 || col >= static_cast<int>(columns_.size())) return;
    if (!cellProvider_) return;

    // A second BeginEdit while the overlay is live: commit the previous edit (Enter-like)
    // before starting the new one. CancelEdit is the wrong semantics — F2 on a different
    // cell reads as "I'm done with the first one, start the next", not "discard my work".
    if (editingRow_ >= 0) CommitEdit();

    editingRow_ = row;
    editingColumn_ = col;

    // Lazy-construct the overlay on first use so an app that never edits pays nothing.
    // Reuse it across edits: a TextBox carries a DWrite layout cache that is wasteful to
    // rebuild per double-click. The control is a MEMBER, not a tree child (see scroll_).
    // Pass 'this' so the overlay can commit on blur (see EditOverlay::OnFocusChanged).
    if (!editOverlay_) {
        editOverlay_ = std::make_unique<EditOverlay>(this);
        // Wire it as if it were a child: attach to the same context so it inherits theme.
        editOverlay_->SetParent(this);
        if (IsAttached()) editOverlay_->AttachToContext(Context());
    }

    editOverlay_->SetText(cellProvider_(row, col));
    // Select-all: typing replaces, like Excel. Text length is in UTF-16 code units, NOT
    // code points, so std::wstring::size() is the right bound for DWrite's API.
    editOverlay_->Select(0, static_cast<UINT32>(editOverlay_->Text().size()));
    // Position it now; Arrange re-runs on scroll to track the cell.
    editOverlay_->Arrange(CellRect(row, col));

    // Focus the overlay so the user can type. FocusManager::SetFocus expects a raw
    // pointer; the control remains valid because it is a unique_ptr member.
    if (Context().focus) Context().focus->SetFocus(editOverlay_.get());

    InvalidateDirty(DirtyFlags::Arrange);
}

void DataGrid::CancelEdit() {
    if (editingRow_ < 0) return;  // not editing

    // Clear the editing state FIRST. Moving focus fires EditOverlay::OnFocusChanged, whose
    // commit-on-blur would otherwise re-enter CommitEdit while editingRow_ is still set and
    // raise CellEdited a second time. Clearing first makes that hook a no-op.
    editingRow_ = -1;
    editingColumn_ = -1;

    // Return focus to the grid so keyboard navigation keeps working after the editor closes.
    if (Context().focus && Context().focus->Focused() == editOverlay_.get())
        Context().focus->SetFocus(this);
    // Do NOT destroy editOverlay_ itself — keep it alive for the next edit.
    InvalidateDirty(DirtyFlags::Arrange);
}

bool DataGrid::CommitEdit() {
    if (editingRow_ < 0) return false;  // not editing
    const int row = editingRow_, col = editingColumn_;
    const std::wstring text = editOverlay_->Text();

    // Validation runs BEFORE the overlay is hidden, so a rejected edit leaves the user
    // focused in the box with their text intact rather than silently discarding it.
    if (cellValidator_ && !cellValidator_(row, col, text)) return false;

    // Publish the payload, hide the overlay, and raise the event so a handler can
    // refresh the backing data and call RefreshCells() without fighting a live editor.
    editedRow_ = row;
    editedColumn_ = col;
    editedText_ = text;
    CancelEdit();  // hide overlay, return focus

    RoutedEventArgs args{};
    args.source = this;
    cellEdited_.Raise(*this, args);
    return true;
}

bool DataGrid::CopySelectionToClipboard() const {
    // TSV format: rows separated by CRLF, columns by tab. selectedRows_ is kept sorted, so
    // a range select produces clipboard output in ascending order, which is what a user
    // expects when pasting into Excel or a text editor.
    if (selectedRows_.empty() || !cellProvider_) return false;

    std::wstring tsv;
    for (size_t i = 0; i < selectedRows_.size(); ++i) {
        const int r = selectedRows_[i];
        if (r < 0 || r >= rowCount_) continue;
        if (i > 0) {
            tsv += static_cast<wchar_t>(13);
            tsv += static_cast<wchar_t>(10);
        }
        for (size_t col = 0; col < columns_.size(); ++col) {
            if (col > 0) tsv += L'\t';
            tsv += cellProvider_(r, static_cast<int>(col));
        }
    }

    // Win32 clipboard dance, same pattern as TextEditBase. Context().hwnd is the top-level
    // window owning this control; GetActiveWindow is the fallback when not yet attached.
    HWND owner = Context().hwnd ? Context().hwnd : GetActiveWindow();

    const size_t bytes = (tsv.size() + 1) * sizeof(wchar_t);
    HGLOBAL mem = GlobalAlloc(GMEM_MOVEABLE, bytes);
    if (!mem) return false;
    void* locked = GlobalLock(mem);
    if (!locked) { GlobalFree(mem); return false; }
    memcpy(locked, tsv.c_str(), bytes);
    GlobalUnlock(mem);

    if (!OpenClipboard(owner)) { GlobalFree(mem); return false; }
    EmptyClipboard();
    SetClipboardData(CF_UNICODETEXT, mem);
    CloseClipboard();
    return true;
}

float DataGrid::MeasureColumnContent(int col) const {
    if (col < 0 || col >= static_cast<int>(columns_.size())) return 0.0f;
    DWriteContext* dw = Dwrite();
    if (!dw || !dw->Valid()) return columns_[static_cast<size_t>(col)].width;

    const TypographyTokens& typo = Theme().typography;
    const Column& column = columns_[static_cast<size_t>(col)];

    // Measures the header plus only the VISIBLE rows. Measuring every row would be O(N)
    // and would undo the virtualization this control exists for — on a 100k-row grid an
    // autosize would stall the UI thread for seconds. The trade-off is honest: the fit is
    // exact for what the user can see, and scrolling to wider content does not re-fit.
    auto measure = [&](const std::wstring& text, DWRITE_FONT_WEIGHT weight) -> float {
        if (text.empty()) return 0.0f;
        IDWriteTextFormat* fmt = dw->Format(typo.bodySize, weight,
                                            DWRITE_TEXT_ALIGNMENT_LEADING,
                                            DWRITE_PARAGRAPH_ALIGNMENT_CENTER,
                                            DWRITE_WORD_WRAPPING_NO_WRAP);
        if (!fmt) return 0.0f;
        ComPtr<IDWriteTextLayout> layout;
        // 1e6 for both axes: an effectively unbounded box, so the metrics report the
        // text's natural width instead of a wrapped one.
        if (FAILED(dw->Factory()->CreateTextLayout(
                text.c_str(), static_cast<UINT32>(text.size()), fmt,
                1e6f, 1e6f, layout.GetAddressOf())))
            return 0.0f;
        DWRITE_TEXT_METRICS m{};
        if (FAILED(layout->GetMetrics(&m))) return 0.0f;
        return m.widthIncludingTrailingWhitespace;
    };

    // Header is SEMI_BOLD (matching Render), so it must be measured at that weight or a
    // bold header clips inside a width fitted to the regular-weight metric.
    float widest = measure(column.header, DWRITE_FONT_WEIGHT_SEMI_BOLD);
    // Reserve the sort arrow when this column carries one, or autosizing the sorted column
    // makes the arrow overlap the text it just fitted.
    if (col == sortColumn_ && sortDirection_ != SortDirection::None)
        widest += kSortArrowW + kSortArrowPad;

    if (cellProvider_) {
        int firstRow = 0, lastRow = -1;
        VisibleRowRange(firstRow, lastRow);
        for (int r = firstRow; r <= lastRow; ++r)
            widest = std::max(widest, measure(cellProvider_(r, col),
                                              DWRITE_FONT_WEIGHT_NORMAL));
    }

    // Both cell padding edges, then the same floor a drag-resize enforces.
    return std::max(kMinColumnWidth, widest + kCellPadH * 2.0f);
}

void DataGrid::AutosizeColumn(int col) {
    if (col < 0 || col >= static_cast<int>(columns_.size())) return;
    const float want = MeasureColumnContent(col);
    Column& column = columns_[static_cast<size_t>(col)];
    if (column.width == want) return;
    column.width = want;
    // Measure-level: the column width feeds TotalContentWidth and the horizontal extent.
    InvalidateDirty(DirtyFlags::Measure);
    ClampScroll();
}

}  // namespace fluent
