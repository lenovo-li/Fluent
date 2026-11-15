// AutomationControlPeerTests.cpp — the phase-2 control peers, through the real
// providers (UiaFixture in AutomationTestHelpers.h): RangeValue, Scroll,
// ExpandCollapse, Hyperlink Invoke/Value, and the labelled leaves.
#include "../framework/Test.h"
#include "AutomationTestHelpers.h"

#include "../../FluentUI/automation/AutomationPeer.h"
#include "../../FluentUI/controls/Expander.h"
#include "../../FluentUI/controls/Hyperlink.h"
#include "../../FluentUI/controls/Image.h"
#include "../../FluentUI/controls/ProgressBar.h"
#include "../../FluentUI/controls/Slider.h"
#include "../../FluentUI/controls/StatusBar.h"
#include "../../FluentUI/controls/TextBlock.h"
#include "../../FluentUI/layout/ScrollPanel.h"
#include "../../FluentUI/controls/Button.h"

using namespace fluent;
using namespace uiatest;

namespace {
ComPtr<IRawElementProviderFragment> First(UiaFixture& fx) {
    return Nav(Nav(fx.Connect().Get(), NavigateDirection_FirstChild).Get(),
               NavigateDirection_FirstChild);
}
}  // namespace

TEST(AutomationControlPeer, SliderRangeValueReadsAndSetsThroughThePostedPath) {
    UiaFixture fx;
    auto* s = fx.root->Emplace<Slider>();
    s->SetMinimum(0);
    s->SetMaximum(50);
    s->SetStep(5);
    s->SetValue(10);
    fx.Attach();
    auto f = First(fx);
    EXPECT_EQ(PropInt(f.Get(), UIA_ControlTypePropertyId), UIA_SliderControlTypeId);
    ComPtr<IRangeValueProvider> rv;
    EXPECT_TRUE(SUCCEEDED(f.As(&rv)));
    double v = 0, mn = 0, mx = 0, smallStep = 0;
    rv->get_Value(&v);
    rv->get_Minimum(&mn);
    rv->get_Maximum(&mx);
    rv->get_SmallChange(&smallStep);
    EXPECT_NEAR(v, 10.0, 1e-6);
    EXPECT_NEAR(mn, 0.0, 1e-6);
    EXPECT_NEAR(mx, 50.0, 1e-6);
    EXPECT_NEAR(smallStep, 5.0, 1e-6);
    EXPECT_EQ(rv->SetValue(99), E_INVALIDARG);  // out of range: refused, not clamped
    EXPECT_EQ(rv->SetValue(30), S_OK);
    EXPECT_NEAR(s->Value(), 10.0f, 1e-6f);  // posted, not yet run
    fx.host.OnFlush();
    EXPECT_NEAR(s->Value(), 30.0f, 1e-6f);
    fx.host.OnFlush();  // the re-check the change queued
    bool raised = false;
    for (const auto& p : fx.core.props) raised |= p.first == UIA_RangeValueValuePropertyId;
    EXPECT_TRUE(raised);
}

TEST(AutomationControlPeer, ProgressBarIsReadOnly) {
    UiaFixture fx;
    auto* pb = fx.root->Emplace<ProgressBar>();
    fx.Attach();
    auto pf = First(fx);
    EXPECT_EQ(PropInt(pf.Get(), UIA_ControlTypePropertyId), UIA_ProgressBarControlTypeId);
    ComPtr<IRangeValueProvider> prv;
    pf.As(&prv);
    BOOL ro = FALSE;
    EXPECT_TRUE(prv && SUCCEEDED(prv->get_IsReadOnly(&ro)) && ro == TRUE);
    EXPECT_EQ(prv ? prv->SetValue(50) : S_OK, UIA_E_ELEMENTNOTENABLED);
    (void)pb;
}

TEST(AutomationControlPeer, ExpanderExpandCollapseAndChildrenFollowState) {
    UiaFixture fx;
    auto* ex = fx.root->Emplace<Expander>();
    ex->SetHeader(L"Advanced");
    ex->SetContent(std::make_unique<TextBlock>())->SetText(L"inside");
    fx.Attach();
    auto f = First(fx);
    EXPECT_TRUE(PropString(f.Get(), UIA_NamePropertyId) == L"Advanced");
    ComPtr<IExpandCollapseProvider> ec;
    EXPECT_TRUE(SUCCEEDED(f.As(&ec)));
    ExpandCollapseState st{};
    ec->get_ExpandCollapseState(&st);
    EXPECT_EQ(st, ExpandCollapseState_Collapsed);
    EXPECT_TRUE(Nav(f.Get(), NavigateDirection_FirstChild) == nullptr);  // collapsed: no content
    EXPECT_EQ(ec->Expand(), S_OK);
    fx.host.OnFlush();
    EXPECT_TRUE(ex->IsExpanded());
    auto child = Nav(f.Get(), NavigateDirection_FirstChild);
    EXPECT_TRUE(PropString(child.Get(), UIA_NamePropertyId) == L"inside");
}

TEST(AutomationControlPeer, HyperlinkInvokeRaisesClickAndValueIsTheUri) {
    UiaFixture fx;
    auto* h = fx.root->Emplace<Hyperlink>();
    h->SetText(L"Docs");
    h->SetUri(L"https://example.invalid/docs");
    int clicks = 0;
    auto sub = h->Click().Subscribe(&clicks, [](void* o, Hyperlink&, RoutedEventArgs&) {
        ++*static_cast<int*>(o);
    });
    fx.Attach();
    auto f = First(fx);
    EXPECT_EQ(PropInt(f.Get(), UIA_ControlTypePropertyId), UIA_HyperlinkControlTypeId);
    EXPECT_TRUE(PropString(f.Get(), UIA_NamePropertyId) == L"Docs");
    ComPtr<IValueProvider> val;
    f.As(&val);
    BSTR uri = nullptr;
    EXPECT_TRUE(val && SUCCEEDED(val->get_Value(&uri)) && uri && wcscmp(uri, L"https://example.invalid/docs") == 0);
    SysFreeString(uri);
    ComPtr<IInvokeProvider> inv;
    f.As(&inv);
    EXPECT_EQ(inv ? inv->Invoke() : E_FAIL, S_OK);
    fx.host.OnFlush();
    // Default policy (Ctrl+Click opens): a plain activation raises Click, no shell.
    EXPECT_EQ(clicks, 1);
    EXPECT_FALSE(h->LastActivationOpenedUri());
}

TEST(AutomationControlPeer, DecorativeImageIsRawOnlyNamedImageIsControl) {
    UiaFixture fx;
    fx.root->Emplace<Image>();
    fx.root->Emplace<Image>()->SetAutomationName(L"Logo");
    fx.Attach();
    auto deco = First(fx);
    auto named = Nav(deco.Get(), NavigateDirection_NextSibling);
    EXPECT_EQ(PropBool(deco.Get(), UIA_IsControlElementPropertyId), 0);
    EXPECT_EQ(PropBool(named.Get(), UIA_IsControlElementPropertyId), 1);
    EXPECT_TRUE(PropString(named.Get(), UIA_NamePropertyId) == L"Logo");
    EXPECT_EQ(PropInt(named.Get(), UIA_ControlTypePropertyId), UIA_ImageControlTypeId);
}

TEST(AutomationControlPeer, ScrollPanelIsAPaneWithScrollPattern) {
    UiaFixture fx;
    auto* sp = fx.root->Emplace<ScrollPanel>();
    sp->SetHeight(100);
    for (int i = 0; i < 20; ++i) sp->Emplace<Button>()->SetHeight(40);  // 800 tall in 100
    fx.Attach();
    fx.root->Measure(400, 300);
    fx.root->Arrange({0, 0, 400, 300});
    auto f = First(fx);
    // Was "Custom" with no name before it had a peer (it is focusable).
    EXPECT_EQ(PropInt(f.Get(), UIA_ControlTypePropertyId), UIA_PaneControlTypeId);
    ComPtr<IScrollProvider> sc;
    EXPECT_TRUE(SUCCEEDED(f.As(&sc)));
    BOOL vs = FALSE;
    sc->get_VerticallyScrollable(&vs);
    EXPECT_TRUE(vs == TRUE);
    EXPECT_TRUE(sp->MaxVerticalOffset() > 0);
    double pct = -2, view = 0;
    sc->get_VerticalScrollPercent(&pct);
    sc->get_VerticalViewSize(&view);
    EXPECT_NEAR(pct, 0.0, 1e-6);
    EXPECT_TRUE(view > 0 && view < 100);
    EXPECT_EQ(sc->SetScrollPercent(UIA_ScrollPatternNoScroll, 50), S_OK);
    EXPECT_EQ(sc->SetScrollPercent(UIA_ScrollPatternNoScroll, 150), E_INVALIDARG);
    fx.host.OnFlush();
    EXPECT_NEAR(sp->TargetVerticalOffset(), sp->MaxVerticalOffset() * 0.5f, 0.5f);
}
