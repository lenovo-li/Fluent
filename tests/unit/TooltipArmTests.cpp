// TooltipArmTests.cpp — NativeWindowHost's tooltip re-arm rule. Pure logic, no HWND.
//
// The hover target is the element under the pointer AND its tooltip text. A composite
// control that is a single hit target (NavRail) swaps its text per region; keying on
// the element alone meant that after the first popup, moving to another item showed
// nothing until a click cleared the state.

#include "../framework/Test.h"
#include "LayoutTestHelpers.h"
#include "../../FluentUI/window/NativeWindowHost.h"

using namespace fluent;

namespace {
class HostProbe : public NativeWindowHost {
public:
    using NativeWindowHost::TooltipTargetChanged;
};
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
