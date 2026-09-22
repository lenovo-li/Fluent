// DataGrid.h — a virtualized, read-only table of text cells.
//
// WHY THIS EXISTS. A data application's central widget is a table, and this framework had
// none: ListBox is one column of strings, TreeView is a hierarchy. The consumer that
// motivated this shows market snapshots and financial statements — thousands of rows,
// tens of columns — so the requirement is not "draw a grid" but "draw a grid whose cost
// does not grow with the data".
//
// VIRTUALIZATION IS THE POINT, and it applies to BOTH axes:
//   * rows: only those intersecting the viewport are drawn;
//   * columns: only those intersecting the horizontal scroll window are drawn.
// A 5000x80 table therefore costs the same per frame as a 20x8 one. Column virtualization
// matters as much as row virtualization here: a wide financial statement has 80+ periods,
// and drawing all of them off-screen would dominate the frame while looking fine on a
// small test table.
//
// PULL, NOT PUSH: the grid never owns the data. The caller supplies a cell provider
// (row, col) -> wstring, so a million-row table costs nothing to "load" and the app keeps
// its own storage, formatting and sort order. This is the same contract ListBox's
// virtualized mode uses, for the same reason.
//
// SCROLLING is internal (it owns two ScrollState objects) rather than delegated to an
// enclosing ScrollPanel. A grid must keep its header row pinned while the body scrolls
// vertically, and its row labels are drawn per-cell; an outer scroller would move the
// header out of view and would have to be told the total extent anyway.
//
// WHAT IT DOES NOT DO, so the limits are known before use:
//   * No editing. Read-only. Cell editing needs a focus/commit/validation model that
//     belongs to a separate control (an editable grid is a different beast).
//   * No sorting or filtering. The caller sorts its own data and re-points the provider;
//     the grid has no opinion about order. Header clicks are reported so the app can act.
//   * No per-cell widgets. Cells are text. A grid of arbitrary elements would defeat the
//     virtualization this control exists for.
//   * No cell wrapping. One line per cell, clipped at the column edge — a wrapped cell
//     makes row height data-dependent, which breaks the constant-time row indexing that
//     makes scrolling O(visible).

#pragma once
#include "../core/Control.h"
#include "../base/Event.h"
#include "../input/RoutedEvent.h"
#include "../layout/ScrollViewer.h"
#include <functional>
#include <string>
#include <vector>

namespace fluent {

class TextBox;   // edit overlay; only the .cpp needs the definition

class DataGrid : public Control {
public:
    // How a column's text sits in its cell. Numeric data is read by comparing digits in a
    // column, which only works when the digits line up, so a numeric column must be
    // right-aligned; that is why this exists rather than one global alignment.
    enum class Align { Left, Right, Center };

    enum class SortDirection { None, Ascending, Descending };

    enum class SelectionMode { Single, Multiple };

    struct Column {
        std::wstring header;
        float width = 96.0f;      // DIPs; fixed (no auto-fit — see the note on Measure)
        Align align = Align::Left;
        bool resizable = true;    // Allow user to drag column divider to resize
        bool frozen = false;      // Pin column to left edge (does not scroll horizontally)
    };

    // (row, col) -> the text to draw. Called only for visible cells, once per cell per
    // frame. Must be cheap and must not allocate if avoidable: it is on the render path.
    using CellProvider = std::function<std::wstring(int row, int col)>;

    // (row) -> a colour for that row's text, or nullopt to use the theme default. Exists
    // for the case that motivated the grid: a market table where a whole row is tinted by
    // whether the instrument rose or fell. Optional — a grid with no accessor draws every
    // row in textPrimary.
    using RowColorProvider = std::function<bool(int row, D2D1_COLOR_F& out)>;

    DataGrid();
    ~DataGrid();  // out-of-line so unique_ptr<TextBox> doesn't need the definition here

    // --- Data ------------------------------------------------------------
    void SetColumns(std::vector<Column> columns);
    const std::vector<Column>& Columns() const { return columns_; }

    // Row COUNT, not rows: the grid pulls text on demand.
    // No-ops when the count is unchanged — so a caller whose BACKING DATA changed
    // while the row count stayed the same must call RefreshCells() as well, or the
    // grid keeps painting the text it last pulled.
    void SetRowCount(int count);
    int RowCount() const { return rowCount_; }

    // Re-pull every visible cell from the provider. For the common case of
    // "same rows, new values" (a polling refresh, an in-place edit): geometry is
    // unchanged, so this is a render-level invalidation, not a re-measure.
    void RefreshCells() { Invalidate(); }

    void SetCellProvider(CellProvider provider);
    void SetRowColorProvider(RowColorProvider provider);

    // --- Appearance ------------------------------------------------------
    void SetRowHeight(float dip);
    float RowHeight() const { return rowHeight_; }

    // Zebra striping. Off by default: it helps a wide table and clutters a narrow one, so
    // the caller decides.
    void SetAlternatingRowFill(bool on);
    bool AlternatingRowFill() const { return alternating_; }

    void SetShowGridLines(bool on);
    bool ShowGridLines() const { return showGridLines_; }

    // --- Column features (Phase 1) ---------------------------------------
    // Enable/disable column resize dragging globally. When enabled, draggable columns
    // (Column::resizable=true) show a resize cursor on their divider and allow dragging.
    void SetColumnResizeEnabled(bool enabled) { columnResizeEnabled_ = enabled; }
    bool ColumnResizeEnabled() const { return columnResizeEnabled_; }

    // Set which column is sorted and in which direction. The grid does NOT sort data —
    // the application sorts its backing data and calls this to update the visual indicator
    // (▲▼ arrow in header). Call SetSortColumn(col, SortDirection::None) to clear.
    void SetSortColumn(int col, SortDirection dir);
    void ClearSort() { sortColumn_ = -1; sortDirection_ = SortDirection::None; Invalidate(); }
    int SortColumn() const { return sortColumn_; }
    SortDirection GetSortDirection() const { return sortDirection_; }

    // Enable Ctrl+C to copy selected rows to clipboard as TSV.
    void SetCopyEnabled(bool enabled) { copyEnabled_ = enabled; }
    bool CopyEnabled() const { return copyEnabled_; }

    // --- Phase 4: row reordering -----------------------------------------
    // Enable drag-and-drop row reordering. When enabled, dragging a selected row moves it
    // (and any other selected rows in Multiple mode) to a new position. The grid does NOT
    // reorder backing data — the app receives RowsReordered(oldIndices, newIndex) and must
    // update its own storage. Call RefreshCells() after reordering.
    void SetRowReorderEnabled(bool enabled) { rowReorderEnabled_ = enabled; }
    bool RowReorderEnabled() const { return rowReorderEnabled_; }

    // Fired when the user drops rows at a new position. oldIndices is the ORIGINAL row
    // indices (before the move) in ascending order, newIndex is where they land (the first
    // row's new index). The app must reorder its backing data and call RefreshCells().
    Event<DataGrid, RoutedEventArgs>& RowsReordered() { return rowsReordered_; }
    const std::vector<int>& ReorderedOldIndices() const { return reorderedOldIndices_; }
    int ReorderedNewIndex() const { return reorderedNewIndex_; }

    // --- Phase 5: column autosizing --------------------------------------
    // Fit a column to its content by measuring all visible cells plus the header. Returns
    // the new width in DIPs. Does NOT change the column — the caller applies it via
    // SetColumns(). Measuring all rows would be O(N), so this samples only the viewport.
    float MeasureColumnContent(int col) const;
    // Convenience: measure and apply in one call.
    void AutosizeColumn(int col);

    // --- Cell editing (Phase 3) ------------------------------------------
    // Enable in-place editing. When enabled, F2 or double-click on a cell opens a text
    // box overlay. Enter commits, Escape cancels. The validation callback is called before
    // commit; return false to reject the edit and keep the overlay open.
    void SetEditingEnabled(bool enabled) { editingEnabled_ = enabled; }
    bool EditingEnabled() const { return editingEnabled_; }

    // (row, col, newText) -> true to accept, false to reject. Called when the user presses
    // Enter in the edit overlay, before CellEdited is raised. Return false to block invalid
    // input (out-of-range numbers, duplicate keys). The callback can modify its own backing
    // data here — CellEdited fires afterward for the app to refresh other dependent cells.
    using CellValidator = std::function<bool(int row, int col, const std::wstring& text)>;
    void SetCellValidator(CellValidator validator) { cellValidator_ = std::move(validator); }

    // Fired after a successful commit (validation passed). Payload: row/col in
    // EditedRow()/EditedColumn(), new text in EditedText(). The app should update its
    // backing data and call RefreshCells() if other cells are affected.
    Event<DataGrid, RoutedEventArgs>& CellEdited() { return cellEdited_; }
    int EditedRow() const { return editedRow_; }
    int EditedColumn() const { return editedColumn_; }
    const std::wstring& EditedText() const { return editedText_; }

    // Programmatically start editing a cell. Same as F2 on that cell.
    void BeginEdit(int row, int col);
    void CancelEdit();
    // editingRow_ is the live flag, not the overlay pointer: the overlay is kept alive
    // between edits for its layout cache, so a non-null pointer does not mean "editing".
    bool IsEditing() const { return editingRow_ >= 0; }
    int EditingRow() const { return editingRow_; }
    int EditingColumn() const { return editingColumn_; }

    // --- Selection -------------------------------------------------------
    // Selection mode: Single (default, one row) or Multiple (Ctrl/Shift modifiers).
    void SetSelectionMode(SelectionMode mode);
    SelectionMode GetSelectionMode() const { return selectionMode_; }

    // Single-selection API (legacy, kept for compatibility). In Multiple mode, these
    // operate on the first selected row or -1 when none are selected.
    void SetSelectedRow(int row);
    int SelectedRow() const;

    // Multi-selection API (Phase 2). In Single mode, SelectedRows() returns a 0- or
    // 1-element vector. ClearSelection() works in both modes.
    const std::vector<int>& SelectedRows() const { return selectedRows_; }
    bool IsRowSelected(int row) const;
    void ClearSelection();

    Event<DataGrid, RoutedEventArgs>& SelectionChanged() { return selectionChanged_; }

    // A header click, so the app can implement sorting. Payload is the column index in
    // ColumnClickedIndex() — read it in the handler.
    Event<DataGrid, RoutedEventArgs>& ColumnHeaderClicked() { return headerClicked_; }
    int ColumnClickedIndex() const { return clickedColumn_; }

    // --- Geometry / virtualization, public for testability ----------------
    // These four are the virtualization contract. They are public because that contract
    // IS the reason this control exists, and a headless test has no other way to prove
    // that a 100k-row grid only touches a screenful — the alternative would be asserting
    // pixels, which cannot distinguish "drew 20 rows" from "drew 100000 rows, 19980 of
    // them off-screen".
    float TotalContentHeight() const { return rowCount_ * rowHeight_; }
    float TotalContentWidth() const;
    // First and last row index intersecting the viewport, inclusive. Returns {0,-1} when
    // nothing is visible (empty grid or scrolled past the end).
    void VisibleRowRange(int& first, int& last) const;
    void VisibleColumnRange(int& first, int& last) const;
    // The header strip, in window DIPs. Pinned: it does not move when the body scrolls
    // vertically (it DOES shift with horizontal scroll, with its cells).
    RectDip HeaderRect() const;
    // The scrollable body area (grid minus header, minus any scrollbar gutters).
    RectDip ViewportRect() const;
    // Where a given cell lands right now, accounting for both scroll offsets. May be
    // outside ViewportRect if the cell is scrolled away.
    RectDip CellRect(int row, int col) const;

    // Scroll offsets, exposed so a test can place the viewport deterministically instead
    // of synthesising wheel events.
    void SetVerticalOffset(float dip);
    float VerticalOffset() const { return scroll_.Offset(); }
    void SetHorizontalOffset(float dip);
    float HorizontalOffset() const { return scroll_.OffsetX(); }

    // Scroll a row into view. Needed by keyboard navigation and by an app restoring a
    // selection; without it "select row 900" would leave the viewport at the top.
    void ScrollRowIntoView(int row);

    // --- Element contract -------------------------------------------------
    void Measure(float availW, float availH) override;
    void Arrange(const RectDip& finalRect) override;
    void Render(const DrawingContext& dc) override;
    UIElement* HitTestDeep(float dipX, float dipY) override;
    void OnPointerPressed(PointerEventArgs& e) override;
    void OnPointerMoved(PointerEventArgs& e) override;
    void OnPointerReleased(PointerEventArgs& e) override;
    void OnPointerLeft() override;
    void OnPointerWheelChanged(PointerEventArgs& e) override;
    void OnKeyDownRouted(KeyEventArgs& e) override;
    float VisualOverflowDip() const override;
    HCURSOR Cursor() const override;
    // Smooth scrolling: the ScrollViewer eases toward its target, so the grid has to be
    // ticked while it does. Same wiring as ListBox.
    bool WantsAnimationTick() const override { return scroll_.NeedsTick(); }
    void OnAnimationTick(float dtSec) override;
    // The member sub-controls (scroll_, editOverlay_) are not tree children, so nothing
    // propagates the context to them automatically. Same wiring ListBox does for scroll_.
    void OnAttachedToTree() override;
    void OnDetachedFromTree() override;

private:
    // Row under a point, or -1. Shared by pointer handling and tests.
    int RowAtPoint(float dipY) const;
    // Column divider under a point (returns the column index whose RIGHT edge is hit),
    // or -1. Used for resize cursor and drag detection.
    int DividerAtPoint(float dipX, float dipY) const;
    // Clamp both scroll offsets to their current extents. Called after anything that
    // changes content size or viewport size, because a resize that shrinks the content
    // below the current offset would otherwise leave the view scrolled into blank space.
    void ClampScroll();
    // Copy the selected rows to the clipboard as TSV. Returns true on success.
    bool CopySelectionToClipboard() const;
    // Commit the active edit overlay. Returns false if validation rejected it.
    bool CommitEdit();
    // Total width of the frozen (pinned) columns at the left edge, in DIPs. Zero when no
    // column is frozen. The frozen band never scrolls, so it is excluded from the
    // horizontal scroll extent and from the scrollable columns' x origin.
    float FrozenWidth() const;
    // X coordinate of the left edge of column `col`, accounting for frozen columns.
    // Frozen columns are never offset by the horizontal scroll; scrolling columns start
    // after the frozen band and are offset.
    float ColumnLeft(int col) const;
    // Multi-selection helpers. Each raises SelectionChanged exactly once.
    void ToggleRowSelection(int row);
    void RangeSelectTo(int row);
    void SelectSingleRow(int row);

    std::vector<Column> columns_;
    int rowCount_ = 0;
    CellProvider cellProvider_;
    RowColorProvider rowColorProvider_;

    float rowHeight_ = 28.0f;
    bool alternating_ = false;
    bool showGridLines_ = true;
    int clickedColumn_ = -1;

    // Selection. selectedRows_ is kept SORTED ASCENDING so SelectedRow() can return
    // front() as "the first selected row" and so a range select produces a predictable
    // order for clipboard copy. Single mode holds at most one entry, which keeps one
    // code path for both modes instead of a bool plus an int.
    SelectionMode selectionMode_ = SelectionMode::Single;
    std::vector<int> selectedRows_;
    // Shift-select pivot: the row a range extends FROM. Set by a plain click and by a
    // Ctrl+click, not by a Shift+click, so repeated Shift+clicks re-extend from the same
    // origin instead of walking the anchor along.
    int selectionAnchor_ = -1;

    // --- Phase 1 features ------------------------------------------------
    bool columnResizeEnabled_ = true;
    bool copyEnabled_ = true;
    int sortColumn_ = -1;
    SortDirection sortDirection_ = SortDirection::None;

    // Column resize drag state. resizingColumn_ >= 0 while a divider is being dragged.
    int resizingColumn_ = -1;
    float resizeStartX_ = 0.0f;
    float resizeStartWidth_ = 0.0f;
    // Divider currently under the pointer (for cursor feedback), or -1.
    mutable int hoverDivider_ = -1;

    // --- Phase 4: row reordering -----------------------------------------
    bool rowReorderEnabled_ = false;
    Event<DataGrid, RoutedEventArgs> rowsReordered_;
    std::vector<int> reorderedOldIndices_;
    int reorderedNewIndex_ = -1;
    // Drag state: draggingRow_ >= 0 while a row is being dragged for reordering.
    int draggingRow_ = -1;
    float dragStartY_ = 0.0f;
    // Visual drop indicator position (row index where the drop would land).
    int dropTargetRow_ = -1;

    // --- Phase 3: cell editing -------------------------------------------
    bool editingEnabled_ = false;
    CellValidator cellValidator_;
    Event<DataGrid, RoutedEventArgs> cellEdited_;
    // Payload for CellEdited event: which cell was edited and what the new text is.
    int editedRow_ = -1;
    int editedColumn_ = -1;
    std::wstring editedText_;

    // The edit overlay. A MEMBER, not a tree child, for the same reason scroll_ is: a
    // Control has no child list, and DataGrid already owns the Arrange/Render/hit-test
    // calls for its internal parts. Created on first edit and reused, because a TextBox
    // carries a DWrite layout cache that is wasteful to rebuild per double-click.
    // editingRow_ >= 0 is the "editing is active" flag, NOT the pointer.
    //
    // Defined in the .cpp: a TextBox subclass that commits the grid's edit when it loses
    // focus (Excel/WPF behaviour). It cannot live here because this header only
    // forward-declares TextBox — deriving needs the complete type.
    class EditOverlay;
    std::unique_ptr<EditOverlay> editOverlay_;
    int editingRow_ = -1;
    int editingColumn_ = -1;

    // ONE ScrollViewer, not two: it already carries both axes, and they deliberately share
    // the fade/idle state so the two rails appear and disappear together (see the comment
    // on its horizontal section). Two instances would desynchronise that.
    //
    // It is a MEMBER, not a child element: it is scroll state plus a scrollbar renderer,
    // and putting it in the tree would give it its own layout slot and hit-testing.
    // ListBox holds it the same way for the same reason.
    ScrollViewer scroll_;

    // Reused across the row loop so a screenful of cells does not allocate a string per
    // cell per frame. Mutable because Render is logically const but must reuse it.
    mutable std::wstring cellScratch_;

    Event<DataGrid, RoutedEventArgs> selectionChanged_;
    Event<DataGrid, RoutedEventArgs> headerClicked_;
};

}  // namespace fluent
