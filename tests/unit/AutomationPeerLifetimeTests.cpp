// AutomationPeerLifetimeTests.cpp — peers across detach/destroy, posted actions,
// and events. Uses the fixture in AutomationTestHelpers.h.
//
// The two hazards these pin down: a provider UIA still holds after its element is
// gone must never reach freed memory (it is frozen, and frozen answers only what
// a disconnect needs), and app code run by a UIA action must never run under UIA
// core's stack (actions are posted, and re-checked when they run).
#include "../framework/Test.h"
#include "AutomationTestHelpers.h"

#include "../../FluentUI/automation/AutomationPeer.h"
#include "../../FluentUI/controls/Button.h"
#include "../../FluentUI/controls/CheckBox.h"

using namespace fluent;
using namespace uiatest;

namespace {
ComPtr<IRawElementProviderFragment> FirstButton(UiaFixture& fx) {
    return Nav(Nav(fx.Connect().Get(), NavigateDirection_FirstChild).Get(),
               NavigateDirection_FirstChild);
}
}  // namespace

TEST(AutomationPeerLifetime, RemovedElementIsFrozenAndDisconnectedLater) {
    UiaFixture fx;
    fx.root->Emplace<Button>()->SetText(L"Gone");
    fx.Attach();
    auto btn = FirstButton(fx);
    EXPECT_TRUE(PropString(btn.Get(), UIA_NamePropertyId) == L"Gone");

    fx.root->RemoveAt(0);  // destroys the button
    // Frozen at once, but not disconnected inside the tree mutation.
    EXPECT_TRUE(PropString(btn.Get(), UIA_NamePropertyId) == L"<error>");
    EXPECT_EQ(fx.core.disconnects, 0);
    EXPECT_EQ(fx.host.PendingDisconnects(), static_cast<size_t>(1));
    // What UIA core asks while disconnecting still answers.
    SAFEARRAY* id = nullptr;
    EXPECT_EQ(btn->GetRuntimeId(&id), S_OK);
    if (id) SafeArrayDestroy(id);
    ComPtr<IRawElementProviderFragmentRoot> fr;
    EXPECT_EQ(btn->get_FragmentRoot(&fr), S_OK);
    EXPECT_TRUE(fr != nullptr);

    fx.host.OnFlush();  // the posted flush
    EXPECT_EQ(fx.core.disconnects, 1);
    EXPECT_EQ(fx.host.PendingDisconnects(), static_cast<size_t>(0));
}

TEST(AutomationPeerLifetime, RuntimeIdIsNotReusedWithTheAddress) {
    UiaFixture fx;
    fx.root->Emplace<Button>();
    fx.Attach();
    auto a = FirstButton(fx);
    SAFEARRAY* ida = nullptr;
    a->GetRuntimeId(&ida);
    fx.root->RemoveAt(0);
    fx.root->Emplace<Button>();  // the allocator may hand back the same address
    auto b = FirstButton(fx);
    SAFEARRAY* idb = nullptr;
    b->GetRuntimeId(&idb);
    LONG i = 1;
    int va = 0, vb = 0;
    SafeArrayGetElement(ida, &i, &va);
    SafeArrayGetElement(idb, &i, &vb);
    EXPECT_NE(va, vb);
    SafeArrayDestroy(ida);
    SafeArrayDestroy(idb);
}

TEST(AutomationPeerLifetime, InvokeIsPostedAndSurvivesHandlerDeletingTheButton) {
    UiaFixture fx;
    auto* btn = fx.root->Emplace<Button>();
    int clicks = 0;
    struct Ctx { UiaFixture* fx; int* clicks; } ctx{&fx, &clicks};
    auto sub = btn->Click().Subscribe(&ctx, [](void* o, Button&, RoutedEventArgs&) {
        auto* c = static_cast<Ctx*>(o);
        ++*c->clicks;
        c->fx->root->RemoveAt(0);  // the handler destroys its own button
    });
    fx.Attach();
    ComPtr<IInvokeProvider> inv;
    EXPECT_TRUE(SUCCEEDED(FirstButton(fx).As(&inv)));

    EXPECT_EQ(inv->Invoke(), S_OK);
    EXPECT_EQ(clicks, 0);  // not run inside the COM call
    fx.host.OnFlush();
    EXPECT_EQ(clicks, 1);
    // The button died in its own handler: no Invoked event for a dead element
    // (the parent's ChildrenInvalidated for the removal is expected), and its
    // provider is now frozen.
    for (int e : fx.core.events) EXPECT_NE(e, UIA_Invoke_InvokedEventId);
    EXPECT_EQ(inv->Invoke(), UIA_E_ELEMENTNOTAVAILABLE);
}

TEST(AutomationPeerLifetime, InvokeRaisesInvokedForASurvivingButton) {
    UiaFixture fx;
    fx.root->Emplace<Button>();
    fx.Attach();
    ComPtr<IInvokeProvider> inv;
    FirstButton(fx).As(&inv);
    inv->Invoke();
    fx.host.OnFlush();  // runs the action, which queues Invoked
    fx.host.OnFlush();  // the flush that action posted
    EXPECT_EQ(fx.core.events.size(), static_cast<size_t>(1));
    EXPECT_TRUE(!fx.core.events.empty() && fx.core.events[0] == UIA_Invoke_InvokedEventId);
}

TEST(AutomationPeerLifetime, ToggleChangeRaisesOnePropertyEvent) {
    UiaFixture fx;
    auto* cb = fx.root->Emplace<CheckBox>();
    fx.Attach();
    auto frag = FirstButton(fx);  // creates the peer; baseline = Off
    ComPtr<IToggleProvider> tog;
    EXPECT_TRUE(SUCCEEDED(frag.As(&tog)));
    // Three changes in one batch: coalesced into one re-check, which reports the
    // net change (Off -> On -> Off -> On = On).
    cb->SetChecked(true);
    cb->SetChecked(false);
    cb->SetChecked(true);
    fx.host.OnFlush();
    size_t toggles = 0;
    for (const auto& p : fx.core.props)
        if (p.first == UIA_ToggleToggleStatePropertyId) {
            ++toggles;
            EXPECT_TRUE(p.second == L"1");
        }
    EXPECT_EQ(toggles, static_cast<size_t>(1));
    ToggleState st{};
    tog->get_ToggleState(&st);
    EXPECT_EQ(st, ToggleState_On);
}

TEST(AutomationPeerLifetime, NothingRaisedWhenNoClientListens) {
    UiaFixture fx;
    auto* cb = fx.root->Emplace<CheckBox>();
    fx.Attach();
    auto frag = FirstButton(fx);
    fx.core.listening = false;
    cb->SetChecked(true);
    fx.host.OnFocusChanged(cb);
    fx.host.OnFlush();
    EXPECT_TRUE(fx.core.props.empty());
    EXPECT_TRUE(fx.core.events.empty());
}

TEST(AutomationPeerLifetime, FocusChangesAreCoalesced) {
    UiaFixture fx;
    auto* a = fx.root->Emplace<Button>();
    auto* b = fx.root->Emplace<Button>();
    fx.Attach();
    fx.Connect();
    fx.host.OnFocusChanged(a);
    fx.host.OnFocusChanged(b);
    fx.host.OnFocusChanged(a);
    EXPECT_EQ(fx.core.flushPosts, 1);  // one posted flush for the batch
    fx.host.OnFlush();
    size_t focus = 0;
    for (int e : fx.core.events) focus += e == UIA_AutomationFocusChangedEventId;
    EXPECT_EQ(focus, static_cast<size_t>(1));
}

TEST(AutomationPeerLifetime, DestroyedWhileAttachedStillQueuesDisconnect) {
    // The ~UIElement path (an element deleted while attached: MenuFlyout reaping a
    // level, an app deleting a root) never runs DetachFromContext, so the release
    // in the destructor is the only thing that hands UIA the provider to disconnect.
    UiaFixture fx;
    auto standalone = std::make_unique<Button>();
    fx.window.roots.push_back(standalone.get());
    fx.Attach();
    UIContext ctx;
    ctx.automation = &fx.host;
    standalone->AttachToContext(ctx);
    auto rootFrag = fx.Connect();
    auto btn = Nav(rootFrag.Get(), NavigateDirection_LastChild);
    EXPECT_EQ(PropInt(btn.Get(), UIA_ControlTypePropertyId), UIA_ButtonControlTypeId);
    fx.window.roots.pop_back();
    standalone.reset();  // still attached
    EXPECT_EQ(fx.host.PendingDisconnects(), static_cast<size_t>(1));
    EXPECT_TRUE(PropString(btn.Get(), UIA_NamePropertyId) == L"<error>");
}

TEST(AutomationPeerLifetime, EachLaterChangeIsReportedAgainstTheLastRaise) {
    UiaFixture fx;
    auto* cb = fx.root->Emplace<CheckBox>();
    fx.Attach();
    auto frag = FirstButton(fx);
    cb->SetChecked(true);
    fx.host.OnFlush();
    cb->SetChecked(false);  // back to the creation-time value: still a change
    fx.host.OnFlush();
    fx.host.OnFlush();      // nothing changed since: nothing raised
    std::vector<std::wstring> toggles;
    for (const auto& p : fx.core.props)
        if (p.first == UIA_ToggleToggleStatePropertyId) toggles.push_back(p.second);
    const std::vector<std::wstring> expected{L"1", L"0"};
    EXPECT_TRUE(toggles == expected);
}
