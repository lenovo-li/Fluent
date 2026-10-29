// TooltipArmTests.cpp — NativeWindowHost's tooltip re-arm rule and its popup
// suppression (TooltipArming). Pure logic, no HWND.
//
// The hover target is the element under the pointer AND its tooltip text. A composite
// control that is a single hit target (NavRail) swaps its text per region; keying on
// the element alone meant that after the first popup, moving to another item showed
// nothing until a click cleared the state.

#include "../framework/Test.h"
#include "LayoutTestHelpers.h"
#include "../../FluentUI/window/NativeWindowHost.h"
#include "../../FluentUI/window/ModalFrameHeartbeat.h"
#include "../../FluentUI/window/TrayIcon.h"

using namespace fluent;

namespace {
class HostProbe : public NativeWindowHost {
public:
    using NativeWindowHost::TooltipTargetChanged;
    using NativeWindowHost::TooltipArming;
};
using Arming = HostProbe::TooltipArming;
using Action = Arming::Action;
}  // namespace

TEST(TooltipArm, SameElementSameTextKeepsState) {
    TestLeaf a;
    a.SetTooltip(L"Overview");
    EXPECT_FALSE(HostProbe::TooltipTargetChanged(&a, L"Overview", &a));
}

TEST(TooltipArm, DifferentElementIsNewTarget) {
    TestLeaf a, b;
    a.SetTooltip(L"Same");
    b.SetTooltip(L"Same");
    EXPECT_TRUE(HostProbe::TooltipTargetChanged(&a, L"Same", &b));
}

// The NavRail case: same element, the text moved on to the next item.
TEST(TooltipArm, SameElementNewTextIsNewTarget) {
    TestLeaf rail;
    rail.SetTooltip(L"Firmware");
    EXPECT_TRUE(HostProbe::TooltipTargetChanged(&rail, L"Overview", &rail));
}

// Entered on a region with no tooltip (armed with empty text), then reached one
// that has a tooltip: must arm.
TEST(TooltipArm, SameElementTextAppearsIsNewTarget) {
    TestLeaf rail;
    rail.SetTooltip(L"Lenovo ThinkPad USB4 Smart Dock 5500 - 40BC DMC");
    EXPECT_TRUE(HostProbe::TooltipTargetChanged(&rail, L"", &rail));
}

// The text went away on the same element: a new (empty) target, so a visible
// tooltip for the previous region is hidden.
TEST(TooltipArm, SameElementTextClearedIsNewTarget) {
    TestLeaf rail;
    EXPECT_TRUE(HostProbe::TooltipTargetChanged(&rail, L"Overview", &rail));
}

TEST(TooltipArm, NothingHoveredTwiceKeepsState) {
    EXPECT_FALSE(HostProbe::TooltipTargetChanged(nullptr, L"", nullptr));
}

TEST(TooltipArm, LeavingToNothingIsNewTarget) {
    TestLeaf a;
    a.SetTooltip(L"Overview");
    EXPECT_TRUE(HostProbe::TooltipTargetChanged(&a, L"Overview", nullptr));
}

// --- TooltipArming: popups dismiss and suppress the tooltip ------------------
//
// The state machine NativeWindowHost drives from WM_MOUSEMOVE / button-down /
// WM_FLUENT_POPUP_OPENED. The HWND side (the message itself, IsWindowVisible
// pruning, the timer, the card) is not exercised here; `popupOpen` stands in for
// NativeWindowHost::InteractivePopupOpen().

TEST(TooltipArming, HoverArmsOnceThenKeeps) {
    TestLeaf a;
    a.SetTooltip(L"Settings");
    Arming s;
    EXPECT_TRUE(s.OnHover(&a, false) == Action::HideAndArm);
    EXPECT_TRUE(s.Armed() == &a);
    EXPECT_TRUE(s.OnHover(&a, false) == Action::Keep);
}

TEST(TooltipArming, HoverWithoutTooltipHidesButDoesNotArm) {
    TestLeaf a, plain;
    a.SetTooltip(L"Settings");
    Arming s;
    s.OnHover(&a, false);
    EXPECT_TRUE(s.OnHover(&plain, false) == Action::Hide);
}

// The reported bug: hover a flyout button until its tooltip shows, click. The
// press clears the armed target, the flyout opens on release, and the next moves
// over the same button must not arm the tooltip over the menu's first row.
TEST(TooltipArming, PopupOpenedSuppressesTheButtonUnderThePointer) {
    TestLeaf button;
    button.SetTooltip(L"More actions");
    Arming s;
    s.OnHover(&button, false);
    s.OnDismissed();                 // WM_LBUTTONDOWN
    s.OnPopupOpened(&button);        // Button::OnClick -> MenuFlyout::ShowBelow
    EXPECT_TRUE(s.Armed() == nullptr);
    EXPECT_TRUE(s.Suppressing());
    EXPECT_TRUE(s.OnHover(&button, true) == Action::Keep);
    EXPECT_TRUE(s.Armed() == nullptr);
}

// Popup opened while the hover timer was still counting down: Armed() goes null,
// so a WM_TIMER that slipped through finds nothing to show.
TEST(TooltipArming, PopupOpenedDisarmsAPendingTimer) {
    TestLeaf a;
    a.SetTooltip(L"Rename");
    Arming s;
    s.OnHover(&a, false);
    s.OnPopupOpened(&a);
    EXPECT_TRUE(s.Armed() == nullptr);
}

// While a popup is visible no element arms, not even one the popup did not open
// from (pointer slides from the flyout button to its neighbour).
TEST(TooltipArming, NothingArmsWhilePopupOpen) {
    TestLeaf button, neighbour;
    button.SetTooltip(L"More actions");
    neighbour.SetTooltip(L"Refresh");
    Arming s;
    s.OnPopupOpened(&button);
    EXPECT_TRUE(s.OnHover(&neighbour, true) == Action::Keep);
    EXPECT_TRUE(s.Armed() == nullptr);
    // Leaving the button lifted its suppression; only the popup gate is holding now.
    EXPECT_FALSE(s.Suppressing());
}

// Something armed before the gate came up (popup reached without an open notice
// clearing it first) is hidden by the next move.
TEST(TooltipArming, ArmedTargetHiddenWhenPopupOpen) {
    TestLeaf a;
    a.SetTooltip(L"Rename");
    Arming s;
    s.OnHover(&a, false);
    EXPECT_TRUE(s.OnHover(&a, true) == Action::Hide);
    EXPECT_TRUE(s.Armed() == nullptr);
}

// Menu closed (Esc / keyboard pick / clicking the button again) while the pointer
// still rests on the button: the tooltip stays away.
TEST(TooltipArming, StaysSuppressedAfterPopupClosesOnSameElement) {
    TestLeaf button;
    button.SetTooltip(L"More actions");
    Arming s;
    s.OnPopupOpened(&button);
    EXPECT_TRUE(s.OnHover(&button, false) == Action::Keep);
    EXPECT_TRUE(s.Armed() == nullptr);
    EXPECT_TRUE(s.Suppressing());
}

TEST(TooltipArming, ReArmsOnAnotherElementAfterPopupCloses) {
    TestLeaf button, other;
    button.SetTooltip(L"More actions");
    other.SetTooltip(L"Refresh");
    Arming s;
    s.OnPopupOpened(&button);
    EXPECT_TRUE(s.OnHover(&other, false) == Action::HideAndArm);
    EXPECT_TRUE(s.Armed() == &other);
}

// Leave the button and come back: it is a fresh hover and arms again.
TEST(TooltipArming, ReArmsAfterLeavingAndReturning) {
    TestLeaf button, other;
    button.SetTooltip(L"More actions");
    Arming s;
    s.OnPopupOpened(&button);
    s.OnHover(&other, false);
    EXPECT_TRUE(s.OnHover(&button, false) == Action::HideAndArm);
    EXPECT_TRUE(s.Armed() == &button);
}

// Pointer moved off the button while the menu was open (into the menu, or to a
// neighbour), menu closed, pointer back on the button: arms.
TEST(TooltipArming, ReArmsOnOriginalElementIfPointerLeftWhileOpen) {
    TestLeaf button, neighbour;
    button.SetTooltip(L"More actions");
    Arming s;
    s.OnPopupOpened(&button);
    s.OnHover(&neighbour, true);
    EXPECT_TRUE(s.OnHover(&button, false) == Action::HideAndArm);
}

// A context menu opens AT the cursor, so the main window gets WM_MOUSELEAVE without
// the pointer moving. That leave must not lift the suppression.
TEST(TooltipArming, WindowLeaveKeepsSuppression) {
    TestLeaf item;
    item.SetTooltip(L"Device");
    Arming s;
    s.OnPopupOpened(&item);          // right-click -> ContextMenu()->ShowAt
    s.OnDismissed();                 // WM_MOUSELEAVE (cursor now over the popup)
    EXPECT_TRUE(s.Suppressing());
    EXPECT_TRUE(s.OnHover(&item, false) == Action::Keep);  // menu closed, same spot
}

// A plain press (no popup) keeps today's behaviour: hidden now, re-armed by the
// next move over the same element.
TEST(TooltipArming, PlainPressHidesThenNextMoveReArms) {
    TestLeaf a;
    a.SetTooltip(L"Apply");
    Arming s;
    s.OnHover(&a, false);
    s.OnDismissed();
    EXPECT_TRUE(s.Armed() == nullptr);
    EXPECT_FALSE(s.Suppressing());
    EXPECT_TRUE(s.OnHover(&a, false) == Action::HideAndArm);
}

// NavRail: one element, text per region. A context menu opened on one region
// suppresses that region only; the next region arms once the menu is gone.
TEST(TooltipArming, SuppressionIsPerRegionText) {
    TestLeaf rail;
    rail.SetTooltip(L"Overview");
    Arming s;
    s.OnPopupOpened(&rail);
    EXPECT_TRUE(s.OnHover(&rail, false) == Action::Keep);
    rail.SetTooltip(L"Firmware");
    EXPECT_TRUE(s.OnHover(&rail, false) == Action::HideAndArm);
}

// The open notice is a WM_APP-relative message delivered to the same window as the
// modal tick and the tray callback; a collision would dismiss tooltips on every
// frame tick or tray click (or treat a popup opening as either of those).
TEST(TooltipArming, PopupOpenedMessageIsDistinct) {
    EXPECT_TRUE(WM_FLUENT_POPUP_OPENED >= WM_APP);
    EXPECT_TRUE(WM_FLUENT_POPUP_OPENED != WM_FLUENT_MODAL_TICK);
    EXPECT_TRUE(WM_FLUENT_POPUP_OPENED != WM_FLUENT_TRAY_CALLBACK);
}

// Popup opened from the keyboard with the pointer over nothing: the first element
// hovered after it closes arms normally.
TEST(TooltipArming, PopupOpenedOverNothingThenHoverArms) {
    TestLeaf a;
    a.SetTooltip(L"Apply");
    Arming s;
    s.OnPopupOpened(nullptr);
    EXPECT_TRUE(s.Suppressing());
    EXPECT_TRUE(s.OnHover(nullptr, false) == Action::Keep);
    EXPECT_TRUE(s.OnHover(&a, false) == Action::HideAndArm);
}
