// AutomationPeerTests.cpp — headless checks for the UIA peer tree and properties.
//
// A real AutomationHost is the site (with RecordingCore standing in for UIA core
// and FakeWindow for the window), and tests talk to the real COM providers through
// their interfaces, exactly as UIA core would, just without a client process.
// Not covered here: anything UIA core itself does with our answers (merging with
// the HWND proxy, Narrator's speech); that is the Inspect.exe check on hardware.
#include "../framework/Test.h"
#include "AutomationTestHelpers.h"

#include "../../FluentUI/automation/AutomationPeer.h"
#include "../../FluentUI/controls/Button.h"
#include "../../FluentUI/controls/CheckBox.h"
#include "../../FluentUI/controls/TextBlock.h"
#include "../../FluentUI/controls/TextBox.h"
#include "../../FluentUI/layout/StackPanel.h"
#include "../../FluentUI/layout/Viewbox.h"
#include "../../FluentUI/automation/AutomationGeometry.h"

using namespace fluent;
using namespace uiatest;


TEST(AutomationPeer, NoClientMeansNoPeers) {
    // Idle cost: a whole tree laid out, focused and edited without a client must
    // create no peer, no provider, and make no UIA call.
    const size_t before = automation::AutomationPeer::LiveCount();
    UiaFixture fx;
    fx.root->Emplace<Button>()->SetText(L"OK");
    fx.root->Emplace<CheckBox>();
    fx.root->Emplace<TextBox>()->SetText(L"hi");
    fx.Attach();
    fx.root->Measure(400, 300);
    fx.root->Arrange({0, 0, 400, 300});
    fx.host.OnFocusChanged(fx.root->ChildAt(0));
    EXPECT_EQ(automation::AutomationPeer::LiveCount(), before);
    EXPECT_TRUE(fx.core.calls.empty());
    EXPECT_EQ(fx.core.flushPosts, 0);
}

TEST(AutomationPeer, RootChildrenAndNavigation) {
    UiaFixture fx;
    auto* ok = fx.root->Emplace<Button>();
    ok->SetText(L"OK");
    auto* hidden = fx.root->Emplace<Button>();
    hidden->SetVisible(false);
    auto* label = fx.root->Emplace<TextBlock>();
    label->SetText(L"Hello");
    fx.Attach();
    auto rootFrag = fx.Connect();

    auto panel = Nav(rootFrag.Get(), NavigateDirection_FirstChild);
    EXPECT_EQ(PropInt(panel.Get(), UIA_ControlTypePropertyId), UIA_PaneControlTypeId);
    EXPECT_EQ(PropBool(panel.Get(), UIA_IsControlElementPropertyId), 0);  // layout: raw only

    auto first = Nav(panel.Get(), NavigateDirection_FirstChild);
    EXPECT_TRUE(PropString(first.Get(), UIA_NamePropertyId) == L"OK");
    EXPECT_EQ(PropInt(first.Get(), UIA_ControlTypePropertyId), UIA_ButtonControlTypeId);
    // The hidden button is skipped: next sibling is the label.
    auto next = Nav(first.Get(), NavigateDirection_NextSibling);
    EXPECT_TRUE(PropString(next.Get(), UIA_NamePropertyId) == L"Hello");
    EXPECT_EQ(PropInt(next.Get(), UIA_ControlTypePropertyId), UIA_TextControlTypeId);
    EXPECT_TRUE(Nav(next.Get(), NavigateDirection_NextSibling) == nullptr);
    EXPECT_TRUE(Nav(panel.Get(), NavigateDirection_LastChild).Get() == next.Get());
    // Parent of a top-level element is the window root; of a child, the panel.
    EXPECT_TRUE(Nav(first.Get(), NavigateDirection_Parent).Get() == panel.Get());
    ComPtr<IUnknown> a, b;
    Nav(panel.Get(), NavigateDirection_Parent).As(&a);
    rootFrag.As(&b);
    EXPECT_TRUE(a.Get() == b.Get());
}

TEST(AutomationPeer, ScreenRectFromHostDipScalesAndOffsets) {
    const auto r = automation::ScreenRectFromHostDip({10, 20, 30, 40}, 100, 200, 144);
    EXPECT_NEAR(r.left, 115.0, 1e-9);
    EXPECT_NEAR(r.top, 230.0, 1e-9);
    EXPECT_NEAR(r.width, 45.0, 1e-9);
    EXPECT_NEAR(r.height, 60.0, 1e-9);
    const auto p = automation::HostDipFromScreen(115, 230, 100, 200, 144);
    EXPECT_NEAR(p.x, 10.0f, 1e-4f);
    EXPECT_NEAR(p.y, 20.0f, 1e-4f);
}

TEST(AutomationPeer, BoundingRectAndHitTestFollowViewboxScale) {
    // A 100x50 child in a 200x100 Viewbox renders at 2x; UIA must report the
    // painted rect, not the unscaled layout rect (the dirty-rect mapping, reused).
    UiaFixture fx;
    auto* vb = fx.root->Emplace<Viewbox>();
    vb->SetWidth(200);
    vb->SetHeight(100);
    auto* leaf = vb->SetChild(std::make_unique<Button>());
    leaf->SetWidth(100);
    leaf->SetHeight(50);
    fx.Attach();
    fx.root->Measure(400, 300);
    fx.root->Arrange({0, 0, 400, 300});
    const RectDip host = leaf->BoundsInHostDip();
    EXPECT_NEAR(host.w, 200.0f, 0.5f);
    EXPECT_NEAR(host.h, 100.0f, 0.5f);

    auto rootFrag = fx.Connect();
    auto btn = Nav(Nav(Nav(rootFrag.Get(), NavigateDirection_FirstChild).Get(),
                       NavigateDirection_FirstChild).Get(),
                   NavigateDirection_FirstChild);
    UiaRect r{};
    EXPECT_EQ(btn->get_BoundingRectangle(&r), S_OK);
    EXPECT_NEAR(r.width, 200.0, 0.5);  // 96 DPI, origin (0,0) on the dummy HWND

    // A point inside the scaled button but outside its unscaled rect still hits it.
    ComPtr<IRawElementProviderFragmentRoot> fr;
    rootFrag.As(&fr);
    ComPtr<IRawElementProviderFragment> hit;
    EXPECT_EQ(fr->ElementProviderFromPoint(host.x + 150, host.y + 75, &hit), S_OK);
    EXPECT_TRUE(hit.Get() == btn.Get());
}
