#include "RichTextDocument.h"
#include <algorithm>
#include <cmath>
#include <numeric>

namespace fluent {

bool TextRun::StyleEquals(const TextRun& other) const
{
    // Compare every attribute except text.
    // Colors are floats, so use a small epsilon rather than exact equality.
    constexpr float epsilon = 0.01f;
    return weight == other.weight
        && underline == other.underline
        && std::abs(fontSize - other.fontSize) < epsilon
        && std::abs(color.r - other.color.r) < epsilon
        && std::abs(color.g - other.color.g) < epsilon
        && std::abs(color.b - other.color.b) < epsilon
        && std::abs(color.a - other.color.a) < epsilon;
}

// ============================================================================
// ParagraphStyle
// ============================================================================

bool ParagraphStyle::StyleEquals(const ParagraphStyle& other) const
{
    constexpr float epsilon = 0.01f;
    return alignment == other.alignment
        && lineSpacingMode == other.lineSpacingMode
        && std::abs(lineSpacing - other.lineSpacing) < epsilon
        && std::abs(spaceBefore - other.spaceBefore) < epsilon
        && std::abs(spaceAfter - other.spaceAfter) < epsilon
        && std::abs(firstLineIndent - other.firstLineIndent) < epsilon
        && std::abs(leftIndent - other.leftIndent) < epsilon
        && std::abs(rightIndent - other.rightIndent) < epsilon;
}

// ============================================================================
// Paragraph
// ============================================================================

size_t Paragraph::Length() const
{
    return std::accumulate(runs.begin(), runs.end(), size_t{0},
                           [](size_t sum, const TextRun& r) { return sum + r.text.size(); });
}

std::wstring Paragraph::GetText() const
{
    std::wstring result;
    result.reserve(Length());
    for (const auto& r : runs)
        result += r.text;
    return result;
}

bool Paragraph::Empty() const
{
    return runs.empty();
}

// ============================================================================
// RichTextDocument - Constructor & Query
// ============================================================================

RichTextDocument::RichTextDocument()
{
    // paragraphs_ is never empty: an empty document is one paragraph with zero runs.
    paragraphs_.emplace_back();
}

bool RichTextDocument::Empty() const
{
    return paragraphs_.size() == 1 && paragraphs_[0].Empty();
}

size_t RichTextDocument::Length() const
{
    if (paragraphs_.empty())
        return 0;

    // Sum all paragraph lengths, plus one newline separator per paragraph except the last.
    size_t total = 0;
    for (size_t i = 0; i < paragraphs_.size(); ++i) {
        total += paragraphs_[i].Length();
        if (i + 1 < paragraphs_.size())
            total += 1;  // newline separator
    }
    return total;
}

std::wstring RichTextDocument::GetPlainText() const
{
    std::wstring result;
    result.reserve(Length());

    for (size_t i = 0; i < paragraphs_.size(); ++i) {
        result += paragraphs_[i].GetText();
        if (i + 1 < paragraphs_.size())
            result += L'\n';
    }
    return result;
}

ParagraphLocation RichTextDocument::GetParagraphAt(size_t offset) const
{
    if (paragraphs_.empty())
        return {false, 0, 0};

    // Walk paragraphs, accumulating offsets. Each paragraph contributes its length
    // plus one newline separator (except the last paragraph has no trailing newline).
    size_t pos = 0;
    for (size_t i = 0; i < paragraphs_.size(); ++i) {
        const size_t paraLen = paragraphs_[i].Length();
        const size_t nextPos = pos + paraLen + (i + 1 < paragraphs_.size() ? 1 : 0);

        // offset in [pos, pos + paraLen) is inside this paragraph.
        // offset == pos + paraLen is the newline separator ending this paragraph.
        // When exactly on the newline, RETURN THE PARAGRAPH ENDING THERE — that is
        // the deliberate tie-break so "current paragraph style" at the caret means
        // the paragraph the user is typing in, not the one they have not entered yet.
        if (offset <= pos + paraLen) {
            return {true, i, offset - pos};
        }

        pos = nextPos;
    }

    // offset == Length() is valid (caret one past the end).
    if (offset == Length()) {
        const size_t last = paragraphs_.size() - 1;
        return {true, last, paragraphs_[last].Length()};
    }

    return {false, 0, 0};
}

RunLocation RichTextDocument::GetRunAt(size_t offset) const
{
    // Find the paragraph first.
    const ParagraphLocation pLoc = GetParagraphAt(offset);
    if (!pLoc.valid)
        return {false, 0, 0};

    const Paragraph& para = paragraphs_[pLoc.paragraphIndex];
    if (para.runs.empty()) {
        // Empty paragraph: offset must be 0 (the paragraph itself is empty).
        if (pLoc.offsetInParagraph == 0)
            return {false, 0, 0};  // no runs, so invalid
        return {false, 0, 0};
    }

    // Walk runs within this paragraph.
    size_t pos = 0;
    for (size_t i = 0; i < para.runs.size(); ++i) {
        const size_t runLen = para.runs[i].text.size();

        // offset in [pos, pos + runLen) is inside this run.
        if (offset >= pos && offset < pos + runLen) {
            return {true, i, pLoc.offsetInParagraph - pos};
        }

        // Boundary case: offset == pos + runLen (exactly at the end of this run).
        // OLD convention (carried over from flat runs_): return the NEXT run so
        // typing at the end of a bold word stays bold. If this is the last run,
        // return this run with offsetInRun == runLen.
        if (pLoc.offsetInParagraph == pos + runLen) {
            if (i + 1 < para.runs.size()) {
                return {true, i + 1, 0};
            }
            return {true, i, runLen};
        }

        pos += runLen;
    }

    // offset one past the last run (caret at the end of the paragraph).
    if (pLoc.offsetInParagraph == para.Length()) {
        const size_t last = para.runs.size() - 1;
        return {true, last, para.runs[last].text.size()};
    }

    return {false, 0, 0};
}

TextRun RichTextDocument::GetStyleAt(size_t offset) const
{
    const RunLocation loc = GetRunAt(offset);
    if (!loc.valid || paragraphs_.empty())
        return TextRun{};

    // GetRunAt already resolved which paragraph, but we need to recover it.
    const ParagraphLocation pLoc = GetParagraphAt(offset);
    if (!pLoc.valid)
        return TextRun{};

    const Paragraph& para = paragraphs_[pLoc.paragraphIndex];
    if (loc.runIndex >= para.runs.size())
        return TextRun{};

    return para.runs[loc.runIndex];
}

ParagraphStyle RichTextDocument::GetParagraphStyleAt(size_t offset) const
{
    const ParagraphLocation loc = GetParagraphAt(offset);
    if (!loc.valid)
        return ParagraphStyle{};
    return paragraphs_[loc.paragraphIndex].style;
}

// ============================================================================
// RichTextDocument - Edit: InsertText
// ============================================================================

void RichTextDocument::InsertText(size_t offset, const std::wstring& text,
                                   const TextRun& style)
{
    if (text.empty())
        return;

    // Clamp offset to valid range.
    if (offset > Length())
        offset = Length();

    // Check if text contains newlines — if so, we need to split paragraphs.
    const bool hasNewline = text.find(L'\n') != std::wstring::npos;

    if (!hasNewline) {
        // Simple case: no newlines, insert within one paragraph.
        InsertTextNoParagraphSplit(offset, text, style);
        return;
    }

    // Complex case: text contains '\n' → split into multiple paragraphs.
    // Strategy:
    // 1. Find the paragraph containing offset.
    // 2. Split that paragraph at offset.
    // 3. Parse text into segments separated by '\n'.
    // 4. First segment appends to the split left half.
    // 5. Middle segments become new paragraphs.
    // 6. Last segment prepends to the split right half.

    ParagraphLocation pLoc = GetParagraphAt(offset);
    if (!pLoc.valid) {
        // Should not happen after clamping, but handle gracefully.
        TextRun r = style;
        r.text = text;
        Paragraph p;
        p.runs.push_back(r);
        paragraphs_.push_back(std::move(p));
        return;
    }

    Paragraph& targetPara = paragraphs_[pLoc.paragraphIndex];
    const ParagraphStyle inheritedStyle = targetPara.style;

    // Split targetPara at offsetInParagraph into leftRuns + rightRuns.
    std::vector<TextRun> leftRuns, rightRuns;
    size_t pos = 0;
    for (auto& run : targetPara.runs) {
        const size_t runLen = run.text.size();
        if (pos + runLen <= pLoc.offsetInParagraph) {
            leftRuns.push_back(run);
        } else if (pos >= pLoc.offsetInParagraph) {
            rightRuns.push_back(run);
        } else {
            // Split this run.
            const size_t splitAt = pLoc.offsetInParagraph - pos;
            TextRun left = run, right = run;
            left.text = run.text.substr(0, splitAt);
            right.text = run.text.substr(splitAt);
            leftRuns.push_back(left);
            rightRuns.push_back(right);
        }
        pos += runLen;
    }

    // Parse text into segments by '\n'.
    std::vector<std::wstring> segments;
    size_t start = 0;
    while (start < text.size()) {
        size_t end = text.find(L'\n', start);
        if (end == std::wstring::npos) {
            segments.push_back(text.substr(start));
            break;
        }
        segments.push_back(text.substr(start, end - start));
        start = end + 1;
    }
    if (text.back() == L'\n')
        segments.push_back(L"");  // trailing newline creates an empty segment

    // Build new paragraphs.
    std::vector<Paragraph> newParas;

    // First segment: append to leftRuns.
    {
        Paragraph p;
        p.style = inheritedStyle;
        p.runs = leftRuns;
        TextRun r = style;
        r.text = segments[0];
        p.runs.push_back(r);
        newParas.push_back(std::move(p));
    }

    // Middle segments: each becomes a new paragraph.
    for (size_t i = 1; i < segments.size() - 1; ++i) {
        Paragraph p;
        p.style = inheritedStyle;
        TextRun r = style;
        r.text = segments[i];
        p.runs.push_back(r);
        newParas.push_back(std::move(p));
    }

    // Last segment: prepend to rightRuns.
    if (segments.size() > 1) {
        Paragraph p;
        p.style = inheritedStyle;
        TextRun r = style;
        r.text = segments.back();
        if (!r.text.empty())
            p.runs.push_back(r);
        p.runs.insert(p.runs.end(), rightRuns.begin(), rightRuns.end());
        newParas.push_back(std::move(p));
    }

    // Replace targetPara with newParas.
    paragraphs_.erase(paragraphs_.begin() + pLoc.paragraphIndex);
    paragraphs_.insert(paragraphs_.begin() + pLoc.paragraphIndex,
                       newParas.begin(), newParas.end());

    MergeAdjacentRuns();
}

// Helper: insert text that contains no newlines (no paragraph split needed).
void RichTextDocument::InsertTextNoParagraphSplit(size_t offset,
                                                   const std::wstring& text,
                                                   const TextRun& style)
{
    ParagraphLocation pLoc = GetParagraphAt(offset);
    if (!pLoc.valid) {
        // Should not happen after clamping, but create a paragraph.
        Paragraph p;
        TextRun r = style;
        r.text = text;
        p.runs.push_back(r);
        paragraphs_.push_back(std::move(p));
        return;
    }

    Paragraph& targetPara = paragraphs_[pLoc.paragraphIndex];

    if (targetPara.runs.empty()) {
        // First insertion into empty paragraph.
        TextRun r = style;
        r.text = text;
        targetPara.runs.push_back(std::move(r));
        return;
    }

    // Find the run containing offsetInParagraph.
    size_t pos = 0;
    for (size_t i = 0; i < targetPara.runs.size(); ++i) {
        TextRun& run = targetPara.runs[i];
        const size_t runLen = run.text.size();

        if (pos + runLen >= pLoc.offsetInParagraph || i + 1 == targetPara.runs.size()) {
            // This run contains the offset or is the last run.
            if (run.StyleEquals(style)) {
                // Same style: just insert text into this run.
                const size_t insertAt = (pLoc.offsetInParagraph > pos) ? (pLoc.offsetInParagraph - pos) : 0;
                run.text.insert(std::min(insertAt, run.text.size()), text);
            } else {
                // Different style: split this run.
                const size_t splitAt = (pLoc.offsetInParagraph > pos) ? (pLoc.offsetInParagraph - pos) : 0;
                if (splitAt == 0) {
                    // Insert before this run.
                    TextRun newRun = style;
                    newRun.text = text;
                    targetPara.runs.insert(targetPara.runs.begin() + i, std::move(newRun));
                } else if (splitAt >= runLen) {
                    // Insert after this run.
                    TextRun newRun = style;
                    newRun.text = text;
                    targetPara.runs.insert(targetPara.runs.begin() + i + 1, std::move(newRun));
                } else {
                    // Split in the middle.
                    TextRun leftRun = run, newRun = style, rightRun = run;
                    leftRun.text = run.text.substr(0, splitAt);
                    newRun.text = text;
                    rightRun.text = run.text.substr(splitAt);

                    targetPara.runs.erase(targetPara.runs.begin() + i);
                    targetPara.runs.insert(targetPara.runs.begin() + i, rightRun);
                    targetPara.runs.insert(targetPara.runs.begin() + i, newRun);
                    targetPara.runs.insert(targetPara.runs.begin() + i, leftRun);
                }
            }
            MergeAdjacentRuns();
            return;
        }

        pos += runLen;
    }

    // Should not reach here.
    MergeAdjacentRuns();
}

// ============================================================================
// RichTextDocument - Edit: DeleteRange
// ============================================================================

void RichTextDocument::DeleteRange(size_t start, size_t length)
{
    if (length == 0 || Empty())
        return;

    // Clamp to document bounds.
    if (start >= Length())
        return;
    if (start + length > Length())
        length = Length() - start;

    const size_t end = start + length;

    // Find start and end paragraphs.
    ParagraphLocation startLoc = GetParagraphAt(start);
    ParagraphLocation endLoc = GetParagraphAt(end);

    if (!startLoc.valid || !endLoc.valid)
        return;

    if (startLoc.paragraphIndex == endLoc.paragraphIndex) {
        // Delete within one paragraph.
        DeleteWithinOneParagraph(startLoc.paragraphIndex, startLoc.offsetInParagraph,
                                 endLoc.offsetInParagraph - startLoc.offsetInParagraph);
    } else {
        // Delete spans multiple paragraphs: merge.
        DeleteAcrossParagraphs(startLoc, endLoc);
    }

    MergeAdjacentRuns();
}

// Helper: delete [start, start+length) within one paragraph.
void RichTextDocument::DeleteWithinOneParagraph(size_t paraIndex, size_t start, size_t length)
{
    if (paraIndex >= paragraphs_.size())
        return;

    Paragraph& para = paragraphs_[paraIndex];
    const size_t end = start + length;

    std::vector<TextRun> newRuns;
    size_t pos = 0;

    for (auto& run : para.runs) {
        const size_t runLen = run.text.size();
        const size_t runEnd = pos + runLen;

        if (runEnd <= start || pos >= end) {
            // Outside the deletion range.
            newRuns.push_back(run);
        } else {
            // Overlaps deletion range.
            const size_t delStart = (start > pos) ? (start - pos) : 0;
            const size_t delEnd = (end < runEnd) ? (end - pos) : runLen;

            TextRun trimmed = run;
            if (delStart == 0 && delEnd >= runLen) {
                // Entire run deleted.
            } else if (delStart == 0) {
                // Delete prefix.
                trimmed.text = run.text.substr(delEnd);
                newRuns.push_back(trimmed);
            } else if (delEnd >= runLen) {
                // Delete suffix.
                trimmed.text = run.text.substr(0, delStart);
                newRuns.push_back(trimmed);
            } else {
                // Delete middle.
                trimmed.text = run.text.substr(0, delStart) + run.text.substr(delEnd);
                newRuns.push_back(trimmed);
            }
        }

        pos += runLen;
    }

    para.runs = std::move(newRuns);

    // If paragraph became empty and it's not the only paragraph, remove it.
    if (para.runs.empty() && paragraphs_.size() > 1) {
        paragraphs_.erase(paragraphs_.begin() + paraIndex);
    }
}

// Helper: delete across paragraphs [startLoc, endLoc).
void RichTextDocument::DeleteAcrossParagraphs(const ParagraphLocation& startLoc,
                                               const ParagraphLocation& endLoc)
{
    // Keep runs before startLoc.offsetInParagraph in startLoc.paragraphIndex.
    // Keep runs after endLoc.offsetInParagraph in endLoc.paragraphIndex.
    // Remove everything in between.

    Paragraph& startPara = paragraphs_[startLoc.paragraphIndex];
    Paragraph& endPara = paragraphs_[endLoc.paragraphIndex];

    // Collect runs to keep from start paragraph.
    std::vector<TextRun> keptRuns;
    size_t pos = 0;
    for (auto& run : startPara.runs) {
        const size_t runLen = run.text.size();
        if (pos + runLen <= startLoc.offsetInParagraph) {
            keptRuns.push_back(run);
        } else if (pos < startLoc.offsetInParagraph) {
            // Partial keep.
            TextRun trimmed = run;
            trimmed.text = run.text.substr(0, startLoc.offsetInParagraph - pos);
            keptRuns.push_back(trimmed);
        }
        pos += runLen;
    }

    // Collect runs to keep from end paragraph.
    pos = 0;
    for (auto& run : endPara.runs) {
        const size_t runLen = run.text.size();
        if (pos >= endLoc.offsetInParagraph) {
            keptRuns.push_back(run);
        } else if (pos + runLen > endLoc.offsetInParagraph) {
            // Partial keep.
            TextRun trimmed = run;
            trimmed.text = run.text.substr(endLoc.offsetInParagraph - pos);
            keptRuns.push_back(trimmed);
        }
        pos += runLen;
    }

    // Merge into startPara, keeping its paragraph style.
    startPara.runs = std::move(keptRuns);

    // Remove paragraphs [startLoc.paragraphIndex + 1, endLoc.paragraphIndex].
    paragraphs_.erase(paragraphs_.begin() + startLoc.paragraphIndex + 1,
                      paragraphs_.begin() + endLoc.paragraphIndex + 1);

    // If merged paragraph is empty and not the only one, remove it.
    if (startPara.runs.empty() && paragraphs_.size() > 1) {
        paragraphs_.erase(paragraphs_.begin() + startLoc.paragraphIndex);
    }

    // Ensure at least one paragraph remains.
    if (paragraphs_.empty()) {
        paragraphs_.emplace_back();
    }
}

// ============================================================================
// RichTextDocument - Edit: ApplyStyle
// ============================================================================

void RichTextDocument::ApplyStyle(size_t start, size_t length,
                                   const std::function<void(TextRun&)>& fn)
{
    if (length == 0 || Empty() || !fn)
        return;

    // Clamp to document bounds.
    if (start >= Length())
        return;
    if (start + length > Length())
        length = Length() - start;

    const size_t end = start + length;

    // Find affected paragraphs.
    ParagraphLocation startLoc = GetParagraphAt(start);
    ParagraphLocation endLoc = GetParagraphAt(end);

    if (!startLoc.valid || !endLoc.valid)
        return;

    // Apply style to each affected paragraph.
    for (size_t pIdx = startLoc.paragraphIndex; pIdx <= endLoc.paragraphIndex; ++pIdx) {
        if (pIdx >= paragraphs_.size())
            break;

        Paragraph& para = paragraphs_[pIdx];

        // Compute the range within this paragraph.
        const size_t paraStart = (pIdx == startLoc.paragraphIndex) ? startLoc.offsetInParagraph : 0;
        const size_t paraEnd = (pIdx == endLoc.paragraphIndex) ? endLoc.offsetInParagraph : para.Length();

        ApplyStyleWithinParagraph(para, paraStart, paraEnd - paraStart, fn);
    }

    MergeAdjacentRuns();
}

// Helper: apply style to [start, start+length) within one paragraph.
void RichTextDocument::ApplyStyleWithinParagraph(Paragraph& para, size_t start,
                                                  size_t length,
                                                  const std::function<void(TextRun&)>& fn)
{
    if (length == 0)
        return;

    const size_t end = start + length;
    std::vector<TextRun> newRuns;
    size_t pos = 0;

    for (auto& run : para.runs) {
        const size_t runLen = run.text.size();
        const size_t runEnd = pos + runLen;

        if (runEnd <= start || pos >= end) {
            // Outside the style range.
            newRuns.push_back(run);
        } else if (pos >= start && runEnd <= end) {
            // Fully inside the style range.
            TextRun styled = run;
            fn(styled);
            newRuns.push_back(styled);
        } else {
            // Partially overlaps: split.
            if (pos < start) {
                // Keep prefix unchanged.
                TextRun prefix = run;
                prefix.text = run.text.substr(0, start - pos);
                newRuns.push_back(prefix);

                // Style the rest.
                TextRun styled = run;
                styled.text = run.text.substr(start - pos, std::min(runLen - (start - pos), end - start));
                fn(styled);
                newRuns.push_back(styled);

                // Keep suffix if any.
                if (runEnd > end) {
                    TextRun suffix = run;
                    suffix.text = run.text.substr(end - pos);
                    newRuns.push_back(suffix);
                }
            } else {
                // pos >= start, but runEnd > end.
                TextRun styled = run;
                styled.text = run.text.substr(0, end - pos);
                fn(styled);
                newRuns.push_back(styled);

                TextRun suffix = run;
                suffix.text = run.text.substr(end - pos);
                newRuns.push_back(suffix);
            }
        }

        pos += runLen;
    }

    para.runs = std::move(newRuns);
}

// ============================================================================
// RichTextDocument - Edit: ApplyParagraphStyle
// ============================================================================

void RichTextDocument::ApplyParagraphStyle(size_t start, size_t length,
                                            const std::function<void(ParagraphStyle&)>& fn)
{
    if (!fn)
        return;

    // start == Length() is NOT out of range here, unlike in the run-level edit
    // paths. It is where the caret sits after SetDocument (and after typing at the
    // end), and it still names a real paragraph: the last one. Rejecting it made
    // every "format the paragraph the caret is in" call a silent no-op whenever the
    // caret had not been moved off the end — `SetParagraphAlignment` on a freshly
    // loaded document did nothing at all, while the same call after SelectAll
    // worked, which is what the caret-only tests caught.
    //
    // Clamp instead: a caret one past the end formats the paragraph it is the end of.
    const size_t docLength = Length();
    if (start > docLength)
        start = docLength;
    if (start + length > docLength)
        length = docLength - start;

    // A zero-length range is still a request to format ONE paragraph — the one
    // containing `start`. That is the bare-caret case, and it has a real target.
    const size_t end = start + length;

    // Find affected paragraphs.
    const ParagraphLocation startLoc = GetParagraphAt(start);
    const ParagraphLocation endLoc = GetParagraphAt(end);

    if (!startLoc.valid || !endLoc.valid)
        return;

    // Apply to each paragraph in range.
    for (size_t pIdx = startLoc.paragraphIndex; pIdx <= endLoc.paragraphIndex; ++pIdx) {
        if (pIdx >= paragraphs_.size())
            break;
        fn(paragraphs_[pIdx].style);
    }
}

// ============================================================================
// RichTextDocument - Edit: AppendRun, Clear
// ============================================================================

void RichTextDocument::AppendRun(const TextRun& run)
{
    if (run.text.empty())
        return;

    if (paragraphs_.empty()) {
        paragraphs_.emplace_back();
    }

    // Split on '\n' rather than storing it.
    //
    // The document's core invariant is that a paragraph separator is IMPLIED by the
    // boundary between paragraphs and is never a character inside a run (see the
    // offset contract on RichTextDocument). Appending the text verbatim breaks that:
    // AppendRun(L"first\n") produced ONE paragraph whose run text contained '\n', so
    // a document built as three AppendRun calls was a single paragraph wearing three
    // paragraphs' text. Every offset past the first separator then resolved to the
    // same paragraph, and per-paragraph formatting silently applied to all of it.
    //
    // Splitting here keeps AppendRun's contract identical to InsertText's: both take
    // text that may contain newlines and produce the paragraph structure that text
    // describes.
    size_t segStart = 0;
    while (segStart < run.text.size()) {
        const size_t nl = run.text.find(L'\n', segStart);
        const size_t segEnd = (nl == std::wstring::npos) ? run.text.size() : nl;

        // Append this segment's text (possibly empty, for a blank line) to the
        // current last paragraph, merging into its last run when the styles match.
        if (segEnd > segStart) {
            TextRun segment = run;
            segment.text = run.text.substr(segStart, segEnd - segStart);

            Paragraph& lastPara = paragraphs_.back();
            if (!lastPara.runs.empty() && lastPara.runs.back().StyleEquals(segment)) {
                lastPara.runs.back().text += segment.text;
            } else {
                lastPara.runs.push_back(std::move(segment));
            }
        }

        if (nl == std::wstring::npos)
            break;

        // The newline itself starts a new paragraph, which inherits the style of the
        // paragraph it was split from — the same rule InsertText follows.
        Paragraph next;
        next.style = paragraphs_.back().style;
        paragraphs_.push_back(std::move(next));

        segStart = nl + 1;
    }
}

void RichTextDocument::Clear()
{
    paragraphs_.clear();
    paragraphs_.emplace_back();  // never empty
}

// ============================================================================
// RichTextDocument - Snapshot / Restore
// ============================================================================

std::vector<TextRun> RichTextDocument::Runs() const
{
    // Flatten all paragraphs into one run vector.
    std::vector<TextRun> result;
    for (size_t i = 0; i < paragraphs_.size(); ++i) {
        for (const auto& run : paragraphs_[i].runs) {
            result.push_back(run);
        }
        // Insert newline run between paragraphs (not after the last).
        if (i + 1 < paragraphs_.size()) {
            TextRun newlineRun;
            newlineRun.text = L"\n";
            // Inherit style from last run of this paragraph, or default.
            if (!paragraphs_[i].runs.empty()) {
                newlineRun = paragraphs_[i].runs.back();
                newlineRun.text = L"\n";
            }
            result.push_back(newlineRun);
        }
    }
    return result;
}

// ============================================================================
// RichTextDocument - MergeAdjacentRuns
// ============================================================================

void RichTextDocument::MergeAdjacentRuns()
{
    for (auto& para : paragraphs_) {
        if (para.runs.size() < 2)
            continue;

        std::vector<TextRun> merged;
        merged.push_back(para.runs[0]);

        for (size_t i = 1; i < para.runs.size(); ++i) {
            TextRun& last = merged.back();
            const TextRun& current = para.runs[i];

            if (last.StyleEquals(current)) {
                last.text += current.text;
            } else {
                merged.push_back(current);
            }
        }

        para.runs = std::move(merged);
    }
}

} // namespace fluent
