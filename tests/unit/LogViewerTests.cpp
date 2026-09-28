// LogViewerTests.cpp — Unit tests for the LogViewer control.
//
// Headless: no window, no device. That bounds what is testable here — every span
// is drawn through DWrite, so real row heights, span advance and clipping cannot
// be exercised from this process. What IS testable is the bookkeeping the
// virtualization rests on: line count, the ring-buffer trim, and the estimated
// content extent that drives the scroll range before anything has been painted.
//
// The estimate is load-bearing rather than incidental: AppendLine must stay O(1)
// at log rates, so it cannot measure, and the scroll extent it publishes has to
// be right from the first append. Asserting on it is asserting on that contract.

#include "../framework/Test.h"
#include "../../FluentUI/controls/LogViewer.h"
#include "../../FluentUI/graphics/DWriteContext.h"
#include "../../FluentUI/core/UIContext.h"

using namespace fluent;

namespace {
constexpr D2D1_COLOR_F kWhite{1.0f, 1.0f, 1.0f, 1.0f};
constexpr D2D1_COLOR_F kRed{1.0f, 0.0f, 0.0f, 1.0f};
constexpr D2D1_COLOR_F kYellow{1.0f, 1.0f, 0.0f, 1.0f};

// The estimate LogViewer uses before a line has been painted: the largest font
// size in the line times the line-spacing factor. Duplicated from the .cpp on
// purpose — a test that recomputes it from the control's own constant would pass
// no matter what that constant became.
constexpr float kLineSpacing = 1.2f;
} // namespace

TEST(LogViewer, EmptyViewerHasNoLines)
{
    LogViewer v;
    EXPECT_EQ(v.LineCount(), size_t(0));
}

TEST(LogViewer, AppendLineAddsOneLine)
{
    LogViewer v;
    v.AppendLine({{L"Hello", kWhite}});
    EXPECT_EQ(v.LineCount(), size_t(1));
}

// A line is one row regardless of how many spans it is built from. This is the
// whole point of the inline-styling design: styling subdivides a row, it does not
// create rows.
TEST(LogViewer, MultipleSpansStillOneLine)
{
    LogViewer v;
    v.AppendLine({
        {L"[", kWhite},
        {L"ERROR", kRed, 14.0f, DWRITE_FONT_WEIGHT_BOLD},
        {L"] disk full", kWhite},
    });
    EXPECT_EQ(v.LineCount(), size_t(1));
}

TEST(LogViewer, EmptySpanListIsRejected)
{
    LogViewer v;
    v.AppendLine({});
    EXPECT_EQ(v.LineCount(), size_t(0));
}

TEST(LogViewer, ClearRemovesEveryLine)
{
    LogViewer v;
    v.AppendLine({{L"one", kWhite}});
    v.AppendLine({{L"two", kWhite}});
    EXPECT_EQ(v.LineCount(), size_t(2));

    v.Clear();
    EXPECT_EQ(v.LineCount(), size_t(0));
}

// --- Scroll extent from estimated heights -----------------------------------

// Nothing has been painted, so every height is still an estimate. The extent must
// already be usable: a log that has appended 10 lines has to be scrollable before
// its first frame, or the scrollbar appears a frame late on every batch.
TEST(LogViewer, ContentExtentFollowsEstimatedHeights)
{
    LogViewer v;
    v.Arrange(RectDip{0, 0, 400, 100});

    const float expectedRow = 12.0f * kLineSpacing;
    for (int i = 0; i < 10; ++i)
        v.AppendLine({{L"line", kWhite, 12.0f}});

    // 10 rows of 14.4 DIP = 144 DIP of content in a 100 DIP viewport.
    EXPECT_NEAR(v.ContentExtentDip(), expectedRow * 10.0f, 0.01f);
    EXPECT_TRUE(v.ContentExtentDip() > 100.0f);
}

// The estimate keys on the LARGEST font in the row, so a row carrying one 24 DIP
// span must reserve more vertical space than an all-12 DIP row. This is the
// dynamic-row-height contract at the estimation stage.
TEST(LogViewer, LargerFontEstimatesTallerRow)
{
    // `small` is a macro in <rpcndr.h> (typedef'd to char), so these cannot be
    // named small/large.
    LogViewer shortRow;
    shortRow.AppendLine({{L"x", kWhite, 12.0f}});

    LogViewer tallRow;
    tallRow.AppendLine({{L"x", kWhite, 12.0f}, {L"BIG", kWhite, 24.0f}});

    EXPECT_NEAR(shortRow.ContentExtentDip(), 12.0f * kLineSpacing, 0.01f);
    EXPECT_NEAR(tallRow.ContentExtentDip(), 24.0f * kLineSpacing, 0.01f);
    EXPECT_TRUE(tallRow.ContentExtentDip() > shortRow.ContentExtentDip());
}

// Underline and weight are appearance-only: they must not change the row's
// height, or a log where one span is bold would have a taller row than its
// neighbours and the whole view would jitter as records scroll past.
TEST(LogViewer, WeightAndUnderlineDoNotChangeRowHeight)
{
    LogViewer plain;
    plain.AppendLine({{L"text", kWhite, 12.0f}});

    LogViewer decorated;
    decorated.AppendLine({{L"text", kYellow, 12.0f, DWRITE_FONT_WEIGHT_BOLD, true}});

    EXPECT_NEAR(decorated.ContentExtentDip(), plain.ContentExtentDip(), 0.01f);
}

// --- Measurement on attach (real DWrite, no GPU needed) ----------------------
//
// Regression: OnAttachedToTree used to measure ONE span of ONE line and stamp that
// height onto every unmeasured line, marking them all measured so the paint pass
// never corrected them. A 24 DIP "BUILD FAILED" row then got a 10 DIP row's height
// and the rows below were drawn on top of it. Each line must be measured from its
// own tallest span.

namespace {
// Attach with a real DWrite context; returns false when DWrite is unavailable so the
// test can bail instead of failing on a machine without it.
bool AttachWithDWrite(LogViewer& v, DWriteContext& dw) {
    if (!dw.Valid() && FAILED(dw.Initialize())) return false;
    UIContext ctx;
    ctx.dwrite = &dw;
    ctx.dpiScale = 1.0f;
    v.AttachToContext(ctx);
    return true;
}
} // namespace

TEST(LogViewer, AttachMeasuresEachLineFromItsOwnTallestSpan)
{
    DWriteContext dw;
    LogViewer v;
    v.Arrange(RectDip{0, 0, 400, 300});
    v.AppendLine({{L"small 10", kWhite, 10.0f}});                    // measured first
    v.AppendLine({{L"BUILD FAILED", kRed, 24.0f, DWRITE_FONT_WEIGHT_BOLD}});
    v.AppendLine({{L"small 10", kWhite, 10.0f}});
    if (!AttachWithDWrite(v, dw)) return;

    const float row0 = v.LineHeightDip(0);
    const float row1 = v.LineHeightDip(1);
    const float row2 = v.LineHeightDip(2);
    // The 24 DIP row must be clearly taller than its 10 DIP neighbours. Under the
    // old code all three were equal (the first line's height copied everywhere).
    EXPECT_TRUE(row1 > row0 * 1.8f);
    EXPECT_NEAR(row0, row2, 0.01f);
    // And the extent is the sum, so the rows below the tall one are pushed down.
    EXPECT_NEAR(v.ContentExtentDip(), row0 + row1 + row2, 0.01f);
    v.DetachFromContext();
}

// A mixed line is as tall as its tallest span, not its first span. The old code
// sampled spans[0] only, so a line that starts small and ends large was undersized.
TEST(LogViewer, AttachUsesTallestSpanNotFirstSpan)
{
    DWriteContext dw;
    LogViewer mixed;
    mixed.AppendLine({{L"a ", kWhite, 10.0f}, {L"BIG", kYellow, 20.0f}});
    LogViewer big;
    big.AppendLine({{L"BIG", kYellow, 20.0f}});
    if (!AttachWithDWrite(mixed, dw) || !AttachWithDWrite(big, dw)) return;

    EXPECT_NEAR(mixed.LineHeightDip(0), big.LineHeightDip(0), 0.01f);
    mixed.DetachFromContext();
    big.DetachFromContext();
}

// --- Ring buffer -------------------------------------------------------------

TEST(LogViewer, MaxLinesDefaultsToUnlimited)
{
    LogViewer v;
    EXPECT_EQ(v.MaxLines(), 0);

    for (int i = 0; i < 500; ++i)
        v.AppendLine({{L"line", kWhite}});
    EXPECT_EQ(v.LineCount(), size_t(500));
}

TEST(LogViewer, MaxLinesTrimsToTheCap)
{
    LogViewer v;
    v.SetMaxLines(3);
    for (int i = 0; i < 10; ++i)
        v.AppendLine({{L"line", kWhite}});
    EXPECT_EQ(v.LineCount(), size_t(3));
}

// Setting a cap below the current line count must trim immediately, not merely
// bound future appends.
TEST(LogViewer, SetMaxLinesTrimsExistingContent)
{
    LogViewer v;
    for (int i = 0; i < 20; ++i)
        v.AppendLine({{L"line", kWhite}});
    EXPECT_EQ(v.LineCount(), size_t(20));

    v.SetMaxLines(5);
    EXPECT_EQ(v.LineCount(), size_t(5));
}

TEST(LogViewer, NegativeMaxLinesClampsToUnlimited)
{
    LogViewer v;
    v.SetMaxLines(-10);
    EXPECT_EQ(v.MaxLines(), 0);
}

// The extent has to shrink with the buffer. If a trim left the old extent behind,
// the scroll range would keep growing forever and the thumb would shrink to
// nothing on a long-running log.
TEST(LogViewer, TrimShrinksContentExtent)
{
    LogViewer v;
    for (int i = 0; i < 20; ++i)
        v.AppendLine({{L"line", kWhite, 12.0f}});
    const float before = v.ContentExtentDip();

    v.SetMaxLines(5);
    const float after = v.ContentExtentDip();

    EXPECT_TRUE(after < before);
    EXPECT_NEAR(after, 12.0f * kLineSpacing * 5.0f, 0.01f);
}

// --- Tail following ----------------------------------------------------------

TEST(LogViewer, AutoScrollToTailDefaultsOn)
{
    LogViewer v;
    EXPECT_TRUE(v.AutoScrollToTail());
}

TEST(LogViewer, AutoScrollToTailIsSettable)
{
    LogViewer v;
    v.SetAutoScrollToTail(false);
    EXPECT_FALSE(v.AutoScrollToTail());
    v.SetAutoScrollToTail(true);
    EXPECT_TRUE(v.AutoScrollToTail());
}

// Following must survive an append. Recomputing "am I at the bottom" inside
// AppendText/AppendLine always concludes the user scrolled away — the append just
// grew the extent — which switches following off on the first batch. The flag is
// read there, never recalculated.
TEST(LogViewer, AppendDoesNotDisableTailFollowing)
{
    LogViewer v;
    v.Arrange(RectDip{0, 0, 400, 50});
    v.SetAutoScrollToTail(true);

    for (int i = 0; i < 100; ++i)
        v.AppendLine({{L"line", kWhite, 12.0f}});

    EXPECT_TRUE(v.AutoScrollToTail());
}

// --- Throughput --------------------------------------------------------------

// Not a timing assertion (this suite is headless and shares a machine) — it is a
// scaling one: 50 k appends must not be O(n²). If AppendLine ever rebuilt the
// Y-position table or the extent from scratch, this test would take minutes
// instead of milliseconds.
TEST(LogViewer, ManyAppendsStayLinear)
{
    LogViewer v;
    for (int i = 0; i < 50000; ++i)
        v.AppendLine({{L"log record", kWhite, 12.0f}});

    EXPECT_EQ(v.LineCount(), size_t(50000));
    EXPECT_NEAR(v.ContentExtentDip(), 12.0f * kLineSpacing * 50000.0f, 200.0f);
}

TEST(LogViewer, RingBufferHoldsSteadyUnderLoad)
{
    LogViewer v;
    v.SetMaxLines(1000);
    for (int i = 0; i < 50000; ++i)
        v.AppendLine({{L"log record", kWhite, 12.0f}});

    EXPECT_EQ(v.LineCount(), size_t(1000));
    EXPECT_NEAR(v.ContentExtentDip(), 12.0f * kLineSpacing * 1000.0f, 0.5f);
}

// --- Selection ---------------------------------------------------------------
//
// The hit test needs DWrite, so headless tests cannot drive selection by
// coordinates. What they CAN check is everything downstream of a selection range:
// SelectAll, the ordering/normalization rules, and how the range is turned back
// into text across line boundaries — which is the part with the off-by-one risks.
//
// The clipboard is deliberately not asserted: it is a process-wide resource, and a
// test that empties it would fight anything else running on the machine.

TEST(LogViewer, SelectionEnabledByDefault)
{
    LogViewer v;
    EXPECT_TRUE(v.SelectionEnabled());
    EXPECT_FALSE(v.HasSelection());
    EXPECT_TRUE(v.SelectedText().empty());
}

TEST(LogViewer, SelectAllOnEmptyViewerSelectsNothing)
{
    LogViewer v;
    v.SelectAll();
    EXPECT_FALSE(v.HasSelection());
    EXPECT_TRUE(v.SelectedText().empty());
}

// One line: the whole line, and no trailing newline. A trailing '\n' matters —
// pasted into a shell it would execute the copied text.
TEST(LogViewer, SelectAllSingleLineHasNoTrailingNewline)
{
    LogViewer v;
    v.AppendLine({{L"hello", kWhite}});
    v.SelectAll();

    EXPECT_TRUE(v.HasSelection());
    EXPECT_TRUE(v.SelectedText() == L"hello");
}

// Spans are a styling detail, not a text detail: a copied line is its spans
// concatenated with nothing inserted between them.
TEST(LogViewer, SelectedTextConcatenatesSpansWithoutSeparators)
{
    LogViewer v;
    v.AppendLine({
        {L"[", kWhite},
        {L"ERROR", kRed, 12.0f, DWRITE_FONT_WEIGHT_BOLD},
        {L"] disk full", kWhite},
    });
    v.SelectAll();
    EXPECT_TRUE(v.SelectedText() == L"[ERROR] disk full");
}

// Multi-line: '\n' BETWEEN lines only. Two lines produce exactly one separator.
TEST(LogViewer, SelectAllJoinsLinesWithSingleNewline)
{
    LogViewer v;
    v.AppendLine({{L"first", kWhite}});
    v.AppendLine({{L"second", kWhite}});
    v.AppendLine({{L"third", kWhite}});
    v.SelectAll();

    const std::wstring got = v.SelectedText();
    EXPECT_TRUE(got == L"first\nsecond\nthird");
    // Guard the count directly: an extra separator is easy to introduce and reads
    // as a blank line only when pasted somewhere.
    size_t newlines = 0;
    for (wchar_t c : got) if (c == L'\n') ++newlines;
    EXPECT_EQ(newlines, size_t(2));
}

// Trailing whitespace inside a span survives the copy. It carries alignment in a
// log, and the draw path already treats it as significant
// (widthIncludingTrailingWhitespace); the copy must agree.
TEST(LogViewer, SelectedTextPreservesTrailingSpacesInsideSpans)
{
    LogViewer v;
    v.AppendLine({{L"[ERROR] ", kRed}, {L"msg", kWhite}});
    v.SelectAll();
    EXPECT_TRUE(v.SelectedText() == L"[ERROR] msg");
}

TEST(LogViewer, ClearSelectionDropsIt)
{
    LogViewer v;
    v.AppendLine({{L"text", kWhite}});
    v.SelectAll();
    EXPECT_TRUE(v.HasSelection());

    v.ClearSelection();
    EXPECT_FALSE(v.HasSelection());
    EXPECT_TRUE(v.SelectedText().empty());
}

// Disabling selection must also drop whatever was selected — leaving a highlight on
// screen that the user can no longer change or clear would be worse than either
// state on its own.
TEST(LogViewer, DisablingSelectionClearsExistingSelection)
{
    LogViewer v;
    v.AppendLine({{L"text", kWhite}});
    v.SelectAll();
    EXPECT_TRUE(v.HasSelection());

    v.SetSelectionEnabled(false);
    EXPECT_FALSE(v.SelectionEnabled());
    EXPECT_FALSE(v.HasSelection());
}

// Clear() wipes the buffer, so any selection into it now points at text that is
// gone. It must not survive.
TEST(LogViewer, ClearAlsoInvalidatesSelection)
{
    LogViewer v;
    v.AppendLine({{L"a", kWhite}});
    v.AppendLine({{L"b", kWhite}});
    v.SelectAll();
    EXPECT_TRUE(v.HasSelection());

    v.Clear();
    EXPECT_FALSE(v.HasSelection());
    EXPECT_TRUE(v.SelectedText().empty());
}

// A ring-buffer trim RENUMBERS lines: old line N+k becomes line k. A stored
// (line, column) therefore points at different text than the user highlighted, and
// keeping it would silently copy the wrong thing. Dropping it is the correct
// answer, not a limitation.
TEST(LogViewer, TrimDropsSelectionBecauseLinesAreRenumbered)
{
    LogViewer v;
    v.SetMaxLines(3);
    for (int i = 0; i < 3; ++i)
        v.AppendLine({{L"line", kWhite}});
    v.SelectAll();
    EXPECT_TRUE(v.HasSelection());

    // This append pushes the buffer over the cap and trims the oldest line.
    v.AppendLine({{L"overflow", kWhite}});
    EXPECT_EQ(v.LineCount(), size_t(3));
    EXPECT_FALSE(v.HasSelection());
}

// --- Keyboard selection (scheme A: no caret) --------------------------------
//
// The anchor is set by a plain click (SetSelectionPoint headless); Shift+keys move
// only the other end. Plain navigation keys scroll and must NOT touch the selection.

namespace {
KeyEventArgs Key(UINT vk, ModifierKeys mods) {
    KeyEventArgs e;
    e.vk = vk;
    e.modifiers = mods;
    return e;
}
constexpr ModifierKeys kShift = ModifierKeys::Shift;
const ModifierKeys kCtrlShift = ModifierKeys::Ctrl | ModifierKeys::Shift;
} // namespace

TEST(LogViewerKeys, ShiftRightExtendsByOneCharacter)
{
    LogViewer v;
    v.AppendLine({{L"hello world", kWhite}});
    v.SetSelectionPoint({0, 0});
    auto e = Key(VK_RIGHT, kShift);
    v.OnKeyDownRouted(e);
    EXPECT_TRUE(e.handled);
    EXPECT_TRUE(v.SelectedText() == L"h");
    auto e2 = Key(VK_RIGHT, kShift);
    v.OnKeyDownRouted(e2);
    EXPECT_TRUE(v.SelectedText() == L"he");
}

TEST(LogViewerKeys, ShiftLeftExtendsBackwardsAndKeepsAnchor)
{
    LogViewer v;
    v.AppendLine({{L"hello", kWhite}});
    v.SetSelectionPoint({0, 5});
    auto e = Key(VK_LEFT, kShift);
    v.OnKeyDownRouted(e);
    EXPECT_TRUE(v.SelectedText() == L"o");
    EXPECT_EQ(v.SelectionAnchor().column, size_t(5));
    EXPECT_EQ(v.SelectionActiveEnd().column, size_t(4));
}

TEST(LogViewerKeys, ShiftEndSelectsToLineEnd)
{
    LogViewer v;
    v.AppendLine({{L"[ERROR] ", kRed}, {L"disk full", kWhite}});
    v.SetSelectionPoint({0, 8});
    auto e = Key(VK_END, kShift);
    v.OnKeyDownRouted(e);
    EXPECT_TRUE(v.SelectedText() == L"disk full");   // across the span boundary
}

TEST(LogViewerKeys, ShiftHomeSelectsToLineStart)
{
    LogViewer v;
    v.AppendLine({{L"hello world", kWhite}});
    v.SetSelectionPoint({0, 5});
    auto e = Key(VK_HOME, kShift);
    v.OnKeyDownRouted(e);
    EXPECT_TRUE(v.SelectedText() == L"hello");
}

TEST(LogViewerKeys, CtrlShiftEndSelectsToDocumentEnd)
{
    LogViewer v;
    v.AppendLine({{L"first", kWhite}});
    v.AppendLine({{L"second", kWhite}});
    v.AppendLine({{L"third", kWhite}});
    v.SetSelectionPoint({1, 0});
    auto e = Key(VK_END, kCtrlShift);
    v.OnKeyDownRouted(e);
    EXPECT_TRUE(v.SelectedText() == L"second\nthird");
}

TEST(LogViewerKeys, CtrlShiftHomeSelectsToDocumentStart)
{
    LogViewer v;
    v.AppendLine({{L"first", kWhite}});
    v.AppendLine({{L"second", kWhite}});
    v.SetSelectionPoint({1, 3});
    auto e = Key(VK_HOME, kCtrlShift);
    v.OnKeyDownRouted(e);
    EXPECT_TRUE(v.SelectedText() == L"first\nsec");
}

TEST(LogViewerKeys, ShiftRightAtLineEndCrossesTheLineBreak)
{
    LogViewer v;
    v.AppendLine({{L"ab", kWhite}});
    v.AppendLine({{L"cd", kWhite}});
    v.SetSelectionPoint({0, 2});
    auto e = Key(VK_RIGHT, kShift);
    v.OnKeyDownRouted(e);
    EXPECT_EQ(v.SelectionActiveEnd().line, size_t(1));
    EXPECT_EQ(v.SelectionActiveEnd().column, size_t(0));
    EXPECT_TRUE(v.SelectedText() == L"\n");
}

TEST(LogViewerKeys, ShiftDownExtendsToNextLineSameColumn)
{
    LogViewer v;
    v.AppendLine({{L"abcdef", kWhite}});
    v.AppendLine({{L"ghijkl", kWhite}});
    v.SetSelectionPoint({0, 2});
    auto e = Key(VK_DOWN, kShift);
    v.OnKeyDownRouted(e);
    EXPECT_TRUE(v.SelectedText() == L"cdef\ngh");
}

TEST(LogViewerKeys, CtrlShiftRightExtendsByWord)
{
    LogViewer v;
    v.AppendLine({{L"alpha beta gamma", kWhite}});
    v.SetSelectionPoint({0, 0});
    auto e = Key(VK_RIGHT, kCtrlShift);
    v.OnKeyDownRouted(e);
    // Lands on the next word start, like Ctrl+Right in TextBox.
    EXPECT_EQ(v.SelectionActiveEnd().column, size_t(6));
}

// The defining property of scheme A: looking around must not destroy the selection.
TEST(LogViewerKeys, PlainNavigationKeysDoNotTouchSelection)
{
    LogViewer v;
    v.Arrange(RectDip{0, 0, 400, 30});
    for (int i = 0; i < 50; ++i) v.AppendLine({{L"line", kWhite, 12.0f}});
    v.SetAutoScrollToTail(false);
    v.SetSelectionPoint({2, 0});
    auto s = Key(VK_END, kShift);
    v.OnKeyDownRouted(s);
    const std::wstring before = v.SelectedText();
    EXPECT_FALSE(before.empty());

    for (UINT vk : {VK_DOWN, VK_NEXT, VK_UP, VK_PRIOR}) {
        auto e = Key(vk, ModifierKeys::None);
        v.OnKeyDownRouted(e);
        EXPECT_TRUE(e.handled);                 // consumed as a scroll
    }
    EXPECT_TRUE(v.SelectedText() == before);
}

TEST(LogViewerKeys, PageDownScrollsTheView)
{
    LogViewer v;
    v.Arrange(RectDip{0, 0, 400, 60});
    for (int i = 0; i < 100; ++i) v.AppendLine({{L"line", kWhite, 12.0f}});
    v.SetAutoScrollToTail(false);
    // AppendLine does not scroll with tail-follow off, so the view starts at the top.
    auto e = Key(VK_NEXT, ModifierKeys::None);
    v.OnKeyDownRouted(e);
    auto end = Key(VK_END, ModifierKeys::Ctrl);
    v.OnKeyDownRouted(end);
    EXPECT_TRUE(e.handled);
    EXPECT_TRUE(end.handled);
}

TEST(LogViewerKeys, ShiftKeysIgnoredWhenSelectionDisabled)
{
    LogViewer v;
    v.AppendLine({{L"hello", kWhite}});
    v.SetSelectionEnabled(false);
    auto e = Key(VK_END, kShift);
    v.OnKeyDownRouted(e);
    EXPECT_FALSE(v.HasSelection());
}

// An empty line inside a selection contributes its (zero) text plus the separator,
// so a blank row in the middle of a copied block stays blank rather than vanishing.
TEST(LogViewer, EmptySpanTextInsideSelectionStillSeparatesLines)
{
    LogViewer v;
    v.AppendLine({{L"first", kWhite}});
    v.AppendLine({{L"", kWhite}});     // a span with no text
    v.AppendLine({{L"third", kWhite}});
    v.SelectAll();

    EXPECT_TRUE(v.SelectedText() == L"first\n\nthird");
}
