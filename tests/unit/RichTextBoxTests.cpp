// RichTextBoxTests.cpp — the control, with a real DirectWrite layout behind it.
//
// RichTextDocumentTests covers the offset↔run bookkeeping with no graphics. This
// file covers what only appears once DWrite is involved: wrap vs no-wrap metrics,
// the content extents that drive the scrollbars, and that a 10k-character document
// lays out at all rather than quietly failing or taking seconds.
//
// There is no HWND here, so nothing about caret blinking, IME, or actual painting
// is exercised — those stay manual-verification items.

#include "../framework/Test.h"
#include "../../FluentUI/controls/RichTextBox.h"
#include "../../FluentUI/controls/RichTextDocument.h"
#include "../../FluentUI/core/UIContext.h"
#include "../../FluentUI/window/WindowServices.h"
#include "../../FluentUI/graphics/D2DContext.h"
#include "../../FluentUI/graphics/DWriteContext.h"
#include "../../FluentUI/input/FocusManager.h"
#include "../../FluentUI/input/InputManager.h"
#include "ClipboardTestHelpers.h"

#include <chrono>
#include <cstdio>
#include <string>

using namespace fluent;

namespace {

class MockHost : public WindowServices {
public:
    // DWriteContext does nothing until Initialize() creates the factory. Without
    // it RebuildLayout returns early and every extent collapses to bare padding —
    // the tests would then "pass" against a control that never laid anything out.
    MockHost() { dwrite_.Initialize(); }

    HINSTANCE Instance() const override { return nullptr; }
    HWND Hwnd() const override { return nullptr; }
    float DpiScale() const override { return 1.0f; }
    D2DContext& D2D() override { return d2d_; }
    DWriteContext& DWrite() override { return dwrite_; }
    ICompositionBackend* Composition() override { return nullptr; }
    Subscription RegisterActivePopupDismiss(
        std::function<bool(PopupDismissReason, HWND, int, int)>) override { return {}; }
    Subscription RegisterActivePopupKeyHandler(
        std::function<bool(UINT)>) override { return {}; }
    Subscription RegisterActivePopupWheelHandler(
        std::function<bool(int, int, int)>) override { return {}; }
private:
    D2DContext d2d_;
    DWriteContext dwrite_;
};

UIContext MakeCtx(MockHost& host) {
    UIContext c;
    c.window = &host;
    c.dwrite = &host.DWrite();
    c.dpiScale = 1.0f;
    return c;
}

constexpr D2D1_COLOR_F kInk{0.1f, 0.1f, 0.12f, 1.0f};
constexpr D2D1_COLOR_F kRed{0.7f, 0.2f, 0.1f, 1.0f};

// A box arranged into a 400x200 viewport, which is narrow enough that the long
// single-line documents below overflow it in both axes.
struct Fixture {
    MockHost host;
    RichTextBox box;

    Fixture() {
        box.AttachToContext(MakeCtx(host));
        Layout();
    }

    void Layout() {
        box.Measure(400.0f, 200.0f);
        box.Arrange(RectDip{0.0f, 0.0f, 400.0f, 200.0f});
    }

    void SetOneLine(std::wstring text) {
        RichTextDocument doc;
        doc.AppendRun(TextRun(std::move(text), kInk, 14.0f));
        box.SetDocument(std::move(doc));
        Layout();
    }
};

// 50 runs of ~200 chars, alternating colours — the same shape the gallery's
// "生成 10k 字" button builds. Alternating colours matter: a single colour would
// be merged into one run and would not exercise the per-run styling loop.
RichTextDocument MakeBigDocument() {
    RichTextDocument big;
    for (int i = 0; i < 50; ++i) {
        std::wstring chunk = L"第" + std::to_wstring(i + 1) + L"段：";
        chunk.append(190, L'字');
        chunk += L"\n";
        big.AppendRun(TextRun(std::move(chunk), (i % 2 == 0) ? kInk : kRed, 14.0f));
    }
    return big;
}

} // namespace

// --- Wrap mode ---------------------------------------------------------------

TEST(RichTextBox, WrapsByDefault)
{
    // Wrap is the default because it is the mode where text can never be lost off
    // the right edge; NoWrap has to be asked for.
    Fixture f;
    EXPECT_TRUE(f.box.WordWrap());
}

TEST(RichTextBox, WrappedLongLineGrowsTallNotWide)
{
    Fixture f;
    f.SetOneLine(std::wstring(2000, L'字'));

    // Wrapped: the text reflows inside the viewport, so it grows downwards and
    // never needs horizontal scrolling.
    EXPECT_TRUE(f.box.ContentHeight() > 200.0f);
    EXPECT_FALSE(f.box.HorizontallyScrollable());
}

TEST(RichTextBox, NoWrapLongLineGrowsWideNotTall)
{
    Fixture f;
    f.box.SetWordWrap(false);
    f.SetOneLine(std::wstring(2000, L'字'));

    // NoWrap: one logical line stays one visual line, so the content is far wider
    // than the viewport and only about one line tall.
    EXPECT_TRUE(f.box.ContentWidth() > 400.0f);
    EXPECT_TRUE(f.box.ContentHeight() < 100.0f);
}

TEST(RichTextBox, TogglingWrapChangesExtentsBothWays)
{
    // The toggle has to be live, not just an initial-construction option: the
    // gallery flips it on an existing document and both extents must follow.
    Fixture f;
    f.SetOneLine(std::wstring(2000, L'字'));

    const float wrappedH = f.box.ContentHeight();
    const float wrappedW = f.box.ContentWidth();

    f.box.SetWordWrap(false);
    f.Layout();
    const float noWrapH = f.box.ContentHeight();
    const float noWrapW = f.box.ContentWidth();

    EXPECT_TRUE(noWrapW > wrappedW);
    EXPECT_TRUE(noWrapH < wrappedH);

    // And back: returning to Wrap restores the original extents, so the layout is
    // genuinely rebuilt from the mode rather than accumulating state.
    f.box.SetWordWrap(true);
    f.Layout();
    EXPECT_NEAR(f.box.ContentHeight(), wrappedH, 1.0f);
    EXPECT_NEAR(f.box.ContentWidth(), wrappedW, 1.0f);
}

TEST(RichTextBox, SettingTheSameWrapModeIsANoOp)
{
    Fixture f;
    f.SetOneLine(L"hello");
    const float h = f.box.ContentHeight();

    f.box.SetWordWrap(true);  // already true
    EXPECT_NEAR(f.box.ContentHeight(), h, 0.001f);
}

// --- Large documents ---------------------------------------------------------

TEST(RichTextBox, TenThousandCharacterDocumentLaysOut)
{
    Fixture f;
    f.box.SetDocument(MakeBigDocument());
    f.Layout();

    // The whole document is one DirectWrite layout, so "does 10k work" is really
    // "did DWrite produce sane metrics for 10k characters".
    //
    // 50 runs × "第N段：" + 190 chars + newline ≈ 9791, not a round 10k — the
    // paragraph prefix varies in length. Each chunk ends in '\n', so the document is
    // 50 paragraphs of text plus an empty trailing one; Runs() flattens that to 50
    // content runs + 50 synthesized separators.
    EXPECT_EQ(f.box.Document().Paragraphs().size(), size_t(51));
    EXPECT_EQ(f.box.Document().Runs().size(), size_t(100));
    EXPECT_TRUE(f.box.Document().Length() > 9000);
    EXPECT_TRUE(f.box.ContentHeight() > 200.0f);  // overflows: scrollbar appears
}

TEST(RichTextBox, TenThousandCharacterLayoutIsFastEnoughToTypeIn)
{
    // A full relayout happens on every keystroke, so this is the number that decides
    // whether a 10k document is usable at all. The bound is deliberately loose (the
    // observed cost is well under it) — it is here to catch an order-of-magnitude
    // regression, e.g. someone reintroducing a per-run layout.
    Fixture f;
    f.box.SetDocument(MakeBigDocument());
    f.Layout();

    // Sanity-check that the loop below is measuring a real rebuild rather than a
    // cache hit: without this, a broken InvalidateLayout would make the timing
    // meaninglessly fast and the test would still pass.
    f.box.InvalidateLayout();
    const float h1 = f.box.ContentHeight();
    EXPECT_TRUE(h1 > 200.0f);

    const auto t0 = std::chrono::steady_clock::now();
    constexpr int kIterations = 50;
    for (int i = 0; i < kIterations; ++i) {
        f.box.InvalidateLayout();
        (void)f.box.ContentHeight();  // forces the rebuild
    }
    const auto t1 = std::chrono::steady_clock::now();

    const double usPerLayout =
        std::chrono::duration<double, std::micro>(t1 - t0).count() / kIterations;
    std::printf("        [perf] 10k-char relayout: %.1f us\n", usPerLayout);

    // A keystroke budget is ~16 ms for the whole frame; 10 ms for text layout alone
    // would already be visible as lag. The observed cost is far below this — the
    // bound exists to catch an order-of-magnitude regression, such as reverting to
    // one DirectWrite layout per run.
    EXPECT_TRUE(usPerLayout < 10000.0);
}

TEST(RichTextBox, NoWrapTenThousandCharactersStaysFiftyLines)
{
    Fixture f;
    f.box.SetWordWrap(false);
    f.box.SetDocument(MakeBigDocument());
    f.Layout();

    // 50 paragraphs, no wrapping: 50 visual lines, each ~200 chars wide. The width
    // is what the horizontal scrollbar is sized from.
    EXPECT_TRUE(f.box.ContentWidth() > 1000.0f);

    const float wrapped = [&] {
        f.box.SetWordWrap(true);
        f.Layout();
        return f.box.ContentHeight();
    }();
    f.box.SetWordWrap(false);
    f.Layout();

    // Wrapping 200-char paragraphs into a 400 DIP box makes the document several
    // times taller than the unwrapped 50 lines.
    EXPECT_TRUE(wrapped > f.box.ContentHeight());
}

// --- Scrollbar visibility ----------------------------------------------------
// These are the assertions that were missing when the control shipped a phantom
// horizontal rail in Wrap mode: every earlier test asked about content extents,
// none asked what the ScrollViewer concluded from them.

TEST(RichTextBox, WrapModeNeverScrollsHorizontally)
{
    Fixture f;
    f.SetOneLine(std::wstring(2000, L'字'));

    // Measure and Arrange are given DIFFERENT widths on purpose. That is the normal
    // case in a real layout — a panel measures a child against the space it might
    // get and then arranges it into the space it actually got — and it is what makes
    // this test able to fail: if Measure reports the measure-time width as the
    // horizontal content extent, the ScrollViewer ends up with a content width that
    // does not match the final bounds and concludes the content overflows sideways.
    // Measuring and arranging at the same width would let that bug cancel itself out
    // (extent - viewport == 0) and the test would pass either way.
    f.box.Measure(600.0f, 200.0f);
    f.box.Arrange(RectDip{0.0f, 0.0f, 400.0f, 200.0f});

    // Wrapped text reflows to the control width, so there is nothing to scroll to.
    EXPECT_FALSE(f.box.HorizontallyScrollable());
}

TEST(RichTextBox, WrapModeStillScrollsVerticallyWhenTall)
{
    Fixture f;
    f.SetOneLine(std::wstring(2000, L'字'));

    // Killing the horizontal axis must not kill the vertical one: 2000 chars
    // wrapped into a 400x200 box is far taller than the viewport.
    EXPECT_TRUE(f.box.ContentHeight() > 200.0f);
}

TEST(RichTextBox, ShortTextDoesNotScrollInEitherDirection)
{
    // The reported bug: one short line, yet a wheel notch made a scrollbar appear.
    // A viewer with nothing to scroll must report no overflow on either axis.
    Fixture f;
    f.SetOneLine(L"一行字");

    EXPECT_TRUE(f.box.ContentHeight() <= 200.0f);
    EXPECT_FALSE(f.box.HorizontallyScrollable());
    EXPECT_FALSE(f.box.VerticallyScrollable());
}

TEST(RichTextBox, NoWrapLongLineScrollsHorizontally)
{
    Fixture f;
    f.box.SetWordWrap(false);
    f.SetOneLine(std::wstring(2000, L'字'));

    // The other side of the same coin: NoWrap content wider than the viewport is
    // exactly when the horizontal rail SHOULD be live.
    EXPECT_TRUE(f.box.HorizontallyScrollable());
}

TEST(RichTextBox, TogglingWrapTogglesHorizontalScrollability)
{
    Fixture f;
    f.SetOneLine(std::wstring(2000, L'字'));
    EXPECT_FALSE(f.box.HorizontallyScrollable());

    f.box.SetWordWrap(false);
    f.Layout();
    EXPECT_TRUE(f.box.HorizontallyScrollable());

    f.box.SetWordWrap(true);
    f.Layout();
    EXPECT_FALSE(f.box.HorizontallyScrollable());
}

TEST(RichTextBox, FirstMeasureOfShortTextDoesNotOverflow)
{
    // The reported bug: every box on the page showed a scrollbar until some other
    // action forced a relayout. On the first pass bounds_ is still 0×0, so laying
    // out against bounds_.w wraps the text into a 1 DIP column — a one-line document
    // becomes hundreds of lines tall and the ScrollViewer is told it overflows.
    //
    // Deliberately NOT using Fixture: its constructor already runs a Measure/Arrange
    // cycle, which is exactly the state this test needs to avoid.
    MockHost host;
    RichTextBox box;
    box.AttachToContext(MakeCtx(host));

    // Long enough to fill most of one 400 DIP line but not two: at the real width it
    // fits in the 200 DIP viewport, while wrapped into a 1 DIP column it becomes one
    // line per character and towers over it. A three-character string would fit
    // either way and the test could not fail.
    RichTextDocument doc;
    doc.AppendRun(TextRun(std::wstring(20, L'字'), kInk, 14.0f));
    box.SetDocument(std::move(doc));

    // The very first measure, with bounds_ still zero.
    box.Measure(400.0f, 200.0f);

    EXPECT_FALSE(box.VerticallyScrollable());
    EXPECT_FALSE(box.HorizontallyScrollable());
}

// --- Extent updates on edit --------------------------------------------------
// The reported bug: pasting more text than fits produced no scrollbar until some
// later event happened to run a measure pass. SetContentHeight lived only in
// Measure(), so between an edit and the next layout the ScrollViewer still held
// the old extent. Every edit path goes through Changed(), so that is where the
// extent has to be refreshed — the same thing TextArea does after each edit.

TEST(RichTextBox, InsertingMoreTextThanFitsMakesItScrollableImmediately)
{
    Fixture f;
    f.SetOneLine(L"短");
    EXPECT_FALSE(f.box.VerticallyScrollable());

    // Typed input, no Measure/Arrange afterwards: this is the state the user is in
    // the instant the text lands. Ctrl+V takes the same path (InsertText → Changed),
    // and OnTextInput is reachable without a clipboard.
    for (int i = 0; i < 400; ++i)
        f.box.OnTextInput(L'字');

    EXPECT_TRUE(f.box.VerticallyScrollable());
}

TEST(RichTextBox, DeletingBackDownToOneLineRemovesScrollability)
{
    // The inverse: an edit that shrinks the document must retract the scrollbar,
    // not leave a rail scrolling over whitespace.
    Fixture f;
    f.SetOneLine(std::wstring(4000, L'字'));
    EXPECT_TRUE(f.box.VerticallyScrollable());

    // Select everything and press Backspace — the user's way to empty the box.
    f.box.SelectAll();
    KeyEventArgs back{};
    back.vk = VK_BACK;
    f.box.OnKeyDownRouted(back);

    EXPECT_FALSE(f.box.VerticallyScrollable());
}

// --- Drag-selection auto-scroll ----------------------------------------------

TEST(RichTextBox, DraggingSelectionBelowTheBottomEdgeScrollsDown)
{
    // Without this, a selection cannot be extended past one screenful: the pointer
    // is captured so moves keep arriving, but the position they resolve to is
    // clamped at the boundary line and the view never follows.
    Fixture f;
    f.box.SetDocument(MakeBigDocument());
    f.Layout();

    const float before = f.box.ScrollOffset();

    // Press inside, then drag well below the bottom edge.
    PointerEventArgs down{};
    down.position = {50.0f, 50.0f};
    f.box.OnPointerPressed(down);

    PointerEventArgs move{};
    move.position = {50.0f, 260.0f};   // 60 DIP below the 200 DIP tall box
    f.box.OnPointerMoved(move);

    EXPECT_TRUE(f.box.ScrollOffset() > before);
}

TEST(RichTextBox, DraggingSelectionAboveTheTopEdgeScrollsUp)
{
    Fixture f;
    f.box.SetDocument(MakeBigDocument());
    f.Layout();

    // Start part-way down so there is somewhere to scroll back to.
    f.box.ScrollTo(400.0f);
    const float before = f.box.ScrollOffset();
    EXPECT_TRUE(before > 0.0f);

    PointerEventArgs down{};
    down.position = {50.0f, 100.0f};
    f.box.OnPointerPressed(down);

    PointerEventArgs move{};
    move.position = {50.0f, -60.0f};   // above the top edge
    f.box.OnPointerMoved(move);

    EXPECT_TRUE(f.box.ScrollOffset() < before);
}

TEST(RichTextBox, DraggingSelectionInsideTheBoxDoesNotScroll)
{
    // The edge check must be an inequality against the bounds, not "is the pointer
    // in the lower half" — otherwise ordinary selection inside the box drifts.
    Fixture f;
    f.box.SetDocument(MakeBigDocument());
    f.Layout();

    const float before = f.box.ScrollOffset();

    PointerEventArgs down;
    down.position = {50.0f, 20.0f};
    f.box.OnPointerPressed(down);

    PointerEventArgs move{};
    move.position = {50.0f, 180.0f};   // still inside the 200 DIP box
    f.box.OnPointerMoved(move);

    EXPECT_NEAR(f.box.ScrollOffset(), before, 0.001f);
}

// --- Caret and selection over a large document -------------------------------

TEST(RichTextBox, SelectAllOnLargeDocumentCoversEveryCharacter)
{
    Fixture f;
    f.box.SetDocument(MakeBigDocument());
    f.Layout();

    f.box.SelectAll();
    EXPECT_EQ(size_t(f.box.SelectionLength()), f.box.Document().Length());
    EXPECT_TRUE(f.box.SelectedText().find(L"第50段") != std::wstring::npos);
}

// --- Read-only mode -----------------------------------------------------------

TEST(RichTextBox, ReadOnlyBlocksTyping)
{
    Fixture f;
    RichTextDocument doc;
    doc.AppendRun(TextRun(L"initial", D2D1::ColorF(0, 0, 0), 12.0f));
    f.box.SetDocument(std::move(doc));
    f.box.SetReadOnly(true);
    f.Layout();

    const size_t initialLen = f.box.Document().Length();

    // Typing should be blocked
    f.box.OnTextInput(L'x');
    f.box.OnTextInput(L'y');
    f.box.OnTextInput(L'z');
    EXPECT_EQ(f.box.Document().Length(), initialLen);
}

TEST(RichTextBox, ReadOnlyBlocksBackspaceAndDelete)
{
    Fixture f;
    RichTextDocument doc;
    doc.AppendRun(TextRun(L"test", D2D1::ColorF(0, 0, 0), 12.0f));
    f.box.SetDocument(std::move(doc));
    f.box.SetReadOnly(true);
    f.Layout();

    const size_t initialLen = f.box.Document().Length();
    f.box.Select(2, 0);  // caret at position 2

    KeyEventArgs e;
    e.vk = VK_BACK;
    f.box.OnKeyDownRouted(e);
    EXPECT_EQ(f.box.Document().Length(), initialLen);

    e.vk = VK_DELETE;
    f.box.OnKeyDownRouted(e);
    EXPECT_EQ(f.box.Document().Length(), initialLen);
}

TEST(RichTextBox, ReadOnlyAllowsSelection)
{
    Fixture f;
    f.box.SetDocument(MakeBigDocument());
    f.box.SetReadOnly(true);
    f.Layout();

    f.box.Select(0, 10);
    EXPECT_EQ(f.box.SelectionLength(), 10U);
}

TEST(RichTextBox, ReadOnlyAllowsCopy)
{
    Fixture f;
    RichTextDocument doc;
    doc.AppendRun(TextRun(L"copy me", D2D1::ColorF(0, 0, 0), 12.0f));
    f.box.SetDocument(std::move(doc));
    f.box.SetReadOnly(true);
    f.Layout();

    f.box.SelectAll();
    // Copy() itself calls Win32 clipboard APIs which may fail headlessly, so we only
    // verify that SelectedText() returns the expected content — the Copy() path is
    // shared with the non-readonly mode and is already exercised elsewhere.
    EXPECT_EQ(f.box.SelectedText(), L"copy me");
}

TEST(RichTextBox, ReadOnlyCutDegradesToCopy)
{
    Fixture f;
    RichTextDocument doc;
    doc.AppendRun(TextRun(L"try cut", D2D1::ColorF(0, 0, 0), 12.0f));
    f.box.SetDocument(std::move(doc));
    f.box.SetReadOnly(true);
    f.Layout();

    const size_t initialLen = f.box.Document().Length();
    f.box.SelectAll();

    // Cut in read-only mode: copy works, delete does not.
    KeyEventArgs e;
    e.modifiers = ModifierKeys::Ctrl;
    e.vk = 'X';
    f.box.OnKeyDownRouted(e);
    EXPECT_EQ(f.box.Document().Length(), initialLen);  // no deletion
}

TEST(RichTextBox, ReadOnlyBlocksPaste)
{
    Fixture f;
    RichTextDocument doc;
    doc.AppendRun(TextRun(L"paste target", D2D1::ColorF(0, 0, 0), 12.0f));
    f.box.SetDocument(std::move(doc));
    f.box.SetReadOnly(true);
    f.Layout();

    const size_t initialLen = f.box.Document().Length();

    KeyEventArgs e;
    e.modifiers = ModifierKeys::Ctrl;
    e.vk = 'V';
    f.box.OnKeyDownRouted(e);
    // Even if clipboard had content, paste should not happen
    EXPECT_EQ(f.box.Document().Length(), initialLen);
}

TEST(RichTextBox, ReadOnlyBlocksUndoRedo)
{
    Fixture f;
    RichTextDocument doc;
    doc.AppendRun(TextRun(L"undo test", D2D1::ColorF(0, 0, 0), 12.0f));
    f.box.SetDocument(std::move(doc));
    f.Layout();

    // Make an edit first (while editable)
    f.box.OnTextInput(L'a');
    EXPECT_TRUE(f.box.CanUndo());

    // Switch to readonly and try undo
    f.box.SetReadOnly(true);
    KeyEventArgs e;
    e.modifiers = ModifierKeys::Ctrl;
    e.vk = 'Z';
    f.box.OnKeyDownRouted(e);
    EXPECT_TRUE(f.box.CanUndo());  // undo stack untouched

    // Try redo as well
    e.vk = 'Y';
    f.box.OnKeyDownRouted(e);
    EXPECT_FALSE(f.box.CanRedo());  // redo was already empty
}

TEST(RichTextBox, ReadOnlyBlocksStyleChangesToSelection)
{
    Fixture f;
    RichTextDocument doc;
    doc.AppendRun(TextRun(L"bold me", D2D1::ColorF(0, 0, 0), 12.0f, DWRITE_FONT_WEIGHT_NORMAL));
    f.box.SetDocument(std::move(doc));
    f.box.SetReadOnly(true);
    f.Layout();

    f.box.SelectAll();
    f.box.SetSelectionBold(true);

    // Verify that the text is still not bold.
    //
    // By VALUE, not by reference: Runs() flattens the paragraph vector and returns
    // a temporary, so `const TextRun& r = doc.Runs()[0]` leaves r dangling at the
    // end of the statement. Reading it then yields garbage that happens to look
    // right when printed and wrong when compared.
    const TextRun firstRun = f.box.Document().Runs()[0];
    EXPECT_EQ(firstRun.weight, DWRITE_FONT_WEIGHT_NORMAL);
}

// --- Paragraph formatting ---------------------------------------------------

TEST(RichTextBox, SetParagraphAlignmentWithNoneSelected)
{
    // No selection: the caret's paragraph is formatted.
    Fixture f;
    RichTextDocument doc;
    doc.AppendRun(TextRun(L"align me", kInk, 14.0f));
    f.box.SetDocument(std::move(doc));
    f.Layout();

    // Caret defaults to offset 0, which is in paragraph 0.
    f.box.SetParagraphAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);

    ParagraphStyle ps = f.box.GetCurrentParagraphStyle();
    EXPECT_EQ(ps.alignment, DWRITE_TEXT_ALIGNMENT_CENTER);
}

TEST(RichTextBox, SetParagraphAlignmentWithSelection)
{
    // Selection: every paragraph the selection touches is formatted.
    Fixture f;
    RichTextDocument doc;
    doc.AppendRun(TextRun(L"first\n", kInk, 14.0f));
    doc.AppendRun(TextRun(L"second\n", kInk, 14.0f));
    doc.AppendRun(TextRun(L"third", kInk, 14.0f));
    f.box.SetDocument(std::move(doc));
    f.Layout();

    // Select from "first" into "second" — two paragraphs touched.
    f.box.Select(0, 8);
    f.box.SetParagraphAlignment(DWRITE_TEXT_ALIGNMENT_TRAILING);

    // Both paragraphs 0 and 1 should be trailing; paragraph 2 should be untouched.
    EXPECT_EQ(f.box.Document().GetParagraphStyleAt(0).alignment, DWRITE_TEXT_ALIGNMENT_TRAILING);
    EXPECT_EQ(f.box.Document().GetParagraphStyleAt(6).alignment, DWRITE_TEXT_ALIGNMENT_TRAILING);
    EXPECT_EQ(f.box.Document().GetParagraphStyleAt(13).alignment, DWRITE_TEXT_ALIGNMENT_LEADING);
}

TEST(RichTextBox, SetParagraphLineSpacingMultiple)
{
    Fixture f;
    f.SetOneLine(L"double spaced");
    f.box.SetParagraphLineSpacing(2.0f);

    ParagraphStyle ps = f.box.GetCurrentParagraphStyle();
    EXPECT_EQ(ps.lineSpacingMode, LineSpacingMode::Multiple);
    EXPECT_NEAR(ps.lineSpacing, 2.0f, 0.001f);
}

TEST(RichTextBox, SetParagraphLineHeightAbsolute)
{
    Fixture f;
    f.SetOneLine(L"fixed height");
    f.box.SetParagraphLineHeight(20.0f);

    ParagraphStyle ps = f.box.GetCurrentParagraphStyle();
    EXPECT_EQ(ps.lineSpacingMode, LineSpacingMode::Absolute);
    EXPECT_NEAR(ps.lineSpacing, 20.0f, 0.001f);
}

TEST(RichTextBox, SetParagraphSpacing)
{
    Fixture f;
    f.SetOneLine(L"spaced");
    f.box.SetParagraphSpaceBefore(10.0f);
    f.box.SetParagraphSpaceAfter(15.0f);

    ParagraphStyle ps = f.box.GetCurrentParagraphStyle();
    EXPECT_NEAR(ps.spaceBefore, 10.0f, 0.001f);
    EXPECT_NEAR(ps.spaceAfter, 15.0f, 0.001f);
}

TEST(RichTextBox, SetParagraphIndents)
{
    Fixture f;
    f.SetOneLine(L"indented");
    f.box.SetParagraphFirstLineIndent(20.0f);
    f.box.SetParagraphLeftIndent(10.0f);
    f.box.SetParagraphRightIndent(10.0f);

    ParagraphStyle ps = f.box.GetCurrentParagraphStyle();
    EXPECT_NEAR(ps.firstLineIndent, 20.0f, 0.001f);
    EXPECT_NEAR(ps.leftIndent, 10.0f, 0.001f);
    EXPECT_NEAR(ps.rightIndent, 10.0f, 0.001f);
}

// --- Indents actually affect layout -----------------------------------------
//
// The tests above only prove the VALUES round-trip through the style struct. These
// prove the layout honours them, which is a separate thing: the values sat in
// ParagraphStyle for a whole step while RebuildLayout ignored them entirely, and
// every one of those value-only assertions passed the whole time.

TEST(RichTextBox, LeftAndRightIndentsNarrowTheWrappingWidth)
{
    // Narrowing the layout box makes wrapped text taller: the same characters have
    // less width per line, so they need more lines. Text long enough to wrap is
    // required — a single short line would fit either way and prove nothing.
    Fixture f;
    f.SetOneLine(std::wstring(300, L'字'));
    const float unindented = f.box.ContentHeight();

    f.box.SetParagraphLeftIndent(60.0f);
    f.box.SetParagraphRightIndent(60.0f);
    f.Layout();

    EXPECT_TRUE(f.box.ContentHeight() > unindented);
}

TEST(RichTextBox, FirstLineIndentDoesNotChangeTheWrappingWidth)
{
    // First-line indent is character spacing on the first glyph, not a change to the
    // layout box, so it must NOT narrow the wrap width the way leftIndent does. It can
    // still push the last word of the first line down, so assert it does not grow by a
    // whole extra line's worth — the distinction being tested is "box unchanged" vs
    // "box narrowed by 60 DIP on both sides".
    Fixture f;
    f.SetOneLine(std::wstring(300, L'字'));
    const float before = f.box.ContentHeight();

    f.box.SetParagraphFirstLineIndent(60.0f);
    f.Layout();
    const float afterFirstLine = f.box.ContentHeight();

    // Compare against what a real box-narrowing does, on a fresh box.
    Fixture g;
    g.SetOneLine(std::wstring(300, L'字'));
    g.box.SetParagraphLeftIndent(60.0f);
    g.Layout();

    EXPECT_TRUE(afterFirstLine - before < g.box.ContentHeight() - before);
}

TEST(RichTextBox, NegativeFirstLineIndentIsAHangingIndent)
{
    // A hanging indent pulls the first line LEFT of the body. It must be accepted
    // rather than clamped to zero — bullet and numbered lists are shaped with it.
    Fixture f;
    f.SetOneLine(L"hanging");
    f.box.SetParagraphLeftIndent(40.0f);
    f.box.SetParagraphFirstLineIndent(-20.0f);

    ParagraphStyle ps = f.box.GetCurrentParagraphStyle();
    EXPECT_NEAR(ps.firstLineIndent, -20.0f, 0.001f);
    EXPECT_NEAR(ps.leftIndent, 40.0f, 0.001f);
}

TEST(RichTextBox, IndentSurvivesUndo)
{
    Fixture f;
    f.SetOneLine(std::wstring(300, L'字'));
    f.box.SetParagraphLeftIndent(60.0f);
    f.Layout();
    const float indented = f.box.ContentHeight();

    KeyEventArgs e;
    e.modifiers = ModifierKeys::Ctrl;
    e.vk = 'Z';
    f.box.OnKeyDownRouted(e);
    f.Layout();

    // Undo must restore the wrap width, not just the stored number.
    EXPECT_NEAR(f.box.GetCurrentParagraphStyle().leftIndent, 0.0f, 0.001f);
    EXPECT_TRUE(f.box.ContentHeight() < indented);
}

TEST(RichTextBox, IndentLargerThanTheBoxDoesNotCollapseLayout)
{
    // Boundary: indents summing to more than the control width. The layout box is
    // clamped to a 1 DIP floor, so this must not divide by zero, hang, or report a
    // zero extent — it degenerates to one character per line, which is ugly but finite.
    Fixture f;
    f.SetOneLine(L"squeezed");
    f.box.SetParagraphLeftIndent(1000.0f);
    f.box.SetParagraphRightIndent(1000.0f);
    f.Layout();

    EXPECT_TRUE(f.box.ContentHeight() > 0.0f);
}

TEST(RichTextBox, ApplyParagraphStyleFlattens)
{
    // ApplyParagraphStyle replaces every attribute at once, deliberately flattening.
    Fixture f;
    RichTextDocument doc;
    doc.AppendRun(TextRun(L"first\n", kInk, 14.0f));
    doc.AppendRun(TextRun(L"second", kInk, 14.0f));
    f.box.SetDocument(std::move(doc));
    f.Layout();

    // Give them different alignments.
    f.box.Select(0, 1);
    f.box.SetParagraphAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
    f.box.Select(7, 1);
    f.box.SetParagraphAlignment(DWRITE_TEXT_ALIGNMENT_TRAILING);

    // Now flatten both with ApplyParagraphStyle.
    ParagraphStyle flat;
    flat.alignment = DWRITE_TEXT_ALIGNMENT_LEADING;
    flat.lineSpacing = 1.5f;
    flat.spaceBefore = 5.0f;

    f.box.SelectAll();
    f.box.ApplyParagraphStyle(flat);

    // Both paragraphs should now have the flat style.
    ParagraphStyle ps0 = f.box.Document().GetParagraphStyleAt(0);
    ParagraphStyle ps1 = f.box.Document().GetParagraphStyleAt(7);
    EXPECT_EQ(ps0.alignment, DWRITE_TEXT_ALIGNMENT_LEADING);
    EXPECT_NEAR(ps0.lineSpacing, 1.5f, 0.001f);
    EXPECT_NEAR(ps0.spaceBefore, 5.0f, 0.001f);
    EXPECT_EQ(ps1.alignment, DWRITE_TEXT_ALIGNMENT_LEADING);
    EXPECT_NEAR(ps1.lineSpacing, 1.5f, 0.001f);
    EXPECT_NEAR(ps1.spaceBefore, 5.0f, 0.001f);
}

TEST(RichTextBox, GetCurrentParagraphStyleReportsCaretParagraph)
{
    Fixture f;
    RichTextDocument doc;
    doc.AppendRun(TextRun(L"first\n", kInk, 14.0f));
    doc.AppendRun(TextRun(L"second", kInk, 14.0f));
    f.box.SetDocument(std::move(doc));
    f.Layout();

    // Give them different alignments.
    f.box.Select(0, 1);
    f.box.SetParagraphAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
    f.box.Select(7, 1);
    f.box.SetParagraphAlignment(DWRITE_TEXT_ALIGNMENT_TRAILING);

    // Move caret to first paragraph.
    f.box.Select(2, 0);
    EXPECT_EQ(f.box.GetCurrentParagraphStyle().alignment, DWRITE_TEXT_ALIGNMENT_CENTER);

    // Move caret to second paragraph.
    f.box.Select(10, 0);
    EXPECT_EQ(f.box.GetCurrentParagraphStyle().alignment, DWRITE_TEXT_ALIGNMENT_TRAILING);
}

TEST(RichTextBox, ParagraphFormattingUndoable)
{
    Fixture f;
    f.SetOneLine(L"undo me");

    f.box.SetParagraphAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
    EXPECT_TRUE(f.box.CanUndo());

    // Trigger undo via keyboard shortcut
    KeyEventArgs e;
    e.modifiers = ModifierKeys::Ctrl;
    e.vk = 'Z';
    f.box.OnKeyDownRouted(e);
    EXPECT_EQ(f.box.GetCurrentParagraphStyle().alignment, DWRITE_TEXT_ALIGNMENT_LEADING);
}

TEST(RichTextBox, ReadOnlyBlocksParagraphFormatting)
{
    // Unlike SetSelectionBold, which has a "pending style" fallback with no
    // selection, paragraph formatting ALWAYS has a target (the caret's paragraph),
    // so it is a document edit even with no selection. It must be blocked in
    // read-only mode.
    Fixture f;
    f.SetOneLine(L"readonly");
    f.box.SetReadOnly(true);

    f.box.SetParagraphAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);

    // Alignment should remain default.
    EXPECT_EQ(f.box.GetCurrentParagraphStyle().alignment, DWRITE_TEXT_ALIGNMENT_LEADING);
}


// --- Edits that must not disturb undo/redo or the newline form ---------------

namespace {

void PressCtrl(RichTextBox& box, UINT vk) {
    KeyEventArgs e;
    e.modifiers = ModifierKeys::Ctrl;
    e.vk = vk;
    box.OnKeyDownRouted(e);
}

} // namespace

TEST(RichTextBox, CutWithNoSelectionKeepsTheRedoHistory)
{
    // Ctrl+X with nothing selected used to PushUndo anyway: a snapshot identical to
    // the current state, plus redoStack_.clear(). The user's redo history vanished on
    // a keystroke that changed nothing. No selection also means Copy() returns before
    // touching the clipboard, so this test needs none.
    Fixture f;
    f.box.OnTextInput(L'a');
    f.box.OnTextInput(L'b');
    PressCtrl(f.box, 'Z');
    EXPECT_TRUE(f.box.CanRedo());
    const std::wstring before = f.box.Document().GetPlainText();

    PressCtrl(f.box, 'X');
    EXPECT_TRUE(f.box.CanRedo());
    EXPECT_EQ(f.box.Document().GetPlainText(), before);
}

TEST(RichTextBox, PasteNormalizesCrLfToParagraphBreaks)
{
    // Clipboard text from Notepad (and nearly everything else on Windows) is CRLF.
    // RichTextDocument splits paragraphs on '\n' only, so an unnormalized paste left a
    // '\r' at the end of each paragraph's run — which DWrite draws as an extra hard
    // line break.
    testclip::ClipboardTextGuard guard;
    if (!testclip::WriteText(L"a\r\nb\rc")) return;   // clipboard held elsewhere: skip
    Fixture f;
    PressCtrl(f.box, 'V');

    const std::wstring text = f.box.Document().GetPlainText();
    EXPECT_EQ(text, std::wstring(L"a\nb\nc"));
    EXPECT_TRUE(text.find(L'\r') == std::wstring::npos);
    EXPECT_EQ(f.box.Document().Paragraphs().size(), size_t{3});
}

TEST(RichTextBox, CompositionAtParagraphStartShiftsTheRunsFormatting)
{
    // The caret at offset 0 of a paragraph is a real composition position — every new
    // paragraph starts there. RebuildLayout used "offset > 0" to mean "composing in
    // this paragraph", so at offset 0 the composed text was spliced in but the runs'
    // ranges were not shifted past it: each run's size landed one character early.
    //
    // Observable through the layout width. Composing "i" in front of a 10 DIP "iiii"
    // run followed by a 40 DIP "W" must lay out exactly like the committed text
    // "iiiii" + "W" — composition text takes the paragraph's base (first-run) style.
    // Unshifted, the 40 DIP range covers the last "i" instead of the "W", and the line
    // comes out about a capital W's width narrower.
    Fixture f;
    f.box.SetWordWrap(false);

    RichTextDocument reference;
    reference.AppendRun(TextRun(L"iiiii", kInk, 10.0f));
    reference.AppendRun(TextRun(L"W", kRed, 40.0f));
    f.box.SetDocument(std::move(reference));
    f.Layout();
    const float expected = f.box.ContentWidth();

    RichTextDocument doc;
    doc.AppendRun(TextRun(L"iiii", kInk, 10.0f));
    doc.AppendRun(TextRun(L"W", kRed, 40.0f));
    f.box.SetDocument(std::move(doc));
    f.box.Select(0, 0);                            // caret at the paragraph start
    f.box.TestSetComposition(L"i");
    f.Layout();

    EXPECT_NEAR(f.box.ContentWidth(), expected, 0.5f);
}
