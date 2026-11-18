// LogViewer.h — high-throughput virtualized log viewer with inline styling.
//
// A read-only scrollable log viewer optimized for high-speed append (target:
// 10k lines/s) and large documents (100M+ lines). Every line is a sequence of
// styled spans (color, font size, weight, underline), so a single log line can
// mix red ERROR markers, cyan URLs, bold timestamps, and normal-weight prose.
//
// VIRTUALIZED: draws only the visible lines, using dynamic row heights computed
// on first render. A Y-coordinate table maps scroll offset to line number via
// binary search, and line heights are cached once measured. Memory scales with
// line count (metadata only), not with content size — a 1M-line log is ~100 MB
// of bookkeeping plus the text itself.
//
// Scrolling is compositor-independent (UI thread only), through the embedded
// ScrollViewer. The viewer supports wheel, drag, and programmatic scroll.
//
// NOT A TEXT EDITOR. Read-only: no caret and no editing, though text can be
// selected (drag, Shift+keys, Ctrl+A) and copied. For an editable multi-line
// control, use TextArea; for rich formatting with editing, use RichTextBox.
//
// DESIGN RATIONALE:
//   - Dynamic row height: log lines with large font sizes naturally expand the
//     row. An estimation phase (未测量行用默认高度) defers measurement until
//     first paint, similar to TextArea's WrapExtentMap.
//   - Per-span TextLayout: each TextSpan gets its own IDWriteTextLayout,
//     simplifying style isolation. A more memory-efficient approach (single
//     layout + SetFontSize ranges) is possible but deferred until proven needed.
//     The layouts are cached for the visible lines only (a sliding window keyed
//     by line index), so a repaint of an unchanged view — a scrollbar fade, a
//     selection drag — builds none, and a one-line tail-follow scroll builds the
//     spans of the one line that came into view.
//   - Append-optimized: AppendLine() is O(1) amortized, including once the ring
//     buffer is full. Lines live in a deque and Y-positions are stored relative
//     to a moving origin, so a trim pops the front and moves the origin instead
//     of shifting and rewriting every surviving entry.
//
// USAGE:
//   auto log = std::make_unique<LogViewer>();
//   log->AppendLine({
//       {L"[ERROR] ", Colors::Red, 12.0f, DWRITE_FONT_WEIGHT_BOLD},
//       {L"Connection timeout at ", Colors::White},
//       {L"192.168.1.1:8080", Colors::Cyan, 12.0f, DWRITE_FONT_WEIGHT_NORMAL, true}
//   });
//   log->SetAutoScrollToTail(true);  // tail -f mode
//   log->SetMaxLines(100000);        // ring buffer
#pragma once

#include "../core/Control.h"
#include "../layout/ScrollViewer.h"
#include <deque>
#include <vector>
#include <string>

namespace fluent {

// One styled text span within a log line.
struct TextSpan {
    std::wstring text;
    // Color: negative alpha as a sentinel for "use theme textPrimary". Allows spans
    // without explicit color to follow the theme (light/dark), while explicit colors
    // (passed by the caller) stay fixed.
    D2D1_COLOR_F color = D2D1::ColorF(-1.0f, -1.0f, -1.0f, -1.0f);  // sentinel
    float fontSize = 12.0f;
    DWRITE_FONT_WEIGHT weight = DWRITE_FONT_WEIGHT_NORMAL;
    bool underline = false;

    TextSpan() = default;
    TextSpan(std::wstring t, D2D1_COLOR_F c = D2D1::ColorF(-1.0f, -1.0f, -1.0f, -1.0f),
             float fs = 12.0f, DWRITE_FONT_WEIGHT w = DWRITE_FONT_WEIGHT_NORMAL,
             bool u = false)
        : text(std::move(t)), color(c), fontSize(fs), weight(w), underline(u) {}
};

// One line in the log (a sequence of spans).
struct LogLine {
    std::vector<TextSpan> spans;
    float height = 0.0f;         // cached row height (DIP); 0 = not yet measured
    bool heightMeasured = false;

    // The line's spans concatenated, which is what a copy of this line yields.
    // Built on demand rather than cached: it is needed only when the user copies,
    // and caching it would double the memory of a million-line buffer for a
    // feature used once in a while.
    std::wstring PlainText() const;
};

// A position inside the document: which line, and how many UTF-16 code units into
// that line's concatenated text. Ordering is lexicographic, which is what makes a
// selection range comparable with < and normalizable by swapping.
struct LogTextPos {
    size_t line = 0;
    size_t column = 0;   // offset into LogLine::PlainText()

    friend bool operator<(const LogTextPos& a, const LogTextPos& b) {
        return a.line != b.line ? a.line < b.line : a.column < b.column;
    }
    friend bool operator==(const LogTextPos& a, const LogTextPos& b) {
        return a.line == b.line && a.column == b.column;
    }
    friend bool operator!=(const LogTextPos& a, const LogTextPos& b) { return !(a == b); }
    friend bool operator<=(const LogTextPos& a, const LogTextPos& b) { return a < b || a == b; }
};

class LogViewer : public Control {
public:
    LogViewer();

    // --- Content management -----------------------------------------------
    // Append a line composed of multiple styled spans. The line is appended to
    // the buffer, and if auto-scroll-to-tail is enabled the view scrolls to
    // the new bottom. Predicted row height is max(span fontSize) × 1.2; actual
    // height is measured on first render.
    void AppendLine(std::vector<TextSpan> spans);

    // Clear all lines.
    void Clear();

    // Set ring-buffer mode: when line count exceeds maxLines, the oldest lines
    // are trimmed. 0 (default) = unlimited. A trim costs O(lines removed): the
    // front of the buffer is popped and the Y origin moves past it, and cached
    // layouts for surviving lines are kept (renumbered with the lines).
    void SetMaxLines(int maxLines);
    int MaxLines() const { return maxLines_; }

    size_t LineCount() const { return lines_.size(); }
    // --- For UI Automation: lines as items, keyed by a serial that survives the
    // front trim (line i's serial is FirstLineSerial() + i; it never repeats).
    uint64_t FirstLineSerial() const { return firstLineSerial_; }
    // The line's text (its spans concatenated), or empty.
    std::wstring LineText(size_t index) const {
        return index < lines_.size() ? lines_[index].PlainText() : std::wstring();
    }
    // Line rect in control space, clipped to the viewport; empty when scrolled out.
    RectDip LineRect(size_t index) const;
    // The line under a control-space y, or SIZE_MAX.
    size_t LineIndexAt(float dipY) const;
    // Minimal scroll that shows the line (turns tail-following off when it scrolls up).
    void ScrollLineIntoView(size_t index);
    float VerticalOffset() const;
    float MaxVerticalOffset() const;
    void SetVerticalOffset(float dip);

    float HorizontalOffset() const;
    float MaxHorizontalOffset() const;
    void SetHorizontalOffset(float dip);

    // Total document height in DIPs: the last line's top plus its height. Before a
    // line has been painted its height is the estimate (largest font size in the
    // row times the spacing factor), so this is usable from the first append —
    // which is what lets the scrollbar be correct on the frame a batch arrives
    // rather than one frame later.
    //
    // Public because it is the only observable consequence of the estimate, and the
    // estimate is a contract (AppendLine must stay O(1), so it cannot measure).
    // Headless tests have no device and therefore no other way to check it.
    float ContentExtentDip() const { return GetContentExtent(); }

    // Current height (DIPs) of one row: the estimate until measured, then the real
    // value. 0 when out of range. Public so a test can check that each row is sized
    // from its own spans — the rows' relative heights are what decide overlap.
    float LineHeightDip(size_t index) const {
        return index < lines_.size() ? lines_[index].height : 0.0f;
    }
    // Content-space Y (DIPs) of a line's top edge; 0 when out of range. The sum of
    // the heights above it, which is what a trim must keep true for the survivors.
    float LineTopDip(size_t index) const {
        return index < lines_.size() ? LineTop(index) : 0.0f;
    }

    // --- Layout cache diagnostics ----------------------------------------
    // Span layouts built since construction (draw, hit test and attach-time
    // measurement all count), and how many lines currently hold cached layouts.
    // Public for the same reason as TextArea::LineIndexRebuildCount: the cost being
    // guarded is invisible in the output, so the counter is the observable.
    unsigned SpanLayoutsBuilt() const { return spanLayoutsBuilt_; }
    size_t CachedLayoutLines() const { return layoutRows_.size(); }
    // Bring the layout cache in line with what a paint at the CURRENT scroll offset
    // would draw — the same call Render makes before its loop, exposed because
    // Render itself needs a device context and cannot run headless.
    void PrepareVisibleLayoutsForTest();
    // Width (DIPs) of line `index` as the selection highlight and hit test see it:
    // ColumnToX at the end of the line, which reads the CACHED layout when the line
    // is in the layout window. A row attached to the wrong line shows up here as
    // the neighbour's width — the only headless-visible symptom, since the glyphs
    // themselves need a device to draw. 0 when out of range.
    // When horizontal scroll is active, reports the logical width before OffsetX.
    float CachedLineWidthForTest(size_t index) const {
        if (index >= lines_.size()) return 0.0f;
        return ColumnToX(index, lines_[index].PlainText().size())
               - (bounds_.x + kPadding) + scroll_.OffsetX();
    }

    // --- Scrolling --------------------------------------------------------
    // Auto-scroll to tail when new lines arrive (tail -f mode). The scroll
    // happens only if the user is already at the bottom; manually scrolling up
    // disables it until the user scrolls back to the end.
    void SetAutoScrollToTail(bool enable) { autoScrollToTail_ = enable; }
    bool AutoScrollToTail() const { return autoScrollToTail_; }

    // --- Selection and clipboard ------------------------------------------
    //
    // Drag-select across lines, Ctrl+C / Ctrl+A / Ctrl+Insert to copy, Escape to
    // clear. READ-ONLY: there is no caret and no editing. Selection exists so a
    // reader can lift an error message out of the log, which is the one thing a log
    // view is useless without.
    //
    // Why this is not TextEditBase: that class owns a single std::wstring plus an
    // undo stack and inserts at a caret. A log's text is per-line spans that are
    // never mutated, and the buffer is a ring that renumbers lines from under any
    // stored offset. Selection here is therefore a (line, column) pair, not an
    // offset into one buffer, and it is dropped whenever a trim renumbers lines
    // rather than being fixed up — a selection silently pointing at different text
    // than the user highlighted is worse than no selection.
    //
    // Enabled by default; turn it off for a view where drag should do something
    // else, or to save the hit-test layouts on a huge always-scrolling display.
    void SetSelectionEnabled(bool enable);
    bool SelectionEnabled() const { return selectionEnabled_; }

    bool HasSelection() const;
    // Selection endpoints as set by the user: the anchor (fixed end) and the movable
    // end that Shift+keys / Shift+click extend. Unordered on purpose, so a test can
    // tell a backwards extension from a forwards one.
    LogTextPos SelectionAnchor() const { return selAnchor_; }
    LogTextPos SelectionActiveEnd() const { return selCurrent_; }
    // Place a collapsed selection point (anchor = active end) without a pointer — what
    // a plain click does. Clamped into the document.
    void SetSelectionPoint(LogTextPos p) { selAnchor_ = selCurrent_ = ClampPos(p); Invalidate(); }
    // The selected text: lines joined with '\n', partial lines cut at the column.
    // Empty when nothing is selected.
    std::wstring SelectedText() const;
    void SelectAll();
    void ClearSelection();
    // Put SelectedText() on the clipboard. Returns false when nothing is selected
    // or the clipboard could not be opened (another process holds it).
    bool CopySelectionToClipboard() const;

    // --- Element overrides ------------------------------------------------
    void Render(const DrawingContext& dc) override;
    void Measure(float availW, float availH) override;
    void Arrange(const RectDip& finalRect) override;
    // --- View zoom (Ctrl+Wheel / Ctrl+0) -------------------------------------
    // Multiplies every span's fontSize by this factor when building DirectWrite formats.
    // Same contract as TextArea::SetZoom: does not change the app's font settings,
    // only the glyph scale. Clamped to [kMinZoom, kMaxZoom].
    static constexpr float kMinZoom = 0.5f;
    static constexpr float kMaxZoom = 4.0f;
    void SetZoom(float factor);
    float Zoom() const { return zoom_; }
    void SetZoomEnabled(bool on) { zoomEnabled_ = on; }
    bool ZoomEnabled() const { return zoomEnabled_; }
    Event<LogViewer, float>& ZoomChanged() { return zoomChanged_; }

    void OnPointerWheelChanged(PointerEventArgs& e) override;
    void OnPointerPressed(PointerEventArgs& e) override;
    void OnPointerMoved(PointerEventArgs& e) override;
    void OnPointerReleased(PointerEventArgs& e) override;
    void OnPointerLeft() override;
    void OnKeyDownRouted(KeyEventArgs& e) override;
    // I-beam over the text, so the pointer says "this is selectable" the way it does
    // over a TextBox. The scrollbar rail keeps the arrow: an I-beam over a draggable
    // thumb would be a lie about what the drag will do.
    HCURSOR Cursor() const override;
    bool WantsAnimationTick() const override;
    void OnAnimationTick(float dtSec) override;

protected:
    void OnAttachedToTree() override;
    void OnDetachedFromTree() override;
    // UI Automation peer (automation/DataPeers.h).
    std::unique_ptr<automation::AutomationPeer> CreateAutomationPeer() override;

private:
    uint64_t firstLineSerial_ = 0;  // see FirstLineSerial
    // Render a single line at the given Y coordinate (relative to content top).
    // On first render, measures and caches the line height.
    void RenderLine(const DrawingContext& dc, size_t lineIndex, float y);

    // Rebuild Y-position table starting from `fromIndex`. Called when a line's
    // measured height differs from its prediction, or when lines are trimmed.
    void RebuildYPositions(size_t fromIndex);

    // Compute the total content height (last line Y + last line height).
    float GetContentExtent() const;
    // Push the current vertical and horizontal extents into scroll_. Every site that
    // changes the line table, the width mark or the bounds calls this, so the two
    // axes can never disagree about whether the horizontal rail is present.
    void SyncScrollExtent();

    // Find the range of visible lines via binary search on lineY_.
    struct VisibleRange { int first; int last; };
    VisibleRange CalculateVisibleRange(float scrollTop, float viewportHeight) const;
    // Move the layout window onto `range` and build the span layouts of every line
    // in it that has none yet. Render calls this before drawing, so the draw loop
    // itself only reads the cache.
    void PrepareVisibleLayouts(const VisibleRange& range) const;
    // Measure-before-draw for the visible range. Every still-estimated line in view
    // is measured from its cached layouts, the Y table rebuilt from the first line
    // whose estimate was off, and the range recomputed (a correction moves the lines
    // below it, which can bring new estimated lines into view) until it is stable.
    // Updates `scrollTop` when a tail-following view is re-pinned to the bottom.
    //
    // Why this exists: Render used to draw a line at its ESTIMATED position and only
    // then learn its height, fixing the table for the next frame. That frame drew
    // every line below a wrong estimate at the wrong place. After a zoom (which
    // resets every row to its estimate) a row whose real height differs a lot from
    // the one-line estimate — text ending in "\r\n" lays out as two lines — put the
    // next line's glyphs inside its own blank lower half for one frame: a visible
    // flash between rows 1 and 2 on every Ctrl+Wheel notch.
    VisibleRange LayoutVisibleLines(float& scrollTop, float dpiScale);
    // Tallest span layout of one line (DIPs), 0 without DWrite. Reads the layout cache.
    float MeasureLineHeight(size_t lineIndex) const;

    // Trim the oldest lines to enforce maxLines_ cap. O(lines removed): pops the
    // front, moves yOrigin_, and shifts the layout window's first index with the
    // renumbering.
    void TrimToMaxLines();

    // Predict row height before measurement: max(span fontSize) × lineSpacing.
    float PredictLineHeight(const LogLine& line) const;

    // A deque, not a vector: the ring-buffer trim pops from the front, which is
    // O(1) here and was an O(maxLines) shift of every surviving line per append on
    // a full buffer. Indexing and the binary searches over lineY_ work the same.
    std::deque<LogLine> lines_;
    // Top edge of each line in an ABSOLUTE coordinate that never moves; a line's
    // content-space Y is lineY_[i] - yOrigin_. A trim pops the front and sets
    // yOrigin_ to the new first line's top, so no surviving entry is rewritten.
    // Double because the absolute value only grows: a float loses whole-DIP
    // precision past ~16.7M DIP, which a 10k-lines/s log reaches in minutes.
    std::deque<double> lineY_;
    double yOrigin_ = 0.0;
    float LineTop(size_t i) const { return static_cast<float>(lineY_[i] - yOrigin_); }
    // Index of the last line whose top is at or above content-space `y` (0 if none).
    size_t LineAtContentY(float y) const;
    ScrollViewer scroll_;

    int maxLines_ = 0;                   // 0 = unlimited
    bool autoScrollToTail_ = true;
    float lastContentExtent_ = 0.0f;     // for detecting scroll-to-tail condition

    static constexpr float kLineSpacing = 1.2f;  // multiplier for predicted height
    static constexpr float kPadding = 8.0f;      // left/right content padding
    // Height of the horizontal rail's hover strip, the same 16 DIP ScrollPanel reserves.
    // Added to the vertical extent only while content is wider than the viewport.
    static constexpr float kHRailReserveDip = 16.0f;

    // --- Selection ---------------------------------------------------------
    // The two ends are NOT normalized: the anchor is where the drag started and
    // stays put, `current` follows the pointer, so `current < anchor` is a backwards
    // drag. Normalization happens where a range is actually needed
    // (SelectedText, the highlight paint) via NormalizedSelection().
    //
    // Equal ends mean no selection, which is why a plain click clears it: pressing
    // sets both to the same place.
    bool selectionEnabled_ = true;
    bool draggingSelection_ = false;
    LogTextPos selAnchor_;
    LogTextPos selCurrent_;
    
    // Multi-click selection granularity (character/word/line) and the anchor range.
    enum class SelectGranularity { Character, Word, Line };
    SelectGranularity dragGranularity_ = SelectGranularity::Character;
    LogTextPos dragStartLo_;
    LogTextPos dragStartHi_;

    // Whether the pointer is currently over the scrollbar's hover strip. Recorded by
    // OnPointerMoved because Cursor() is passed no coordinates and must not guess.
    bool pointerOverBar_ = false;

    // Grow-only horizontal extent mark, same logic as TextArea::maxSeenLineWidth_. Updated
    // whenever a line is measured (MeasureLineHeight, which LayoutVisibleLines and the
    // attach pass call), so the cost stays O(lines measured) and never walks the document.
    // Reset on Clear and SetZoom (every width changes with the zoom). Deliberately NOT
    // reset by TrimToMaxLines: with a full buffer every append trims, and resetting there
    // would make the rail flicker and snap OffsetX back to 0 on each batch. The cost is
    // that a trimmed-away widest line leaves some slack at the right edge.
    // Mutable because MeasureLineHeight is const (it only reads the layout cache).
    mutable float maxLineWidth_ = 0.0f;
    // Horizontal offset as a fraction of the range, saved by SetZoom and applied by the
    // next LayoutVisibleLines that has measured a line; -1 when nothing is pending.
    float pendingZoomFracX_ = -1.0f;

    // Drag-select auto-scroll: true when the pointer is being dragged outside the
    // control bounds during selection. WantsAnimationTick returns true while this
    // flag is set, and OnAnimationTick continues scrolling every frame until the
    // pointer moves back inside or the drag ends (so the selection can extend beyond
    // one screenful even if the pointer sits still outside the edge).
    bool dragOutsideEdge_ = false;
    float dragPointerY_ = 0.0f;  // cached Y to re-resolve against each tick

    // View zoom (see SetZoom). Every DirectWrite format and every height estimate is
    // built at span.fontSize * zoom_; a change drops the layout cache and re-estimates
    // all row heights, because both are functions of the zoomed size.
    float zoom_ = 1.0f;
    bool zoomEnabled_ = true;
    Event<LogViewer, float> zoomChanged_;

    // Ordered copy of the selection, or false when the ends are equal.
    bool NormalizedSelection(LogTextPos& from, LogTextPos& to) const;
    
    // Multi-click selection: double-click selects word, triple-click selects line.
    // Returns true if a multi-click selection was applied (clickCount >= 2).
    bool ApplyMultiClickSelection(const LogTextPos& pos, int clickCount);
    
    // Extend the selection during drag, respecting the current granularity.
    void ExtendDragSelection(const LogTextPos& pos);

    // --- Keyboard selection (no caret) -----------------------------------------
    // Move the selection's movable end (selCurrent_) for a Shift+navigation key;
    // the anchor stays put. Returns false for keys it does not handle.
    bool MoveSelectionEnd(UINT vk, bool ctrl);
    // Scroll the view for an unshifted navigation key, leaving the selection alone.
    bool ScrollByKey(UINT vk, bool ctrl);
    // Scroll just enough to bring `line` fully into view.
    void EnsureLineVisible(size_t line);
    // Clamp a position into the document (line and column).
    LogTextPos ClampPos(LogTextPos p) const;

    // Hit-test: the (line, column) under a point in WINDOW DIPs. Clamps into the
    // document, so a press below the last line lands at its end rather than failing.
    // Costs one DWrite layout per span on the hit line — O(spans of one line), not
    // O(document), and only on pointer events.
    LogTextPos HitTestPosition(float dipX, float dipY) const;

    // X offset (window DIPs) of `column` within a line, and the line's total width.
    // Used both by the hit test and by the highlight rect.
    float ColumnToX(size_t lineIndex, size_t column) const;

    // Paint the selection highlight for one line. Called before the glyphs so the
    // text sits on top of the fill.
    void RenderSelectionForLine(const DrawingContext& dc, const LogLine& line,
                                size_t lineIndex, float y,
                                const LogTextPos& from, const LogTextPos& to) const;

    // Build a DWrite layout for one span with the log's fixed layout attributes
    // (leading/near/no-wrap). Returns null when DWrite is unavailable. Shared by
    // the draw path and the hit test so both agree on metrics — if they disagreed,
    // a click would select text other than the one under the pointer.
    ComPtr<IDWriteTextLayout> MakeSpanLayout(const TextSpan& span) const;

    // --- Span layout cache (visible lines only) ----------------------------
    // One row per line in [layoutFirst_, layoutFirst_ + layoutRows_.size()), each
    // holding that line's span layouts (null for an empty span) once built. The
    // window follows the drawn range: SyncLayoutWindow drops rows that scrolled
    // out and opens empty rows for lines that scrolled in, so memory stays at one
    // screenful of layouts however long the log is. Layouts carry no colour and no
    // DPI (both are applied at draw time), so nothing but the line's own spans
    // invalidates one — and spans never change after AppendLine.
    struct LayoutRow {
        bool built = false;
        std::vector<ComPtr<IDWriteTextLayout>> spans;
    };
    mutable std::deque<LayoutRow> layoutRows_;
    mutable size_t layoutFirst_ = 0;
    mutable unsigned spanLayoutsBuilt_ = 0;
    // Move the window to [first, last] (inclusive). Rows for lines still inside it
    // are kept.
    void SyncLayoutWindow(size_t first, size_t last) const;
    // The layout for span `spanIndex` of line `lineIndex`: the cached one when the
    // line is in the window (building the row on first use), otherwise built into
    // `scratch`. Null for an empty span or without DWrite.
    IDWriteTextLayout* SpanLayout(size_t lineIndex, size_t spanIndex,
                                  ComPtr<IDWriteTextLayout>& scratch) const;
    // Drop every cached layout (Clear, detach).
    void ResetLayoutCache() const { layoutRows_.clear(); layoutFirst_ = 0; }
};

} // namespace fluent
