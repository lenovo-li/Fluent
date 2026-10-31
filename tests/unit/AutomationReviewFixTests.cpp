// AutomationReviewFixTests.cpp — UIA behaviours found missing by review, pinned.
//
// Each test names the failure it prevents: a UIA SetValue that bypassed the app's
// TextChanged and MaxLength; re-checks that kept posting flushes after the client
// left; a sibling walk that was quadratic; focus reported on a node its parent
// does not list; and an interface set that changed when a provider froze.
#include "../framework/Test.h"
#include "AutomationTestHelpers.h"

#include "../../FluentUI/automation/AutomationPeer.h"
#include "../../FluentUI/automation/AutomationTree.h"
#include "../../FluentUI/controls/Button.h"
#include "../../FluentUI/controls/TextBox.h"

using namespace fluent;
using namespace uiatest;

namespace {
ComPtr<IRawElementProviderFragment> FirstChildOfPanel(UiaFixture& fx) {
    return Nav(Nav(fx.Connect().Get(), NavigateDirection_FirstChild).Get(),
               NavigateDirection_FirstChild);
}
// An element parented to a container that does not list it (a control's internal
// part, like DataGrid's cell editor).
class UnlistedPart : public Button {};
struct CountingPanel : StackPanel {
    mutable int reads = 0;
    UIElement* LogicalChildAt(size_t i) const override {
        ++reads;
        return StackPanel::LogicalChildAt(i);
    }
};
class HostWithHiddenPart : public Button {
public:
    HostWithHiddenPart() { part.SetParent(this); }
    ~HostWithHiddenPart() override { part.SetParent(nullptr); }
    UnlistedPart part;
};
}  // namespace

TEST(AutomationReviewFix, SetValueGoesThroughTheUserEditPath) {
    UiaFixture fx;
    auto* tb = fx.root->Emplace<TextBox>();
    tb->SetMaxLength(3);
    std::wstring seen;
    auto sub = tb->TextChanged().Subscribe(&seen, [](void* o, TextEditBase&, std::wstring& t) {
        *static_cast<std::wstring*>(o) = t;
    });
    fx.Attach();
    ComPtr<IValueProvider> val;
    EXPECT_TRUE(SUCCEEDED(FirstChildOfPanel(fx).As(&val)));
    EXPECT_EQ(val->SetValue(L"abcdef"), S_OK);
    fx.host.OnFlush();
    EXPECT_TRUE(tb->Text() == L"abc");  // MaxLength applied
    EXPECT_TRUE(seen == L"abc");        // the app's TextChanged saw it
    EXPECT_TRUE(tb->CanUndo());         // one undoable edit, not a document reload
    val->SetValue(L"");
    fx.host.OnFlush();
    EXPECT_TRUE(tb->Text().empty());
    EXPECT_TRUE(seen.empty());
}

TEST(AutomationReviewFix, InvalidationWithoutListenerPostsNothing) {
    // Peers outlive the client that created them; once nobody listens, an
    // animating or blinking element must not post a flush per frame.
    UiaFixture fx;
    auto* btn = fx.root->Emplace<Button>();
    fx.Attach();
    FirstChildOfPanel(fx);  // a client created the peer...
    fx.core.listening = false;  // ...and left
    const int posts = fx.core.flushPosts;
    for (int i = 0; i < 10; ++i) btn->SetText(i % 2 ? L"a" : L"b");
    EXPECT_EQ(fx.core.flushPosts, posts);
}

TEST(AutomationReviewFix, FocusWithoutListenerCreatesNoPeer) {
    UiaFixture fx;
    auto* btn = fx.root->Emplace<Button>();
    fx.Attach();
    fx.Connect();
    fx.core.listening = false;
    const size_t before = automation::AutomationPeer::LiveCount();
    fx.host.OnFocusChanged(btn);  // btn has no peer yet
    EXPECT_EQ(automation::AutomationPeer::LiveCount(), before);
}

TEST(AutomationReviewFix, SiblingWalkIsLinear) {
    // Each NextSibling must start from the element's own index (seeded by the step
    // that returned it), not rescan the parent from 0: N steps, O(N) child reads.
    UiaFixture fx;
    auto* panel = fx.root->Emplace<CountingPanel>();
    constexpr int kN = 50;
    for (int i = 0; i < kN; ++i) panel->Emplace<Button>();
    fx.Attach();
    auto cur = Nav(FirstChildOfPanel(fx).Get(), NavigateDirection_FirstChild);
    panel->reads = 0;
    int steps = 1;
    while (auto next = Nav(cur.Get(), NavigateDirection_NextSibling)) {
        cur = next;
        ++steps;
    }
    EXPECT_EQ(steps, kN);
    EXPECT_TRUE(panel->reads < 4 * kN);  // quadratic would be ~kN*kN/2 = 1250
}

TEST(AutomationReviewFix, FocusOnAnUnlistedPartReportsItsListedAncestor) {
    UiaFixture fx;
    auto* host = fx.root->Emplace<HostWithHiddenPart>();
    fx.Attach();
    UIContext ctx;
    ctx.automation = &fx.host;
    host->part.AttachToContext(ctx);
    EXPECT_TRUE(automation::NearestListed(fx.host, &host->part) == host);
    fx.window.focused = &host->part;
    ComPtr<IRawElementProviderFragmentRoot> fr;
    fx.Connect().As(&fr);
    ComPtr<IRawElementProviderFragment> f;
    EXPECT_EQ(fr->GetFocus(&f), S_OK);
    EXPECT_EQ(PropInt(f.Get(), UIA_ControlTypePropertyId), UIA_ButtonControlTypeId);
    EXPECT_TRUE(host->AutomationPeerIfAny() != nullptr);
    EXPECT_TRUE(host->part.AutomationPeerIfAny() == nullptr);  // never exposed
    host->part.DetachFromContext();
}

TEST(AutomationReviewFix, InterfaceSetIsStableAcrossFreeze) {
    UiaFixture fx;
    fx.root->Emplace<Button>();
    fx.Attach();
    auto frag = FirstChildOfPanel(fx);
    ComPtr<IInvokeProvider> before, after;
    EXPECT_TRUE(SUCCEEDED(frag.As(&before)));
    fx.root->RemoveAt(0);  // freezes the provider
    EXPECT_TRUE(SUCCEEDED(frag.As(&after)));  // same interfaces as before
    if (after) EXPECT_EQ(after->Invoke(), UIA_E_ELEMENTNOTAVAILABLE);
}

TEST(AutomationReviewFix, DisconnectsInsideSendMessageDoNotRepostForever) {
    // A modal loop nested inside a sent message still dispatches posted messages;
    // a flush that cannot disconnect there must not post the next one.
    UiaFixture fx;
    fx.root->Emplace<Button>();
    fx.Attach();
    FirstChildOfPanel(fx);
    fx.root->RemoveAt(0);
    fx.core.inSend = true;
    const int posts = fx.core.flushPosts;
    fx.host.OnFlush();
    EXPECT_EQ(fx.core.flushPosts, posts);  // no self-repost
    EXPECT_EQ(fx.host.PendingDisconnects(), static_cast<size_t>(1));
    fx.core.inSend = false;
    fx.host.OnFlush();  // the next flush catches up
    EXPECT_EQ(fx.host.PendingDisconnects(), static_cast<size_t>(0));
}
