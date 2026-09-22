// RichTextBox.h — multi-style editable text control.
//
// A rich-text editor where each run has its own font size, weight, color, and
// underline. Built on RichTextDocument (the plain C++ model) plus Control (not
// TextEditBase — that class owns std::wstring text_, which would be a second
// source of truth). Reimplements caret, selection, typing, Backspace/Delete,
// and clipboard for the rich-text model.
//
// NOW USES ONE LAYOUT for the entire document with per-range formatting, so
// runs flow inline and wrap correctly. No virtualization (the full document is
// in memory and rendered in one DirectWrite layout).
//
// Indices are UTF-16 code-unit offsets into the document's plain text; the caret
// sits between code units [0..length].
//
// NOT YET IMPLEMENTED: undo, IME composition, input filters, max-length, readonly.
// These can be added by lifting the relevant pieces from TextEditBase once the
// core editing loop works.
#pragma once

#include "../core/Control.h"
#include "../base/Event.h"
#include "RichTextDocument.h"
#include "../layout/ScrollViewer.h"
#include <memory>
#include <vector>

namespace fluent {

class RichTextBox : public Control {
public:
    RichTextBox();

    // --- Document access --------------------------------------------------
    // Get the underlying document (read-only for inspection, style queries).
    const RichTextDocument& Document() const { return doc_; }

    // Replace the entire document. Clears selection, places caret at the end.
    void SetDocument(RichTextDocument doc);

    // --- Read-only mode ---------------------------------------------------
    void SetReadOnly(bool ro) { readOnly_ = ro; }
    bool ReadOnly() const { return readOnly_; }

    // --- Style at caret ---------------------------------------------------
    // The style used for the next insertion. When there is a selection, returns
    // the style at the selection start. When the caret is at offset 0 in an
    // empty document, returns a default style (12pt black normal sans-serif).
    TextRun GetCurrentStyle() const;

    // Set the style for future insertions. Does NOT change existing text; it
    // only affects what gets inserted next (typing, paste).
    void SetCurrentStyle(const TextRun& style);

    // Apply a style to the current selection. If there is no selection (caret
    // only), this becomes the insertion style for future text. If there is a
    // selection, the selected runs are re-styled.
    void ApplyStyle(const TextRun& style);

    // --- Per-attribute selection styling ----------------------------------
    //
    // These change ONE attribute and leave the others alone, so bolding a
    // selection that mixes colors and sizes keeps every color and size. That is
    // the difference from ApplyStyle(TextRun), which imposes one whole style on
    // the range and therefore flattens it.
    //
    // With no selection they set the pending insertion style instead, so the
    // toolbar button works the same whether the user selects first or types first.
    void SetSelectionBold(bool bold);
    void SetSelectionUnderline(bool underline);
    void SetSelectionColor(const D2D1_COLOR_F& color);
    void SetSelectionFontSize(float sizeDip);
    // Weight for anything finer than bold/not-bold (SemiBold, Light, Black...).
    void SetSelectionFontWeight(DWRITE_FONT_WEIGHT weight);

    // --- Paragraph formatting ---------------------------------------------
    //
    // These apply to whole PARAGRAPHS, which is the unit these attributes belong
    // to — "centered" is not something a character can be. The affected set is
    // every paragraph the selection touches, or the caret's own paragraph when
    // there is no selection. Touching one character of a paragraph formats all
    // of it, because there is no way to centre half a paragraph.
    //
    // Unlike the SetSelectionXxx family, these have no "pending style" fallback:
    // with a bare caret there is still a real paragraph to format (the one the
    // caret sits in), so the call always has a target and always mutates the
    // document. They are therefore blocked in read-only mode even with no
    // selection, which is the opposite of SetSelectionBold's behaviour.
    void SetParagraphAlignment(DWRITE_TEXT_ALIGNMENT alignment);
    // Multiplier on the font-derived line height: 1.0 single, 1.5, 2.0 double.
    // Values <= 0 are rejected (DWrite would produce a degenerate line box).
    void SetParagraphLineSpacing(float multiple);
    // Pins the line height to an exact DIP value regardless of font size — what a
    // code view or a table wants, where rows must align across differing sizes.
    void SetParagraphLineHeight(float dip);
    void SetParagraphSpaceBefore(float dip);
    void SetParagraphSpaceAfter(float dip);
    // Negative firstLineIndent is a hanging indent (first line starts left of the
    // body), which is how bullet and numbered lists are shaped.
    void SetParagraphFirstLineIndent(float dip);
    void SetParagraphLeftIndent(float dip);
    void SetParagraphRightIndent(float dip);
    // Replaces every attribute at once. Deliberately flattens the affected
    // paragraphs to one style, the way ApplyStyle does for runs.
    void ApplyParagraphStyle(const ParagraphStyle& style);

    // The paragraph style at the caret, or at the selection start when there is a
    // selection. What a toolbar reads to show which alignment button is active.
    // A selection spanning paragraphs with different alignments reports the first
    // one — the toolbar shows a state rather than nothing.
    ParagraphStyle GetCurrentParagraphStyle() const;

    // --- Selection --------------------------------------------------------
    UINT32 SelectionStart() const { return std::min(caret_, anchor_); }
    UINT32 SelectionLength() const { return caret_ > anchor_ ? caret_ - anchor_ : anchor_ - caret_; }
    // The selected characters as plain text (empty when there is no selection).
    // What Copy() puts on the clipboard, without touching the clipboard.
    std::wstring SelectedText() const;
    bool HasSelection() const { return caret_ != anchor_; }
    void Select(UINT32 start, UINT32 length);
    void SelectAll();

    // --- Undo / redo ------------------------------------------------------
    bool CanUndo() const { return !undoStack_.empty(); }
    bool CanRedo() const { return !redoStack_.empty(); }

    // --- Content extents --------------------------------------------------
    // The laid-out size of the whole document including padding, i.e. what the
    // scrollbars are sized against. Public because "how tall is my text" is a fair
    // question for a caller sizing a container around the box; both force a layout
    // rebuild if one is pending.
    // outerWidth < 0 (the default) means "use the current bounds"; Measure passes
    // the width it was handed, because bounds_ is not updated until Arrange.
    float ContentHeight(float outerWidth = -1.0f) const;
    // Widest laid-out line plus padding. Equals the control width in Wrap mode;
    // only meaningful (and only used) in NoWrap, where it drives horizontal scroll.
    float ContentWidth(float outerWidth = -1.0f) const;
    // Force a rebuild on the next height/width query. For perf measurement only.
    void InvalidateLayout() { layoutDirty_ = true; }
    // True when the content is wider than the viewport, i.e. a horizontal scrollbar
    // is live. Always false in Wrap mode.
    bool HorizontallyScrollable() const;
    // True when the content is taller than the viewport. Both exist so a caller —
    // or a test — can ask what the scrollbars concluded, rather than re-deriving it
    // from the extents and getting a different answer than the ScrollViewer.
    bool VerticallyScrollable() const;

    // --- Scroll position --------------------------------------------------
    // How far the view is scrolled, in DIP from the top of the document, and a way
    // to move it. Legitimate API rather than a test hook: restoring a saved scroll
    // position, or scrolling programmatically to a search hit, both need these.
    // The value is clamped to the scrollable range.
    float ScrollOffset() const;
    void ScrollTo(float offsetDip);

    // --- Wrapping ---------------------------------------------------------
    //
    // Wrap (default): lines reflow to the control width, so there is never
    // horizontal scrolling and a resize re-breaks every line.
    //
    // NoWrap: each logical line stays one visual line and runs off to the right.
    // Wanted for code and for tabular text, where a wrapped line destroys the
    // column alignment that carries the meaning.
    //
    // Changing this rebuilds the layout, which also changes the document height —
    // NoWrap is usually much shorter, so the scroll offset is re-clamped.
    void SetWordWrap(bool wrap);
    bool WordWrap() const { return wordWrap_; }

    // --- Element overrides ------------------------------------------------
    void Render(const DrawingContext& dc) override;
    void Measure(float availW, float availH) override;
    void Arrange(const RectDip& finalRect) override;
    bool CaretRectDip(RectDip& out) const override;
    
    bool WantsAnimationTick() const override;
    void OnAnimationTick(float dtSec) override;
    void OnAttachedToTree() override;

    // Input routing.
    void OnTextInput(wchar_t ch) override;
    void OnKeyDownRouted(KeyEventArgs& e) override;
    void OnPointerPressed(PointerEventArgs& e) override;
    void OnPointerMoved(PointerEventArgs& e) override;
    void OnPointerReleased(PointerEventArgs& e) override;
    void OnPointerWheelChanged(PointerEventArgs& e) override;
    // Clears the cached scrollbar-hover flags: without this the pointer can leave
    // over a rail and the arrow cursor sticks after it returns onto the text.
    void OnPointerLeft() override;
    void OnFocusChanged() override;

    // Blink the caret.
    bool WantsBlink() const override { return IsFocused(); }
    void OnBlink() override { caretVisible_ = !caretVisible_; Invalidate(); }

    HCURSOR Cursor() const override;

    // IME composition. Routed here by the window's WndProc for the focused
    // element, so Chinese / Japanese / Korean input works the same as in TextBox.
    void OnImeStartComposition(HWND hwnd) override;
    void OnImeComposition(HWND hwnd, LPARAM flags) override;
    void OnImeEndComposition(HWND hwnd) override;

    // --- Events -----------------------------------------------------------
    // Raised after the text changes (typing, paste, delete, undo, redo, SetDocument).
    // The snapshot is the CURRENT text, post-change.
    Event<RichTextBox, std::wstring>& TextChanged() { return textChanged_; }

    struct SelectionChangedArgs { UINT32 start; UINT32 length; };
    // Raised when the selection or caret moves without the text changing. Also raised
    // after TextChanged when an edit moved the caret, so subscribers get both.
    Event<RichTextBox, SelectionChangedArgs>& SelectionChanged() { return selectionChanged_; }

private:
    // --- Internal state ---------------------------------------------------
    RichTextDocument doc_;
    TextRun currentStyle_;  // style for next insertion

    UINT32 caret_ = 0;      // caret position (code-unit offset)
    UINT32 anchor_ = 0;     // selection anchor (when caret != anchor, there's a selection)
    bool caretVisible_ = true;

    // Cached plain text (rebuilt from doc_ when text changes).
    mutable std::wstring plainTextCache_;
    mutable bool plainTextDirty_ = true;

    // Wrapping mode. Wrap by default, matching TextArea and every other multi-line
    // editor — NoWrap is the deliberate choice, not the accident.
    bool wordWrap_ = true;

    // The in-flight IME composition string, NOT yet part of the document.
    //
    // It is laid out inline at the caret so the user sees the characters they are
    // composing, but it stays out of doc_ until the IME commits it (GCS_RESULTSTR),
    // because a composition is provisional — it can be cancelled, and it must not
    // land in the undo stack or in a copy of the document.
    //
    // Consequence: while composing, offsets in layout_ are shifted by
    // composition_.size() for everything at or after the caret. LayoutText() is the
    // single place that shift is applied, and LayoutOffset() converts a document
    // offset into a layout offset for caret and selection geometry.
    std::wstring composition_;

    // Scroll state.
    std::unique_ptr<ScrollViewer> scrollViewer_;

    // Layout: one IDWriteTextLayout per paragraph, enabling per-paragraph formatting
    // (alignment, line spacing, indentation). Each paragraph is laid out independently,
    // then stacked vertically with spaceBefore/spaceAfter gaps.
    //
    // Brushes for SetDrawingEffect must stay alive for the layout's lifetime, so they
    // are held in a parallel vector indexed by paragraph.
    //
    // Indent values are cached here so Render() can offset the drawing origin without
    // re-reading the paragraph vector. DirectWrite has no built-in indent API; we narrow
    // the layout box by left+right indents and shift the draw origin manually.
    struct ParagraphLayoutInfo {
        ComPtr<IDWriteTextLayout> layout;
        std::vector<ComPtr<ID2D1SolidColorBrush>> runBrushes;
        float spaceBefore = 0.0f;   // DIP gap before this paragraph
        float spaceAfter = 0.0f;    // DIP gap after this paragraph
        float leftIndent = 0.0f;    // DIP offset for left edge
        float rightIndent = 0.0f;   // (narrows layout box, not rendered directly)
        float firstLineIndent = 0.0f;  // Additional DIP offset for first line only
    };
    std::vector<ParagraphLayoutInfo> paragraphLayouts_;
    bool layoutDirty_ = true;
    float lastLayoutWidth_ = 0.0f;

    // Drag-selection state. selecting_ is true between a press on text and the
    // matching release, which is what distinguishes a drag from mere hovering.
    bool selecting_ = false;
    
    // Read-only mode: blocks all editing operations (typing, paste, cut, delete,
    // undo/redo, style changes). Selection, copy, and scrolling remain enabled.
    bool readOnly_ = false;
    
    // Multi-click selection granularity (character/word/line) and the anchor range.
    // When dragging word-by-word, the initially clicked word stays selected even if
    // the pointer moves backward.
    enum class SelectGranularity { Character, Word, Line };
    SelectGranularity dragGranularity_ = SelectGranularity::Character;
    UINT32 dragStartLo_ = 0;
    UINT32 dragStartHi_ = 0;

    // Which scrollbar the pointer is currently over, cached from OnPointerMoved.
    // Cursor() is const and cannot ask the ScrollViewer directly, and the answer is
    // needed there to show an arrow over a rail instead of an I-beam.
    bool pointerOverVBar_ = false;
    bool pointerOverHBar_ = false;

    // --- Undo / redo ------------------------------------------------------
    // Snapshot-based: each entry is the whole paragraph vector (which includes runs)
    // plus the caret it was taken with. Chosen over an operation log because a
    // rich-text edit changes both text AND styles, so an inverse operation would
    // have to carry the previous style of every touched run — which is the snapshot
    // anyway, minus the simplicity. Bounded to kMaxUndo so a long session cannot
    // grow forever.
    //
    // After the paragraph refactor, a snapshot must capture the PARAGRAPH vector,
    // not a flat run list — runs cannot represent alignment, spacing, or indents,
    // so undo would silently reset every paragraph to default formatting.
    struct Snapshot {
        std::vector<Paragraph> paragraphs;
        UINT32 caret = 0;
        UINT32 anchor = 0;
    };
    static constexpr size_t kMaxUndo = 100;
    std::vector<Snapshot> undoStack_;
    std::vector<Snapshot> redoStack_;

    // Push the CURRENT state onto the undo stack. Call BEFORE mutating, and only
    // from the top-level edit entry points (typing, delete, paste, style change) —
    // calling it from a helper that another edit path also uses would record two
    // snapshots for one user action and make Ctrl+Z need two presses.
    void PushUndo();
    void Undo();
    void Redo();
    void ApplySnapshot(const Snapshot& s);

    // Shared implementation behind ApplyStyle and every SetSelectionXxx.
    void RestyleSelection(const std::function<void(TextRun&)>& fn);

    // Shared implementation behind ApplyParagraphStyle and every SetParagraphXxx.
    // Applies to every paragraph the selection touches, or the caret's paragraph
    // when there is no selection. Always a document edit (a bare caret still has a
    // paragraph), so unlike RestyleSelection it is blocked in read-only mode even
    // with no selection.
    void RestyleParagraphs(const std::function<void(ParagraphStyle&)>& fn);

    // --- Line-aware navigation --------------------------------------------
    // Visual lines from the layout's line metrics, not '\n' positions — with
    // wrapping on, the two do not coincide.
    bool LineRangeAt(UINT32 pos, UINT32& outStart, UINT32& outEnd) const;
    UINT32 LineStart(UINT32 pos) const;
    UINT32 LineEnd(UINT32 pos) const;
    // The offset one visual line above (-1) or below (+1) the caret, at the
    // caret's current X. Returns the caret unchanged when there is no such line.
    UINT32 CaretLineStep(int delta) const;

    // --- Editing helpers --------------------------------------------------
    void InsertText(const std::wstring& s);
    void DeleteSelection();
    void Backspace();
    void Delete();
    void MoveCaret(UINT32 to, bool extendSelection);

    // The document's CONTENT changed (text or run/paragraph styles): refresh layout
    // and extents, then raise TextChanged followed by SelectionChanged (an edit moves
    // the caret too). Call from every path that mutates doc_.
    void Changed();

    // The selection or caret moved but doc_ is untouched: refresh layout and extents,
    // then raise SelectionChanged only. Call from every path that assigns caret_ /
    // anchor_ WITHOUT editing the document — Select, SelectAll, MoveCaret, click and
    // drag selection. Using Changed() here instead would tell subscribers the document
    // was edited when it was not, which breaks "mark unsaved" and auto-save.
    void SelectionMoved();

    // The layout/extent/caret-visibility work both of the above share. Raises nothing.
    void Refresh();

    // Raise SelectionChanged with the current selection. Cheap when nobody listens.
    void RaiseSelectionChanged();

    // Multi-click selection: double-click selects word, triple-click selects logical line.
    // Returns true if selection was applied (clickCount >= 2), false otherwise.
    bool ApplyMultiClickSelection(UINT32 index, int clickCount);
    
    // Extend the drag selection to include the character/word/line at `index`, depending
    // on the granularity set by ApplyMultiClickSelection.
    void ExtendDragSelection(UINT32 index);

    // --- Clipboard --------------------------------------------------------
    void Copy();
    void Cut();
    void Paste();
    HWND ClipboardOwner() const;

    // --- Layout -----------------------------------------------------------
    void RebuildLayout(float outerWidth);
    void EnsureLayoutUpToDate(float outerWidth = -1.0f) const;
    void RebuildPlainText() const;
    const std::wstring& PlainText() const;
    void ClampScroll();
    void EnsureCaretVisible();

    // Return the text that gets laid out (document + in-flight composition).
    std::wstring LayoutText() const;
    // Map a document offset to the layout's coordinate system (accounting for
    // the composition string sitting at the caret in the layout but not in doc_).
    UINT32 LayoutOffset(UINT32 docOffset) const;

    // --- Hit testing ------------------------------------------------------
    // Map a content-relative DIP coordinate to a code-unit offset.
    UINT32 IndexAtPoint(float dipX, float dipY) const;

    // Get the caret's DIP rectangle (content-relative, not accounting for scroll).
    RectDip CaretRectContentRelative() const;

    // --- Events -----------------------------------------------------------
    Event<RichTextBox, std::wstring> textChanged_;
    Event<RichTextBox, SelectionChangedArgs> selectionChanged_;
};

} // namespace fluent
