#pragma once
#include "../fl_common.h"
#include <d2d1.h>
#include <dwrite.h>
#include <vector>
#include <string>
#include <functional>

namespace fluent {

// TextRun — a contiguous span of characters with uniform style.
//
// The document model is a vector of runs. Adjacent runs with identical styles
// are merged on insertion to keep the run count low (single-run text stays cheap).
struct TextRun {
    std::wstring text;
    D2D1_COLOR_F color{0.0f, 0.0f, 0.0f, 1.0f};
    float fontSize{12.0f};
    DWRITE_FONT_WEIGHT weight{DWRITE_FONT_WEIGHT_NORMAL};
    bool underline{false};

    TextRun() = default;
    TextRun(std::wstring t, D2D1_COLOR_F c, float sz,
            DWRITE_FONT_WEIGHT w = DWRITE_FONT_WEIGHT_NORMAL, bool ul = false)
        : text(std::move(t)), color(c), fontSize(sz), weight(w), underline(ul) {}

    // StyleEquals compares every attribute except text — used to decide whether
    // adjacent runs can merge.
    bool StyleEquals(const TextRun& other) const;
};

// RunLocation — the result of mapping a character offset to a run index.
struct RunLocation {
    bool valid{false};
    size_t runIndex{0};
    size_t offsetInRun{0};
};

// LineSpacingMode — how a paragraph's line height is derived.
//
// Multiple is the familiar word-processor control ("1.5 line spacing"): the
// height DWrite computed from the font is scaled. Absolute pins every line to a
// fixed DIP height regardless of font size, which is what a code editor or a
// table wants — rows must align across paragraphs whose font sizes differ, and a
// multiplier cannot promise that.
enum class LineSpacingMode {
    Multiple,   // lineSpacing is a factor applied to the font-derived height
    Absolute,   // lineSpacing is the line height in DIP
};

// ParagraphStyle — the block-level formatting of one paragraph.
//
// Separate from TextRun because these attributes belong to a *range of lines*,
// not to a span of characters: "centered" is not a property any single character
// can carry, and two runs inside one paragraph cannot disagree about alignment.
// That distinction is the whole reason the document grew a paragraph layer —
// see the header comment on RichTextDocument.
//
// Indents are modelled as DIP insets on the paragraph's layout box rather than
// as DWrite attributes, because IDWriteTextLayout has no indent property at all.
// RichTextBox narrows the box by leftIndent/rightIndent and offsets the first
// line by firstLineIndent. A NEGATIVE firstLineIndent is a hanging indent (the
// first line starts left of the body), which is how bullet and numbered lists
// are shaped; the sum leftIndent + firstLineIndent is clamped at the padding
// edge so a hanging indent cannot push text outside the control.
struct ParagraphStyle {
    DWRITE_TEXT_ALIGNMENT alignment{DWRITE_TEXT_ALIGNMENT_LEADING};

    LineSpacingMode lineSpacingMode{LineSpacingMode::Multiple};
    // Multiple mode: 1.0 = single, 1.5 = one-and-a-half, 2.0 = double.
    // Absolute mode: the line height in DIP.
    float lineSpacing{1.0f};

    float spaceBefore{0.0f};       // DIP of blank space above the paragraph
    float spaceAfter{0.0f};        // DIP of blank space below it
    float firstLineIndent{0.0f};   // DIP; negative = hanging indent
    float leftIndent{0.0f};        // DIP inset from the left content edge
    float rightIndent{0.0f};       // DIP inset from the right content edge

    // Compared when deciding whether two paragraphs can keep separate styles or
    // whether a style change actually changed anything. Floats use an epsilon for
    // the same reason TextRun::StyleEquals does.
    bool StyleEquals(const ParagraphStyle& other) const;
};

// Paragraph — a run of text between two hard newlines, plus its block formatting.
//
// The '\n' that ENDS a paragraph is not stored in any run. It is implied by the
// boundary between consecutive paragraphs and contributes exactly one character
// to every offset computation — see the offset contract on RichTextDocument.
// Storing it would mean two sources of truth for "where does this paragraph end",
// and every edit would have to keep them agreeing.
struct Paragraph {
    std::vector<TextRun> runs;
    ParagraphStyle style;

    // Total code units in this paragraph, NOT counting the implied newline.
    size_t Length() const;
    std::wstring GetText() const;
    bool Empty() const;
};

// ParagraphLocation — the result of mapping a document offset to a paragraph.
//
// offsetInParagraph == paragraph length means "on the implied newline at the end
// of this paragraph", which is a legal caret position and the thing Backspace
// deletes to merge two paragraphs.
struct ParagraphLocation {
    bool valid{false};
    size_t paragraphIndex{0};
    size_t offsetInParagraph{0};
};

// RichTextDocument — the data model behind RichTextBox.
//
// Responsibilities:
// - Store text as a vector of paragraphs, each holding styled runs
// - Insert/delete with automatic run splitting/merging and paragraph
//   splitting/merging
// - Map character offsets to paragraph and run indices
// - Carry per-paragraph block formatting (alignment, spacing, indents)
// - Export plain text and snapshots (for rendering and undo)
//
// NOT responsible for:
// - Layout (that is IDWriteTextLayout's job)
// - Cursor or selection (RichTextBox owns those)
// - Input or events (also RichTextBox)
//
// This is a pure data structure with no dependencies on window, device, or
// DWrite — it can be unit-tested without creating a window.
//
// ** Offset space and the implied newline — read this before touching an edit path **
//
// Offsets are UTF-16 code units counted from the document start, WITH an implicit
// '\n' between consecutive paragraphs. A document of paragraphs "ab", "cd", "ef"
// behaves offset-wise like the string "ab\ncd\nef" (8 code units), while the runs
// themselves hold only "ab"/"cd"/"ef" (6 code units).
//
// The separator is NOT stored in any run. It is implied by the boundary between
// paragraphs, for one reason: storing it would create a second source of truth for
// "where does this paragraph end", and every edit would then have to keep the
// stored newline and the paragraph boundary agreeing. They would eventually
// disagree, and the symptom would be caret arithmetic drifting by one per
// paragraph.
//
// The last paragraph has no trailing separator: "ab\ncd" is length 5, and offset 5
// is the end of the document rather than a newline followed by an empty paragraph.
//
// ** paragraphs_ is never empty **
//
// An empty document is ONE paragraph holding zero runs, not zero paragraphs. This
// keeps every call site free of a "no paragraphs at all" branch, and it gives the
// paragraph style somewhere to live in an empty document — a user who centers an
// empty box and then types expects centered text.
//
// ** Boundary positions are ambiguous, and the tie-break is deliberate **
//
// An offset exactly on a separator can be read as "end of paragraph N" or "start
// of paragraph N+1". GetParagraphAt returns the paragraph ENDING there, because
// that is the paragraph whose style should govern a formatting key pressed at that
// caret position. Run lookup keeps the opposite convention it always had (see
// GetRunAt), so typing at the end of a bold word stays bold.
class RichTextDocument {
public:
    // Seeds paragraphs_ with one empty paragraph — see the "never empty"
    // invariant in the class comment. Not `= default` for that reason.
    RichTextDocument();

    // --- Query ---

    bool Empty() const;
    size_t Length() const;
    std::wstring GetPlainText() const;

    const std::vector<Paragraph>& Paragraphs() const { return paragraphs_; }

    // GetParagraphAt maps a document offset to the paragraph containing it.
    // Returns valid=false if offset is out of bounds.
    // offset == Length() is valid (the caret one past the document end).
    //
    // When exactly on a newline separator, returns the paragraph ENDING there
    // rather than the one starting after it. That tie-break is deliberate: the
    // style at the caret's position should be the paragraph the user is
    // currently sitting in, not the one they have not entered yet.
    ParagraphLocation GetParagraphAt(size_t offset) const;

    // GetRunAt maps a document offset to the run that contains it, with run
    // offsets local to the containing paragraph.
    // Returns valid=false if offset is out of bounds.
    //
    // DIFFERENT tie-break than GetParagraphAt: when exactly at a run boundary,
    // returns the NEXT run, not the current one. This is the old convention
    // carried over from the flat runs_ model — typing at the end of a bold word
    // should continue in bold, not switch back to whatever style follows.
    RunLocation GetRunAt(size_t offset) const;

    // GetStyleAt returns the style of the character at offset, or a default
    // style if the document is empty or offset is out of bounds.
    TextRun GetStyleAt(size_t offset) const;

    // GetParagraphStyleAt returns the paragraph style covering offset.
    // When exactly on a newline separator, returns the style of the paragraph
    // ENDING there (same tie-break as GetParagraphAt).
    ParagraphStyle GetParagraphStyleAt(size_t offset) const;

    // --- Edit ---

    // InsertText at offset with the given run style.
    // If text contains '\n', splits into multiple paragraphs.
    // If style matches the run at offset, appends without creating a new run.
    // Otherwise splits the run at offset and inserts a new run in between.
    // offset is clamped to Length() — inserting past the end appends.
    //
    // The newly created paragraph(s) inherit the paragraph style of the insertion
    // point BEFORE the split. That is the familiar word-processor behavior: press
    // Enter in a centered paragraph, and the next paragraph starts centered too.
    void InsertText(size_t offset, const std::wstring& text, const TextRun& style);

    // DeleteRange removes [start, start+length).
    // Clamps to document bounds. Removes runs that become empty. When a deletion
    // removes the newline separator between two paragraphs, merges them: the
    // FIRST paragraph's style is kept, the second's is discarded.
    void DeleteRange(size_t start, size_t length);

    // ApplyStyle rewrites the RUN-LEVEL style of [start, start+length) in place,
    // keeping the text untouched. Runs that straddle either end are split so the
    // boundary lands exactly on the requested range.
    //
    // `fn` receives each covered run and mutates whichever attributes it cares
    // about — that is what lets "bold the selection" preserve per-run colors and
    // sizes instead of flattening the range to one uniform style.
    void ApplyStyle(size_t start, size_t length,
                    const std::function<void(TextRun&)>& fn);

    // ApplyParagraphStyle rewrites the PARAGRAPH-LEVEL style of every paragraph
    // that intersects [start, start+length).
    //
    // `fn` receives the paragraph style and mutates it. This is what backs "center
    // the current paragraph" or "increase line spacing in the selection".
    void ApplyParagraphStyle(size_t start, size_t length,
                             const std::function<void(ParagraphStyle&)>& fn);

    // AppendRun adds a styled run at the end of the document. Merges with the
    // last run of the last paragraph when the styles match.
    //
    // Equivalent to InsertText(Length(), run.text, run) but without the offset
    // arithmetic — this is the natural call when constructing a document from
    // scratch (loading saved content, seeding a demo).
    void AppendRun(const TextRun& run);

    void Clear();

    // --- Snapshot / restore (for undo) ---
    //
    // The snapshot unit is the PARAGRAPH vector, not a flat run vector. An earlier
    // draft of this refactor kept a `Runs()` / `SetRuns()` pair that flattened the
    // document to runs and re-split it on '\n'. Both directions lose information —
    // a flat run list cannot carry alignment, spacing, or indents — so undo silently
    // reset every paragraph to default formatting. Do not reintroduce it.
    // (The read side is Paragraphs(), declared in the Query section above.)
    void SetParagraphs(const std::vector<Paragraph>& p) { paragraphs_ = p; }

    // Flattens all paragraphs into one run vector for code that needs a linear view
    // (e.g., enumerating all runs to compute plain text, or checking run count in
    // tests). The returned vector is ephemeral — do NOT use it for snapshots, since
    // it loses paragraph boundaries and formatting. Use Paragraphs() for that.
    std::vector<TextRun> Runs() const;

private:
    // paragraphs_ is NEVER empty. An empty document is one paragraph with zero runs.
    std::vector<Paragraph> paragraphs_;

    // Merge adjacent runs within each paragraph when they have identical styles.
    // Called after every InsertText and DeleteRange.
    void MergeAdjacentRuns();

    // Helpers for InsertText when text contains no '\n'.
    void InsertTextNoParagraphSplit(size_t offset, const std::wstring& text,
                                     const TextRun& style);

    // Helpers for DeleteRange.
    void DeleteWithinOneParagraph(size_t paraIndex, size_t start, size_t length);
    void DeleteAcrossParagraphs(const ParagraphLocation& startLoc,
                                 const ParagraphLocation& endLoc);

    // Helper for ApplyStyle.
    void ApplyStyleWithinParagraph(Paragraph& para, size_t start, size_t length,
                                    const std::function<void(TextRun&)>& fn);
};

} // namespace fluent
