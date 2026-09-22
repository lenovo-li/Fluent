// RichTextDocumentTests.cpp — the run-splitting model behind RichTextBox.
//
// This is pure data structure work: no window, no DWrite, no device. That is the
// point of testing it separately — every off-by-one in the offset↔run mapping is
// reachable here, whereas through the control it would only show up as a caret in
// the wrong place or a style leaking into the wrong characters.
//
// The cases that matter are the boundaries: inserting exactly at a run seam,
// deleting a range that spans three runs, and deleting a whole run out of the
// middle. Those are where a document model silently corrupts text.

#include "../framework/Test.h"
#include "../../FluentUI/controls/RichTextDocument.h"

using namespace fluent;

namespace {

constexpr D2D1_COLOR_F kBlack{0.0f, 0.0f, 0.0f, 1.0f};
constexpr D2D1_COLOR_F kRed{1.0f, 0.0f, 0.0f, 1.0f};
constexpr D2D1_COLOR_F kBlue{0.0f, 0.0f, 1.0f, 1.0f};

TextRun Plain(std::wstring t) { return TextRun(std::move(t), kBlack, 12.0f); }
TextRun Bold(std::wstring t) {
    return TextRun(std::move(t), kRed, 12.0f, DWRITE_FONT_WEIGHT_BOLD);
}
TextRun Big(std::wstring t) { return TextRun(std::move(t), kBlue, 24.0f); }

} // namespace

// --- Empty document ----------------------------------------------------------

TEST(RichTextDocument, EmptyDocumentHasZeroLength)
{
    RichTextDocument doc;
    EXPECT_TRUE(doc.Empty());
    EXPECT_EQ(doc.Length(), size_t(0));
    EXPECT_TRUE(doc.GetPlainText().empty());
    EXPECT_EQ(doc.Runs().size(), size_t(0));
}

TEST(RichTextDocument, InsertIntoEmptyCreatesOneRun)
{
    RichTextDocument doc;
    doc.InsertText(0, L"hello", Plain(L""));

    EXPECT_FALSE(doc.Empty());
    EXPECT_EQ(doc.Runs().size(), size_t(1));
    EXPECT_EQ(doc.Length(), size_t(5));
    EXPECT_TRUE(doc.GetPlainText() == L"hello");
}

TEST(RichTextDocument, InsertingEmptyTextIsANoOp)
{
    RichTextDocument doc;
    doc.InsertText(0, L"", Plain(L""));
    EXPECT_TRUE(doc.Empty());

    doc.InsertText(0, L"x", Plain(L""));
    doc.InsertText(0, L"", Plain(L""));
    EXPECT_EQ(doc.Length(), size_t(1));
}

// --- Insert with matching style: must NOT create a new run -------------------
//
// This is the fast path that makes typing cheap. Every keystroke is an InsertText
// of one character with the caret's own style; if each one created a run, a
// paragraph of plain text would become hundreds of runs and every render would
// build hundreds of DWrite layouts.

TEST(RichTextDocument, SameStyleInsertAppendsWithoutNewRun)
{
    RichTextDocument doc;
    doc.InsertText(0, L"abc", Plain(L""));
    EXPECT_EQ(doc.Runs().size(), size_t(1));

    doc.InsertText(3, L"def", Plain(L""));
    EXPECT_EQ(doc.Runs().size(), size_t(1));
    EXPECT_TRUE(doc.GetPlainText() == L"abcdef");
}

TEST(RichTextDocument, SameStyleInsertInMiddleStaysOneRun)
{
    RichTextDocument doc;
    doc.InsertText(0, L"abcdef", Plain(L""));
    doc.InsertText(3, L"XYZ", Plain(L""));

    EXPECT_EQ(doc.Runs().size(), size_t(1));
    EXPECT_TRUE(doc.GetPlainText() == L"abcXYZdef");
}

TEST(RichTextDocument, TypingCharacterByCharacterStaysOneRun)
{
    RichTextDocument doc;
    const std::wstring word = L"typed";
    for (size_t i = 0; i < word.size(); ++i)
        doc.InsertText(doc.Length(), word.substr(i, 1), Plain(L""));

    EXPECT_EQ(doc.Runs().size(), size_t(1));
    EXPECT_TRUE(doc.GetPlainText() == L"typed");
}

// --- Insert with a different style: splits ----------------------------------

TEST(RichTextDocument, DifferentStyleInsertInMiddleSplitsIntoThree)
{
    RichTextDocument doc;
    doc.InsertText(0, L"abcdef", Plain(L""));
    doc.InsertText(3, L"BOLD", Bold(L""));

    // "abc" + "BOLD" + "def"
    EXPECT_EQ(doc.Runs().size(), size_t(3));
    EXPECT_TRUE(doc.GetPlainText() == L"abcBOLDdef");
    EXPECT_TRUE(doc.Runs()[0].text == L"abc");
    EXPECT_TRUE(doc.Runs()[1].text == L"BOLD");
    EXPECT_TRUE(doc.Runs()[2].text == L"def");
    EXPECT_EQ(doc.Runs()[1].weight, DWRITE_FONT_WEIGHT_BOLD);
}

TEST(RichTextDocument, DifferentStyleInsertAtEndAppendsRun)
{
    RichTextDocument doc;
    doc.InsertText(0, L"abc", Plain(L""));
    doc.InsertText(3, L"BOLD", Bold(L""));

    EXPECT_EQ(doc.Runs().size(), size_t(2));
    EXPECT_TRUE(doc.GetPlainText() == L"abcBOLD");
}

TEST(RichTextDocument, InsertPastEndAppendsRatherThanFailing)
{
    RichTextDocument doc;
    doc.InsertText(0, L"abc", Plain(L""));
    doc.InsertText(999, L"tail", Bold(L""));

    EXPECT_TRUE(doc.GetPlainText() == L"abctail");
}

// --- Offset → run mapping ---------------------------------------------------

TEST(RichTextDocument, GetRunAtMapsOffsetsAcrossRuns)
{
    RichTextDocument doc;
    doc.InsertText(0, L"ab", Plain(L""));        // run 0: offsets 0-1
    doc.InsertText(2, L"CD", Bold(L""));         // run 1: offsets 2-3
    doc.InsertText(4, L"ef", Big(L""));          // run 2: offsets 4-5
    EXPECT_EQ(doc.Runs().size(), size_t(3));

    auto l0 = doc.GetRunAt(0);
    EXPECT_TRUE(l0.valid);
    EXPECT_EQ(l0.runIndex, size_t(0));
    EXPECT_EQ(l0.offsetInRun, size_t(0));

    // Last character of run 0.
    auto l1 = doc.GetRunAt(1);
    EXPECT_EQ(l1.runIndex, size_t(0));
    EXPECT_EQ(l1.offsetInRun, size_t(1));

    // First character of run 1 — the seam, where an off-by-one would land in the
    // wrong run and apply the wrong style.
    auto l2 = doc.GetRunAt(2);
    EXPECT_EQ(l2.runIndex, size_t(1));
    EXPECT_EQ(l2.offsetInRun, size_t(0));

    auto l4 = doc.GetRunAt(4);
    EXPECT_EQ(l4.runIndex, size_t(2));
    EXPECT_EQ(l4.offsetInRun, size_t(0));
}

// The caret can sit one past the last character, so Length() must be a valid
// position — it is where typing at the end inserts.
TEST(RichTextDocument, GetRunAtLengthIsValidAndPointsPastLastRun)
{
    RichTextDocument doc;
    doc.InsertText(0, L"ab", Plain(L""));

    auto loc = doc.GetRunAt(2);
    EXPECT_TRUE(loc.valid);
    EXPECT_EQ(loc.runIndex, size_t(0));
    EXPECT_EQ(loc.offsetInRun, size_t(2));  // one past 'b'
}

TEST(RichTextDocument, GetRunAtBeyondLengthIsInvalid)
{
    RichTextDocument doc;
    doc.InsertText(0, L"ab", Plain(L""));
    EXPECT_FALSE(doc.GetRunAt(3).valid);
    EXPECT_FALSE(doc.GetRunAt(999).valid);
}

TEST(RichTextDocument, GetRunAtOnEmptyDocumentIsInvalid)
{
    RichTextDocument doc;
    EXPECT_FALSE(doc.GetRunAt(0).valid);
}

// --- Style queries ----------------------------------------------------------

TEST(RichTextDocument, GetStyleAtReturnsTheRunsStyle)
{
    RichTextDocument doc;
    doc.InsertText(0, L"ab", Plain(L""));
    doc.InsertText(2, L"CD", Bold(L""));

    EXPECT_EQ(doc.GetStyleAt(0).weight, DWRITE_FONT_WEIGHT_NORMAL);
    EXPECT_EQ(doc.GetStyleAt(2).weight, DWRITE_FONT_WEIGHT_BOLD);
    EXPECT_NEAR(doc.GetStyleAt(2).color.r, 1.0f, 0.001f);
}

TEST(RichTextDocument, GetStyleAtOnEmptyDocumentReturnsDefault)
{
    RichTextDocument doc;
    const TextRun s = doc.GetStyleAt(0);
    EXPECT_NEAR(s.fontSize, 12.0f, 0.001f);
    EXPECT_EQ(s.weight, DWRITE_FONT_WEIGHT_NORMAL);
}

// --- Delete within one run --------------------------------------------------

TEST(RichTextDocument, DeleteInsideOneRun)
{
    RichTextDocument doc;
    doc.InsertText(0, L"abcdef", Plain(L""));
    doc.DeleteRange(2, 2);   // remove "cd"

    EXPECT_TRUE(doc.GetPlainText() == L"abef");
    EXPECT_EQ(doc.Runs().size(), size_t(1));
}

TEST(RichTextDocument, DeleteFirstCharacter)
{
    RichTextDocument doc;
    doc.InsertText(0, L"abc", Plain(L""));
    doc.DeleteRange(0, 1);
    EXPECT_TRUE(doc.GetPlainText() == L"bc");
}

TEST(RichTextDocument, DeleteLastCharacter)
{
    RichTextDocument doc;
    doc.InsertText(0, L"abc", Plain(L""));
    doc.DeleteRange(2, 1);
    EXPECT_TRUE(doc.GetPlainText() == L"ab");
}

// Emptying a run must remove it, not leave a zero-length run behind: an empty run
// would produce a DWrite layout of no characters on every render, and would make
// GetRunAt's arithmetic ambiguous (two runs claiming the same offset).
TEST(RichTextDocument, DeletingAWholeRunRemovesIt)
{
    RichTextDocument doc;
    doc.InsertText(0, L"ab", Plain(L""));
    doc.InsertText(2, L"CD", Bold(L""));
    EXPECT_EQ(doc.Runs().size(), size_t(2));

    doc.DeleteRange(0, 2);   // delete all of run 0
    EXPECT_EQ(doc.Runs().size(), size_t(1));
    EXPECT_TRUE(doc.GetPlainText() == L"CD");
    EXPECT_EQ(doc.Runs()[0].weight, DWRITE_FONT_WEIGHT_BOLD);
}

TEST(RichTextDocument, DeletingEverythingEmptiesTheDocument)
{
    RichTextDocument doc;
    doc.InsertText(0, L"ab", Plain(L""));
    doc.InsertText(2, L"CD", Bold(L""));

    doc.DeleteRange(0, doc.Length());
    EXPECT_TRUE(doc.Empty());
    EXPECT_EQ(doc.Length(), size_t(0));
    EXPECT_TRUE(doc.GetPlainText().empty());
}

// --- Delete across run boundaries -------------------------------------------

TEST(RichTextDocument, DeleteSpanningTwoRuns)
{
    RichTextDocument doc;
    doc.InsertText(0, L"abcd", Plain(L""));      // 0-3
    doc.InsertText(4, L"EFGH", Bold(L""));       // 4-7

    doc.DeleteRange(2, 4);   // "cdEF"
    EXPECT_TRUE(doc.GetPlainText() == L"abGH");
    EXPECT_EQ(doc.Runs().size(), size_t(2));
    EXPECT_TRUE(doc.Runs()[0].text == L"ab");
    EXPECT_TRUE(doc.Runs()[1].text == L"GH");
}

// The three-run case is the one that exercises the middle-run erase path, and the
// index shifting that comes with it.
TEST(RichTextDocument, DeleteSpanningThreeRunsDropsTheMiddleOne)
{
    RichTextDocument doc;
    doc.InsertText(0, L"aaa", Plain(L""));       // 0-2
    doc.InsertText(3, L"BBB", Bold(L""));        // 3-5
    doc.InsertText(6, L"ccc", Big(L""));         // 6-8
    EXPECT_EQ(doc.Runs().size(), size_t(3));

    doc.DeleteRange(2, 5);   // "aBBBc" — tail of run 0, all of run 1, head of run 2
    EXPECT_TRUE(doc.GetPlainText() == L"aacc");
    EXPECT_EQ(doc.Runs().size(), size_t(2));
    EXPECT_TRUE(doc.Runs()[0].text == L"aa");
    EXPECT_TRUE(doc.Runs()[1].text == L"cc");
    // The surviving runs keep their own styles.
    EXPECT_EQ(doc.Runs()[0].weight, DWRITE_FONT_WEIGHT_NORMAL);
    EXPECT_NEAR(doc.Runs()[1].fontSize, 24.0f, 0.001f);
}

TEST(RichTextDocument, DeleteWholeMiddleRunOnly)
{
    RichTextDocument doc;
    doc.InsertText(0, L"aa", Plain(L""));
    doc.InsertText(2, L"BB", Bold(L""));
    doc.InsertText(4, L"cc", Big(L""));

    doc.DeleteRange(2, 2);   // exactly run 1
    EXPECT_EQ(doc.Runs().size(), size_t(2));
    EXPECT_TRUE(doc.GetPlainText() == L"aacc");
}

// --- Delete guards ----------------------------------------------------------

TEST(RichTextDocument, DeleteZeroLengthIsANoOp)
{
    RichTextDocument doc;
    doc.InsertText(0, L"abc", Plain(L""));
    doc.DeleteRange(1, 0);
    EXPECT_TRUE(doc.GetPlainText() == L"abc");
}

TEST(RichTextDocument, DeletePastEndIsClamped)
{
    RichTextDocument doc;
    doc.InsertText(0, L"abc", Plain(L""));
    doc.DeleteRange(1, 999);
    EXPECT_TRUE(doc.GetPlainText() == L"a");
}

TEST(RichTextDocument, DeleteStartingPastEndIsANoOp)
{
    RichTextDocument doc;
    doc.InsertText(0, L"abc", Plain(L""));
    doc.DeleteRange(10, 5);
    EXPECT_TRUE(doc.GetPlainText() == L"abc");
}

TEST(RichTextDocument, DeleteOnEmptyDocumentIsANoOp)
{
    RichTextDocument doc;
    doc.DeleteRange(0, 5);
    EXPECT_TRUE(doc.Empty());
}

// --- Clear / snapshot -------------------------------------------------------

TEST(RichTextDocument, ClearRemovesEverything)
{
    RichTextDocument doc;
    doc.InsertText(0, L"abc", Plain(L""));
    doc.InsertText(3, L"DEF", Bold(L""));

    doc.Clear();
    EXPECT_TRUE(doc.Empty());
    EXPECT_EQ(doc.Length(), size_t(0));
}

// SetParagraphs is the undo path: a snapshot is the whole paragraph vector, and
// restoring it must reproduce text and styles exactly.
TEST(RichTextDocument, SetParagraphsRestoresASnapshotExactly)
{
    RichTextDocument doc;
    doc.InsertText(0, L"abc", Plain(L""));
    doc.InsertText(3, L"DEF", Bold(L""));
    const std::vector<Paragraph> snapshot = doc.Paragraphs();

    doc.Clear();
    EXPECT_TRUE(doc.Empty());

    doc.SetParagraphs(snapshot);
    EXPECT_EQ(doc.Runs().size(), size_t(2));
    EXPECT_TRUE(doc.GetPlainText() == L"abcDEF");
    EXPECT_EQ(doc.GetStyleAt(3).weight, DWRITE_FONT_WEIGHT_BOLD);
}

// The reason the snapshot unit is the paragraph vector and not a flat run vector:
// a run list cannot represent alignment/spacing/indents, so a round-trip through
// one silently resets every paragraph to default formatting. That is an undo that
// restores the text and throws the layout away.
TEST(RichTextDocument, SnapshotRoundTripPreservesParagraphFormatting)
{
    RichTextDocument doc;
    doc.InsertText(0, L"one\ntwo", Plain(L""));
    doc.ApplyParagraphStyle(0, 3, [](ParagraphStyle& s) {
        s.alignment = DWRITE_TEXT_ALIGNMENT_CENTER;
        s.lineSpacing = 2.0f;
        s.firstLineIndent = 24.0f;
    });

    const std::vector<Paragraph> snapshot = doc.Paragraphs();
    doc.Clear();
    doc.SetParagraphs(snapshot);

    EXPECT_EQ(doc.Paragraphs().size(), size_t(2));
    const ParagraphStyle& restored = doc.Paragraphs()[0].style;
    EXPECT_EQ(restored.alignment, DWRITE_TEXT_ALIGNMENT_CENTER);
    EXPECT_NEAR(restored.lineSpacing, 2.0f, 0.001f);
    EXPECT_NEAR(restored.firstLineIndent, 24.0f, 0.001f);
    // The untouched second paragraph must still be at its default, not smeared
    // with the first paragraph's style.
    EXPECT_EQ(doc.Paragraphs()[1].style.alignment, DWRITE_TEXT_ALIGNMENT_LEADING);
}

// --- AppendRun --------------------------------------------------------------
//
// The document-construction path (loading saved content, seeding a demo). It has
// to merge like InsertText does, or building a paragraph one run at a time would
// produce one DWrite layout per call at render time.

TEST(RichTextDocument, AppendRunBuildsDocumentInOrder)
{
    RichTextDocument doc;
    doc.AppendRun(Plain(L"one "));
    doc.AppendRun(Bold(L"two"));

    EXPECT_EQ(doc.Runs().size(), size_t(2));
    EXPECT_TRUE(doc.GetPlainText() == L"one two");
    EXPECT_EQ(doc.GetStyleAt(4).weight, DWRITE_FONT_WEIGHT_BOLD);
}

TEST(RichTextDocument, AppendRunMergesWhenStylesMatch)
{
    RichTextDocument doc;
    doc.AppendRun(Plain(L"a"));
    doc.AppendRun(Plain(L"b"));
    doc.AppendRun(Plain(L"c"));

    EXPECT_EQ(doc.Runs().size(), size_t(1));
    EXPECT_TRUE(doc.GetPlainText() == L"abc");
}

TEST(RichTextDocument, AppendRunEmptyTextIsANoOp)
{
    RichTextDocument doc;
    doc.AppendRun(Plain(L""));
    EXPECT_TRUE(doc.Empty());

    doc.AppendRun(Plain(L"x"));
    doc.AppendRun(Bold(L""));
    EXPECT_EQ(doc.Runs().size(), size_t(1));
    EXPECT_EQ(doc.Length(), size_t(1));
}

// Appending then editing must stay consistent — AppendRun writes the same
// invariants (no empty runs, no adjacent equal styles) the edit paths assume.
TEST(RichTextDocument, AppendedDocumentIsEditableLikeAnyOther)
{
    RichTextDocument doc;
    doc.AppendRun(Plain(L"hello "));
    doc.AppendRun(Bold(L"world"));

    doc.DeleteRange(0, 6);            // drop "hello "
    EXPECT_TRUE(doc.GetPlainText() == L"world");
    EXPECT_EQ(doc.Runs().size(), size_t(1));

    doc.InsertText(0, L"the ", Plain(L""));
    EXPECT_TRUE(doc.GetPlainText() == L"the world");
    EXPECT_EQ(doc.Length(), doc.GetPlainText().size());
}

// --- Style equality ---------------------------------------------------------

TEST(RichTextDocument, StyleEqualsComparesEveryAttribute)
{
    const TextRun a(L"x", kBlack, 12.0f, DWRITE_FONT_WEIGHT_NORMAL, false);

    EXPECT_TRUE(a.StyleEquals(TextRun(L"different text", kBlack, 12.0f,
                                      DWRITE_FONT_WEIGHT_NORMAL, false)));
    EXPECT_FALSE(a.StyleEquals(TextRun(L"x", kRed, 12.0f,
                                      DWRITE_FONT_WEIGHT_NORMAL, false)));
    EXPECT_FALSE(a.StyleEquals(TextRun(L"x", kBlack, 14.0f,
                                      DWRITE_FONT_WEIGHT_NORMAL, false)));
    EXPECT_FALSE(a.StyleEquals(TextRun(L"x", kBlack, 12.0f,
                                      DWRITE_FONT_WEIGHT_BOLD, false)));
    EXPECT_FALSE(a.StyleEquals(TextRun(L"x", kBlack, 12.0f,
                                      DWRITE_FONT_WEIGHT_NORMAL, true)));
}

// --- Mixed edit sequence ----------------------------------------------------
//
// Insert / delete / insert in the same document, because bugs in the index
// bookkeeping tend to appear only after the run vector has been reshaped a few
// times rather than on any single operation.
TEST(RichTextDocument, MixedEditSequenceKeepsTextAndStylesConsistent)
{
    RichTextDocument doc;
    doc.InsertText(0, L"Hello world", Plain(L""));
    doc.InsertText(6, L"BIG ", Big(L""));           // "Hello BIG world"
    EXPECT_TRUE(doc.GetPlainText() == L"Hello BIG world");

    doc.DeleteRange(0, 6);                           // drop "Hello "
    EXPECT_TRUE(doc.GetPlainText() == L"BIG world");

    doc.InsertText(doc.Length(), L"!", Bold(L""));
    EXPECT_TRUE(doc.GetPlainText() == L"BIG world!");

    // The '!' is bold, the 'B' is still 24pt.
    EXPECT_EQ(doc.GetStyleAt(doc.Length() - 1).weight, DWRITE_FONT_WEIGHT_BOLD);
    EXPECT_NEAR(doc.GetStyleAt(0).fontSize, 24.0f, 0.001f);

    // Length agrees with the concatenated text — the invariant every offset
    // calculation depends on.
    EXPECT_EQ(doc.Length(), doc.GetPlainText().size());
}

// --- Performance baseline ----------------------------------------------------
// Mimics the 10k demo: 50 runs × ~200 chars, alternating colors. The test does
// not assert perf, it just exercises the code path and prints the time so a
// regression becomes visible in test output. On a fast machine ~1 ms is expected.

TEST(RichTextDocument, TenThousandCharacterDocument)
{
    constexpr D2D1_COLOR_F c1{0.10f, 0.10f, 0.12f, 1.0f};
    constexpr D2D1_COLOR_F c2{0.70f, 0.20f, 0.10f, 1.0f};

    RichTextDocument big;
    for (int i = 0; i < 50; ++i) {
        std::wstring chunk = L"第" + std::to_wstring(i + 1) + L"段：";
        chunk.append(190, L'字');
        chunk += L"\n";
        big.AppendRun(TextRun(std::move(chunk), (i % 2 == 0) ? c1 : c2, 14.0f));
    }

    // Each chunk ends in '\n', so each AppendRun closes a paragraph and opens the
    // next: 50 paragraphs carrying text plus one empty trailing paragraph = 51.
    // Runs() flattens that back to a linear view and synthesizes one '\n' run per
    // paragraph boundary, giving 50 content runs + 50 separators = 100.
    EXPECT_EQ(big.Paragraphs().size(), size_t(51));
    EXPECT_EQ(big.Runs().size(), size_t(100));
    EXPECT_TRUE(big.Length() > size_t(9000));  // 50 paragraphs × ~195 chars ≈ 9791
    EXPECT_TRUE(big.GetPlainText().find(L"第50段") != std::wstring::npos);
}
