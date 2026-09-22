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
