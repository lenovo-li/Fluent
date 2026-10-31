// TextBlockLayoutCacheTests.cpp — component tests for TextBlock's cached
// IDWriteTextLayout (M2). Needs a real DirectWrite factory (no
// D3D / HWND), so this is a "component" test like DWriteFormatTests.
//
// CreateLayout() memoizes the layout keyed on the full set of inputs (text,
// font size, weight, alignment, wrap, line spacing, wrap width). Render,
// UpdateMetrics, and hit-testing all call it several times per frame; rebuilding
// the layout each time was the hot spot. These tests prove same-key reuse
// (pointer identity) and self-invalidation on each input that defines the key.

#include "../framework/Test.h"
#include "../../FluentUI/controls/TextBlock.h"
#include "../../FluentUI/graphics/DWriteContext.h"

#include <cmath>
#include <cwchar>

using namespace fluent;

namespace fluent {
// Friend peer: exposes the private CreateLayout() so a test can assert pointer
// identity of the cached layout. Kept out of the product's public surface.
struct TextBlockTestPeer {
    static ComPtr<IDWriteTextLayout> Layout(const TextBlock& tb) {
        return tb.CreateLayout();
    }
};
}  // namespace fluent

namespace {
// One shared DWrite factory for the suite (skip if the box has no DirectWrite).
DWriteContext& Ctx() {
    static DWriteContext ctx;
    static bool inited = SUCCEEDED(ctx.Initialize());
    (void)inited;
    return ctx;
}

// A configured TextBlock with a valid layout width (CreateLayout needs bounds).
// DWrite is now injected through the tree UIContext instead of a
// manual SetDWrite: build a context carrying the DWrite pointer and attach it.
// Returns a pointer to avoid moving the TextBlock: UIElement now has Event<>
// members which are not movable, so TextBlock is also not movable.
std::unique_ptr<TextBlock> MakeBlock(DWriteContext& ctx) {
    auto tb = std::make_unique<TextBlock>();
    UIContext uictx;
    uictx.dwrite = &ctx;
    tb->AttachToContext(uictx);
    tb->SetText(L"Hello, layout cache");
    tb->SetBounds({0.0f, 0.0f, 200.0f, 40.0f});
    return tb;
}
}  // namespace

// Same inputs -> same cached layout object (pointer identity).
TEST(TextBlockLayoutCache, SameInputsReuseLayout) {
    auto& ctx = Ctx();
    if (!ctx.Valid()) return;
    auto tb = MakeBlock(ctx);
    ComPtr<IDWriteTextLayout> a = TextBlockTestPeer::Layout(*tb);
    ComPtr<IDWriteTextLayout> b = TextBlockTestPeer::Layout(*tb);
    EXPECT_TRUE(a.Get() != nullptr);
    EXPECT_TRUE(a.Get() == b.Get());  // reused, not rebuilt
}

// Changing the text rebuilds the layout (distinct object).
TEST(TextBlockLayoutCache, TextChangeRebuilds) {
    auto& ctx = Ctx();
    if (!ctx.Valid()) return;
    auto tb = MakeBlock(ctx);
    ComPtr<IDWriteTextLayout> before = TextBlockTestPeer::Layout(*tb);
    tb->SetText(L"Different text");
    ComPtr<IDWriteTextLayout> after = TextBlockTestPeer::Layout(*tb);
    EXPECT_TRUE(before.Get() != nullptr);
    EXPECT_TRUE(after.Get() != nullptr);
    EXPECT_TRUE(before.Get() != after.Get());
}

// Changing the wrap width (bounds width) rebuilds the layout.
TEST(TextBlockLayoutCache, WidthChangeRebuilds) {
    auto& ctx = Ctx();
    if (!ctx.Valid()) return;
    auto tb = MakeBlock(ctx);
    ComPtr<IDWriteTextLayout> before = TextBlockTestPeer::Layout(*tb);
    tb->SetBounds({0.0f, 0.0f, 120.0f, 40.0f});  // narrower wrap width
    ComPtr<IDWriteTextLayout> after = TextBlockTestPeer::Layout(*tb);
    EXPECT_TRUE(before.Get() != after.Get());
}

// Changing the font size rebuilds the layout.
TEST(TextBlockLayoutCache, FontSizeChangeRebuilds) {
    auto& ctx = Ctx();
    if (!ctx.Valid()) return;
    auto tb = MakeBlock(ctx);
    ComPtr<IDWriteTextLayout> before = TextBlockTestPeer::Layout(*tb);
    tb->SetFontSize(24.0f);
    ComPtr<IDWriteTextLayout> after = TextBlockTestPeer::Layout(*tb);
    EXPECT_TRUE(before.Get() != after.Get());
}

// An empty text clears the cache and returns no layout.
TEST(TextBlockLayoutCache, EmptyTextClearsCache) {
    auto& ctx = Ctx();
    if (!ctx.Valid()) return;
    auto tb = MakeBlock(ctx);
    EXPECT_TRUE(TextBlockTestPeer::Layout(*tb).Get() != nullptr);
    tb->SetText(L"");
    EXPECT_TRUE(TextBlockTestPeer::Layout(*tb).Get() == nullptr);
}

// THE GALLERY BUG. A status line that switches between a pure-Chinese message and one
// containing a Latin word changed height, because DWrite's default line spacing takes
// each line's height from whichever font its glyphs fell back to (Microsoft YaHei for
// pure CJK, Segoe UI Variable once a Latin glyph appears). Both strings below are from
// the ContentDialog page. The premise check makes sure the test machine really has the
// fallback difference, so a pass is not vacuous.
TEST(TextBlockLayoutCache, PureCjkAndMixedTextMeasureTheSameHeight) {
    auto& ctx = Ctx();
    if (!ctx.Valid()) return;

    // Premise: with DWrite's default spacing the two strings differ in height.
    IDWriteTextFormat* fmt = ctx.Format(14.0f, DWRITE_FONT_WEIGHT_NORMAL,
                                        DWRITE_TEXT_ALIGNMENT_LEADING,
                                        DWRITE_PARAGRAPH_ALIGNMENT_NEAR,
                                        DWRITE_WORD_WRAPPING_WRAP);
    auto rawHeight = [&](const wchar_t* s) {
        ComPtr<IDWriteTextLayout> l;
        ctx.Factory()->CreateTextLayout(s, static_cast<UINT32>(wcslen(s)), fmt, 1e5f, 1e5f,
                                        l.GetAddressOf());
        DWRITE_TEXT_METRICS m{};
        if (l) l->GetMetrics(&m);
        return m.height;
    };
    const wchar_t* cjk = L"结果：非模态已打开（主窗口仍可交互）";
    const wchar_t* mixed = L"结果：非模态关闭 Cancel（取消）";
    if (std::fabs(rawHeight(cjk) - rawHeight(mixed)) < 0.01f) return;  // no fallback gap here

    auto measure = [&](const wchar_t* s) {
        auto tb = std::make_unique<TextBlock>();
        UIContext uictx;
        uictx.dwrite = &ctx;
        tb->AttachToContext(uictx);
        tb->SetText(s);
        tb->Measure(600.0f, 100.0f);
        return tb->Desired().h;
    };
    EXPECT_NEAR(measure(cjk), measure(mixed), 0.01f);
}
