// AutomationTextPatternTests.cpp — TextPattern on TextBox / TextArea through the
// real providers: document and selection ranges, unit navigation (character, word,
// line, document), text of a range, Select (posted), clamping after an edit, the
// password exclusion, and the event-driven TextChanged / TextSelectionChanged.
#include "../framework/Test.h"
#include "AutomationTestHelpers.h"

#include "../../FluentUI/controls/TextArea.h"
#include "../../FluentUI/controls/TextBox.h"

using namespace fluent;
using namespace uiatest;

namespace {
ComPtr<ITextProvider> TextOf(UiaFixture& fx) {
    auto f = Nav(Nav(fx.Connect().Get(), NavigateDirection_FirstChild).Get(),
                 NavigateDirection_FirstChild);
    ComPtr<ITextProvider> t;
    if (f) f.As(&t);
    return t;
}
std::wstring RangeText(ITextRangeProvider* r) {
    BSTR b = nullptr;
    if (!r || FAILED(r->GetText(-1, &b)) || !b) return L"<null>";
    // Length-exact: an unclamped range would read past the text, and an embedded
    // NUL must not hide that.
    std::wstring s(b, SysStringLen(b));
    SysFreeString(b);
    return s;
}
}  // namespace

TEST(AutomationText, DocumentRangeAndUnitNavigation) {
    UiaFixture fx;
    auto* ta = fx.root->Emplace<TextArea>();
    ta->SetText(L"hello brave\nnew world");
    fx.Attach();
    auto tp = TextOf(fx);
    EXPECT_TRUE(tp != nullptr);
    if (!tp) return;
    ComPtr<ITextRangeProvider> doc;
    EXPECT_EQ(tp->get_DocumentRange(&doc), S_OK);
    EXPECT_TRUE(RangeText(doc.Get()) == L"hello brave\nnew world");

    // A degenerate range at 0, expanded to its word / line.
    ComPtr<ITextRangeProvider> r;
    doc->Clone(&r);
    int moved = 0;
    r->MoveEndpointByRange(TextPatternRangeEndpoint_End, r.Get(), TextPatternRangeEndpoint_Start);
    r->ExpandToEnclosingUnit(TextUnit_Word);
    EXPECT_TRUE(RangeText(r.Get()).rfind(L"hello", 0) == 0);
    r->ExpandToEnclosingUnit(TextUnit_Line);
    EXPECT_TRUE(RangeText(r.Get()) == L"hello brave\n");
    // Next line.
    EXPECT_EQ(r->Move(TextUnit_Line, 1, &moved), S_OK);
    EXPECT_EQ(moved, 1);
    EXPECT_TRUE(RangeText(r.Get()) == L"new world");
    // Characters: one forward from the line start.
    r->ExpandToEnclosingUnit(TextUnit_Character);
    EXPECT_TRUE(RangeText(r.Get()) == L"n");
    r->Move(TextUnit_Character, 2, &moved);
    EXPECT_EQ(moved, 2);
    EXPECT_TRUE(RangeText(r.Get()) == L"w");
    // GetText honours maxLength.
    BSTR b = nullptr;
    doc->GetText(5, &b);
    EXPECT_TRUE(b && std::wstring(b) == L"hello");
    SysFreeString(b);
}

TEST(AutomationText, SelectionRangeAndPostedSelect) {
    UiaFixture fx;
    auto* tb = fx.root->Emplace<TextBox>();
    tb->SetText(L"abcdef");
    tb->Select(1, 2);
    fx.Attach();
    auto tp = TextOf(fx);
    if (!tp) { EXPECT_TRUE(false); return; }
    SAFEARRAY* arr = nullptr;
    EXPECT_EQ(tp->GetSelection(&arr), S_OK);
    ComPtr<ITextRangeProvider> sel;
    LONG i = 0;
    if (arr) {
        IUnknown* u = nullptr;
        SafeArrayGetElement(arr, &i, &u);
        if (u) { u->QueryInterface(IID_PPV_ARGS(&sel)); u->Release(); }
        SafeArrayDestroy(arr);
    }
    EXPECT_TRUE(RangeText(sel.Get()) == L"bc");
    // Select a different range: posted, then applied by the flush.
    ComPtr<ITextRangeProvider> doc;
    tp->get_DocumentRange(&doc);
    int moved = 0;
    doc->MoveEndpointByUnit(TextPatternRangeEndpoint_Start, TextUnit_Character, 3, &moved);
    EXPECT_EQ(doc->Select(), S_OK);
    EXPECT_EQ(tb->SelectionStart(), 1u);
    fx.host.OnFlush();
    EXPECT_EQ(tb->SelectionStart(), 3u);
    EXPECT_EQ(tb->SelectionLength(), 3u);
}

TEST(AutomationText, RangeClampsAfterTheTextShrinks) {
    UiaFixture fx;
    auto* tb = fx.root->Emplace<TextBox>();
    tb->SetText(L"0123456789012345678901234567890123456789");
    fx.Attach();
    auto tp = TextOf(fx);
    if (!tp) { EXPECT_TRUE(false); return; }
    ComPtr<ITextRangeProvider> doc;
    tp->get_DocumentRange(&doc);
    tb->SetText(L"abc");  // the client still holds a 40-char range
    EXPECT_TRUE(RangeText(doc.Get()) == L"abc");  // clamped, not out of bounds
}

TEST(AutomationText, PasswordHasNoTextPattern) {
    UiaFixture fx;
    auto* tb = fx.root->Emplace<TextBox>();
    tb->SetPassword(true);
    tb->SetText(L"secret");
    fx.Attach();
    EXPECT_TRUE(TextOf(fx) == nullptr);
}

TEST(AutomationText, TextAndSelectionChangedAreRaisedFromTheControlsEvents) {
    UiaFixture fx;
    auto* tb = fx.root->Emplace<TextBox>();
    fx.Attach();
    auto tp = TextOf(fx);  // creates the peer (subscriptions)
    fx.core.events.clear();
    tb->ReplaceAllAsUserEdit(L"typed");  // a user-path edit raises TextChanged
    tb->Select(0, 2);
    fx.host.OnFlush();
    bool text = false, sel = false;
    for (int e : fx.core.events) {
        text |= e == UIA_Text_TextChangedEventId;
        sel |= e == UIA_Text_TextSelectionChangedEventId;
    }
    EXPECT_TRUE(text);
    EXPECT_TRUE(sel);
}

TEST(AutomationText, ExpandAtTheDocumentEndCoversTheLastUnit) {
    // The caret after the last character (the normal state while typing). Narrator's
    // "read current line / word" expands the degenerate range there.
    UiaFixture fx;
    auto* tb = fx.root->Emplace<TextBox>();
    tb->SetText(L"say hello");
    fx.Attach();
    auto tp = TextOf(fx);
    if (!tp) return;
    ComPtr<ITextRangeProvider> doc, r;
    tp->get_DocumentRange(&doc);
    doc->Clone(&r);
    r->MoveEndpointByRange(TextPatternRangeEndpoint_Start, doc.Get(), TextPatternRangeEndpoint_End);
    EXPECT_TRUE(RangeText(r.Get()).empty());
    r->ExpandToEnclosingUnit(TextUnit_Line);
    EXPECT_TRUE(RangeText(r.Get()) == L"say hello");
    r->MoveEndpointByRange(TextPatternRangeEndpoint_Start, doc.Get(), TextPatternRangeEndpoint_End);
    r->ExpandToEnclosingUnit(TextUnit_Word);
    EXPECT_TRUE(RangeText(r.Get()) == L"hello");
}

TEST(AutomationText, InsertionPointMovesOntoTheEndAndCountZeroIsANoOp) {
    UiaFixture fx;
    auto* tb = fx.root->Emplace<TextBox>();
    tb->SetText(L"ab");
    fx.Attach();
    auto tp = TextOf(fx);
    if (!tp) return;
    ComPtr<ITextRangeProvider> doc, r;
    tp->get_DocumentRange(&doc);
    doc->Clone(&r);
    r->MoveEndpointByRange(TextPatternRangeEndpoint_End, doc.Get(), TextPatternRangeEndpoint_Start);
    int moved = 0;
    r->Move(TextUnit_Character, 1, &moved);  // insertion point at 1
    EXPECT_EQ(moved, 1);
    r->Move(TextUnit_Character, 1, &moved);  // onto the end
    EXPECT_EQ(moved, 1);
    int cmp = 1;
    r->CompareEndpoints(TextPatternRangeEndpoint_Start, doc.Get(), TextPatternRangeEndpoint_End, &cmp);
    EXPECT_EQ(cmp, 0);
    // Count 0 leaves a non-degenerate range exactly as it was.
    ComPtr<ITextRangeProvider> whole;
    doc->Clone(&whole);
    whole->MoveEndpointByUnit(TextPatternRangeEndpoint_Start, TextUnit_Character, 1, &moved);  // "b"
    whole->Move(TextUnit_Word, 0, &moved);
    EXPECT_EQ(moved, 0);
    EXPECT_TRUE(RangeText(whole.Get()) == L"b");
}
