// PasswordBoxTests.cpp — PasswordBox reveal button, clipboard refusal, and IME refusal.
//
// What is covered headlessly, and what is not:
//   * The reveal gesture is driven by calling the routed pointer virtuals directly, the
//     same way TextEditInteractionTests drives TextBox. The canceling release InputManager
//     sends on deactivation / capture theft is simulated with its exact args (-1,-1).
//   * The IME policy is checked through ImeSuppressed(), the decision TextEditBase applies
//     to the window. With no HWND (headless) ImmAssociateContextEx is not called, so that
//     the language bar actually goes grey has to be checked on a real window.
//   * The eye glyph is checked by counting ink in a real offscreen D2D target. The
//     PixelSurface has no ResourceCache, so the fallback ellipse is what gets drawn there;
//     the cached-geometry path is the one the live window uses.
//
// Needs a real DirectWrite factory (layout widths and hit-testing must be genuine), so
// this is a component test and self-skips when DWrite is unavailable.

#include "../framework/Test.h"
#include "../framework/PixelSurface.h"
#include "../../FluentUI/controls/PasswordBox.h"
#include "../../FluentUI/graphics/DWriteContext.h"
#include "../../FluentUI/core/UIContext.h"
#include <imm.h>
#include <cstdio>
#include <cstdlib>
#include <string>

using namespace fluent;
using fltest::Pixel;
using fltest::PixelSurface;

namespace fluent {
// Friend peer: reaches the protected TextBox / TextEditBase state through a PasswordBox
// (protected access through the derived type), plus the private button rect.
struct PasswordBoxTestPeer {
    static std::wstring Display(const PasswordBox& b) {
        std::wstring scratch;
        return b.DisplayText(scratch);
    }
    static bool AllowCopy(const PasswordBox& b) { return b.AllowCopyToClipboard(); }
    static RectDip Button(const PasswordBox& b) { return b.RevealButtonRect(); }
    static float ContentWidth(const PasswordBox& b) { return b.ContentWidth(); }
    // The Cut / Copy items as the context menu would build them right now.
    static void MenuState(PasswordBox& b, bool* cut, bool* copy) {
        std::vector<MenuItem> items = b.BuildEditMenuItems();
        *cut = items.size() > 0 && items[0].enabled;
        *copy = items.size() > 1 && items[1].enabled;
    }
    static std::wstring Composition(const PasswordBox& b) { return b.composition_; }
};
}  // namespace fluent

namespace {

DWriteContext& Ctx() {
    static DWriteContext ctx;
    static bool inited = SUCCEEDED(ctx.Initialize());
    (void)inited;
    return ctx;
}

const std::wstring kSecret = L"hunter2";
const std::wstring kBullets(7, L'\x2022');

// An attached, focused PasswordBox at (0,0) 240x32, holding kSecret.
struct Box {
    UIContext ctx{};
    PasswordBox box;
    explicit Box(DWriteContext* dw, const std::wstring& text = kSecret) {
        ctx.dwrite = dw;
        box.AttachToContext(ctx);
        box.SetBounds({0.0f, 0.0f, 240.0f, 32.0f});
        box.SetText(text);
        box.SetFocused(true);
    }
};

PointerEventArgs PressAt(float x, float y, int clickCount = 1) {
    PointerEventArgs e;
    e.position = Point{x, y};
    e.button = PointerButton::Left;
    e.clickCount = clickCount;
    return e;
}

Point Center(const RectDip& r) { return Point{r.x + r.w * 0.5f, r.y + r.h * 0.5f}; }

}  // namespace

// --- Defaults and button visibility ------------------------------------------

TEST(PasswordBox, DefaultsToPeekWithButtonOnlyWhileTextPresent) {
    auto& dw = Ctx();
    if (!dw.Valid()) return;
    Box b(&dw, L"");
    EXPECT_TRUE(b.box.IsPassword());
    EXPECT_TRUE(b.box.GetPasswordRevealMode() == PasswordRevealMode::Peek);
    EXPECT_FALSE(b.box.IsRevealButtonVisible());   // empty: no button

    b.box.SetText(kSecret);
    EXPECT_TRUE(b.box.IsRevealButtonVisible());

    // Unfocused still shows it (documented deviation from WinUI, see PasswordBox.h).
    b.box.SetFocused(false);
    EXPECT_TRUE(b.box.IsRevealButtonVisible());

    b.box.SetEnabled(false);
    EXPECT_FALSE(b.box.IsRevealButtonVisible());
    b.box.SetEnabled(true);

    b.box.SetPasswordRevealMode(PasswordRevealMode::Hidden);
    EXPECT_FALSE(b.box.IsRevealButtonVisible());
}

// --- Peek gesture ------------------------------------------------------------

TEST(PasswordBox, HoldingButtonRevealsAndReleaseMasks) {
    auto& dw = Ctx();
    if (!dw.Valid()) return;
    Box b(&dw);
    EXPECT_TRUE(PasswordBoxTestPeer::Display(b.box) == kBullets);

    const Point c = Center(PasswordBoxTestPeer::Button(b.box));
    auto down = PressAt(c.x, c.y);
    b.box.OnPointerPressed(down);
    EXPECT_TRUE(down.handled);
    EXPECT_TRUE(b.box.IsPeeking());
    EXPECT_TRUE(PasswordBoxTestPeer::Display(b.box) == kSecret);

    // Dragging off the button keeps the peek; only the release ends it.
    auto move = PressAt(5.0f, 16.0f);
    b.box.OnPointerMoved(move);
    EXPECT_TRUE(PasswordBoxTestPeer::Display(b.box) == kSecret);

    auto up = PressAt(c.x, c.y);
    b.box.OnPointerReleased(up);
    EXPECT_FALSE(b.box.IsPeeking());
    EXPECT_TRUE(PasswordBoxTestPeer::Display(b.box) == kBullets);
}

// The press is consumed by the button: caret and selection stay where they were, and a
// following move does not drag-select. A double-click on the eye must not select a word.
TEST(PasswordBox, ButtonPressLeavesCaretAndSelectionAlone) {
    auto& dw = Ctx();
    if (!dw.Valid()) return;
    Box b(&dw);
    b.box.TestSetCaret(2);

    const Point c = Center(PasswordBoxTestPeer::Button(b.box));
    for (int clicks = 1; clicks <= 2; ++clicks) {
        auto down = PressAt(c.x, c.y, clicks);
        b.box.OnPointerPressed(down);
        auto move = PressAt(5.0f, 16.0f);
        b.box.OnPointerMoved(move);
        auto up = PressAt(5.0f, 16.0f);
        b.box.OnPointerReleased(up);
        EXPECT_EQ(b.box.SelectionStartForTest(), 2u);
        EXPECT_EQ(b.box.SelectionEndForTest(), 2u);
    }
    EXPECT_TRUE(b.box.IsFocused());
}

// Negative control for the test above: with the button hidden, the same press is a
// normal TextBox click and DOES move the caret, so the test above is measuring the
// button and not a press that never moves the caret anyway.
TEST(PasswordBox, HiddenModePressInButtonAreaPlacesCaret) {
    auto& dw = Ctx();
    if (!dw.Valid()) return;
    Box b(&dw);
    b.box.TestSetCaret(2);
    const Point c = Center(PasswordBoxTestPeer::Button(b.box));
    b.box.SetPasswordRevealMode(PasswordRevealMode::Hidden);
    auto down = PressAt(c.x, c.y);
    b.box.OnPointerPressed(down);
    auto up = PressAt(c.x, c.y);
    b.box.OnPointerReleased(up);
    EXPECT_EQ(b.box.SelectionEndForTest(), static_cast<UINT32>(kSecret.size()));
    EXPECT_TRUE(PasswordBoxTestPeer::Display(b.box) == kBullets);
}

// InputManager's canceling release (window deactivated, capture stolen, detach) carries
// position (-1,-1). It must mask, not leave the password on screen.
TEST(PasswordBox, CancelingReleaseMasks) {
    auto& dw = Ctx();
    if (!dw.Valid()) return;
    Box b(&dw);
    const Point c = Center(PasswordBoxTestPeer::Button(b.box));
    auto down = PressAt(c.x, c.y);
    b.box.OnPointerPressed(down);
    EXPECT_TRUE(b.box.IsPeeking());

    auto cancel = PressAt(-1.0f, -1.0f);
    b.box.OnPointerReleased(cancel);
    EXPECT_FALSE(b.box.IsPeeking());
    EXPECT_TRUE(PasswordBoxTestPeer::Display(b.box) == kBullets);
}

TEST(PasswordBox, FocusLossWhileHeldMasks) {
    auto& dw = Ctx();
    if (!dw.Valid()) return;
    Box b(&dw);
    const Point c = Center(PasswordBoxTestPeer::Button(b.box));
    auto down = PressAt(c.x, c.y);
    b.box.OnPointerPressed(down);
    EXPECT_TRUE(b.box.IsPeeking());

    b.box.SetFocused(false);
    EXPECT_FALSE(b.box.IsPeeking());
    EXPECT_TRUE(PasswordBoxTestPeer::Display(b.box) == kBullets);
}

TEST(PasswordBox, VisibleModeShowsPlainTextWithoutButton) {
    auto& dw = Ctx();
    if (!dw.Valid()) return;
    Box b(&dw);
    b.box.SetPasswordRevealMode(PasswordRevealMode::Visible);
    EXPECT_FALSE(b.box.IsRevealButtonVisible());
    EXPECT_TRUE(PasswordBoxTestPeer::Display(b.box) == kSecret);
    b.box.SetPasswordRevealMode(PasswordRevealMode::Peek);
    EXPECT_TRUE(PasswordBoxTestPeer::Display(b.box) == kBullets);
}

// --- Clipboard ---------------------------------------------------------------

// Copy / Cut key on password mode, not on the mask: a peek or Visible mode must not make
// the secret copyable. Checked on the menu items (what the user sees) and the policy hook
// (what Ctrl+C / Ctrl+X consult).
TEST(PasswordBox, CopyAndCutRefusedEvenWhileRevealed) {
    auto& dw = Ctx();
    if (!dw.Valid()) return;
    Box b(&dw);
    b.box.Select(0, static_cast<UINT32>(kSecret.size()));

    bool cut = true, copy = true;
    PasswordBoxTestPeer::MenuState(b.box, &cut, &copy);
    EXPECT_FALSE(PasswordBoxTestPeer::AllowCopy(b.box));
    EXPECT_FALSE(cut);
    EXPECT_FALSE(copy);

    const Point c = Center(PasswordBoxTestPeer::Button(b.box));
    auto down = PressAt(c.x, c.y);
    b.box.OnPointerPressed(down);
    EXPECT_TRUE(PasswordBoxTestPeer::Display(b.box) == kSecret);
    PasswordBoxTestPeer::MenuState(b.box, &cut, &copy);
    EXPECT_FALSE(PasswordBoxTestPeer::AllowCopy(b.box));
    EXPECT_FALSE(cut);
    EXPECT_FALSE(copy);
    auto up = PressAt(c.x, c.y);
    b.box.OnPointerReleased(up);

    b.box.SetPasswordRevealMode(PasswordRevealMode::Visible);
    PasswordBoxTestPeer::MenuState(b.box, &cut, &copy);
    EXPECT_FALSE(PasswordBoxTestPeer::AllowCopy(b.box));
    EXPECT_FALSE(copy);

    // Control: out of password mode the same selection IS copyable, so the assertions
    // above are measuring the password rule and not a menu that is always greyed.
    b.box.SetPassword(false);
    PasswordBoxTestPeer::MenuState(b.box, &cut, &copy);
    EXPECT_TRUE(PasswordBoxTestPeer::AllowCopy(b.box));
    EXPECT_TRUE(cut);
    EXPECT_TRUE(copy);
}

// --- Text area reserve -------------------------------------------------------

TEST(PasswordBox, ButtonReservesTextWidth) {
    auto& dw = Ctx();
    if (!dw.Valid()) return;
    Box b(&dw);
    const float withButton = PasswordBoxTestPeer::ContentWidth(b.box);
    b.box.SetPasswordRevealMode(PasswordRevealMode::Hidden);
    const float without = PasswordBoxTestPeer::ContentWidth(b.box);
    EXPECT_TRUE(withButton < without);

    // The text viewport ends left of the button plate.
    b.box.SetPasswordRevealMode(PasswordRevealMode::Peek);
    const RectDip btn = PasswordBoxTestPeer::Button(b.box);
    EXPECT_TRUE(10.0f + withButton <= btn.x);
}

// A password longer than the box scrolls so the caret at the end sits left of the
// button, not under it. Fails if ContentWidth ignores the reserve (the caret would be
// placed up to the right padding, inside the plate).
TEST(PasswordBox, CaretAtEndStaysLeftOfButton) {
    auto& dw = Ctx();
    if (!dw.Valid()) return;
    Box b(&dw, std::wstring(80, L'x'));  // far wider than 240 DIP even as bullets
    RectDip caret{};
    EXPECT_TRUE(b.box.CaretRectDip(caret));
    const RectDip btn = PasswordBoxTestPeer::Button(b.box);
    std::printf("  caret x %.1f, button x %.1f\n", caret.x, btn.x);
    EXPECT_TRUE(caret.right() <= btn.x);
}

// --- IME -----------------------------------------------------------------------

TEST(PasswordBox, ImeSuppressedOnlyWhileFocusedInPasswordMode) {
    auto& dw = Ctx();
    if (!dw.Valid()) return;
    Box b(&dw);
    EXPECT_TRUE(b.box.ImeSuppressed());       // focused password box

    b.box.SetFocused(false);
    EXPECT_FALSE(b.box.ImeSuppressed());      // blur restores

    b.box.SetFocused(true);
    EXPECT_TRUE(b.box.ImeSuppressed());
    b.box.SetPassword(false);                 // leaving password mode while focused
    EXPECT_FALSE(b.box.ImeSuppressed());
    b.box.SetPassword(true);
    EXPECT_TRUE(b.box.ImeSuppressed());

    // Revealing does not bring the IME back, including on a fresh focus while the text
    // is shown (the refocus is what re-evaluates the policy).
    b.box.SetPasswordRevealMode(PasswordRevealMode::Visible);
    EXPECT_TRUE(b.box.ImeSuppressed());
    b.box.SetFocused(false);
    b.box.SetFocused(true);
    EXPECT_TRUE(b.box.ImeSuppressed());
    b.box.SetPasswordRevealMode(PasswordRevealMode::Peek);

    // Leaving the tree while focused restores (FocusManager does not blur on detach).
    b.box.DetachFromContext();
    EXPECT_FALSE(b.box.ImeSuppressed());
}

// Two boxes: focus moving password -> plain gives the plain box its IME (it never
// suppresses), and plain -> password suppresses again.
TEST(PasswordBox, PlainTextBoxKeepsIme) {
    auto& dw = Ctx();
    if (!dw.Valid()) return;
    UIContext ctx{};
    ctx.dwrite = &dw;
    PasswordBox pwd;
    TextBox plain;
    pwd.AttachToContext(ctx);
    plain.AttachToContext(ctx);

    pwd.SetFocused(true);
    EXPECT_TRUE(pwd.ImeSuppressed());
    pwd.SetFocused(false);                    // FocusManager blurs the old one first
    plain.SetFocused(true);
    EXPECT_FALSE(pwd.ImeSuppressed());
    EXPECT_FALSE(plain.ImeSuppressed());
}

// Backstop: a composition message that still reaches a password box is dropped, and
// any composition string it had is cleared. (The HWND is null here, so a plain TextBox
// returns early for lack of an input context and keeps its composition — which is the
// control showing the password path is the one doing the clearing.)
TEST(PasswordBox, CompositionRefusedInPasswordMode) {
    auto& dw = Ctx();
    if (!dw.Valid()) return;
    Box b(&dw);
    b.box.TestSetComposition(L"zhong");
    b.box.OnImeComposition(nullptr, GCS_COMPSTR);
    EXPECT_TRUE(PasswordBoxTestPeer::Composition(b.box).empty());

    b.box.SetPassword(false);
    b.box.TestSetComposition(L"zhong");
    b.box.OnImeComposition(nullptr, GCS_COMPSTR);
    EXPECT_TRUE(PasswordBoxTestPeer::Composition(b.box) == L"zhong");
}

// --- Pixels --------------------------------------------------------------------

namespace {
constexpr Pixel kBg{255, 255, 255, 255};

int InkIn(const PixelSurface& s, const RectDip& r) {
    int n = 0;
    for (int y = static_cast<int>(r.y); y < static_cast<int>(r.bottom()); ++y)
        for (int x = static_cast<int>(r.x); x < static_cast<int>(r.right()); ++x) {
            const Pixel p = s.At(x, y);
            const int d = std::abs(int(p.r) - int(kBg.r)) + std::abs(int(p.g) - int(kBg.g)) +
                          std::abs(int(p.b) - int(kBg.b));
            if (d > 60) ++n;
        }
    return n;
}

// Ink inside the button rect for a box in `mode`. -1 = no D2D device.
int ButtonInk(PasswordRevealMode mode) {
    PixelSurface surf(260, 40);
    if (!surf.Valid()) return -1;
    UIContext ctx{};
    ctx.dwrite = surf.Dwrite();
    PasswordBox box;
    box.AttachToContext(ctx);
    box.SetBounds({0.0f, 0.0f, 240.0f, 32.0f});
    box.SetText(kSecret);
    box.SetPasswordRevealMode(mode);
    const RectDip btn = PasswordBoxTestPeer::Button(box);
    surf.Paint(kBg, [&](DrawingContext& gc) { box.Render(gc); });
    if (!surf.ReadBack()) return -1;
    // Shrink by 2 DIP so the box's own border stroke never counts.
    return InkIn(surf, {btn.x + 2.0f, btn.y + 2.0f, btn.w - 4.0f, btn.h - 4.0f});
}
}  // namespace

// The eye is painted in Peek mode and absent in Hidden mode. A difference, not an
// absolute count: the same box differs only in the mode, so nothing else can explain
// the extra ink.
TEST(PasswordBox, EyeGlyphPaintedInPeekMode) {
    const int peek = ButtonInk(PasswordRevealMode::Peek);
    const int hidden = ButtonInk(PasswordRevealMode::Hidden);
    if (peek < 0 || hidden < 0) { std::printf("  SKIP: no D2D device\n"); EXPECT_TRUE(false); return; }
    std::printf("  button-rect ink: peek %d, hidden %d\n", peek, hidden);
    EXPECT_TRUE(peek > hidden + 10);
}
