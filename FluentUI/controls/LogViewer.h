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
// NOT A TEXT EDITOR. No selection, no caret, no editing. For an editable
// multi-line control with full text-editing features, use TextArea. For rich
// formatting with editing, use RichTextBox (planned).
//
// DESIGN RATIONALE:
//   - Dynamic row height: log lines with large font sizes naturally expand the
//     row. An estimation phase (未测量行用默认高度) defers measurement until
//     first paint, similar to TextArea's WrapExtentMap.
//   - Per-span TextLayout: each TextSpan gets its own IDWriteTextLayout,
//     simplifying style isolation. A more memory-efficient approach (single
//     layout + SetFontSize ranges) is possible but deferred until proven needed.
//   - Append-optimized: AppendLine() is O(1) amortized; Y-positions are
//     incrementally extended, and layout cache survives appends (only the
//     changed last line is invalidated).
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
#include <vector>
#include <string>

namespace fluent {

// One styled text span within a log line.
struct TextSpan {
    std::wstring text;
    D2D1_COLOR_F color = D2D1::ColorF(1.0f, 1.0f, 1.0f);  // white default
    float fontSize = 12.0f;
    DWRITE_FONT_WEIGHT weight = DWRITE_FONT_WEIGHT_NORMAL;
    bool underline = false;

    TextSpan() = default;
    TextSpan(std::wstring t, D2D1_COLOR_F c = D2D1::ColorF(1.0f, 1.0f, 1.0f),
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
    // are trimmed. 0 (default) = unlimited. Trimming invalidates the layout
    // cache (lines are renumbered) and recomputes Y-positions from scratch.
    void SetMaxLines(int maxLines);
    int MaxLines() const { return maxLines_; }

    size_t LineCount() const { return lines_.size(); }

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

private:
    // Render a single line at the given Y coordinate (relative to content top).
    // On first render, measures and caches the line height.
    void RenderLine(const DrawingContext& dc, LogLine& line, float y);

    // Rebuild Y-position table starting from `fromIndex`. Called when a line's
    // measured height differs from its prediction, or when lines are trimmed.
    void RebuildYPositions(size_t fromIndex);

    // Compute the total content height (last line Y + last line height).
    float GetContentExtent() const;

    // Find the range of visible lines via binary search on lineYPositions_.
    struct VisibleRange { int first; int last; };
    VisibleRange CalculateVisibleRange(float scrollTop, float viewportHeight) const;

    // Trim the oldest lines to enforce maxLines_ cap. Clears layout cache and
    // rebuilds Y-positions (lines are renumbered).
    void TrimToMaxLines();

    // Predict row height before measurement: max(span fontSize) × lineSpacing.
    float PredictLineHeight(const LogLine& line) const;

    std::vector<LogLine> lines_;
    std::vector<float> lineYPositions_;  // Y coordinate of each line's top edge
    ScrollViewer scroll_;

    int maxLines_ = 0;                   // 0 = unlimited
    bool autoScrollToTail_ = true;
    float lastContentExtent_ = 0.0f;     // for detecting scroll-to-tail condition

    static constexpr float kLineSpacing = 1.2f;  // multiplier for predicted height
    static constexpr float kPadding = 8.0f;      // left/right content padding

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
    float ColumnToX(const LogLine& line, size_t column) const;

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
};

} // namespace fluent
