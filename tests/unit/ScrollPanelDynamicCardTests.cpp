// ScrollPanelDynamicCardTests.cpp
//
// Tests that a Grid (fixed | Star | fixed) with a padded ScrollPanel in the Star
// column correctly allows the last card to scroll fully into view when cards are
// added dynamically after the first layout pass. The card structure mirrors the
// real DeviceCard: Border + StackPanel + two wrapping TextBlocks (wrap defaults
// to true in TextBlock) + a status TextBlock + a horizontal button row.
//
// This covers the STATIC path (Measure → Arrange → AddCards → Measure → Arrange).
// The live-GUI scenario (cards added mid-frame with the scroll viewer already
// showing) requires the real window pipeline and cannot be driven headlessly; the
// test notes that gap explicitly.
#include "../framework/Test.h"
#include "../../FluentUI/layout/Grid.h"
#include "../../FluentUI/layout/ScrollPanel.h"
#include "../../FluentUI/layout/StackPanel.h"
#include "../../FluentUI/layout/Border.h"
#include "../../FluentUI/controls/TextBlock.h"
#include "../../FluentUI/controls/Button.h"
#include "../../FluentUI/window/WindowServices.h"
#include "../../FluentUI/graphics/D2DContext.h"
#include "../../FluentUI/graphics/DWriteContext.h"
#include "../../FluentUI/animation/AnimationRegistry.h"
#include "../../FluentUI/core/UIContext.h"
#include <cmath>
#include <limits>

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
    Subscription RegisterActivePopupWheelHandler(
        std::function<bool(int, int, int)>) override { return {}; }
    bool Ready() const { return dwrite_.Valid(); }
private:
    D2DContext d2d_;
    DWriteContext dwrite_;
};

// Add one card identical in structure to VkLiteDevInterfaceGUI::DeviceCard.
void AddCard(StackPanel* list, const wchar_t* title, const wchar_t* path) {
    auto* card = list->Emplace<Border>();
    card->SetPadding(Thickness(12.0f));
    card->SetBorderThickness(1.0f);
    card->SetCornerRadius(8.0f);

    auto* inner = card->SetChild(std::make_unique<StackPanel>());
    inner->SetSpacing(4.0f);

    // Title: wrap defaults to true in TextBlock.
    inner->Emplace<TextBlock>()->SetText(title);

    // Path: long device-interface path that wraps at the column width.
    auto* pathLbl = inner->Emplace<TextBlock>();
    pathLbl->SetText(path);
    pathLbl->SetWrap(true);

    // Status label + button row.
    inner->Emplace<TextBlock>()->SetText(L"○ 未打开");
    auto* btns = inner->Emplace<StackPanel>();
    btns->SetOrientation(StackPanel::Orientation::Horizontal);
    btns->SetSpacing(6.0f);
    btns->Emplace<Button>()->SetText(L"打开");
    btns->Emplace<Button>()->SetText(L"关闭");
}
}  // namespace

// After dynamically adding 7 overflow-causing cards to the ScrollPanel and
// re-running the layout pass, verify that every TextBlock in the last card
// fits within its arranged bounds when re-measured at its actual width. This
// catches stale contentHeight_ bugs where the extent was computed at a width
// wider than the one cards were arranged at (Grid's first pass measures at
// full width before resolving Star columns, then re-measures at the narrow width).
//
// A weaker assertion (lastCard.bottom <= viewport.bottom after scrolling to max)
// is tautological: both the scroll extent and the child bounds come from the same
// Desired().h cache, so if that cache is stale (measured at the wrong width), both
// are wrong in the same way and still match each other. The independent oracle is
// fresh measurement at the arranged width.
TEST(ScrollPanel, DynamicCardBottomClipping) {
    MockHost host;
    AnimationRegistry anims;
    // DWrite is required for real TextBlock wrap measurement.
    if (!host.Ready()) return;

    UIContext ctx;
    ctx.window = &host;
    ctx.animations = &anims;
    ctx.dwrite = &host.DWrite();
    ctx.dpiScale = 1.0f;

    Grid grid;
    grid.AttachToContext(ctx);
    // Three-column layout: 280px | * | 340px, 12 DIP column spacing, 12 DIP
    // padding — same as the real VkLiteDevInterfaceGUI that exhibits the bug.
    grid.SetColumns({GridLength::Pixels(280.0f),
                     GridLength::Star(1.0f),
                     GridLength::Pixels(340.0f)});
    grid.SetRows({GridLength::Star(1.0f)});
    grid.SetColumnSpacing(12.0f);
    grid.SetPadding(Thickness(12.0f));

    auto* scroll = grid.Emplace<ScrollPanel>();
    scroll->SetPadding(8.0f);
    grid.SetCell(scroll, 0, 1);

    auto* cardList = scroll->Emplace<StackPanel>();
    cardList->SetSpacing(10.0f);

    // First layout pass with an empty list (mirrors app startup).
    grid.Measure(1280.0f, 800.0f);
    grid.Arrange(RectDip{0, 0, 1280.0f, 800.0f});

    // Add 7 cards — enough to overflow the 776 DIP viewport at any wrapping width.
    const wchar_t* kTitle = L"[VKL] VendorKeyLite Device (DevInterface Experiment)";
    const wchar_t* kPath  =
        L"\\\\?\\HID#VID_17EF&PID_629D&MI_02&Col04#7"
        L"&1c6351ca&0&0003#{ef0fea84-2d1d-4429-88e3-59dd1bb9f1c2}";
    for (int i = 0; i < 7; ++i)
        AddCard(cardList, kTitle, kPath);

    // Second layout pass (the frame that follows the Emplace calls).
    grid.Measure(1280.0f, 800.0f);
    grid.Arrange(RectDip{0, 0, 1280.0f, 800.0f});

    // There must be enough content to scroll.
    const float maxOff = scroll->MaxVerticalOffset();
    EXPECT_TRUE(maxOff > 50.0f);

    // After scrolling to the maximum offset and re-arranging, walk the last card's
    // TextBlocks and verify each one fits within its arranged bounds when measured
    // at its actual width. This is the independent oracle: a fresh Measure at the
    // arranged width must not exceed the height the element was given.
    scroll->SetVerticalOffset(maxOff);
    grid.Arrange(RectDip{0, 0, 1280.0f, 800.0f});

    auto* lastCard = cardList->ChildAt(cardList->ChildCount() - 1);
    auto* border = dynamic_cast<Border*>(lastCard);
    EXPECT_TRUE(border != nullptr);
    auto* inner = dynamic_cast<StackPanel*>(border->Child());
    EXPECT_TRUE(inner != nullptr);

    // Walk inner's TextBlocks (title, path, status), re-measure each at its
    // arranged width, and confirm the fresh desired height fits.
    for (size_t i = 0; i < inner->ChildCount(); ++i) {
        auto* child = inner->ChildAt(i);
        auto* tb = dynamic_cast<TextBlock*>(child);
        if (!tb) continue;  // skip button row

        const RectDip& arranged = tb->Bounds();
        const float arrangedW = arranged.w;
        const float arrangedH = arranged.h;

        // Fresh measurement at the arranged width.
        TextBlock probe;
        probe.AttachToContext(ctx);
        probe.SetText(tb->Text());
        probe.SetWrap(true);
        probe.Measure(arrangedW, std::numeric_limits<float>::infinity());
        const float freshH = probe.Desired().h;

        // The fresh height must not exceed what was allocated (tolerance for
        // fractional rounding).
        EXPECT_TRUE(freshH <= arrangedH + 0.5f);
    }
}

// Regression: scrolled to the bottom, then a child invalidates its measure. Grid
// re-measures the ScrollPanel twice: first at the full grid width (text wraps
// less, so content is shorter), then at the column width. If the ScrollPanel
// committed the first, shorter extent to its ScrollViewer, SetContentHeight
// clamped the offset against that too-small maximum, and the second, correct
// extent could not restore it: the view jumped up and the last card was clipped.
// The extent is now committed only in Arrange, after the final measure.
TEST(ScrollPanel, BottomOffsetSurvivesSpeculativeFullWidthMeasure) {
    MockHost host;
    AnimationRegistry anims;
    if (!host.Ready()) return;

    UIContext ctx;
    ctx.window = &host;
    ctx.animations = &anims;
    ctx.dwrite = &host.DWrite();
    ctx.dpiScale = 1.0f;

    Grid grid;
    grid.AttachToContext(ctx);
    grid.SetColumns({GridLength::Pixels(280.0f),
                     GridLength::Star(1.0f),
                     GridLength::Pixels(340.0f)});
    grid.SetRows({GridLength::Star(1.0f)});
    grid.SetColumnSpacing(12.0f);
    grid.SetPadding(Thickness(12.0f));

    auto* scroll = grid.Emplace<ScrollPanel>();
    scroll->SetPadding(8.0f);
    grid.SetCell(scroll, 0, 1);
    auto* cardList = scroll->Emplace<StackPanel>();
    cardList->SetSpacing(10.0f);

    // A path long enough to wrap more at the column width than at the full width.
    const wchar_t* kTitle = L"[VKL] VendorKeyLite Device (DevInterface Experiment)";
    const wchar_t* kPath  =
        L"\\?\HID#VID_17EF&PID_629D&MI_02&Col04#7"
        L"&1c6351ca&0&0003#{ef0fea84-2d1d-4429-88e3-59dd1bb9f1c2}";
    for (int i = 0; i < 7; ++i)
        AddCard(cardList, kTitle, kPath);

    const RectDip win{0, 0, 1280.0f, 800.0f};
    grid.Measure(win.w, win.h);
    grid.Arrange(win);

    const float maxBefore = scroll->MaxVerticalOffset();
    EXPECT_TRUE(maxBefore > 50.0f);
    scroll->SetVerticalOffset(maxBefore);
    EXPECT_TRUE(scroll->VerticalOffset() == maxBefore);

    // Invalidate one card's measure without changing its height (same-length
    // text), so the correct extent after the frame equals the one before it.
    auto* lastCard = dynamic_cast<Border*>(cardList->ChildAt(cardList->ChildCount() - 1));
    EXPECT_TRUE(lastCard != nullptr);
    auto* inner = dynamic_cast<StackPanel*>(lastCard->Child());
    EXPECT_TRUE(inner != nullptr);
    auto* status = dynamic_cast<TextBlock*>(inner->ChildAt(2));
    EXPECT_TRUE(status != nullptr);
    status->SetText(L"● 已打开");

    grid.Measure(win.w, win.h);
    grid.Arrange(win);

    // The extent is unchanged, so the offset must still be at the bottom.
    EXPECT_TRUE(std::fabs(scroll->MaxVerticalOffset() - maxBefore) < 0.5f);
    EXPECT_TRUE(std::fabs(scroll->VerticalOffset() - maxBefore) < 0.5f);
}
