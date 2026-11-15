// AutomationExtensionPeerTests.cpp — UI Automation for the controls in extensions/:
// each library adds its peers from outside the core (CreateAutomationPeer on the
// public automation headers), the same path an app's own control library takes.
//
// What each test pins is what a reader (Narrator, an AI agent) would otherwise miss:
// the text a control DRAWS beyond its base class (BigButton's subtitle, a lunar
// date under a day number), the state a container IS IN (which wizard step), and
// controls that are not interactive but still mean something (a busy spinner, a
// modal overlay card).
#include "../framework/Test.h"
#include "AutomationTestHelpers.h"

#include "../../extensions/CustomControls/CustomControls.h"
#include "../../extensions/CustomControls/SpinnerControl.h"
#include "../../extensions/InstallerControls/WizardControl.h"
#include "../../extensions/NavigationControls/OverlayHost.h"
#include "../../extensions/NavigationControls/PageHost.h"
#include "../../FluentUI/controls/Button.h"
#include "../../FluentUI/controls/TextBlock.h"

using namespace fluent;
using namespace uiatest;

namespace {
ComPtr<IRawElementProviderFragment> First(UiaFixture& fx) {
    return Nav(Nav(fx.Connect().Get(), NavigateDirection_FirstChild).Get(),
               NavigateDirection_FirstChild);
}
void Layout(UiaFixture& fx) {
    fx.root->Measure(400, 300);
    fx.root->Arrange({0, 0, 400, 300});
}
std::vector<std::wstring> ChildNames(IRawElementProviderFragment* parent) {
    std::vector<std::wstring> out;
    for (auto c = Nav(parent, NavigateDirection_FirstChild); c && out.size() < 64;
         c = Nav(c.Get(), NavigateDirection_NextSibling))
        out.push_back(PropString(c.Get(), UIA_NamePropertyId));
    return out;
}
bool Contains(const std::vector<std::wstring>& v, const std::wstring& s) {
    for (const auto& x : v)
        if (x == s) return true;
    return false;
}
}  // namespace

TEST(AutomationExtension, BigButtonReportsItsSubtitleAsHelpText) {
    UiaFixture fx;
    auto* b = fx.root->Emplace<custom::BigButton>();
    b->SetText(L"Install");
    b->SetSubtitle(L"1.2 GB required");
    int clicks = 0;
    auto sub = b->Click().Subscribe(&clicks, [](void* o, Button&, RoutedEventArgs&) { ++*static_cast<int*>(o); });
    fx.Attach();
    auto f = First(fx);
    EXPECT_EQ(PropInt(f.Get(), UIA_ControlTypePropertyId), UIA_ButtonControlTypeId);
    EXPECT_TRUE(PropString(f.Get(), UIA_NamePropertyId) == L"Install");
    EXPECT_TRUE(PropString(f.Get(), UIA_HelpTextPropertyId) == L"1.2 GB required");
    ComPtr<IInvokeProvider> inv;
    if (f) f.As(&inv);
    if (!inv) return;
    inv->Invoke();  // still a Button: the click path
    fx.host.OnFlush();
    EXPECT_EQ(clicks, 1);
}


TEST(AutomationExtension, RunningSpinnerIsABusyProgressBarAndAStoppedOneLeavesTheControlView) {
    UiaFixture fx;
    auto* s = fx.root->Emplace<custom::SpinnerControl>();
    fx.Attach();
    Layout(fx);
    auto f = First(fx);
    EXPECT_EQ(PropInt(f.Get(), UIA_ControlTypePropertyId), UIA_ProgressBarControlTypeId);
    EXPECT_TRUE(PropString(f.Get(), UIA_NamePropertyId) == L"Busy");
    EXPECT_EQ(PropBool(f.Get(), UIA_IsControlElementPropertyId), 1);
    s->Stop();
    EXPECT_EQ(PropBool(f.Get(), UIA_IsControlElementPropertyId), 0);
}

TEST(AutomationExtension, WizardIsNamedAfterItsCurrentStepAndListsOnlyThatPage) {
    UiaFixture fx;
    auto* w = fx.root->Emplace<wizard::WizardControl>();
    auto* p0 = w->AddPage(L"Welcome", std::make_unique<TextBlock>());
    p0->SetText(L"hello page");
    auto* p1 = w->AddPage(L"Options", std::make_unique<TextBlock>());
    p1->SetText(L"options page");
    fx.Attach();
    Layout(fx);
    auto f = First(fx);
    EXPECT_EQ(PropInt(f.Get(), UIA_ControlTypePropertyId), UIA_GroupControlTypeId);
    EXPECT_TRUE(PropString(f.Get(), UIA_NamePropertyId) == L"Welcome, 1/2");
    auto names = ChildNames(f.Get());
    EXPECT_TRUE(Contains(names, L"hello page"));
    EXPECT_FALSE(Contains(names, L"options page"));  // hidden page: not in the tree
    EXPECT_TRUE(Contains(names, w->NextLabel()));     // the real Next button
    w->Next();
    EXPECT_TRUE(PropString(f.Get(), UIA_NamePropertyId) == L"Options, 2/2");
    names = ChildNames(f.Get());
    EXPECT_TRUE(Contains(names, L"options page"));
    EXPECT_FALSE(Contains(names, L"hello page"));
}

TEST(AutomationExtension, ShownOverlayIsADialogWindowAndAHiddenOneIsNotInTheTree) {
    UiaFixture fx;
    auto* o = fx.root->Emplace<OverlayHost>();
    fx.Attach();
    Layout(fx);
    auto root = fx.Connect();
    EXPECT_TRUE(Nav(Nav(root.Get(), NavigateDirection_FirstChild).Get(), NavigateDirection_FirstChild) == nullptr);
    OverlaySpec spec;
    spec.title = L"Firmware Update";
    spec.message = L"Do not unplug the device.";
    spec.primaryText = L"OK";
    o->Show(spec);
    Layout(fx);
    auto f = First(fx);
    EXPECT_EQ(PropInt(f.Get(), UIA_ControlTypePropertyId), UIA_WindowControlTypeId);
    EXPECT_EQ(PropBool(f.Get(), UIA_IsDialogPropertyId), 1);
    EXPECT_TRUE(PropString(f.Get(), UIA_NamePropertyId) == L"Firmware Update");
    EXPECT_TRUE(PropString(f.Get(), UIA_HelpTextPropertyId) == L"Do not unplug the device.");
    EXPECT_TRUE(Contains(ChildNames(f.Get()), L"OK"));  // the card's real button
}

TEST(AutomationExtension, PageHostListsOnlyTheCurrentPage) {
    UiaFixture fx;
    auto* h = fx.root->Emplace<PageHost>();
    auto a = std::make_unique<TextBlock>();
    a->SetText(L"page A");
    auto b = std::make_unique<TextBlock>();
    b->SetText(L"page B");
    h->AddPage(std::move(a));
    h->AddPage(std::move(b));
    h->SetTransition(PageTransition::None);
    h->ShowPage(0);
    fx.Attach();
    Layout(fx);
    auto rootFrag = fx.Connect();
    auto host = Nav(Nav(rootFrag.Get(), NavigateDirection_FirstChild).Get(), NavigateDirection_FirstChild);
    auto names = ChildNames(host.Get());
    EXPECT_TRUE(Contains(names, L"page A"));
    EXPECT_FALSE(Contains(names, L"page B"));
    h->ShowPage(1);
    names = ChildNames(host.Get());
    EXPECT_TRUE(Contains(names, L"page B"));
    EXPECT_FALSE(Contains(names, L"page A"));
}
