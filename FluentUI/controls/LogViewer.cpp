// LogViewer.cpp — virtualized log rendering with per-span styling.
//
// The rendering loop is the whole design: binary-search the Y-position table for
// the visible band, draw only those lines, and correct each line's estimated
// height with the real measurement the first time it is painted. Everything else
// (append, trim, scroll) exists to keep that table consistent.

#include "LogViewer.h"
#include "../styling/ThemeTokens.h"
#include "../input/InputManager.h"
#include "../graphics/DWriteContext.h"
#include "../graphics/DrawingContext.h"
#include "../graphics/PixelSnapping.h"
#include "../text/WordBoundary.h"
#include <algorithm>
#include <cmath>

namespace fluent {

namespace {
// A layout box wide enough never to wrap a log line. NoWrap is the mode a log
// viewer wants — a wrapped log line destroys column alignment.
constexpr float kUnboundedWidth = 1.0e6f;
constexpr float kUnboundedHeight = 1.0e4f;
// Wheel step per line: estimated average row height (12pt font × 1.2 line spacing).
// User's system "lines per notch" setting multiplies this, so one notch scrolls
// roughly (setting × kWheelStepDip) / actual_row_height rows — around 3-5 rows
// at the default 3-lines setting with typical 12-14pt logs.
constexpr float kWheelStepDip = 16.0f;
// Right gutter so text clears the scrollbar rail.
constexpr float kScrollReserve = 12.0f;
// Max auto-scroll speed when dragging selection past the edge. Capped so an
// accidental overshoot does not rocket through the entire log.
constexpr float kAutoScrollMaxStepDip = 20.0f;
} // namespace

std::wstring LogLine::PlainText() const {
    size_t total = 0;
    for (const auto& s : spans) total += s.text.size();
    std::wstring out;
    out.reserve(total);
    for (const auto& s : spans) out += s.text;
    return out;
}

LogViewer::LogViewer() {
    // Focusable so Ctrl+C / Ctrl+A / Escape reach OnKeyDownRouted. There is still no
    // caret and no editing — focus here means "keyboard commands apply to this log",
    // the same sense in which a read-only list is focusable.
    SetFocusable(true);
    scroll_.SetKeepVisibleWhenOverflow(true);
    // White rail, not the theme's textPrimary.
    //
    // ScrollViewer's default ink assumes the scrolled surface follows the theme. A log
    // view does not: it is dark in a light theme, because that is what a log is
    // supposed to look like. Under the default the rail was near-black at 35% alpha on
    // near-black — drawn every frame, provably present in the geometry, and completely
    // invisible on screen. This is the fix for that, and it is why the ink is a
    // property rather than derived from the theme alone.
    scroll_.SetScrollbarColor(D2D1::ColorF(D2D1::ColorF::White));
    // A log's scrollbar is navigational rather than incidental — it is how a reader
    // judges position in a large file — so it lingers longer than the 1.2 s default.
    scroll_.SetIdleHideDelay(3.0f);
}

// ---------------------------------------------------------------------------
// Content
// ---------------------------------------------------------------------------

void LogViewer::AppendLine(std::vector<TextSpan> spans) {
    if (spans.empty()) return;

    LogLine line;
    line.spans = std::move(spans);
    // Estimated height until the line is first painted. The estimate is what lets
    // AppendLine stay O(1): a real measurement would need a DWrite layout per span
    // per append, which at log rates is the stall this path exists to avoid.
    line.height = PredictLineHeight(line);
    line.heightMeasured = false;

    const float y = lines_.empty()
        ? 0.0f
        : (lineYPositions_.back() + lines_.back().height);
    lineYPositions_.push_back(y);
    lines_.push_back(std::move(line));

    // The tail-follow flag is READ here, never recomputed — same rule as
    // TextArea::AppendText. An append grows the extent, so asking "is the view at
    // the bottom" after growing it always answers no, and following would switch
    // itself off on the first batch.
    scroll_.SetContentHeight(GetContentExtent());
    if (autoScrollToTail_)
        scroll_.SetOffset(scroll_.MaxOffset());

    if (maxLines_ > 0 && static_cast<int>(lines_.size()) > maxLines_)
        TrimToMaxLines();

    Invalidate();
}

void LogViewer::Clear() {
    lines_.clear();
    lineYPositions_.clear();
    // The selection pointed into text that no longer exists.
    selAnchor_ = selCurrent_ = LogTextPos{};
    scroll_.SetContentHeight(0.0f);
    scroll_.SetOffset(0.0f);
    Invalidate();
}

void LogViewer::SetMaxLines(int maxLines) {
    maxLines_ = std::max(0, maxLines);
    if (maxLines_ > 0 && static_cast<int>(lines_.size()) > maxLines_)
        TrimToMaxLines();
}

void LogViewer::TrimToMaxLines() {
    if (maxLines_ <= 0 || static_cast<int>(lines_.size()) <= maxLines_)
        return;

    const size_t toRemove = lines_.size() - static_cast<size_t>(maxLines_);
    // The height being dropped, measured BEFORE erasing: it is the exact amount the
    // scroll offset must shrink by for the visible text to hold still. Recomputing
    // it afterwards is impossible — the lines are gone.
    const float droppedHeight = lineYPositions_[toRemove];

    lines_.erase(lines_.begin(), lines_.begin() + static_cast<ptrdiff_t>(toRemove));
    lineYPositions_.erase(lineYPositions_.begin(),
                          lineYPositions_.begin() + static_cast<ptrdiff_t>(toRemove));

    // Every surviving line moved up by droppedHeight; rebase rather than re-walking
    // from zero so measured heights are preserved.
    for (auto& y : lineYPositions_)
        y -= droppedHeight;

    // A trim RENUMBERS lines: old line N+toRemove is now line N. A stored
    // (line, column) therefore designates different text than the user highlighted,
    // so the selection is dropped rather than shifted. Shifting is tempting and
    // wrong in the case that matters: when the selection's own lines were the ones
    // discarded there is nothing to shift it to, and a partially-surviving selection
    // that silently copies different text is worse than none.
    selAnchor_ = selCurrent_ = LogTextPos{};

    scroll_.SetContentHeight(GetContentExtent());
    // Compensate the offset by exactly what was dropped, so a reader parked
    // mid-history sees the text stay put instead of jumping.
    scroll_.SetOffset(std::max(0.0f, std::min(scroll_.Offset() - droppedHeight,
                                              scroll_.MaxOffset())));
    Invalidate();
}

float LogViewer::PredictLineHeight(const LogLine& line) const {
    float maxFontSize = 0.0f;
    for (const auto& span : line.spans)
        maxFontSize = std::max(maxFontSize, span.fontSize);
    if (maxFontSize <= 0.0f) maxFontSize = 12.0f;
    return maxFontSize * kLineSpacing;
}

float LogViewer::GetContentExtent() const {
    if (lines_.empty()) return 0.0f;
    return lineYPositions_.back() + lines_.back().height;
}

// ---------------------------------------------------------------------------
// Virtualization
// ---------------------------------------------------------------------------

LogViewer::VisibleRange LogViewer::CalculateVisibleRange(float scrollTop,
                                                        float viewportHeight) const {
    if (lines_.empty() || viewportHeight <= 0.0f) return {0, -1};

    // lineYPositions_ is sorted ascending (each entry is the previous plus a
    // positive height), so both ends are a binary search. upper_bound - 1 gives the
    // last line whose TOP is at or above scrollTop, which is the first line whose
    // body can intersect the viewport.
    const auto begin = lineYPositions_.begin();
    const auto end = lineYPositions_.end();

    auto itFirst = std::upper_bound(begin, end, scrollTop);
    int first = static_cast<int>(std::distance(begin, itFirst)) - 1;
    first = std::max(0, first);

    auto itLast = std::lower_bound(begin, end, scrollTop + viewportHeight);
    int last = static_cast<int>(std::distance(begin, itLast)) - 1;
    last = std::min(last, static_cast<int>(lines_.size()) - 1);
    // A viewport shorter than one line still shows that line.
    last = std::max(last, first);

    if (first > last) return {0, -1};
    return {first, last};
}

void LogViewer::RebuildYPositions(size_t fromIndex) {
    if (fromIndex >= lines_.size()) return;

    float y = (fromIndex == 0)
        ? 0.0f
        : (lineYPositions_[fromIndex - 1] + lines_[fromIndex - 1].height);
    for (size_t i = fromIndex; i < lines_.size(); ++i) {
        lineYPositions_[i] = y;
        y += lines_[i].height;
    }
    scroll_.SetContentHeight(GetContentExtent());
}

// ---------------------------------------------------------------------------
// Layout
// ---------------------------------------------------------------------------

void LogViewer::Measure(float availW, float availH) {
    // A log view fills whatever it is given: its natural size is the document's,
    // which is unbounded and not a useful thing to report. Explicit Width/Height
    // still win, which is what the base implementation resolves.
    Control::Measure(availW, availH);
}

void LogViewer::Arrange(const RectDip& finalRect) {
    Control::Arrange(finalRect);
    // The scrollbar model is an embedded member, not a tree node, so it is handed
    // the bounds directly rather than being arranged as a child.
    scroll_.SetBounds(bounds_);
    scroll_.SetContentHeight(GetContentExtent());
}

// ---------------------------------------------------------------------------
// Rendering
// ---------------------------------------------------------------------------

// One span's layout, with the attributes every log span shares.
//
// Shared by the draw path and the hit test on purpose: if the two built layouts
// differently (a different alignment, a different wrap mode) a click would resolve
// to a different character than the one drawn under the pointer, and the bug would
// only show at the ends of long spans where the discrepancy accumulates.
ComPtr<IDWriteTextLayout> LogViewer::MakeSpanLayout(const TextSpan& span) const {
    DWriteContext* dw = Dwrite();
    if (!dw || !dw->Factory() || span.text.empty()) return nullptr;

    // Left-aligned, top-anchored, no wrapping: a log line is a single visual line
    // and the spans must butt up against each other. The cached-format defaults
    // (centered) would place every span in the middle of its own box.
    IDWriteTextFormat* fmt = dw->Format(span.fontSize, span.weight,
                                        DWRITE_TEXT_ALIGNMENT_LEADING,
                                        DWRITE_PARAGRAPH_ALIGNMENT_NEAR,
                                        DWRITE_WORD_WRAPPING_NO_WRAP);
    if (!fmt) return nullptr;

    ComPtr<IDWriteTextLayout> layout;
    HRESULT hr = dw->Factory()->CreateTextLayout(
        span.text.c_str(), static_cast<UINT32>(span.text.size()),
        fmt, kUnboundedWidth, kUnboundedHeight, &layout);
    if (FAILED(hr) || !layout) return nullptr;

    if (span.underline)
        layout->SetUnderline(TRUE, {0, static_cast<UINT32>(span.text.size())});
    return layout;
}

void LogViewer::RenderLine(const DrawingContext& dc, LogLine& line, float y) {
    DWriteContext* dw = Dwrite();
    if (!dw || !dw->Factory()) return;

    float x = bounds_.x + kPadding;
    float maxHeight = 0.0f;

    for (const auto& span : line.spans) {
        if (span.text.empty()) continue;

        ComPtr<IDWriteTextLayout> layout = MakeSpanLayout(span);
        if (!layout) continue;

        dc.DrawTextLayout(D2D1::Point2F(x, y), layout.Get(), span.color);

        DWRITE_TEXT_METRICS tm{};
        if (SUCCEEDED(layout->GetMetrics(&tm))) {
            // widthIncludingTrailingWhitespace, not width: a span ending in spaces
            // ("[ERROR] ") must advance the pen past them, or the next span
            // overlaps the last glyph.
            x += tm.widthIncludingTrailingWhitespace;
            maxHeight = std::max(maxHeight, tm.height);
        }
    }

    // First paint is where the estimate becomes a measurement. The correction is
    // recorded on the line; the caller decides whether the table needs rebuilding,
    // because only it knows this line's index.
    if (!line.heightMeasured && maxHeight > 0.0f) {
        line.height = maxHeight;
        line.heightMeasured = true;
    }
}

void LogViewer::Render(const DrawingContext& dc) {
    if (!IsVisible()) return;

    const ColorTokens& pal = Theme().colors;
    const D2D1_RECT_F box = D2D1::RectF(bounds_.x, bounds_.y,
                                       bounds_.right(), bounds_.bottom());
    // Use EffectiveBackground so SetBackground() works (Control's override system).
    // Default fallback is controlFillDefault, but LogViewer semantically wants a
    // darker terminal-like fill, so pass black as the fallback token.
    dc.FillRect(box, EffectiveBackground(D2D1::ColorF(D2D1::ColorF::Black)));

    if (lines_.empty()) {
        scroll_.Render(dc);
        return;
    }

    // Clip to the control: a partially visible line at either edge must not paint
    // outside the bounds, and CollectDirtyBounds does not report anything wider.
    auto clip = dc.PushClip(box);

    // Snap scroll offset to physical pixel boundary to eliminate subpixel rendering
    // jitter. Without snapping, fractional scroll offsets (e.g., 15.7 DIPs) cause text
    // to render at non-integer pixel coordinates, producing visible blur/shimmer during
    // smooth scrolling. The conversion DIP → px → round → DIP ensures text always lands
    // on integer pixel boundaries where DirectWrite's ClearType produces consistent output.
    const float dpiScale = dc.DpiScale();
    const float scrollTop = SnapToPixel(scroll_.Offset(), dpiScale);
    const float viewportH = bounds_.h;
    const VisibleRange range = CalculateVisibleRange(scrollTop, viewportH);

    // Which lines were still estimates when this frame started. Drawing is
    // measuring here (same shape as TextArea's Wrap path), so the corrections have
    // to be applied after the loop — mutating lineYPositions_ mid-walk would move
    // lines the loop has not reached yet.
    size_t firstCorrected = lines_.size();

    // The selection range, resolved once per frame rather than per line.
    LogTextPos selFrom, selTo;
    const bool hasSel = NormalizedSelection(selFrom, selTo);

    for (int i = range.first; i <= range.last; ++i) {
        const size_t idx = static_cast<size_t>(i);
        const bool wasEstimate = !lines_[idx].heightMeasured;
        const float before = lines_[idx].height;

        const float y = bounds_.y + lineYPositions_[idx] - scrollTop;

        // Highlight first, glyphs second: the fill is opaque-ish and would cover the
        // text if drawn after it.
        if (hasSel && idx >= selFrom.line && idx <= selTo.line)
            RenderSelectionForLine(dc, lines_[idx], idx, y, selFrom, selTo);

        RenderLine(dc, lines_[idx], y);

        if (wasEstimate && lines_[idx].heightMeasured &&
            std::fabs(lines_[idx].height - before) > 0.5f) {
            firstCorrected = std::min(firstCorrected, idx);
        }
    }

    if (firstCorrected < lines_.size()) {
        // An estimate was wrong, so every line below it sits at the wrong offset.
        // Rebuilding from the first correction is O(lines) of integer arithmetic —
        // no layout work — and the next frame draws at the corrected positions.
        RebuildYPositions(firstCorrected);
        Invalidate();
    }

    scroll_.Render(dc);
}

// ---------------------------------------------------------------------------
// Input
// ---------------------------------------------------------------------------

void LogViewer::OnPointerWheelChanged(PointerEventArgs& e) {
    if (scroll_.MaxOffset() <= 0.0f) return;

    scroll_.Wake();
    // WheelScrollLines() 已归一化为系统设置的行数，直接乘步进距离即可
    scroll_.AnimateBy(e.WheelScrollLines() * kWheelStepDip);

    // Follow-mode is recomputed only on user-driven scrolls, and against the
    // TARGET rather than the current offset: on the frame the notch arrives the
    // tween has not moved yet, so Offset() would still report "at the bottom".
    autoScrollToTail_ = scroll_.TargetOffset() >= scroll_.MaxOffset() - 1.0f;

    e.handled = true;
    Invalidate();
}

void LogViewer::OnPointerPressed(PointerEventArgs& e) {
    const float dipX = e.position.x, dipY = e.position.y;

    // Middle-button: toggle autoscroll if there's anywhere to scroll.
    if (e.button == PointerButton::Middle) {
        if (scroll_.IsAutoscrolling()) {
            scroll_.EndAutoscroll();
        } else if (scroll_.MaxOffset() > 0.0f) {
            scroll_.BeginAutoscroll(dipX, dipY);
        }
        e.handled = true;
        return;
    }

    // Left-button press while autoscrolling: cancel autoscroll first.
    if (e.button == PointerButton::Left && scroll_.IsAutoscrolling()) {
        scroll_.EndAutoscroll();
        // Fall through to normal left-press handling.
    }

    if (e.button != PointerButton::Left) return;

    // Press anywhere in the hover strip, not just on the visible thumb: the rail is
    // a few DIP wide and a control that lights up on hover but ignores clicks
    // beside the thumb reads as broken.
    if (scroll_.HitBarRegion(dipX, dipY)) {
        if (!scroll_.HitThumb(dipX, dipY)) {
            // Off-thumb: centre the thumb on the pointer first, then drag from
            // there. Without the jump the thumb stays put and leaps on the first
            // move, which feels like lag.
            const RectDip thumb = scroll_.ThumbRect();
            const float trackRange = std::max(1.0f, bounds_.h - thumb.h);
            const float wantTop = (dipY - thumb.h * 0.5f) - bounds_.y;
            scroll_.SetOffset(wantTop / trackRange * scroll_.MaxOffset());
        }
        scroll_.BeginDrag(dipY);
        if (Context().input)
            Context().input->CapturePointer(this);
        e.handled = true;
        Invalidate();
        return;
    }

    // Text drag starts here. Both ends are set to the same position, so the existing
    // selection vanishes — that is the 'click clears selection' behavior.
    if (selectionEnabled_) {
        const LogTextPos pos = HitTestPosition(dipX, dipY);
        const bool shift = (e.modifiers & ModifierKeys::Shift) == ModifierKeys::Shift;
        
        // Shift+click: extend selection from anchor to clicked position (keep anchor).
        // A plain click sets both anchor and current to the same point, so a bare
        // "caret" is the anchor — Shift+click then selects from there.
        if (shift && e.clickCount <= 1) {
            dragGranularity_ = SelectGranularity::Character;
            selCurrent_ = pos;  // keep selAnchor_
        }
        // Multi-click selection: double-click selects word, triple-click selects line.
        else if (!ApplyMultiClickSelection(pos, e.clickCount)) {
            // Single click: reset to character granularity and clear selection.
            dragGranularity_ = SelectGranularity::Character;
            selAnchor_ = selCurrent_ = pos;
        }
        
        draggingSelection_ = true;
        if (Context().input)
            Context().input->CapturePointer(this);
        e.handled = true;
        Invalidate();
    }
}

void LogViewer::OnPointerMoved(PointerEventArgs& e) {
    const float dipX = e.position.x, dipY = e.position.y;

    if (scroll_.IsAutoscrolling()) {
        scroll_.UpdateAutoscrollVelocity(dipX, dipY);
        e.handled = true;
        return;
    }

    if (scroll_.IsDragging()) {
        scroll_.DragTo(dipY);
        autoScrollToTail_ = scroll_.TargetOffset() >= scroll_.MaxOffset() - 1.0f;
        e.handled = true;
        Invalidate();
        return;
    }

    if (draggingSelection_) {
        const LogTextPos pos = HitTestPosition(dipX, dipY);
        ExtendDragSelection(pos);

        // Auto-scroll while the pointer is dragged past either edge. Without this a
        // selection cannot be extended beyond one screenful: the pointer is captured,
        // so it keeps delivering moves, but the position it resolves to is clamped at
        // the boundary line and the view never follows.
        //
        // The speed is proportional to how far outside the pointer is, capped, so a
        // small overshoot creeps and a large one runs — the behaviour every editor
        // has, and the reason it is not a fixed step is that a fixed step feels stuck
        // when you want to cross thousands of lines.
        const float above = bounds_.y - dipY;
        const float below = dipY - bounds_.bottom();
        if (above > 0.0f || below > 0.0f) {
            const float over = std::max(above, below);
            const float step = std::min(over, kAutoScrollMaxStepDip);
            scroll_.SetOffset(scroll_.Offset() + (above > 0.0f ? -step : step));
            // Re-resolve against the scrolled view so the selection end lands on the
            // line that just came into view rather than lagging a frame behind.
            selCurrent_ = HitTestPosition(dipX, dipY);
            autoScrollToTail_ = scroll_.Offset() >= scroll_.MaxOffset() - 1.0f;
        }

        e.handled = true;
        Invalidate();
        return;
    }

    scroll_.Wake();
    pointerOverBar_ = scroll_.HitBarRegion(dipX, dipY);
    scroll_.SetBarHover(pointerOverBar_);
}

void LogViewer::OnPointerReleased(PointerEventArgs& e) {
    if (e.button != PointerButton::Left) return;

    if (scroll_.IsDragging()) {
        scroll_.EndDrag();
        autoScrollToTail_ = scroll_.TargetOffset() >= scroll_.MaxOffset() - 1.0f;
    }
    if (draggingSelection_) {
        draggingSelection_ = false;
        // Reset the drag granularity so the NEXT press starts fresh. Without this a
        // double-click-then-release leaves Word granularity armed, and a later plain
        // drag elsewhere would keep snapping to word boundaries and re-anchoring to
        // the old double-clicked range.
        dragGranularity_ = SelectGranularity::Character;
    }
    if (Context().input && Context().input->Captured() == this)
        Context().input->ReleaseCapture(this);
    e.handled = true;
}

void LogViewer::OnPointerLeft() {
    pointerOverBar_ = false;
    scroll_.SetBarHover(false);
    Invalidate();
}

HCURSOR LogViewer::Cursor() const {
    // Autoscroll: direction-indicating arrow.
    if (scroll_.IsAutoscrolling()) {
        POINT pt;
        if (GetCursorPos(&pt) && Context().hwnd) {
            ScreenToClient(Context().hwnd, &pt);
            const float dpiScale = Context().dpiScale;
            const float dipX = pt.x / dpiScale;
            const float dipY = pt.y / dpiScale;
            return scroll_.AutoscrollCursor(dipX, dipY);
        }
        static HCURSOR sizeAll = LoadCursor(nullptr, IDC_SIZEALL);
        return sizeAll;
    }
    // I-beam over the text, arrow over the scrollbar rail — the pointer is the only
    // affordance saying "this text is selectable", which is why TextEditBase does the
    // same thing.
    //
    // Cursor() takes no coordinates, so the decision has to be made from state that
    // OnPointerMoved already recorded. pointerOverBar_ is set there from the same
    // HitBarRegion call that drives the rail's hover highlight, so the cursor and the
    // highlight can never disagree about where the pointer is.
    if (!selectionEnabled_ || pointerOverBar_)
        return nullptr;  // null = the host's default arrow

    static HCURSOR ibeam = LoadCursor(nullptr, IDC_IBEAM);
    return ibeam;
}

// ---------------------------------------------------------------------------
// Ticking and lifecycle
// ---------------------------------------------------------------------------

bool LogViewer::WantsAnimationTick() const {
    return scroll_.NeedsTick() || scroll_.IsAutoscrolling();
}

void LogViewer::OnAnimationTick(float dtSec) {
    scroll_.Tick(dtSec);
    if (scroll_.IsAutoscrolling()) {
        scroll_.TickAutoscroll(dtSec);
        autoScrollToTail_ = scroll_.TargetOffset() >= scroll_.MaxOffset() - 1.0f;
    }
    // scroll_ is a member, not a tree node: the Invalidate() inside its Tick has no
    // path to the frame scheduler, so its fade/expand steps would only appear on
    // frames some other element happened to dirty.
    Invalidate();
}

void LogViewer::OnAttachedToTree() {
    Control::OnAttachedToTree();
    // Same reason as ListBox / TreeView / TextArea: scroll_ is not a tree node, so
    // nothing else would ever give it a context, and UIElement::Theme() would fall
    // back to the LIGHT default snapshot — drawing the rail in near-black ink
    // regardless of the real theme.
    scroll_.AttachToContext(Context());

    // Fix first-frame layout jump: if lines were appended before attach, their heights
    // are estimated (fontSize * kLineSpacing). Now that we have DWrite context, measure
    // each unmeasured line's actual height (= max height of all its spans) to avoid
    // visible reflow when the first Render measures them.
    bool anyMeasured = false;
    for (size_t i = 0; i < lines_.size(); ++i) {
        if (!lines_[i].heightMeasured && !lines_[i].spans.empty()) {
            // Measure all spans in this line to find the tallest one
            float maxHeight = 0.0f;
            for (const auto& span : lines_[i].spans) {
                if (span.text.empty()) continue;
                ComPtr<IDWriteTextLayout> layout = MakeSpanLayout(span);
                if (layout) {
                    DWRITE_TEXT_METRICS tm{};
                    if (SUCCEEDED(layout->GetMetrics(&tm)) && tm.height > maxHeight) {
                        maxHeight = tm.height;
                    }
                }
            }
            if (maxHeight > 0.0f) {
                lines_[i].height = maxHeight;
                lines_[i].heightMeasured = true;
                anyMeasured = true;
            }
        }
    }
    // If any heights changed, rebuild Y-position table and update scroll extent
    if (anyMeasured) {
        RebuildYPositions(0);
    }
}

void LogViewer::OnDetachedFromTree() {
    Control::OnDetachedFromTree();
}

// ---------------------------------------------------------------------------
// Selection
// ---------------------------------------------------------------------------

void LogViewer::SetSelectionEnabled(bool enable) {
    if (selectionEnabled_ == enable) return;
    selectionEnabled_ = enable;
    if (!enable) ClearSelection();
}

bool LogViewer::NormalizedSelection(LogTextPos& from, LogTextPos& to) const {
    if (selAnchor_ == selCurrent_) return false;
    if (selCurrent_ < selAnchor_) {
        from = selCurrent_;
        to = selAnchor_;
    } else {
        from = selAnchor_;
        to = selCurrent_;
    }
    return true;
}

bool LogViewer::ApplyMultiClickSelection(const LogTextPos& pos, int clickCount) {
    if (clickCount < 2 || pos.line >= lines_.size()) return false;
    
    const std::wstring text = lines_[pos.line].PlainText();
    if (text.empty()) return false;
    
    LogTextPos lo = pos, hi = pos;
    
    if (clickCount == 2) {
        // Double-click: select word on the clicked line.
        dragGranularity_ = SelectGranularity::Word;
        const size_t clampedCol = std::min(pos.column, text.size());
        auto [start, end] = WordRangeAt(text, static_cast<UINT32>(clampedCol));
        lo.column = start;
        hi.column = end;
    } else {
        // Triple-click (or more): select entire line.
        dragGranularity_ = SelectGranularity::Line;
        lo.column = 0;
        hi.column = text.size();
    }
    
    dragStartLo_ = lo;
    dragStartHi_ = hi;
    selAnchor_ = lo;
    selCurrent_ = hi;
    return true;
}

void LogViewer::ExtendDragSelection(const LogTextPos& pos) {
    if (pos.line >= lines_.size()) return;

    // Character granularity is a PLAIN caret move: the anchor stays where the press put
    // it and the caret follows the pointer. It must NOT consult dragStartLo_/Hi_ — those
    // describe the last word/line gesture, and merging with them is what made a fresh
    // drag re-anchor to a previously double-clicked range.
    if (dragGranularity_ == SelectGranularity::Character) {
        selCurrent_ = pos;
        return;
    }

    // Snap the pointer end outward to a whole unit, then pin whichever edge of the
    // ORIGINAL unit is on the far side. That is what keeps the initially clicked
    // word/line fully selected no matter which way the drag goes.
    LogTextPos lo = pos, hi = pos;

    if (dragGranularity_ == SelectGranularity::Word) {
        const std::wstring text = lines_[pos.line].PlainText();
        if (!text.empty()) {
            const size_t clampedCol = std::min(pos.column, text.size());
            auto [start, end] = WordRangeAt(text, static_cast<UINT32>(clampedCol));
            lo.column = start;
            hi.column = end;
            // A click exactly on a newline yields an empty range; fall back to the raw
            // position so the drag tracks the pointer instead of freezing.
            if (lo.column == hi.column) { lo = hi = pos; }
        }
    } else {
        // Line granularity: snap to entire line
        const std::wstring text = lines_[pos.line].PlainText();
        lo.column = 0;
        hi.column = text.size();
    }

    if (dragStartHi_ < hi) {          // dragging forward
        selAnchor_ = dragStartLo_;
        selCurrent_ = hi;
    } else if (lo < dragStartLo_) {   // dragging backward
        selAnchor_ = dragStartHi_;
        selCurrent_ = lo;
    } else {                          // still inside the original unit
        selAnchor_ = dragStartLo_;
        selCurrent_ = dragStartHi_;
    }
}

bool LogViewer::HasSelection() const {
    LogTextPos a, b;
    return NormalizedSelection(a, b);
}

void LogViewer::ClearSelection() {
    if (selAnchor_ == selCurrent_) return;
    selAnchor_ = selCurrent_ = LogTextPos{};
    Invalidate();
}

void LogViewer::SelectAll() {
    if (lines_.empty()) return;
    selAnchor_ = LogTextPos{0, 0};
    selCurrent_ = LogTextPos{lines_.size() - 1, lines_.back().PlainText().size()};
    Invalidate();
}

std::wstring LogViewer::SelectedText() const {
    LogTextPos from, to;
    if (!NormalizedSelection(from, to)) return {};

    std::wstring out;
    for (size_t i = from.line; i <= to.line && i < lines_.size(); ++i) {
        const std::wstring text = lines_[i].PlainText();
        // Each end is clamped independently: an interior line contributes whole,
        // the first contributes from its column, the last up to its column, and a
        // single-line selection is both at once.
        const size_t begin = (i == from.line) ? std::min(from.column, text.size()) : 0;
        const size_t end = (i == to.line) ? std::min(to.column, text.size()) : text.size();
        if (begin < end)
            out.append(text, begin, end - begin);
        // '\n' between lines, never after the last one — a trailing newline pasted
        // into a shell would execute the copied command.
        if (i != to.line)
            out.push_back(L'\n');
    }
    return out;
}

bool LogViewer::CopySelectionToClipboard() const {
    const std::wstring sel = SelectedText();
    if (sel.empty()) return false;

    // The window from the context, not GetActiveWindow(): the same reasoning as
    // TextEditBase::ClipboardOwner — an inactive app or a dialog makes
    // GetActiveWindow return the wrong window (or null), and then the clipboard has
    // no owner to receive WM_DESTROYCLIPBOARD.
    HWND owner = Context().hwnd ? Context().hwnd : GetActiveWindow();

    const SIZE_T bytes = (sel.size() + 1) * sizeof(wchar_t);
    HGLOBAL mem = GlobalAlloc(GMEM_MOVEABLE, bytes);
    if (!mem) return false;
    void* dst = GlobalLock(mem);
    if (!dst) { GlobalFree(mem); return false; }
    memcpy(dst, sel.c_str(), bytes);
    GlobalUnlock(mem);

    if (!OpenClipboard(owner)) { GlobalFree(mem); return false; }
    EmptyClipboard();
    // Ownership of `mem` transfers to the clipboard on success only; on failure it
    // is still ours to free.
    if (!SetClipboardData(CF_UNICODETEXT, mem)) {
        CloseClipboard();
        GlobalFree(mem);
        return false;
    }
    CloseClipboard();
    return true;
}

// X of a column within a line, in window DIPs.
//
// Spans are laid out and drawn one after another, each with its own layout, so a
// column has to be resolved by walking spans and asking the span that contains it.
// This is why the walk uses widthIncludingTrailingWhitespace exactly as the draw
// loop does — any disagreement here shows up as a highlight offset from the glyphs.
float LogViewer::ColumnToX(const LogLine& line, size_t column) const {
    float x = bounds_.x + kPadding;
    size_t consumed = 0;

    for (const auto& span : line.spans) {
        if (span.text.empty()) continue;
        const size_t spanLen = span.text.size();

        if (column <= consumed) return x;

        ComPtr<IDWriteTextLayout> layout = MakeSpanLayout(span);
        if (!layout) { consumed += spanLen; continue; }

        if (column < consumed + spanLen) {
            // Inside this span: the caret X of a character position is the leading
            // edge of that character's cluster.
            const UINT32 local = static_cast<UINT32>(column - consumed);
            DWRITE_HIT_TEST_METRICS htm{};
            float px = 0.0f, py = 0.0f;
            if (SUCCEEDED(layout->HitTestTextPosition(local, FALSE, &px, &py, &htm)))
                return x + px;
            return x;
        }

        DWRITE_TEXT_METRICS tm{};
        if (SUCCEEDED(layout->GetMetrics(&tm)))
            x += tm.widthIncludingTrailingWhitespace;
        consumed += spanLen;
    }
    return x;  // past the end of the line
}

LogTextPos LogViewer::HitTestPosition(float dipX, float dipY) const {
    if (lines_.empty()) return LogTextPos{};

    // Vertical: reuse the same table the draw loop searches, then clamp. A press
    // above the first line lands at its start; below the last, at its end — which is
    // what makes a drag that overshoots the control select to the edge instead of
    // doing nothing.
    const float scrollTop = scroll_.Offset();
    const float contentY = dipY - bounds_.y + scrollTop;

    size_t lineIdx;
    if (contentY <= 0.0f) {
        lineIdx = 0;
    } else {
        auto it = std::upper_bound(lineYPositions_.begin(), lineYPositions_.end(), contentY);
        const size_t idx = static_cast<size_t>(std::distance(lineYPositions_.begin(), it));
        lineIdx = (idx == 0) ? 0 : std::min(idx - 1, lines_.size() - 1);
    }

    const LogLine& line = lines_[lineIdx];
    const size_t lineLen = line.PlainText().size();

    // Horizontal: walk the spans, and inside the containing span ask DWrite. The
    // trailing-side flag is what makes clicking the right half of a glyph put the
    // boundary after it, so a drag selects the character you dragged across.
    float x = bounds_.x + kPadding;
    if (dipX <= x) return LogTextPos{lineIdx, 0};

    size_t consumed = 0;
    for (const auto& span : line.spans) {
        if (span.text.empty()) continue;
        const size_t spanLen = span.text.size();

        ComPtr<IDWriteTextLayout> layout = MakeSpanLayout(span);
        if (!layout) { consumed += spanLen; continue; }

        DWRITE_TEXT_METRICS tm{};
        float spanW = 0.0f;
        if (SUCCEEDED(layout->GetMetrics(&tm)))
            spanW = tm.widthIncludingTrailingWhitespace;

        if (dipX < x + spanW) {
            BOOL isTrailing = FALSE, isInside = FALSE;
            DWRITE_HIT_TEST_METRICS htm{};
            if (SUCCEEDED(layout->HitTestPoint(dipX - x, 0.0f,
                                               &isTrailing, &isInside, &htm))) {
                size_t local = htm.textPosition + (isTrailing ? htm.length : 0);
                local = std::min(local, spanLen);
                return LogTextPos{lineIdx, consumed + local};
            }
            return LogTextPos{lineIdx, consumed};
        }

        x += spanW;
        consumed += spanLen;
    }
    // Past the last glyph: the end of the line.
    return LogTextPos{lineIdx, lineLen};
}

void LogViewer::RenderSelectionForLine(const DrawingContext& dc, const LogLine& line,
                                       size_t lineIndex, float y,
                                       const LogTextPos& from,
                                       const LogTextPos& to) const {
    const size_t lineLen = line.PlainText().size();

    // Clamp the range to this line. An interior line of a multi-line selection is
    // fully covered; the first and last are cut at their columns.
    const size_t begin = (lineIndex == from.line) ? std::min(from.column, lineLen) : 0;
    const size_t end = (lineIndex == to.line) ? std::min(to.column, lineLen) : lineLen;

    // A fully-selected line whose text is empty still gets a thin marker, otherwise a
    // blank line inside a multi-line selection reads as a gap in the highlight.
    float x0 = ColumnToX(line, begin);
    float x1 = (begin < end) ? ColumnToX(line, end) : x0 + 4.0f;
    if (lineIndex != to.line) {
        // Not the last line: the selection continues past the end of the text, so
        // extend a little to show the newline is included.
        x1 = std::max(x1, ColumnToX(line, lineLen) + 4.0f);
    }
    if (x1 <= x0) return;

    // accentSelection is the token the rest of the framework highlights with; alpha
    // keeps the glyphs legible through the fill rather than needing a second text
    // color for selected runs.
    const ColorTokens& pal = Theme().colors;
    D2D1_COLOR_F fill = pal.accent;
    fill.a = 0.35f;

    dc.FillRect(D2D1::RectF(x0, y, std::min(x1, bounds_.right()), y + line.height), fill);
}

// ---------------------------------------------------------------------------
// Keyboard
// ---------------------------------------------------------------------------

void LogViewer::OnKeyDownRouted(KeyEventArgs& e) {
    const bool ctrl = (e.modifiers & ModifierKeys::Ctrl) == ModifierKeys::Ctrl;

    if (ctrl && (e.vk == 'C' || e.vk == VK_INSERT)) {
        // Ctrl+Insert as well as Ctrl+C: it is the older binding and still what some
        // terminal users reach for.
        if (CopySelectionToClipboard())
            e.handled = true;
        return;
    }
    if (ctrl && e.vk == 'A') {
        SelectAll();
        e.handled = true;
        return;
    }
    if (e.vk == VK_ESCAPE && HasSelection()) {
        ClearSelection();
        e.handled = true;
        return;
    }

    // Keyboard selection without a caret (scheme A). The movable end is selCurrent_;
    // the anchor is wherever the last plain click put it. Only Shift combinations are
    // handled — an unshifted arrow has no caret to move in a read-only view, so plain
    // Up/Down/PageUp/PageDown scroll instead, and plain Left/Right/Home/End fall through.
    const bool shift = (e.modifiers & ModifierKeys::Shift) == ModifierKeys::Shift;
    if (selectionEnabled_ && shift && !lines_.empty()) {
        if (MoveSelectionEnd(e.vk, ctrl)) {
            e.handled = true;
            return;
        }
    }

    if (!shift && ScrollByKey(e.vk, ctrl)) {
        e.handled = true;
        return;
    }
    Control::OnKeyDownRouted(e);
}

LogTextPos LogViewer::ClampPos(LogTextPos p) const {
    if (lines_.empty()) return {};
    p.line = std::min(p.line, lines_.size() - 1);
    p.column = std::min(p.column, lines_[p.line].PlainText().size());
    return p;
}

bool LogViewer::MoveSelectionEnd(UINT vk, bool ctrl) {
    LogTextPos p = ClampPos(selCurrent_);
    // A never-clicked viewer has its anchor at {0,0}; keep it consistent with the end.
    selAnchor_ = ClampPos(selAnchor_);

    switch (vk) {
    case VK_LEFT: {
        const std::wstring text = lines_[p.line].PlainText();
        if (ctrl) {
            // Ctrl+Shift+Left: previous word start; at column 0 cross to the prior line end.
            if (p.column == 0 && p.line > 0) { --p.line; p.column = lines_[p.line].PlainText().size(); }
            else p.column = PrevWordBoundary(text, p.column);
        } else if (p.column > 0) {
            --p.column;
        } else if (p.line > 0) {
            --p.line;
            p.column = lines_[p.line].PlainText().size();  // the line break is one step
        }
        break;
    }
    case VK_RIGHT: {
        const std::wstring text = lines_[p.line].PlainText();
        if (ctrl) {
            if (p.column >= text.size() && p.line + 1 < lines_.size()) { ++p.line; p.column = 0; }
            else p.column = NextWordBoundary(text, p.column);
        } else if (p.column < text.size()) {
            ++p.column;
        } else if (p.line + 1 < lines_.size()) {
            ++p.line;
            p.column = 0;
        }
        break;
    }
    case VK_UP:
        if (p.line > 0) { --p.line; p = ClampPos(p); }
        else p.column = 0;
        break;
    case VK_DOWN:
        if (p.line + 1 < lines_.size()) { ++p.line; p = ClampPos(p); }
        else p.column = lines_[p.line].PlainText().size();
        break;
    case VK_HOME:
        if (ctrl) p = LogTextPos{0, 0};
        else p.column = 0;
        break;
    case VK_END:
        if (ctrl) p = LogTextPos{lines_.size() - 1, lines_.back().PlainText().size()};
        else p.column = lines_[p.line].PlainText().size();
        break;
    default:
        return false;
    }

    selCurrent_ = p;
    EnsureLineVisible(p.line);
    Invalidate();
    return true;
}

bool LogViewer::ScrollByKey(UINT vk, bool ctrl) {
    // No caret to move, so plain navigation keys scroll the view. The selection is
    // left untouched — the whole point of scheme A is that looking around a log does
    // not destroy what the reader highlighted.
    float target = scroll_.TargetOffset();
    const float line = kWheelStepDip;
    const float page = std::max(line, bounds_.h - line);
    switch (vk) {
    case VK_UP:    target -= line; break;
    case VK_DOWN:  target += line; break;
    case VK_PRIOR: target -= page; break;
    case VK_NEXT:  target += page; break;
    case VK_HOME:  if (!ctrl) return false; target = 0.0f; break;
    case VK_END:   if (!ctrl) return false; target = scroll_.MaxOffset(); break;
    default: return false;
    }
    scroll_.Wake();
    scroll_.SetOffset(target);
    autoScrollToTail_ = scroll_.Offset() >= scroll_.MaxOffset() - 1.0f;
    Invalidate();
    return true;
}

void LogViewer::EnsureLineVisible(size_t line) {
    if (line >= lines_.size()) return;
    const float top = lineYPositions_[line];
    const float bottom = top + lines_[line].height;
    const float viewTop = scroll_.Offset();
    if (top < viewTop) scroll_.SetOffset(top);
    else if (bottom > viewTop + bounds_.h) scroll_.SetOffset(bottom - bounds_.h);
    autoScrollToTail_ = scroll_.Offset() >= scroll_.MaxOffset() - 1.0f;
}

} // namespace fluent
