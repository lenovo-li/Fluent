// RichTextBoxEventsTests.cpp — RichTextBox.TextChanged / SelectionChanged
// (event_completion_plan §1).
//
// The point of these events is that an app can tell "the document was edited" from
// "the caret moved", because the first marks the file dirty and the second must not.
// So the tests check both that each event fires where it should AND that it stays
// silent where it should — NoTextChangedOnSelectionOnly is the one that matters.
//
// Needs a real DirectWrite factory (the same MockHost as RichTextBoxTests) because
// every edit path goes through Refresh(), which lays the document out.

#include "../framework/Test.h"
#include "../../FluentUI/controls/RichTextBox.h"
#include "../../FluentUI/controls/RichTextDocument.h"
#include "../../FluentUI/core/UIContext.h"
#include "../../FluentUI/window/WindowServices.h"
#include "../../FluentUI/graphics/D2DContext.h"
#include "../../FluentUI/graphics/DWriteContext.h"

#include <string>

using namespace fluent;

namespace {

class MockHost : public WindowServices {
public:
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

struct Rec {
    std::vector<std::wstring> texts;
    std::vector<RichTextBox::SelectionChangedArgs> selections;

    static void OnText(void* owner, RichTextBox&, std::wstring& s) {
        static_cast<Rec*>(owner)->texts.push_back(s);
    }
    static void OnSelection(void* owner, RichTextBox&, RichTextBox::SelectionChangedArgs& a) {
        static_cast<Rec*>(owner)->selections.push_back(a);
    }
};

// A box with "Hello World" in it, laid out, with both events subscribed.
struct Fixture {
    MockHost host;
    RichTextBox box;
    Rec rec;
    Subscription textSub;
    Subscription selSub;

    Fixture() {
        box.AttachToContext(MakeCtx(host));
        RichTextDocument doc;
        doc.AppendRun(TextRun(L"Hello World", kInk, 14.0f));
        box.SetDocument(std::move(doc));
        box.Measure(400.0f, 200.0f);
        box.Arrange(RectDip{0.0f, 0.0f, 400.0f, 200.0f});
        // Subscribe AFTER the initial SetDocument so the fixture's own setup does
        // not show up as a recorded event.
        textSub = box.TextChanged().Subscribe(&rec, &Rec::OnText);
        selSub = box.SelectionChanged().Subscribe(&rec, &Rec::OnSelection);
    }
};

} // namespace

// --- TextChanged -------------------------------------------------------------

TEST(RichTextBoxEvents, SetDocumentRaisesTextChanged) {
    MockHost host;
    RichTextBox box;
    box.AttachToContext(MakeCtx(host));
    box.Measure(400.0f, 200.0f);
    box.Arrange(RectDip{0.0f, 0.0f, 400.0f, 200.0f});

    Rec rec;
    auto sub = box.TextChanged().Subscribe(&rec, &Rec::OnText);

    RichTextDocument doc;
    doc.AppendRun(TextRun(L"Replaced", kInk, 14.0f));
    box.SetDocument(std::move(doc));

    EXPECT_EQ(rec.texts.size(), size_t{1});
    EXPECT_TRUE(rec.texts[0] == L"Replaced");
}

// The payload is the text AFTER the edit, not before.
TEST(RichTextBoxEvents, PayloadIsPostEditText) {
    Fixture f;
    f.box.Select(0, 0);              // caret to the start
    f.rec.texts.clear();

    f.box.OnTextInput(L'X');
    EXPECT_EQ(f.rec.texts.size(), size_t{1});
    EXPECT_TRUE(f.rec.texts[0] == L"XHello World");
}

TEST(RichTextBoxEvents, SelectAllThenTypeReplaces) {
    Fixture f;
    f.box.SelectAll();
    f.rec.texts.clear();

    f.box.OnTextInput(L'Z');
    EXPECT_EQ(f.rec.texts.size(), size_t{1});
    EXPECT_TRUE(f.rec.texts[0] == L"Z");
}

// --- SelectionChanged --------------------------------------------------------

TEST(RichTextBoxEvents, SelectRaisesSelectionChangedWithRange) {
    Fixture f;
    f.box.Select(2, 5);

    EXPECT_EQ(f.rec.selections.size(), size_t{1});
    EXPECT_EQ(f.rec.selections[0].start, UINT32{2});
    EXPECT_EQ(f.rec.selections[0].length, UINT32{5});
}

TEST(RichTextBoxEvents, SelectAllReportsWholeDocument) {
    Fixture f;
    f.box.SelectAll();

    EXPECT_EQ(f.rec.selections.size(), size_t{1});
    EXPECT_EQ(f.rec.selections[0].start, UINT32{0});
    EXPECT_EQ(f.rec.selections[0].length, UINT32{11});  // "Hello World"
}

// THE central test of this file. Moving the caret / changing the selection must NOT
// report a text change: an app that marks the document dirty on TextChanged would
// otherwise show unsaved changes after a bare click, and auto-save would fire on
// every arrow key.
TEST(RichTextBoxEvents, NoTextChangedOnSelectionOnly) {
    Fixture f;

    f.box.Select(3, 4);
    f.box.SelectAll();
    f.box.Select(0, 0);

    EXPECT_EQ(f.rec.selections.size(), size_t{3});
    EXPECT_EQ(f.rec.texts.size(), size_t{0});   // nothing was edited
}

// An edit moves the caret too, so it reports BOTH — a toolbar that tracks the style
// at the caret has to update after typing, not only after a click.
TEST(RichTextBoxEvents, EditRaisesBothEvents) {
    Fixture f;
    f.box.Select(0, 0);
    f.rec.texts.clear();
    f.rec.selections.clear();

    f.box.OnTextInput(L'A');

    EXPECT_EQ(f.rec.texts.size(), size_t{1});
    EXPECT_TRUE(f.rec.selections.size() >= size_t{1});
    // The caret sits after the inserted character, with nothing selected.
    const auto& last = f.rec.selections.back();
    EXPECT_EQ(last.start, UINT32{1});
    EXPECT_EQ(last.length, UINT32{0});
}

// --- Lifetime ----------------------------------------------------------------

TEST(RichTextBoxEvents, SubscriptionRAII) {
    Fixture f;
    f.box.SelectAll();
    const size_t before = f.rec.selections.size();

    f.selSub.reset();          // unsubscribe
    f.box.Select(1, 2);
    EXPECT_EQ(f.rec.selections.size(), before);   // no new event
}
