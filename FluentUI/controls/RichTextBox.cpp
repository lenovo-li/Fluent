// RichTextBox.cpp — multi-style editable text control.

#include "RichTextBox.h"
#include "../styling/ThemeTokens.h"
#include "../graphics/DWriteContext.h"
#include "../graphics/DrawingContext.h"
#include "../graphics/PixelSnapping.h"
#include "../input/InputManager.h"
#include "../text/WordBoundary.h"
#include <algorithm>
#include <Imm.h>

#pragma comment(lib, "Imm32.lib")

namespace fluent {

namespace {
constexpr float kPadding = 8.0f;
constexpr float kCaretWidth = 1.5f;
constexpr float kWheelStepDip = 60.0f;
// Layout box width in NoWrap mode — wide enough that no realistic line reaches it.
constexpr float kUnboundedWidth = 1.0e6f;

// Default style for empty documents.
TextRun DefaultStyle() {
    TextRun r;
    r.text = L"";
    r.fontSize = 12.0f;
    r.weight = DWRITE_FONT_WEIGHT_NORMAL;
    r.color = D2D1::ColorF(D2D1::ColorF::Black);
    r.underline = false;
    return r;
}
} // namespace

RichTextBox::RichTextBox()
    : scrollViewer_(std::make_unique<ScrollViewer>()) {
    SetFocusable(true);
    currentStyle_ = DefaultStyle();
    // Overlay behaviour, not a permanently dimmed rail: the bar appears on scroll
    // or hover and fades after the idle delay, and ScrollViewer draws nothing at
    // all while the content fits. An editor is read far more than it is scrolled,
    // so a rail parked next to the text is noise for the common case.
    scrollViewer_->SetKeepVisibleWhenOverflow(false);
}

void RichTextBox::SetWordWrap(bool wrap) {
    if (wordWrap_ == wrap) return;
    wordWrap_ = wrap;
    // The wrap mode is baked into the layout's text format, so the layout has to
    // be rebuilt; the document height changes with it, hence the re-clamp.
    layoutDirty_ = true;
    ClampScroll();
    InvalidateMeasure();
    Invalidate();
}

// --- Document access -------------------------------------------------------

void RichTextBox::SetDocument(RichTextDocument doc) {
    doc_ = std::move(doc);
    caret_ = static_cast<UINT32>(doc_.GetPlainText().size());
    anchor_ = caret_;
    plainTextDirty_ = true;
    layoutDirty_ = true;
    Changed();
}

const std::wstring& RichTextBox::PlainText() const {
    if (plainTextDirty_) {
        RebuildPlainText();
    }
    return plainTextCache_;
}

void RichTextBox::RebuildPlainText() const {
    plainTextCache_ = doc_.GetPlainText();
    plainTextDirty_ = false;
}

// --- Style at caret --------------------------------------------------------

TextRun RichTextBox::GetCurrentStyle() const {
    if (doc_.Runs().empty()) {
        return currentStyle_;
    }
    const UINT32 pos = HasSelection() ? SelectionStart() : caret_;
    if (pos == 0 && doc_.GetPlainText().empty()) {
        return currentStyle_;
    }
    // Get the run at pos, or the last run if pos is at the end.
    const UINT32 queryPos = (pos > 0 && pos >= static_cast<UINT32>(doc_.GetPlainText().size()))
                                ? static_cast<UINT32>(doc_.GetPlainText().size()) - 1
                                : pos;
    return doc_.GetStyleAt(queryPos);
}

void RichTextBox::SetCurrentStyle(const TextRun& style) {
    currentStyle_ = style;
}

// RestyleSelection is the one place selection styling mutates the document, so
// undo recording and cache invalidation cannot be forgotten at a call site.
//
// With no selection it retargets the pending insertion style: a toolbar button
// pressed with a bare caret has to affect what gets typed next, or it looks dead.
void RichTextBox::RestyleSelection(const std::function<void(TextRun&)>& fn) {
    if (!fn) return;

    if (!HasSelection()) {
        // No selection: update the pending style for the next insertion. This is NOT
        // an edit of the document — merely a note of what style to use — and that is
        // allowed even in read-only mode (it does not mutate doc_).
        fn(currentStyle_);
        return;
    }

    // Selection exists: applying style to existing text mutates the document.
    if (readOnly_) return;
    PushUndo();
    doc_.ApplyStyle(SelectionStart(), SelectionLength(), fn);
    // Keep the caret's pending style in step with what the user just did, so
    // typing straight after styling a selection continues in the same style.
    fn(currentStyle_);
    plainTextDirty_ = true;
    layoutDirty_ = true;
    Changed();
}

// RestyleParagraphs applies paragraph-level formatting to every paragraph the
// selection touches, or to the caret's paragraph when there is no selection.
//
// Unlike RestyleSelection, this ALWAYS mutates the document even without a
// selection — a bare caret still sits in a real paragraph, and formatting it is
// a document edit. It is therefore blocked in read-only mode with no selection,
// which is the opposite of RestyleSelection's behaviour.
void RichTextBox::RestyleParagraphs(const std::function<void(ParagraphStyle&)>& fn) {
    if (!fn || readOnly_) return;

    // The affected range spans from selection start (or caret) to selection end.
    // doc_.ApplyParagraphStyle finds every paragraph that intersects this range.
    const UINT32 start = SelectionStart();
    const UINT32 length = HasSelection() ? SelectionLength() : 1;

    PushUndo();
    doc_.ApplyParagraphStyle(start, length, fn);
    plainTextDirty_ = true;  // ApplyParagraphStyle can merge runs during normalization
    layoutDirty_ = true;
    Changed();
}

void RichTextBox::ApplyStyle(const TextRun& style) {
    // Whole-style replacement: every attribute of the range becomes `style`.
    // Deliberately flattens — use the SetSelectionXxx methods to change one
    // attribute and keep the rest.
    RestyleSelection([&style](TextRun& r) {
        r.color = style.color;
        r.fontSize = style.fontSize;
        r.weight = style.weight;
        r.underline = style.underline;
    });
}

void RichTextBox::SetSelectionBold(bool bold) {
    const DWRITE_FONT_WEIGHT w = bold ? DWRITE_FONT_WEIGHT_BOLD : DWRITE_FONT_WEIGHT_NORMAL;
    RestyleSelection([w](TextRun& r) { r.weight = w; });
}

void RichTextBox::SetSelectionFontWeight(DWRITE_FONT_WEIGHT weight) {
    RestyleSelection([weight](TextRun& r) { r.weight = weight; });
}

void RichTextBox::SetSelectionUnderline(bool underline) {
    RestyleSelection([underline](TextRun& r) { r.underline = underline; });
}

void RichTextBox::SetSelectionColor(const D2D1_COLOR_F& color) {
    RestyleSelection([color](TextRun& r) { r.color = color; });
}

void RichTextBox::SetSelectionFontSize(float sizeDip) {
    // DWrite rejects a non-positive size, and a caller computing a size from a
    // slider or a zoom factor can reach 0 without meaning to.
    if (!(sizeDip > 0.0f)) return;
    RestyleSelection([sizeDip](TextRun& r) { r.fontSize = sizeDip; });
}

// --- Paragraph formatting --------------------------------------------------

void RichTextBox::SetParagraphAlignment(DWRITE_TEXT_ALIGNMENT alignment) {
    RestyleParagraphs([alignment](ParagraphStyle& ps) { ps.alignment = alignment; });
}

void RichTextBox::SetParagraphLineSpacing(float multiple) {
    // A non-positive multiplier would produce a degenerate line box. Clamp at a
    // small positive value rather than rejecting — the alternative is a stall or a
    // crash somewhere downstream in DWrite.
    if (!(multiple > 0.0f)) return;
    RestyleParagraphs([multiple](ParagraphStyle& ps) {
        ps.lineSpacingMode = LineSpacingMode::Multiple;
        ps.lineSpacing = multiple;
    });
}

void RichTextBox::SetParagraphLineHeight(float dip) {
    if (!(dip > 0.0f)) return;
    RestyleParagraphs([dip](ParagraphStyle& ps) {
        ps.lineSpacingMode = LineSpacingMode::Absolute;
        ps.lineSpacing = dip;
    });
}

void RichTextBox::SetParagraphSpaceBefore(float dip) {
    RestyleParagraphs([dip](ParagraphStyle& ps) { ps.spaceBefore = dip; });
}

void RichTextBox::SetParagraphSpaceAfter(float dip) {
    RestyleParagraphs([dip](ParagraphStyle& ps) { ps.spaceAfter = dip; });
}

void RichTextBox::SetParagraphFirstLineIndent(float dip) {
    RestyleParagraphs([dip](ParagraphStyle& ps) { ps.firstLineIndent = dip; });
}

void RichTextBox::SetParagraphLeftIndent(float dip) {
    RestyleParagraphs([dip](ParagraphStyle& ps) { ps.leftIndent = dip; });
}

void RichTextBox::SetParagraphRightIndent(float dip) {
    RestyleParagraphs([dip](ParagraphStyle& ps) { ps.rightIndent = dip; });
}

void RichTextBox::ApplyParagraphStyle(const ParagraphStyle& style) {
    // Whole-style replacement: every attribute of the affected paragraphs becomes
    // `style`. Deliberately flattens, the way ApplyStyle(TextRun) does for runs.
    RestyleParagraphs([&style](ParagraphStyle& ps) { ps = style; });
}

ParagraphStyle RichTextBox::GetCurrentParagraphStyle() const {
    // Report the style at the caret (or selection start). A selection spanning
    // paragraphs with different alignments reports the first one — the toolbar
    // shows a state rather than nothing, and "what is at the caret" is that state.
    return doc_.GetParagraphStyleAt(SelectionStart());
}

// --- Selection -------------------------------------------------------------

void RichTextBox::Select(UINT32 start, UINT32 length) {
    const UINT32 maxPos = static_cast<UINT32>(PlainText().size());
    start = std::min(start, maxPos);
    length = std::min(length, maxPos - start);
    anchor_ = start;
    caret_ = start + length;
    SelectionMoved();
}

void RichTextBox::SelectAll() {
    anchor_ = 0;
    caret_ = static_cast<UINT32>(PlainText().size());
    SelectionMoved();
}

// ══════════════════════════════════════════════════════════════════════════════
// Undo / Redo
// ══════════════════════════════════════════════════════════════════════════════

void RichTextBox::PushUndo() {
    Snapshot s;
    s.paragraphs = doc_.Paragraphs();
    s.caret = caret_;
    s.anchor = anchor_;
    undoStack_.push_back(std::move(s));
    if (undoStack_.size() > kMaxUndo) {
        undoStack_.erase(undoStack_.begin());
    }
    // Any new edit clears the redo stack — you cannot redo something after you've
    // taken a different branch.
    redoStack_.clear();
}

void RichTextBox::Undo() {
    if (readOnly_) return;
    if (undoStack_.empty()) return;
    // Move current state to redo, then restore the snapshot.
    Snapshot current;
    current.paragraphs = doc_.Paragraphs();
    current.caret = caret_;
    current.anchor = anchor_;
    redoStack_.push_back(std::move(current));

    ApplySnapshot(undoStack_.back());
    undoStack_.pop_back();
}

void RichTextBox::Redo() {
    if (readOnly_) return;
    if (redoStack_.empty()) return;
    // Move current state to undo, then restore the redo snapshot.
    Snapshot current;
    current.paragraphs = doc_.Paragraphs();
    current.caret = caret_;
    current.anchor = anchor_;
    undoStack_.push_back(std::move(current));

    ApplySnapshot(redoStack_.back());
    redoStack_.pop_back();
}

void RichTextBox::ApplySnapshot(const Snapshot& s) {
    doc_.SetParagraphs(s.paragraphs);
    caret_ = s.caret;
    anchor_ = s.anchor;
    // The document changed underneath both caches.
    plainTextDirty_ = true;
    layoutDirty_ = true;
    Changed();
}

// --- Layout ----------------------------------------------------------------

// The string that actually gets laid out: the document plus the in-flight IME
// composition spliced in at the caret. With no composition this is the document
// text unchanged, which is the case every frame that is not mid-composition.
std::wstring RichTextBox::LayoutText() const {
    const std::wstring& text = PlainText();
    if (composition_.empty()) return text;

    std::wstring out = text;
    out.insert(std::min<size_t>(caret_, out.size()), composition_);
    return out;
}

// A document offset expressed in layout coordinates. Everything at or after the
// caret is pushed right by the composition length, because the composition sits
// at the caret in the laid-out string but does not exist in the document.
UINT32 RichTextBox::LayoutOffset(UINT32 docOffset) const {
    if (composition_.empty() || docOffset < caret_) return docOffset;
    return docOffset + static_cast<UINT32>(composition_.size());
}

void RichTextBox::RebuildLayout(float outerWidth) {
    paragraphLayouts_.clear();
    const float availWidth = std::max(1.0f, outerWidth - kPadding * 2.0f);

    auto* dw = Dwrite();
    if (!dw || !dw->Factory()) {
        layoutDirty_ = false;
        return;
    }

    // In NoWrap the layout box must be effectively unbounded, not the control
    // width: NO_WRAP stops DWrite breaking lines, but a box narrower than the text
    // still clips the glyph runs, so long lines would simply vanish at the edge
    // instead of running past it.
    const float baseLayoutWidth = wordWrap_ ? availWidth : kUnboundedWidth;

    // Build one layout per paragraph. Each paragraph's runs are concatenated into
    // a single string for that paragraph, then per-run formatting is applied via
    // SetFontSize/SetFontWeight/SetDrawingEffect ranges within that string.
    const auto& paragraphs = doc_.Paragraphs();

    for (size_t paraIdx = 0; paraIdx < paragraphs.size(); ++paraIdx) {
        const auto& para = paragraphs[paraIdx];

        // Calculate layout width for this paragraph: narrow by left and right indents.
        const float paraLayoutWidth = std::max(1.0f, baseLayoutWidth - para.style.leftIndent - para.style.rightIndent);

        // Concatenate all runs in this paragraph into one string.
        std::wstring paraText;
        for (const auto& run : para.runs) {
            paraText += run.text;
        }

        // Handle IME composition: if the caret is in THIS paragraph, splice the
        // composition string in at the paragraph-relative caret position.
        UINT32 paraStartOffset = 0;
        for (size_t i = 0; i < paraIdx; ++i) {
            for (const auto& r : paragraphs[i].runs) {
                paraStartOffset += static_cast<UINT32>(r.text.size());
            }
            paraStartOffset += 1; // the '\n' separator
        }
        const UINT32 paraEndOffset = paraStartOffset + static_cast<UINT32>(paraText.size());

        UINT32 compositionOffsetInPara = 0;  // where composition sits within this para's layout
        if (!composition_.empty() && caret_ >= paraStartOffset && caret_ <= paraEndOffset) {
            compositionOffsetInPara = caret_ - paraStartOffset;
            paraText.insert(compositionOffsetInPara, composition_);
        }

        // Use the first run's size/weight as the base format for this paragraph.
        // If the paragraph is empty (only a newline), use the default style.
        const TextRun baseStyle = para.runs.empty() ? DefaultStyle() : para.runs[0];
        IDWriteTextFormat* fmt = dw->Format(
            baseStyle.fontSize, baseStyle.weight,
            DWRITE_TEXT_ALIGNMENT_LEADING,  // will override with para.style.alignment
            DWRITE_PARAGRAPH_ALIGNMENT_NEAR,
            wordWrap_ ? DWRITE_WORD_WRAPPING_WRAP : DWRITE_WORD_WRAPPING_NO_WRAP);
        if (!fmt) {
            layoutDirty_ = false;
            return;
        }

        ComPtr<IDWriteTextLayout> layout;
        HRESULT hr = dw->Factory()->CreateTextLayout(
            paraText.c_str(), static_cast<UINT32>(paraText.size()),
            fmt, paraLayoutWidth, 1.0e6f, &layout);
        if (FAILED(hr) || !layout) {
            layoutDirty_ = false;
            return;
        }

        // Apply paragraph-level styles.
        layout->SetTextAlignment(para.style.alignment);

        // Line spacing.
        //
        // DEFAULT is not the same as UNIFORM with a hand-computed height, and the
        // difference is not cosmetic. A font's line height is ascent + descent +
        // lineGap — roughly 1.2-1.4x the em size, never equal to it. Passing
        // `fontSize` as the UNIFORM height therefore COMPRESSES every line below
        // what the font asks for, which silently shrinks the document: a test that
        // types 400 characters into a 200 DIP viewport stopped overflowing at all,
        // and the scrollbar never appeared.
        //
        // So single spacing must mean "whatever DWrite derives from the font", not
        // "fontSize". Only a non-1.0 multiplier switches to UNIFORM, and it scales
        // the font-derived height rather than the em size.
        if (para.style.lineSpacingMode == LineSpacingMode::Absolute) {
            // Absolute: the caller is pinning the line box, so use the value as given.
            // Baseline at 80% of the line height is DWrite's own default ratio and
            // keeps glyphs from sitting on the very bottom of the box.
            layout->SetLineSpacing(DWRITE_LINE_SPACING_METHOD_UNIFORM,
                                   para.style.lineSpacing,
                                   para.style.lineSpacing * 0.8f);
        } else if (std::abs(para.style.lineSpacing - 1.0f) > 0.001f) {
            // A multiplier other than single. Scale the font-derived line height,
            // which requires asking DWrite what that height actually is first.
            DWRITE_LINE_METRICS probe{};
            UINT32 probeCount = 0;
            float naturalHeight = baseStyle.fontSize * 1.3f;   // fallback if the probe fails
            float naturalBaseline = naturalHeight * 0.8f;
            if (SUCCEEDED(layout->GetLineMetrics(&probe, 1, &probeCount)) && probeCount > 0) {
                naturalHeight = probe.height;
                naturalBaseline = probe.baseline;
            }
            const float scaled = naturalHeight * para.style.lineSpacing;
            layout->SetLineSpacing(DWRITE_LINE_SPACING_METHOD_UNIFORM, scaled, naturalBaseline);
        }
        // else: single spacing — leave the format's DEFAULT method untouched.

        // First-line indent.
        //
        // DirectWrite has no indent property. leftIndent/rightIndent are handled by
        // narrowing the layout box and shifting the draw origin (see above and
        // Render), but those move EVERY line — the first line needs its own extra
        // offset, and only DWrite knows where the first line ends.
        //
        // IDWriteTextLayout1::SetCharacterSpacing gives leading space to a character
        // range; applied to just the first character it indents exactly the first
        // line, and it is a formatting attribute rather than inserted text, so
        // offsets, hit-testing, selection, and the undo stack are all untouched.
        //
        // Rejected alternatives, both of which break something load-bearing:
        // - An invisible IDWriteInlineObject at the paragraph start occupies a
        //   character position, so the layout string becomes one longer than the
        //   paragraph. Every caret offset, selection range, and hit-test result past
        //   it would be off by one.
        // - A custom IDWriteTextRenderer could offset the first line's glyph runs,
        //   but it means reimplementing all four draw callbacks (glyph run,
        //   underline, strikethrough, inline object) to keep underline and selection
        //   rendering working.
        //
        // Requires IDWriteTextLayout1 (Windows 8+). If the QI fails, the indent is
        // silently skipped rather than drawn wrong.
        if (std::abs(para.style.firstLineIndent) > 0.001f && !paraText.empty()) {
            ComPtr<IDWriteTextLayout1> layout1;
            if (SUCCEEDED(layout->QueryInterface(__uuidof(IDWriteTextLayout1),
                                                 reinterpret_cast<void**>(layout1.GetAddressOf())))) {
                // leadingSpacing on the first character only. A negative value is a
                // hanging indent, which is how bullet and numbered lists are shaped.
                layout1->SetCharacterSpacing(para.style.firstLineIndent, 0.0f, 0.0f,
                                             DWRITE_TEXT_RANGE{0, 1});
            }
        }

        // Apply per-run formatting: font size, weight, underline.
        // Offsets are paragraph-relative now, not document-global.
        UINT32 runOffset = 0;
        for (const auto& run : para.runs) {
            const UINT32 length = static_cast<UINT32>(run.text.size());
            if (length == 0) continue;

            const UINT32 runStart = runOffset;
            const UINT32 runEnd = runOffset + length;
            runOffset = runEnd;

            auto styleRange = [&](UINT32 layoutStart, UINT32 len) {
                if (len == 0) return;
                const DWRITE_TEXT_RANGE range{layoutStart, len};
                layout->SetFontSize(run.fontSize, range);
                layout->SetFontWeight(run.weight, range);
                if (run.underline) layout->SetUnderline(TRUE, range);
            };

            // If the composition is in this paragraph and splits this run, style
            // the part before the composition and the part after separately.
            if (!composition_.empty() && compositionOffsetInPara > 0 &&
                runEnd > compositionOffsetInPara && runStart < compositionOffsetInPara) {
                const UINT32 head = compositionOffsetInPara - runStart;
                styleRange(runStart, head);  // before composition
                const UINT32 tailStart = compositionOffsetInPara + static_cast<UINT32>(composition_.size());
                styleRange(tailStart, length - head);  // after composition
            } else {
                // Adjust offset if this run is after the composition.
                UINT32 layoutStart = runStart;
                if (!composition_.empty() && compositionOffsetInPara > 0 && runStart >= compositionOffsetInPara) {
                    layoutStart += static_cast<UINT32>(composition_.size());
                }
                styleRange(layoutStart, length);
            }
        }

        // Mark the composition itself: underlined, so the user can see which
        // characters are still provisional and which are committed text.
        if (!composition_.empty() && compositionOffsetInPara > 0) {
            layout->SetUnderline(TRUE, DWRITE_TEXT_RANGE{
                compositionOffsetInPara,
                static_cast<UINT32>(composition_.size())
            });
        }

        ParagraphLayoutInfo info;
        info.layout = layout;
        info.spaceBefore = para.style.spaceBefore;
        info.spaceAfter = para.style.spaceAfter;
        info.leftIndent = para.style.leftIndent;
        info.rightIndent = para.style.rightIndent;
        info.firstLineIndent = para.style.firstLineIndent;
        // runBrushes will be filled in Render() — they need the device context.

        paragraphLayouts_.push_back(std::move(info));
    }

    lastLayoutWidth_ = availWidth;
    layoutDirty_ = false;
}

// Rebuilds the layout if it is dirty or if the width it was built for has changed.
//
// outerWidth defaults to bounds_.w, which is right everywhere EXCEPT inside Measure:
// there bounds_ still holds the previous pass's value (0×0 on the first frame), so
// Measure passes the availW it was given instead. Wrapping a one-line document into
// a 1 DIP column otherwise makes it hundreds of lines tall, and the ScrollViewer is
// told the content overflows — a scrollbar on a document that fits.
void RichTextBox::EnsureLayoutUpToDate(float outerWidth) const {
    const float target = outerWidth < 0.0f ? bounds_.w : outerWidth;
    const float availWidth = std::max(1.0f, target - kPadding * 2.0f);
    if (layoutDirty_ || paragraphLayouts_.empty() || lastLayoutWidth_ != availWidth) {
        const_cast<RichTextBox*>(this)->RebuildLayout(target);
    }
}

float RichTextBox::ContentHeight(float outerWidth) const {
    EnsureLayoutUpToDate(outerWidth);
    if (paragraphLayouts_.empty()) return kPadding * 2.0f;

    // Sum all paragraph heights plus spaceBefore/spaceAfter.
    float totalHeight = 0.0f;
    for (const auto& paraInfo : paragraphLayouts_) {
        totalHeight += paraInfo.spaceBefore;

        if (paraInfo.layout) {
            DWRITE_TEXT_METRICS tm{};
            if (SUCCEEDED(paraInfo.layout->GetMetrics(&tm))) {
                totalHeight += tm.height;
            }
        }

        totalHeight += paraInfo.spaceAfter;
    }

    return totalHeight + kPadding * 2.0f;
}

bool RichTextBox::HorizontallyScrollable() const {
    return scrollViewer_->HorizontallyScrollable();
}

bool RichTextBox::VerticallyScrollable() const {
    return scrollViewer_->MaxOffset() > 0.0f;
}

float RichTextBox::ScrollOffset() const {
    return scrollViewer_->Offset();
}

void RichTextBox::ScrollTo(float offsetDip) {
    scrollViewer_->SetOffset(offsetDip);   // ScrollViewer clamps to its own range
    Invalidate();
}

float RichTextBox::ContentWidth(float outerWidth) const {
    EnsureLayoutUpToDate(outerWidth);
    if (paragraphLayouts_.empty()) return kPadding * 2.0f;

    // The widest paragraph determines the horizontal scroll extent.
    //
    // widthIncludingTrailingWhitespace is what the glyph runs actually occupy; the
    // narrower 'width' excludes trailing spaces on each line, which would make the
    // horizontal scrollbar disappear before the last character scrolls into view.
    float maxWidth = 0.0f;
    for (const auto& paraInfo : paragraphLayouts_) {
        if (!paraInfo.layout) continue;

        DWRITE_TEXT_METRICS tm{};
        if (SUCCEEDED(paraInfo.layout->GetMetrics(&tm))) {
            maxWidth = std::max(maxWidth, tm.widthIncludingTrailingWhitespace);
        }
    }

    return maxWidth + kPadding * 2.0f;
}

void RichTextBox::ClampScroll() {
    const float maxScroll = std::max(0.0f, ContentHeight() - bounds_.h);
    scrollViewer_->SetOffset(std::max(0.0f, std::min(scrollViewer_->Offset(), maxScroll)));
    
    // Horizontal, for NoWrap. Inert in Wrap where MaxOffsetX() is 0.
    const float maxX = scrollViewer_->MaxOffsetX();
    scrollViewer_->SetOffsetX(std::max(0.0f, std::min(scrollViewer_->OffsetX(), maxX)));
}

void RichTextBox::EnsureCaretVisible() {
    const RectDip caretRect = CaretRectContentRelative();

    const float viewTop = scrollViewer_->Offset();
    const float viewBottom = viewTop + bounds_.h;
    if (caretRect.y < viewTop) {
        scrollViewer_->AnimateTo(caretRect.y);
    } else if (caretRect.bottom() > viewBottom) {
        scrollViewer_->AnimateTo(caretRect.bottom() - bounds_.h);
    }

    // Horizontal, for NoWrap: without this, typing past the right edge walks the
    // caret out of view and the user is editing text they cannot see. Inert in
    // Wrap mode, where MaxOffsetX() is 0.
    if (scrollViewer_->HorizontallyScrollable()) {
        const float viewW = std::max(1.0f, bounds_.w - kPadding * 2.0f);
        const float viewLeft = scrollViewer_->OffsetX();
        const float viewRight = viewLeft + viewW;
        if (caretRect.x < viewLeft) {
            scrollViewer_->SetOffsetX(caretRect.x);
        } else if (caretRect.x > viewRight) {
            scrollViewer_->SetOffsetX(caretRect.x - viewW);
        }
    }

    ClampScroll();
}

// --- Rendering -------------------------------------------------------------

void RichTextBox::Render(const DrawingContext& dc) {
    if (!IsVisible()) return;

    const ColorTokens& pal = Theme().colors;
    const D2D1_RECT_F box = D2D1::RectF(bounds_.x, bounds_.y,
                                       bounds_.right(), bounds_.bottom());
    dc.FillRect(box, EffectiveBackground(pal.controlFillDefault));

    EnsureLayoutUpToDate();

    if (paragraphLayouts_.empty()) return;

    auto clip = dc.PushClip(box);

    // Snap scroll offsets to physical pixel boundaries to eliminate subpixel rendering
    // jitter. Without snapping, fractional scroll offsets (e.g., 15.7 DIPs) cause text
    // to render at non-integer pixel coordinates, producing visible blur/shimmer during
    // smooth scrolling. The conversion DIP → px → round → DIP ensures text always lands
    // on integer pixel boundaries where DirectWrite's ClearType produces consistent output.
    const float dpiScale = dc.DpiScale();
    const float scrollY = SnapToPixel(scrollViewer_->Offset(), dpiScale);
    const float scrollX = SnapToPixel(scrollViewer_->OffsetX(), dpiScale);

    // Draw each paragraph layout, accumulating Y offset as we go.
    float currentY = kPadding - scrollY;
    const auto& paragraphs = doc_.Paragraphs();

    for (size_t paraIdx = 0; paraIdx < paragraphLayouts_.size(); ++paraIdx) {
        auto& paraInfo = paragraphLayouts_[paraIdx];

        // Space before this paragraph.
        currentY += paraInfo.spaceBefore;

        if (!paraInfo.layout) {
            currentY += paraInfo.spaceAfter;
            continue;
        }

        // Origin for this paragraph's layout.
        // X: shift right by leftIndent; Y: current accumulated height.
        const D2D1_POINT_2F baseOrigin = D2D1::Point2F(
            bounds_.x + kPadding + paraInfo.leftIndent - scrollX,
            bounds_.y + currentY);

        // Per-run color, applied here rather than in RebuildLayout because creating a
        // brush needs the frame's device context, which only exists during Render.
        //
        // SetDrawingEffect attaches the brush to a text range; D2D's default text
        // renderer honors an ID2D1Brush effect as the fill color for that range. The
        // brushes are kept alive in runBrushes until the next frame — the layout only
        // holds weak references to the effects.
        if (ID2D1DeviceContext* d2d = dc.Dc()) {
            paraInfo.runBrushes.clear();

            // Calculate paragraph start offset in document-global space for composition handling.
            UINT32 paraStartOffset = 0;
            for (size_t i = 0; i < paraIdx; ++i) {
                for (const auto& r : paragraphs[i].runs) {
                    paraStartOffset += static_cast<UINT32>(r.text.size());
                }
                paraStartOffset += 1; // '\n' separator
            }

            const auto& para = paragraphs[paraIdx];
            UINT32 runOffset = 0;

            for (const auto& run : para.runs) {
                const UINT32 length = static_cast<UINT32>(run.text.size());
                if (length == 0) continue;

                ComPtr<ID2D1SolidColorBrush> brush;
                if (FAILED(d2d->CreateSolidColorBrush(run.color, &brush))) continue;

                const UINT32 runStart = runOffset;
                const UINT32 runEnd = runOffset + length;
                runOffset = runEnd;

                auto paintRange = [&](UINT32 layoutStart, UINT32 len) {
                    if (len == 0) return;
                    paraInfo.layout->SetDrawingEffect(brush.Get(), DWRITE_TEXT_RANGE{layoutStart, len});
                };

                // Handle composition splitting this run.
                const UINT32 docRunStart = paraStartOffset + runStart;
                const UINT32 docRunEnd = paraStartOffset + runEnd;

                if (!composition_.empty() && caret_ >= paraStartOffset &&
                    caret_ <= paraStartOffset + runOffset &&
                    docRunEnd > caret_ && docRunStart < caret_) {
                    // This run is split by the composition.
                    const UINT32 head = caret_ - docRunStart;
                    paintRange(runStart, head);
                    const UINT32 tailStart = runStart + head + static_cast<UINT32>(composition_.size());
                    paintRange(tailStart, length - head);
                } else {
                    // Adjust offset if this run is after the composition.
                    UINT32 layoutStart = runStart;
                    if (!composition_.empty() && caret_ >= paraStartOffset &&
                        caret_ < paraStartOffset + runStart) {
                        layoutStart += static_cast<UINT32>(composition_.size());
                    }
                    paintRange(layoutStart, length);
                }

                paraInfo.runBrushes.push_back(brush);
            }
        }

        // Selection highlight for this paragraph, drawn UNDER the text so glyphs stay readable.
        if (HasSelection()) {
            const UINT32 selStart = SelectionStart();
            const UINT32 selEnd = selStart + SelectionLength();

            // Calculate this paragraph's document offset range.
            UINT32 paraStartOffset = 0;
            for (size_t i = 0; i < paraIdx; ++i) {
                for (const auto& r : paragraphs[i].runs) {
                    paraStartOffset += static_cast<UINT32>(r.text.size());
                }
                paraStartOffset += 1; // '\n'
            }
            UINT32 paraEndOffset = paraStartOffset;
            for (const auto& r : paragraphs[paraIdx].runs) {
                paraEndOffset += static_cast<UINT32>(r.text.size());
            }

            // Check if selection intersects this paragraph.
            if (selEnd > paraStartOffset && selStart < paraEndOffset) {
                // Clip selection to this paragraph's range.
                const UINT32 paraSelStart = selStart > paraStartOffset ? selStart - paraStartOffset : 0;
                const UINT32 paraSelEnd = std::min(selEnd - paraStartOffset, paraEndOffset - paraStartOffset);
                const UINT32 paraSelLen = paraSelEnd - paraSelStart;

                if (paraSelLen > 0) {
                    // Adjust for composition if it's in this paragraph.
                    UINT32 layoutSelStart = paraSelStart;
                    if (!composition_.empty() && caret_ >= paraStartOffset && caret_ <= paraEndOffset) {
                        const UINT32 caretInPara = caret_ - paraStartOffset;
                        if (paraSelStart >= caretInPara) {
                            layoutSelStart += static_cast<UINT32>(composition_.size());
                        }
                    }

                    UINT32 actualCount = 0;
                    paraInfo.layout->HitTestTextRange(layoutSelStart, paraSelLen, baseOrigin.x, baseOrigin.y,
                                                     nullptr, 0, &actualCount);
                    if (actualCount > 0) {
                        std::vector<DWRITE_HIT_TEST_METRICS> hits(actualCount);
                        if (SUCCEEDED(paraInfo.layout->HitTestTextRange(layoutSelStart, paraSelLen,
                                                                        baseOrigin.x, baseOrigin.y,
                                                                        hits.data(), actualCount, &actualCount))) {
                            const D2D1_COLOR_F fill =
                                D2D1::ColorF(pal.accent.r, pal.accent.g, pal.accent.b, 0.30f);
                            for (UINT32 i = 0; i < actualCount; ++i) {
                                const auto& h = hits[i];
                                dc.FillRect(D2D1::RectF(h.left, h.top, h.left + h.width, h.top + h.height),
                                           fill);
                            }
                        }
                    }
                }
            }
        }

        // Draw the paragraph's text layout.
        dc.DrawTextLayout(baseOrigin, paraInfo.layout.Get(), pal.textPrimary, D2D1_DRAW_TEXT_OPTIONS_CLIP);

        // Advance Y by this paragraph's height.
        DWRITE_TEXT_METRICS tm{};
        if (SUCCEEDED(paraInfo.layout->GetMetrics(&tm))) {
            currentY += tm.height;
        }

        // Space after this paragraph.
        currentY += paraInfo.spaceAfter;
    }

    // Draw caret.
    if (IsFocused() && caretVisible_) {
        const RectDip caretRect = CaretRectContentRelative();
        const D2D1_RECT_F caretBox = D2D1::RectF(
            bounds_.x + kPadding + caretRect.x - scrollX,
            bounds_.y + kPadding + caretRect.y - scrollY,
            bounds_.x + kPadding + caretRect.x + kCaretWidth - scrollX,
            bounds_.y + kPadding + caretRect.y + caretRect.h - scrollY);
        dc.FillRect(caretBox, pal.textPrimary);
    }

    scrollViewer_->Render(dc);
}

void RichTextBox::Measure(float availW, float availH) {
    // Measure against availW, not bounds_.w — see EnsureLayoutUpToDate.
    scrollViewer_->SetContentHeight(ContentHeight(availW));
    // In NoWrap mode the content width is whatever DirectWrite measured; in Wrap
    // it is 0 (no horizontal scrolling — lines reflow to fit the control width).
    scrollViewer_->SetContentWidth(wordWrap_ ? 0.0f : ContentWidth(availW));
    scrollViewer_->SetBounds(RectDip{0, 0, availW, availH});
    Control::Measure(availW, availH);
}

void RichTextBox::Arrange(const RectDip& finalRect) {
    Control::Arrange(finalRect);
    scrollViewer_->SetBounds(finalRect);
    ClampScroll();
}

bool RichTextBox::WantsAnimationTick() const {
    return scrollViewer_->NeedsTick() || scrollViewer_->IsAutoscrolling();
}

void RichTextBox::OnAnimationTick(float dtSec) {
    scrollViewer_->Tick(dtSec);
    if (scrollViewer_->IsAutoscrolling()) {
        scrollViewer_->TickAutoscroll(dtSec);
    }
    Invalidate();
}

void RichTextBox::OnAttachedToTree() {
    Control::OnAttachedToTree();
    scrollViewer_->AttachToContext(Context());
}

// --- Caret rect ------------------------------------------------------------

RectDip RichTextBox::CaretRectContentRelative() const {
    EnsureLayoutUpToDate();

    // Nothing laid out at all — an empty document with no composition.
    if (paragraphLayouts_.empty() || (PlainText().empty() && composition_.empty())) {
        return RectDip{0.0f, 0.0f, kCaretWidth, currentStyle_.fontSize * 1.3f};
    }

    const auto& paragraphs = doc_.Paragraphs();

    // Find which paragraph contains the caret.
    UINT32 offset = 0;
    for (size_t paraIdx = 0; paraIdx < paragraphs.size(); ++paraIdx) {
        const auto& para = paragraphs[paraIdx];
        UINT32 paraLength = 0;
        for (const auto& run : para.runs) {
            paraLength += static_cast<UINT32>(run.text.size());
        }

        // Check if caret is in this paragraph (or at the newline immediately after).
        if (caret_ <= offset + paraLength) {
            const auto& paraInfo = paragraphLayouts_[paraIdx];
            if (!paraInfo.layout) {
                // Paragraph has no layout (shouldn't happen), return default.
                return RectDip{0.0f, 0.0f, kCaretWidth, currentStyle_.fontSize * 1.3f};
            }

            // Convert document-global caret to paragraph-relative position.
            UINT32 caretInPara = caret_ - offset;

            // While composing, the caret belongs at the END of the composition, which is
            // where the next composed character will land — not at the document caret,
            // which sits before the whole composition.
            UINT32 layoutPos = caretInPara;
            if (!composition_.empty() && caret_ >= offset && caret_ <= offset + paraLength) {
                layoutPos += static_cast<UINT32>(composition_.size());
            }

            // Query DWrite for the caret position within this paragraph's layout.
            float caretX = 0.0f, caretY = 0.0f;
            DWRITE_HIT_TEST_METRICS htm{};
            if (FAILED(paraInfo.layout->HitTestTextPosition(layoutPos, FALSE, &caretX, &caretY, &htm))) {
                return RectDip{0.0f, 0.0f, kCaretWidth, currentStyle_.fontSize * 1.3f};
            }

            // Calculate Y offset to this paragraph's top edge.
            float yOffset = 0.0f;
            for (size_t i = 0; i < paraIdx; ++i) {
                yOffset += paragraphLayouts_[i].spaceBefore;
                DWRITE_TEXT_METRICS tm{};
                if (paragraphLayouts_[i].layout &&
                    SUCCEEDED(paragraphLayouts_[i].layout->GetMetrics(&tm))) {
                    yOffset += tm.height;
                }
                yOffset += paragraphLayouts_[i].spaceAfter;
            }
            yOffset += paraInfo.spaceBefore;

            return RectDip{caretX, yOffset + caretY, kCaretWidth, htm.height};
        }

        offset += paraLength + 1;  // +1 for the '\n' separator
    }

    // Caret is past the end of all paragraphs: return a position after the last paragraph.
    float yOffset = 0.0f;
    for (const auto& paraInfo : paragraphLayouts_) {
        yOffset += paraInfo.spaceBefore;
        DWRITE_TEXT_METRICS tm{};
        if (paraInfo.layout && SUCCEEDED(paraInfo.layout->GetMetrics(&tm))) {
            yOffset += tm.height;
        }
        yOffset += paraInfo.spaceAfter;
    }

    return RectDip{0.0f, yOffset, kCaretWidth, currentStyle_.fontSize * 1.3f};
}

bool RichTextBox::CaretRectDip(RectDip& out) const {
    if (!IsFocused()) return false;
    const RectDip contentRelative = CaretRectContentRelative();
    const float scrollY = scrollViewer_->Offset();
    const float scrollX = scrollViewer_->OffsetX();
    out = RectDip{
        bounds_.x + kPadding + contentRelative.x - scrollX,
        bounds_.y + kPadding + contentRelative.y - scrollY,
        contentRelative.w,
        contentRelative.h
    };
    return true;
}

// --- Editing ---------------------------------------------------------------

void RichTextBox::InsertText(const std::wstring& s) {
    if (readOnly_) return;
    if (s.empty()) return;
    PushUndo();
    DeleteSelection();

    // Insert at caret using the current style.
    doc_.InsertText(caret_, s, currentStyle_);

    caret_ += static_cast<UINT32>(s.size());
    anchor_ = caret_;
    plainTextDirty_ = true;
    layoutDirty_ = true;
    Changed();
}

void RichTextBox::DeleteSelection() {
    if (!HasSelection()) return;
    const UINT32 start = SelectionStart();
    const UINT32 len = SelectionLength();
    doc_.DeleteRange(start, len);
    caret_ = start;
    anchor_ = start;
    plainTextDirty_ = true;
    layoutDirty_ = true;
}

void RichTextBox::Backspace() {
    if (readOnly_) return;
    if (HasSelection()) {
        PushUndo();
        DeleteSelection();
        Changed();
        return;
    }
    if (caret_ == 0) return;
    PushUndo();
    doc_.DeleteRange(caret_ - 1, 1);
    caret_--;
    anchor_ = caret_;
    plainTextDirty_ = true;
    layoutDirty_ = true;
    Changed();
}

void RichTextBox::Delete() {
    if (readOnly_) return;
    if (HasSelection()) {
        PushUndo();
        DeleteSelection();
        Changed();
        return;
    }
    const UINT32 len = static_cast<UINT32>(PlainText().size());
    if (caret_ >= len) return;
    PushUndo();
    doc_.DeleteRange(caret_, 1);
    plainTextDirty_ = true;
    layoutDirty_ = true;
    Changed();
}

void RichTextBox::MoveCaret(UINT32 to, bool extendSelection) {
    const UINT32 maxPos = static_cast<UINT32>(PlainText().size());
    caret_ = std::min(to, maxPos);
    if (!extendSelection) {
        anchor_ = caret_;
    }
    SelectionMoved();
}

void RichTextBox::Refresh() {
    // Content just changed: the layout is stale, the scrollable extent may have grown
    // (paste added ten paragraphs) or shrunk (delete removed half the document), and
    // the caret may have moved outside the visible window. Rebuild the layout with the
    // current arranged width so the new extent is known now, then tell the ScrollViewer
    // the new height — if we wait until the next Measure(), the scrollbar stays in its
    // old state (no bar when the content is now tall, or a bar when it just got short).
    // This mirrors TextArea, which calls SetContentHeight() immediately after every edit
    // rather than deferring to Arrange().
    EnsureLayoutUpToDate(bounds_.w);
    scrollViewer_->SetContentHeight(ContentHeight(bounds_.w));
    // Horizontal too: in NoWrap an edit can lengthen the longest line. Wrap keeps
    // reporting 0 (there is nothing to scroll to sideways) — see Measure().
    scrollViewer_->SetContentWidth(wordWrap_ ? 0.0f : ContentWidth(bounds_.w));

    EnsureCaretVisible();
    caretVisible_ = true;
    Invalidate();
}

void RichTextBox::RaiseSelectionChanged() {
    if (!selectionChanged_.HasSubscribers()) return;
    SelectionChangedArgs args{SelectionStart(), SelectionLength()};
    selectionChanged_.Raise(*this, args);
}

void RichTextBox::Changed() {
    Refresh();
    // The full text is only materialised when somebody is listening: PlainText()
    // rebuilds the cache from the paragraph vector, so an unsubscribed editor pays
    // nothing per keystroke. A subscriber pays one copy per edit, which is the honest
    // cost of asking for the whole document on every change (same trade as TextArea).
    if (textChanged_.HasSubscribers()) {
        std::wstring snapshot = PlainText();
        textChanged_.Raise(*this, snapshot);
    }
    // An edit also moves the caret (insert advances it, delete pulls it back), so the
    // selection genuinely changed too — a toolbar tracking the style at the caret has
    // to update after typing, not only after a click.
    RaiseSelectionChanged();
}

void RichTextBox::SelectionMoved() {
    Refresh();
    RaiseSelectionChanged();
}

// --- Multi-click selection -------------------------------------------------

bool RichTextBox::ApplyMultiClickSelection(UINT32 index, int clickCount) {
    if (clickCount < 2) return false;
    
    const std::wstring text = PlainText();
    if (text.empty() || index > text.size()) return false;
    
    UINT32 lo = index, hi = index;
    
    if (clickCount == 2) {
        // Double-click: select word
        dragGranularity_ = SelectGranularity::Word;
        auto [start, end] = WordRangeAt(text, index);
        lo = static_cast<UINT32>(start);
        hi = static_cast<UINT32>(end);
    } else {
        // Triple-click (or more): select logical line (up to '\n')
        dragGranularity_ = SelectGranularity::Line;
        auto [start, end] = LogicalLineRangeAt(text, index);
        lo = static_cast<UINT32>(start);
        hi = static_cast<UINT32>(end);
    }
    
    dragStartLo_ = lo;
    dragStartHi_ = hi;
    anchor_ = lo;
    caret_ = hi;
    SelectionMoved();
    return true;
}

void RichTextBox::ExtendDragSelection(UINT32 index) {
    const std::wstring text = PlainText();
    const UINT32 len = static_cast<UINT32>(text.size());
    if (index > len) index = len;

    // Character granularity is a PLAIN caret move: the anchor stays where the press put
    // it and the caret follows the pointer. It must NOT consult dragStartLo_/Hi_ — those
    // describe the last word/line gesture, and merging with them is what made a fresh
    // drag re-anchor to a previously double-clicked range.
    if (dragGranularity_ == SelectGranularity::Character) {
        caret_ = index;
        SelectionMoved();
        return;
    }

    if (text.empty()) return;

    // Snap the pointer end outward to a whole unit, then pin whichever edge of the
    // ORIGINAL unit is on the far side. That is what keeps the initially clicked
    // word/line fully selected no matter which way the drag goes, and it preserves
    // which END the caret is on (so a backward drag reads as backward).
    UINT32 lo = index, hi = index;
    if (dragGranularity_ == SelectGranularity::Word) {
        auto [s, e] = WordRangeAt(text, index);
        lo = static_cast<UINT32>(s);
        hi = static_cast<UINT32>(e);
        // A click exactly on a newline yields an empty range; fall back to the raw
        // index so the drag tracks the pointer instead of freezing.
        if (lo == hi) { lo = hi = index; }
    } else {
        auto [s, e] = LogicalLineRangeAt(text, index);
        lo = static_cast<UINT32>(s);
        hi = static_cast<UINT32>(e);
    }

    if (hi > dragStartHi_) {          // dragging forward
        anchor_ = dragStartLo_;
        caret_ = hi;
    } else if (lo < dragStartLo_) {   // dragging backward
        anchor_ = dragStartHi_;
        caret_ = lo;
    } else {                          // still inside the original unit
        anchor_ = dragStartLo_;
        caret_ = dragStartHi_;
    }
    SelectionMoved();
}

// --- Clipboard -------------------------------------------------------------

std::wstring RichTextBox::SelectedText() const {
    if (!HasSelection()) return {};
    return PlainText().substr(SelectionStart(), SelectionLength());
}

void RichTextBox::Copy() {
    if (!HasSelection()) return;
    const std::wstring text = SelectedText();

    HWND owner = ClipboardOwner();
    const SIZE_T bytes = (text.size() + 1) * sizeof(wchar_t);
    HGLOBAL mem = GlobalAlloc(GMEM_MOVEABLE, bytes);
    if (!mem) return;
    void* dst = GlobalLock(mem);
    if (!dst) { GlobalFree(mem); return; }
    memcpy(dst, text.c_str(), bytes);
    GlobalUnlock(mem);

    if (!OpenClipboard(owner)) { GlobalFree(mem); return; }
    EmptyClipboard();
    if (!SetClipboardData(CF_UNICODETEXT, mem)) {
        CloseClipboard();
        GlobalFree(mem);
        return;
    }
    CloseClipboard();
}

void RichTextBox::Cut() {
    // Copy still happens in read-only mode (that is Ctrl+C's job), but the delete
    // half does not — so a read-only Cut degrades to a Copy rather than being a
    // silent no-op the user reads as "the clipboard is broken".
    Copy();
    if (readOnly_) return;
    PushUndo();
    DeleteSelection();
    Changed();
}

void RichTextBox::Paste() {
    if (readOnly_) return;
    HWND owner = ClipboardOwner();
    if (!OpenClipboard(owner)) return;
    HANDLE hData = GetClipboardData(CF_UNICODETEXT);
    if (!hData) { CloseClipboard(); return; }
    const wchar_t* text = static_cast<const wchar_t*>(GlobalLock(hData));
    if (text) {
        InsertText(text);
        GlobalUnlock(hData);
    }
    CloseClipboard();
}

HWND RichTextBox::ClipboardOwner() const {
    // Context().hwnd, not GetActiveWindow(): an inactive app or an open dialog
    // makes GetActiveWindow return the wrong window (or null), and then the
    // clipboard has no owner to receive WM_DESTROYCLIPBOARD.
    const UIContext& ctx = Context();
    return ctx.hwnd ? ctx.hwnd : GetActiveWindow();
}

// --- Input -----------------------------------------------------------------

void RichTextBox::OnTextInput(wchar_t ch) {
    if (ch < 32 && ch != '\t' && ch != '\r' && ch != '\n') return;
    if (ch == '\r') ch = '\n';
    InsertText(std::wstring(1, ch));
}

void RichTextBox::OnKeyDownRouted(KeyEventArgs& e) {
    const bool ctrl = (e.modifiers & ModifierKeys::Ctrl) == ModifierKeys::Ctrl;
    const bool shift = (e.modifiers & ModifierKeys::Shift) == ModifierKeys::Shift;

    if (e.vk == VK_BACK) {
        Backspace();
        e.handled = true;
        return;
    }
    if (e.vk == VK_DELETE) {
        Delete();
        e.handled = true;
        return;
    }
    if (ctrl && e.vk == 'C') {
        Copy();
        e.handled = true;
        return;
    }
    if (ctrl && e.vk == 'X') {
        Cut();
        e.handled = true;
        return;
    }
    if (ctrl && e.vk == 'V') {
        Paste();
        e.handled = true;
        return;
    }
    if (ctrl && e.vk == 'A') {
        SelectAll();
        e.handled = true;
        return;
    }
    if (ctrl && e.vk == 'Z') {
        Undo();
        e.handled = true;
        return;
    }
    if (ctrl && e.vk == 'Y') {
        Redo();
        e.handled = true;
        return;
    }
    if (e.vk == VK_LEFT) {
        if (caret_ > 0) MoveCaret(caret_ - 1, shift);
        e.handled = true;
        return;
    }
    if (e.vk == VK_RIGHT) {
        MoveCaret(caret_ + 1, shift);
        e.handled = true;
        return;
    }
    // Home / End move within the VISUAL line, not the document: with wrapping on,
    // one paragraph is several visual lines and jumping to the document start from
    // the middle of a wrapped paragraph is not what Home means anywhere else.
    if (e.vk == VK_HOME) {
        MoveCaret(ctrl ? 0 : LineStart(caret_), shift);
        e.handled = true;
        return;
    }
    if (e.vk == VK_END) {
        MoveCaret(ctrl ? static_cast<UINT32>(PlainText().size()) : LineEnd(caret_), shift);
        e.handled = true;
        return;
    }
    // Up / Down keep the caret's X and move one line box vertically, so walking
    // down a column of text does not drift left or right.
    if (e.vk == VK_UP) {
        MoveCaret(CaretLineStep(-1), shift);
        e.handled = true;
        return;
    }
    if (e.vk == VK_DOWN) {
        MoveCaret(CaretLineStep(+1), shift);
        e.handled = true;
        return;
    }
}

// --- Line-aware navigation -------------------------------------------------
//
// All three helpers work off the layout's line metrics rather than scanning for
// '\n', because with wrapping the visual lines a user sees are not the newlines
// in the text.

bool RichTextBox::LineRangeAt(UINT32 pos, UINT32& outStart, UINT32& outEnd) const {
    EnsureLayoutUpToDate();
    if (paragraphLayouts_.empty()) return false;

    const auto& paragraphs = doc_.Paragraphs();
    const UINT32 textLen = static_cast<UINT32>(PlainText().size());
    pos = std::min(pos, textLen);

    // Find which paragraph contains pos.
    UINT32 docOffset = 0;
    for (size_t paraIdx = 0; paraIdx < paragraphLayouts_.size(); ++paraIdx) {
        UINT32 paraLength = 0;
        if (paraIdx < paragraphs.size()) {
            for (const auto& run : paragraphs[paraIdx].runs) {
                paraLength += static_cast<UINT32>(run.text.size());
            }
        }

        if (pos <= docOffset + paraLength) {
            const auto& paraInfo = paragraphLayouts_[paraIdx];
            if (!paraInfo.layout) {
                outStart = docOffset;
                outEnd = docOffset + paraLength;
                return true;
            }

            // Get line metrics for this paragraph.
            UINT32 lineCount = 0;
            if (FAILED(paraInfo.layout->GetLineMetrics(nullptr, 0, &lineCount)) || lineCount == 0)
                return false;

            std::vector<DWRITE_LINE_METRICS> lines(lineCount);
            if (FAILED(paraInfo.layout->GetLineMetrics(lines.data(), lineCount, &lineCount)))
                return false;

            const UINT32 posInPara = pos - docOffset;
            UINT32 lineStart = 0;
            for (UINT32 i = 0; i < lineCount; ++i) {
                const UINT32 lineLen = lines[i].length;
                const UINT32 lineEnd = lineStart + lineLen;

                if (posInPara < lineEnd || i == lineCount - 1) {
                    outStart = docOffset + lineStart;
                    outEnd = docOffset + lineEnd - lines[i].newlineLength;
                    return true;
                }
                lineStart = lineEnd;
            }
            return false;
        }

        docOffset += paraLength + 1;  // +1 for '\n' separator
    }

    return false;
}

UINT32 RichTextBox::LineStart(UINT32 pos) const {
    UINT32 s = 0, e = 0;
    return LineRangeAt(pos, s, e) ? s : 0;
}

UINT32 RichTextBox::LineEnd(UINT32 pos) const {
    UINT32 s = 0, e = 0;
    return LineRangeAt(pos, s, e) ? e : static_cast<UINT32>(PlainText().size());
}

UINT32 RichTextBox::CaretLineStep(int delta) const {
    EnsureLayoutUpToDate();
    if (paragraphLayouts_.empty()) return caret_;

    const auto& paragraphs = doc_.Paragraphs();

    // Find which paragraph contains the caret.
    UINT32 docOffset = 0;
    for (size_t paraIdx = 0; paraIdx < paragraphLayouts_.size(); ++paraIdx) {
        UINT32 paraLength = 0;
        if (paraIdx < paragraphs.size()) {
            for (const auto& run : paragraphs[paraIdx].runs) {
                paraLength += static_cast<UINT32>(run.text.size());
            }
        }

        if (caret_ <= docOffset + paraLength) {
            const auto& paraInfo = paragraphLayouts_[paraIdx];
            if (!paraInfo.layout) return caret_;

            const UINT32 caretInPara = caret_ - docOffset;

            // Where the caret is now, in paragraph-local coordinates.
            float caretX = 0.0f, caretY = 0.0f;
            DWRITE_HIT_TEST_METRICS htm{};
            if (FAILED(paraInfo.layout->HitTestTextPosition(caretInPara, FALSE, &caretX, &caretY, &htm)))
                return caret_;

            // Calculate the paragraph's Y offset in document space.
            float paraYOffset = 0.0f;
            for (size_t i = 0; i < paraIdx; ++i) {
                paraYOffset += paragraphLayouts_[i].spaceBefore;
                DWRITE_TEXT_METRICS tm{};
                if (paragraphLayouts_[i].layout &&
                    SUCCEEDED(paragraphLayouts_[i].layout->GetMetrics(&tm))) {
                    paraYOffset += tm.height;
                }
                paraYOffset += paragraphLayouts_[i].spaceAfter;
            }
            paraYOffset += paraInfo.spaceBefore;

            // Probe one line box up or down from the caret's own line, at the same X.
            const float probeYInPara = caretY + (delta < 0 ? -1.0f : htm.height + 1.0f);
            const float probeYGlobal = paraYOffset + probeYInPara;

            // If moving up and we're already at the first line of the first paragraph,
            // or moving down and we're past the last paragraph, clamp.
            if (probeYGlobal < 0.0f) return 0;

            // Find which paragraph contains the probe Y.
            float scanY = 0.0f;
            UINT32 scanOffset = 0;
            for (size_t scanIdx = 0; scanIdx < paragraphLayouts_.size(); ++scanIdx) {
                const auto& scanInfo = paragraphLayouts_[scanIdx];
                UINT32 scanLength = 0;
                if (scanIdx < paragraphs.size()) {
                    for (const auto& run : paragraphs[scanIdx].runs) {
                        scanLength += static_cast<UINT32>(run.text.size());
                    }
                }

                scanY += scanInfo.spaceBefore;
                float scanHeight = 0.0f;
                if (scanInfo.layout) {
                    DWRITE_TEXT_METRICS tm{};
                    if (SUCCEEDED(scanInfo.layout->GetMetrics(&tm))) {
                        scanHeight = tm.height;
                    }
                }

                const bool isLastPara = (scanIdx == paragraphLayouts_.size() - 1);
                if (probeYGlobal < scanY + scanHeight || isLastPara) {
                    if (!scanInfo.layout) return scanOffset;

                    const float probeYInScan = probeYGlobal - scanY;
                    BOOL trailing = FALSE, inside = FALSE;
                    DWRITE_HIT_TEST_METRICS hit{};
                    if (FAILED(scanInfo.layout->HitTestPoint(caretX, probeYInScan, &trailing, &inside, &hit)))
                        return caret_;

                    UINT32 posInScan = hit.textPosition + (trailing ? 1 : 0);
                    return scanOffset + std::min(posInScan, scanLength);
                }

                scanY += scanHeight + scanInfo.spaceAfter;
                scanOffset += scanLength + 1;
            }

            // Fallback: at the end of the document.
            return static_cast<UINT32>(PlainText().size());
        }

        docOffset += paraLength + 1;
    }

    return caret_;
}

void RichTextBox::OnPointerPressed(PointerEventArgs& e) {
    if (!bounds_.contains(e.position.x, e.position.y)) return;

    const float scrollY = scrollViewer_->Offset();

    // Middle-button: toggle autoscroll if there's anywhere to scroll.
    if (e.button == PointerButton::Middle) {
        if (scrollViewer_->IsAutoscrolling()) {
            scrollViewer_->EndAutoscroll();
        } else if (scrollViewer_->MaxOffset() > 0.0f || scrollViewer_->MaxOffsetX() > 0.0f) {
            scrollViewer_->BeginAutoscroll(e.position.x, e.position.y);
        }
        e.handled = true;
        return;
    }

    // Left-button press while autoscrolling: cancel autoscroll first.
    if (e.button == PointerButton::Left && scrollViewer_->IsAutoscrolling()) {
        scrollViewer_->EndAutoscroll();
        // Fall through to normal left-press handling.
    }

    // Check if the press is on the vertical scrollbar.
    if (scrollViewer_->HitBarRegion(e.position.x, e.position.y)) {
        if (!scrollViewer_->HitThumb(e.position.x, e.position.y)) {
            // Off-thumb: centre the thumb on the pointer first.
            const RectDip thumb = scrollViewer_->ThumbRect();
            const float trackRange = std::max(1.0f, bounds_.h - thumb.h);
            const float wantTop = (e.position.y - thumb.h * 0.5f) - bounds_.y;
            scrollViewer_->SetOffset(wantTop / trackRange * scrollViewer_->MaxOffset());
        }
        scrollViewer_->BeginDrag(e.position.y);
        if (Context().input)
            Context().input->CapturePointer(this);
        e.handled = true;
        Invalidate();
        return;
    }

    // Check if the press is on the horizontal scrollbar.
    if (scrollViewer_->HitHBarRegion(e.position.x, e.position.y)) {
        if (!scrollViewer_->HitHThumb(e.position.x, e.position.y)) {
            // Off-thumb: centre the thumb on the pointer first.
            const RectDip hthumb = scrollViewer_->HThumbRect();
            const float trackRange = std::max(1.0f, bounds_.w - hthumb.w);
            const float wantLeft = (e.position.x - hthumb.w * 0.5f) - bounds_.x;
            scrollViewer_->SetOffsetX(wantLeft / trackRange * scrollViewer_->MaxOffsetX());
        }
        scrollViewer_->BeginHDrag(e.position.x);
        if (Context().input)
            Context().input->CapturePointer(this);
        e.handled = true;
        Invalidate();
        return;
    }

    // Text click: position the caret.
    const float contentX = e.position.x - bounds_.x - kPadding + scrollViewer_->OffsetX();
    const float contentY = e.position.y - bounds_.y - kPadding + scrollY;
    const UINT32 index = IndexAtPoint(contentX, contentY);
    const bool shift = (e.modifiers & ModifierKeys::Shift) == ModifierKeys::Shift;
    
    // Shift+click extends from the anchor to the click, keeping anchor_ (works on a
    // bare caret too). SelectionMoved below raises the notification.
    if (shift && e.clickCount <= 1) {
        caret_ = index;
        dragGranularity_ = SelectGranularity::Character;
    }
    // Multi-click selection: double-click selects word, triple-click selects line.
    else if (!ApplyMultiClickSelection(index, e.clickCount)) {
        // Single click: reset to character granularity and place caret.
        dragGranularity_ = SelectGranularity::Character;
        caret_ = index;
        anchor_ = index;
    }
    
    selecting_ = true;
    if (Context().input)
        Context().input->CapturePointer(this);
    // ApplyMultiClickSelection already raised SelectionChanged on the double/triple
    // click path; a single click set caret_/anchor_ directly and still needs it.
    SelectionMoved();
    e.handled = true;
}

void RichTextBox::OnPointerMoved(PointerEventArgs& e) {
    // Hover state serves two consumers: the ScrollViewer, which fades the rail in
    // and expands it to a pill, and Cursor(), which is const and so cannot ask the
    // ScrollViewer directly. Both axes are told every move — passing only the axis
    // under the pointer would leave the other rail stuck in its expanded state.
    pointerOverVBar_ = scrollViewer_->HitBarRegion(e.position.x, e.position.y);
    pointerOverHBar_ = scrollViewer_->HitHBarRegion(e.position.x, e.position.y);
    scrollViewer_->SetBarHover(pointerOverVBar_);
    scrollViewer_->SetHBarHover(pointerOverHBar_);

    if (scrollViewer_->IsAutoscrolling()) {
        scrollViewer_->UpdateAutoscrollVelocity(e.position.x, e.position.y);
        e.handled = true;
        return;
    }

    if (scrollViewer_->IsDragging()) {
        scrollViewer_->DragTo(e.position.y);
        Invalidate();
        e.handled = true;
        return;
    }

    if (scrollViewer_->IsHDragging()) {
        scrollViewer_->HDragTo(e.position.x);
        Invalidate();
        e.handled = true;
        return;
    }

    if (selecting_) {
        const float scrollY = scrollViewer_->Offset();
        const float scrollX = scrollViewer_->OffsetX();
        const float contentX = e.position.x - bounds_.x - kPadding + scrollX;
        const float contentY = e.position.y - bounds_.y - kPadding + scrollY;
        const UINT32 index = IndexAtPoint(contentX, contentY);

        // Auto-scroll when the pointer is dragged past the top or bottom edge, so a
        // selection can extend beyond one screenful. The step is proportional to how
        // far outside the pointer is, capped at 20 DIP/frame: a small overshoot
        // creeps, a large one runs — the behaviour every editor has.
        constexpr float kAutoScrollMaxStepDip = 20.0f;
        const float above = bounds_.y - e.position.y;
        const float below = e.position.y - bounds_.bottom();
        if (above > 0.0f || below > 0.0f) {
            const float over = std::max(above, below);
            const float step = std::min(over, kAutoScrollMaxStepDip);
            scrollViewer_->SetOffset(scrollY + (above > 0.0f ? -step : step));
            // Re-resolve against the scrolled view so the selection end lands on the
            // line that just scrolled into view rather than lagging a frame behind.
            const float scrollY2 = scrollViewer_->Offset();
            const float contentY2 = e.position.y - bounds_.y - kPadding + scrollY2;
            const UINT32 index2 = IndexAtPoint(contentX, contentY2);
            ExtendDragSelection(index2);
        } else {
            // Extend selection to the current pointer position, respecting granularity.
            ExtendDragSelection(index);
        }

        // ExtendDragSelection already called SelectionMoved() when it changed the selection.
        // No need to call it again here — that would be a duplicate notification.
        e.handled = true;
    }
}

void RichTextBox::OnPointerReleased(PointerEventArgs& e) {
    if (scrollViewer_->IsDragging()) {
        scrollViewer_->EndDrag();
        if (Context().input)
            Context().input->ReleaseCapture(this);
        Invalidate();
        e.handled = true;
        return;
    }

    if (scrollViewer_->IsHDragging()) {
        scrollViewer_->EndHDrag();
        if (Context().input)
            Context().input->ReleaseCapture(this);
        Invalidate();
        e.handled = true;
        return;
    }

    if (selecting_) {
        selecting_ = false;
        // Reset the drag granularity so the NEXT press starts fresh. Without this a
        // double-click-then-release leaves Word granularity armed, and a later plain
        // drag elsewhere would keep snapping to word boundaries and re-anchoring to
        // the old double-clicked range.
        dragGranularity_ = SelectGranularity::Character;
        if (Context().input)
            Context().input->ReleaseCapture(this);
        e.handled = true;
    }
}

void RichTextBox::OnPointerWheelChanged(PointerEventArgs& e) {
    if (!bounds_.contains(e.position.x, e.position.y)) return;
    // Only scroll if content actually overflows (prevents phantom scrollbar when
    // the text fits comfortably in one screen).
    if (scrollViewer_->MaxOffset() > 0.0f || scrollViewer_->MaxOffsetX() > 0.0f) {
        scrollViewer_->AnimateBy(-e.wheelDelta * kWheelStepDip / 120.0f);
        Invalidate();
    }
    e.handled = true;
}

void RichTextBox::OnPointerLeft() {
    pointerOverVBar_ = false;
    pointerOverHBar_ = false;
    // Tell the ScrollViewer too, or a rail expanded on the way out stays expanded
    // and never starts its idle fade.
    scrollViewer_->SetBarHover(false);
    scrollViewer_->SetHBarHover(false);
    Invalidate();
}

void RichTextBox::OnFocusChanged() {
    Invalidate();
}

HCURSOR RichTextBox::Cursor() const {
    // Autoscroll: direction-indicating arrow.
    if (scrollViewer_->IsAutoscrolling()) {
        POINT pt;
        if (GetCursorPos(&pt) && Context().hwnd) {
            ScreenToClient(Context().hwnd, &pt);
            const float dpiScale = Context().dpiScale;
            const float dipX = pt.x / dpiScale;
            const float dipY = pt.y / dpiScale;
            return scrollViewer_->AutoscrollCursor(dipX, dipY);
        }
        static HCURSOR sizeAll = LoadCursor(nullptr, IDC_SIZEALL);
        return sizeAll;
    }
    // Over a scrollbar track: system default (arrow).
    if (pointerOverVBar_ || pointerOverHBar_)
        return nullptr;
    // Over text: I-beam.
    return LoadCursorW(nullptr, IDC_IBEAM);
}

// --- Hit testing -----------------------------------------------------------

UINT32 RichTextBox::IndexAtPoint(float dipX, float dipY) const {
    EnsureLayoutUpToDate();

    if (paragraphLayouts_.empty()) return 0;

    const auto& paragraphs = doc_.Paragraphs();

    // Walk paragraphs top to bottom, finding which one contains dipY.
    // Each paragraph occupies [paraTop, paraTop + height), with spaceBefore/After gaps.
    float paraTop = 0.0f;
    UINT32 docOffset = 0;

    for (size_t paraIdx = 0; paraIdx < paragraphLayouts_.size(); ++paraIdx) {
        const auto& paraInfo = paragraphLayouts_[paraIdx];

        // Calculate this paragraph's length in document offsets.
        UINT32 paraLength = 0;
        if (paraIdx < paragraphs.size()) {
            for (const auto& run : paragraphs[paraIdx].runs) {
                paraLength += static_cast<UINT32>(run.text.size());
            }
        }

        paraTop += paraInfo.spaceBefore;

        float paraHeight = 0.0f;
        if (paraInfo.layout) {
            DWRITE_TEXT_METRICS tm{};
            if (SUCCEEDED(paraInfo.layout->GetMetrics(&tm))) {
                paraHeight = tm.height;
            }
        }

        // Is the click within this paragraph's vertical band?
        // For the last paragraph, extend the band downward so clicks below the text
        // still land at the end rather than returning 0.
        const bool isLastPara = (paraIdx == paragraphLayouts_.size() - 1);
        if (dipY < paraTop + paraHeight || isLastPara) {
            if (!paraInfo.layout) {
                return docOffset;
            }

            // Convert to paragraph-relative Y coordinate.
            const float paraRelativeY = dipY - paraTop;

            BOOL isTrailingHit = FALSE;
            BOOL isInside = FALSE;
            DWRITE_HIT_TEST_METRICS htm{};
            if (SUCCEEDED(paraInfo.layout->HitTestPoint(dipX, paraRelativeY,
                                                        &isTrailingHit, &isInside, &htm))) {
                UINT32 posInPara = htm.textPosition + (isTrailingHit ? 1 : 0);

                // Adjust for composition if it's in this paragraph: the layout string
                // is longer than the document, so a hit past the composition needs to
                // be pulled back by the composition length.
                if (!composition_.empty() && caret_ >= docOffset && caret_ <= docOffset + paraLength) {
                    const UINT32 caretInPara = caret_ - docOffset;
                    const UINT32 compLen = static_cast<UINT32>(composition_.size());
                    if (posInPara > caretInPara + compLen) {
                        posInPara -= compLen;
                    } else if (posInPara > caretInPara) {
                        posInPara = caretInPara;
                    }
                }

                return docOffset + std::min(posInPara, paraLength);
            }

            return docOffset;
        }

        paraTop += paraHeight + paraInfo.spaceAfter;
        docOffset += paraLength + 1;  // +1 for the '\n' separator
    }

    // Fallback: clamp to document length.
    return static_cast<UINT32>(PlainText().size());
}

// ══════════════════════════════════════════════════════════════════════════════
// IME
// ══════════════════════════════════════════════════════════════════════════════

void RichTextBox::OnImeStartComposition(HWND) {
    composition_.clear();
    layoutDirty_ = true;
    Invalidate();
}

void RichTextBox::OnImeComposition(HWND hwnd, LPARAM flags) {
    HIMC himc = ImmGetContext(hwnd);
    if (!himc) return;

    if (flags & GCS_RESULTSTR) {
        LONG bytes = ImmGetCompositionStringW(himc, GCS_RESULTSTR, nullptr, 0);
        if (bytes > 0) {
            std::wstring result(bytes / sizeof(wchar_t), L'\0');
            ImmGetCompositionStringW(himc, GCS_RESULTSTR, result.data(), bytes);
            composition_.clear();
            InsertText(result);
        }
    }
    if (flags & GCS_COMPSTR) {
        LONG bytes = ImmGetCompositionStringW(himc, GCS_COMPSTR, nullptr, 0);
        composition_.assign(std::max<LONG>(0, bytes) / sizeof(wchar_t), L'\0');
        if (bytes > 0)
            ImmGetCompositionStringW(himc, GCS_COMPSTR, composition_.data(), bytes);
    }
    ImmReleaseContext(hwnd, himc);

    // GCS_RESULTSTR went through InsertText, which already raised Changed().
    // What is left here is the composition-string update: the document is
    // unchanged, so only the layout text is dirty.
    layoutDirty_ = true;
    EnsureCaretVisible();
    Invalidate();
}

void RichTextBox::OnImeEndComposition(HWND) {
    composition_.clear();
    // Only the composition went away; text_ is whatever InsertText committed.
    layoutDirty_ = true;
    Invalidate();
}

} // namespace fluent
