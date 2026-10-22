// WizardControlTests.cpp — headless tests for extensions/InstallerControls WizardControl:
// page management, navigation rules, validation/veto, lock, events, labels, the
// step indicator, the fade transition, layout geometry, and focus handling.

#include "../framework/Test.h"
#include "../../extensions/InstallerControls/WizardControl.h"
#include "../../FluentUI/layout/StackPanel.h"
#include "../../FluentUI/layout/Border.h"
#include "../../FluentUI/input/FocusManager.h"
#include "../../FluentUI/core/UIContext.h"
#include <cmath>
#include <limits>
#include <memory>

using namespace fluent;
using namespace fluent::wizard;

namespace {

struct Counts {
    int pageChanged = 0;
    int lastPage = -1;
    int finishing = 0;
    int cancelled = 0;
    int navigating = 0;
    int lastFrom = -2;
    int lastTo = -2;
    bool veto = false;
};

void OnPageChanged(void* o, WizardControl&, int& idx) {
    auto* c = static_cast<Counts*>(o);
    ++c->pageChanged;
    c->lastPage = idx;
}
void OnFinishing(void* o, WizardControl&, RoutedEventArgs&) { ++static_cast<Counts*>(o)->finishing; }
void OnCancelled(void* o, WizardControl&, RoutedEventArgs&) { ++static_cast<Counts*>(o)->cancelled; }
void OnNavigating(void* o, WizardControl&, WizardNavigatingArgs& a) {
    auto* c = static_cast<Counts*>(o);
    ++c->navigating;
    c->lastFrom = a.from;
    c->lastTo = a.to;
    if (c->veto) a.cancel = true;
}

// Three plain pages; fade off so state assertions are not animation-dependent.
struct Wiz3 {
    WizardControl w;
    StackPanel* p[3]{};
    Wiz3() {
        w.SetTransitionStyle(WizardControl::TransitionStyle::None);
        p[0] = w.AddPage(L"Welcome", std::make_unique<StackPanel>());
        p[1] = w.AddPage(L"Path", std::make_unique<StackPanel>());
        p[2] = w.AddPage(L"Done", std::make_unique<StackPanel>());
    }
};

class ValidatingWizard : public WizardControl {
public:
    bool ok = false;
    int validateCalls = 0;
    int hookOld = -2, hookNew = -2;
protected:
    bool ValidateCurrentPage() override { ++validateCalls; return ok; }
    void OnPageChanged(int o, int n) override { hookOld = o; hookNew = n; }
};

// Press a button the way the keyboard does (Space -> ButtonBase::OnActivate ->
// Click). Proves the wizard's Click subscriptions are live, not discarded.
void Press(Button* b) {
    KeyEventArgs e;
    e.vk = VK_SPACE;
    b->OnKeyDownRouted(e);
}

// A focusable, hit-testable leaf to put inside a page. FrameworkElement, not
// UIElement: Panel children are FrameworkElements.
struct FocusLeaf : FrameworkElement {
    FocusLeaf() {
        SetFocusable(true);
        SetWidth(10.0f);
        SetHeight(10.0f);
        SetHAlign(HAlign::Left);
        SetVAlign(VAlign::Top);
    }
    void Render(const DrawingContext&) override {}
};

// Attach `w` to a context whose only service is a FocusManager rooted at `w`.
struct FocusRig {
    FocusManager fm;
    std::vector<UIElement*> roots;
    explicit FocusRig(WizardControl& w) : roots{&w} {
        fm.SetRoots(&roots);
        UIContext ctx;
        ctx.focus = &fm;
        ctx.dpiScale = 1.0f;
        w.AttachToContext(ctx);
    }
};

}  // namespace

// --- page management ---------------------------------------------------------

TEST(WizardControl, EmptyWizardHasNoPage) {
    WizardControl w;
    EXPECT_EQ(w.PageCount(), 0);
    EXPECT_EQ(w.CurrentPage(), -1);
    EXPECT_FALSE(w.CanGoNext());
    EXPECT_FALSE(w.CanGoPrevious());
    EXPECT_FALSE(w.IsLastPage());
    EXPECT_TRUE(w.PageContent(0) == nullptr);
    EXPECT_TRUE(w.PageTitle(0).empty());
}

TEST(WizardControl, FirstPageBecomesCurrentAndOnlyItIsVisible) {
    Wiz3 z;
    EXPECT_EQ(z.w.PageCount(), 3);
    EXPECT_EQ(z.w.CurrentPage(), 0);
    EXPECT_TRUE(z.p[0]->IsVisible());
    EXPECT_FALSE(z.p[1]->IsVisible());
    EXPECT_FALSE(z.p[2]->IsVisible());
    EXPECT_TRUE(z.w.PageTitle(1) == L"Path");
    EXPECT_TRUE(z.w.PageContent(2) == z.p[2]);
}

TEST(WizardControl, NullContentIsIgnored) {
    WizardControl w;
    EXPECT_TRUE(w.AddPage<StackPanel>(L"x", nullptr) == nullptr);
    EXPECT_EQ(w.PageCount(), 0);
    EXPECT_EQ(w.CurrentPage(), -1);
}

TEST(WizardControl, OnlyFirstAddRaisesPageChanged) {
    WizardControl w;
    Counts c;
    auto s = w.PageChanged().Subscribe(&c, OnPageChanged);
    w.AddPage(L"a", std::make_unique<StackPanel>());
    w.AddPage(L"b", std::make_unique<StackPanel>());
    EXPECT_EQ(c.pageChanged, 1);
    EXPECT_EQ(c.lastPage, 0);
}

// --- navigation --------------------------------------------------------------

TEST(WizardControl, NextPreviousAndVisibilityFollow) {
    Wiz3 z;
    z.w.Next();
    EXPECT_EQ(z.w.CurrentPage(), 1);
    EXPECT_FALSE(z.p[0]->IsVisible());
    EXPECT_TRUE(z.p[1]->IsVisible());
    z.w.Previous();
    EXPECT_EQ(z.w.CurrentPage(), 0);
    EXPECT_TRUE(z.p[0]->IsVisible());
    EXPECT_FALSE(z.p[1]->IsVisible());
}

TEST(WizardControl, NextStopsAtLastPreviousStopsAtFirst) {
    Wiz3 z;
    z.w.Previous();
    EXPECT_EQ(z.w.CurrentPage(), 0);
    z.w.Next(); z.w.Next(); z.w.Next(); z.w.Next();
    EXPECT_EQ(z.w.CurrentPage(), 2);
    EXPECT_TRUE(z.w.IsLastPage());
    EXPECT_FALSE(z.w.CanGoNext());
}

TEST(WizardControl, GoToPageJumpsAndIgnoresOutOfRange) {
    Wiz3 z;
    z.w.GoToPage(2);
    EXPECT_EQ(z.w.CurrentPage(), 2);
    EXPECT_TRUE(z.p[2]->IsVisible());
    z.w.GoToPage(3);
    z.w.GoToPage(-1);
    EXPECT_EQ(z.w.CurrentPage(), 2);
}

TEST(WizardControl, PageChangedCarriesNewIndexOncePerMove) {
    Wiz3 z;
    Counts c;
    auto s = z.w.PageChanged().Subscribe(&c, OnPageChanged);
    z.w.Next();
    EXPECT_EQ(c.pageChanged, 1);
    EXPECT_EQ(c.lastPage, 1);
    z.w.GoToPage(1);  // already there: no event
    EXPECT_EQ(c.pageChanged, 1);
    z.w.GoToPage(0);
    EXPECT_EQ(c.pageChanged, 2);
    EXPECT_EQ(c.lastPage, 0);
}

TEST(WizardControl, OnPageChangedHookGetsOldAndNew) {
    ValidatingWizard w;
    w.ok = true;
    w.AddPage(L"a", std::make_unique<StackPanel>());
    w.AddPage(L"b", std::make_unique<StackPanel>());
    EXPECT_EQ(w.hookOld, -1);
    EXPECT_EQ(w.hookNew, 0);
    w.Next();
    EXPECT_EQ(w.hookOld, 0);
    EXPECT_EQ(w.hookNew, 1);
}

// --- validation and veto -----------------------------------------------------

TEST(WizardControl, ValidateFalseBlocksNextButNotBackOrJump) {
    ValidatingWizard w;
    w.AddPage(L"a", std::make_unique<StackPanel>());
    w.AddPage(L"b", std::make_unique<StackPanel>());
    w.AddPage(L"c", std::make_unique<StackPanel>());
    w.Next();
    EXPECT_EQ(w.CurrentPage(), 0);
    EXPECT_EQ(w.validateCalls, 1);
    w.ok = true;
    w.Next();
    EXPECT_EQ(w.CurrentPage(), 1);
    w.ok = false;
    const int calls = w.validateCalls;
    w.Previous();  // going back never validates
    EXPECT_EQ(w.CurrentPage(), 0);
    w.GoToPage(2);  // programmatic jump never validates
    EXPECT_EQ(w.CurrentPage(), 2);
    EXPECT_EQ(w.validateCalls, calls);
}

TEST(WizardControl, NavigatingReportsFromToAndCanVeto) {
    Wiz3 z;
    Counts c;
    auto s = z.w.Navigating().Subscribe(&c, OnNavigating);
    c.veto = true;
    z.w.Next();
    EXPECT_EQ(c.navigating, 1);
    EXPECT_EQ(c.lastFrom, 0);
    EXPECT_EQ(c.lastTo, 1);
    EXPECT_EQ(z.w.CurrentPage(), 0);
    c.veto = false;
    z.w.Next();
    EXPECT_EQ(z.w.CurrentPage(), 1);
}

TEST(WizardControl, NavigatingVetoSkipsValidateHook) {
    ValidatingWizard w;
    w.ok = true;
    w.AddPage(L"a", std::make_unique<StackPanel>());
    w.AddPage(L"b", std::make_unique<StackPanel>());
    Counts c;
    c.veto = true;
    auto s = w.Navigating().Subscribe(&c, OnNavigating);
    w.Next();
    EXPECT_EQ(w.validateCalls, 0);
    EXPECT_EQ(w.CurrentPage(), 0);
}

// --- finish / cancel ---------------------------------------------------------

TEST(WizardControl, NextButtonReadsFinishOnlyOnLastPage) {
    Wiz3 z;
    EXPECT_TRUE(z.w.NextButton()->Text() == L"Next");
    z.w.GoToPage(2);
    EXPECT_TRUE(z.w.NextButton()->Text() == L"Finish");
    z.w.Previous();
    EXPECT_TRUE(z.w.NextButton()->Text() == L"Next");
}

TEST(WizardControl, AddingAPageAfterTheLastRestoresNextLabel) {
    WizardControl w;
    w.AddPage(L"only", std::make_unique<StackPanel>());
    EXPECT_TRUE(w.NextButton()->Text() == L"Finish");
    w.AddPage(L"more", std::make_unique<StackPanel>());
    EXPECT_TRUE(w.NextButton()->Text() == L"Next");
    EXPECT_TRUE(w.CanGoNext());
}

TEST(WizardControl, AdvanceOnLastPageRaisesFinishingNotPageChanged) {
    Wiz3 z;
    z.w.GoToPage(2);
    Counts c;
    auto s1 = z.w.Finishing().Subscribe(&c, OnFinishing);
    auto s2 = z.w.PageChanged().Subscribe(&c, OnPageChanged);
    auto s3 = z.w.Navigating().Subscribe(&c, OnNavigating);
    z.w.Advance();
    EXPECT_EQ(c.finishing, 1);
    EXPECT_EQ(c.pageChanged, 0);
    EXPECT_EQ(c.lastTo, WizardNavigatingArgs::kFinish);
    EXPECT_EQ(z.w.CurrentPage(), 2);
}

TEST(WizardControl, FinishIsVetoedByValidation) {
    ValidatingWizard w;
    w.AddPage(L"only", std::make_unique<StackPanel>());
    Counts c;
    auto s = w.Finishing().Subscribe(&c, OnFinishing);
    w.Advance();
    EXPECT_EQ(c.finishing, 0);
    w.ok = true;
    w.Advance();
    EXPECT_EQ(c.finishing, 1);
}

TEST(WizardControl, ButtonsAreWiredToNavigation) {
    Wiz3 z;
    Counts c;
    auto s1 = z.w.Finishing().Subscribe(&c, OnFinishing);
    auto s2 = z.w.Cancelled().Subscribe(&c, OnCancelled);
    Press(z.w.NextButton());
    EXPECT_EQ(z.w.CurrentPage(), 1);
    Press(z.w.BackButton());
    EXPECT_EQ(z.w.CurrentPage(), 0);
    Press(z.w.CancelButton());
    EXPECT_EQ(c.cancelled, 1);
    z.w.GoToPage(2);
    Press(z.w.NextButton());
    EXPECT_EQ(c.finishing, 1);
}

// --- button state and lock ---------------------------------------------------

TEST(WizardControl, BackDisabledOnFirstPage) {
    Wiz3 z;
    EXPECT_TRUE(z.w.BackButton()->State() == VisualState::Disabled);
    EXPECT_TRUE(z.w.NextButton()->State() != VisualState::Disabled);
    z.w.Next();
    EXPECT_TRUE(z.w.BackButton()->State() != VisualState::Disabled);
}

TEST(WizardControl, LockDisablesButtonsAndUserNavigationOnly) {
    Wiz3 z;
    z.w.Next();
    Counts c;
    auto s = z.w.Cancelled().Subscribe(&c, OnCancelled);
    z.w.SetNavigationLocked(true);
    EXPECT_TRUE(z.w.NavigationLocked());
    for (Button* b : {z.w.BackButton(), z.w.NextButton(), z.w.CancelButton()})
        EXPECT_TRUE(b->State() == VisualState::Disabled);
    z.w.Next();
    z.w.Previous();
    z.w.Cancel();
    EXPECT_EQ(z.w.CurrentPage(), 1);
    EXPECT_EQ(c.cancelled, 0);
    z.w.GoToPage(2);  // programmatic path still works while locked
    EXPECT_EQ(z.w.CurrentPage(), 2);
    for (Button* b : {z.w.BackButton(), z.w.NextButton(), z.w.CancelButton()})
        EXPECT_TRUE(b->State() == VisualState::Disabled);  // page change keeps the lock
    z.w.SetNavigationLocked(false);
    EXPECT_TRUE(z.w.NextButton()->State() != VisualState::Disabled);
    z.w.Cancel();
    EXPECT_EQ(c.cancelled, 1);
}

TEST(WizardControl, CustomLabelsApplyToButtons) {
    Wiz3 z;
    z.w.SetButtonLabels(L"上一步", L"下一步", L"取消", L"完成");
    EXPECT_TRUE(z.w.BackButton()->Text() == L"上一步");
    EXPECT_TRUE(z.w.NextButton()->Text() == L"下一步");
    EXPECT_TRUE(z.w.CancelButton()->Text() == L"取消");
    z.w.GoToPage(2);
    EXPECT_TRUE(z.w.NextButton()->Text() == L"完成");
}

// --- step indicator ----------------------------------------------------------

TEST(WizardControl, StepIndicatorTextTracksPage) {
    Wiz3 z;
    EXPECT_TRUE(z.w.StepIndicatorText().empty());  // hidden by default
    z.w.SetShowStepIndicator(true);
    EXPECT_TRUE(z.w.StepIndicatorText() == L"Step 1 of 3: Welcome");
    z.w.Next();
    EXPECT_TRUE(z.w.StepIndicatorText() == L"Step 2 of 3: Path");
    z.w.SetStepIndicatorFormat(L"第 {current}/{total} 步");
    EXPECT_TRUE(z.w.StepIndicatorText() == L"第 2/3 步");
    z.w.SetShowStepIndicator(false);
    EXPECT_TRUE(z.w.StepIndicatorText().empty());
}

TEST(WizardControl, StepIndicatorTitleIsNotReexpanded) {
    WizardControl w;
    w.AddPage(L"{current}%s%d", std::make_unique<StackPanel>());
    w.SetShowStepIndicator(true);
    EXPECT_TRUE(w.StepIndicatorText() == L"Step 1 of 1: {current}%s%d");
}

// --- fade --------------------------------------------------------------------

TEST(WizardControl, FirstPageNeverFadesIn) {
    WizardControl w;
    w.SetRespectSystemAnimations(false);
    w.AddPage(L"a", std::make_unique<StackPanel>());
    EXPECT_EQ(w.PageOpacity(), 1.0f);
    EXPECT_FALSE(w.WantsAnimationTick());
}

TEST(WizardControl, PageSwitchFadesInAndSettles) {
    WizardControl w;
    w.SetRespectSystemAnimations(false);
    w.AddPage(L"a", std::make_unique<StackPanel>());
    w.AddPage(L"b", std::make_unique<StackPanel>());
    w.Next();
    EXPECT_EQ(w.PageOpacity(), 0.0f);
    EXPECT_TRUE(w.WantsAnimationTick());
    // Theme fadeTau is 0.05 s; one second of ticks is 20 tau, far past settling.
    for (int i = 0; i < 60; ++i) w.OnAnimationTick(1.0f / 60.0f);
    EXPECT_EQ(w.PageOpacity(), 1.0f);
    EXPECT_FALSE(w.WantsAnimationTick());  // idle costs zero once settled
}

TEST(WizardControl, TransitionNoneSwitchesInstantly) {
    Wiz3 z;
    z.w.Next();
    EXPECT_EQ(z.w.PageOpacity(), 1.0f);
    EXPECT_FALSE(z.w.WantsAnimationTick());
}

TEST(WizardControl, SwitchingToNoneMidFadeSnapsOpaque) {
    WizardControl w;
    w.SetRespectSystemAnimations(false);
    w.AddPage(L"a", std::make_unique<StackPanel>());
    w.AddPage(L"b", std::make_unique<StackPanel>());
    w.Next();
    w.SetTransitionStyle(WizardControl::TransitionStyle::None);
    EXPECT_EQ(w.PageOpacity(), 1.0f);
    EXPECT_FALSE(w.WantsAnimationTick());
}

// --- layout ------------------------------------------------------------------

TEST(WizardControl, ArrangeGivesPageEverythingAboveTheBar) {
    Wiz3 z;
    z.w.Measure(600, 400);
    z.w.Arrange({0, 0, 600, 400});
    const RectDip pb = z.p[0]->Bounds();
    EXPECT_EQ(pb.y, 0.0f);
    EXPECT_EQ(pb.w, 600.0f);
    EXPECT_EQ(pb.h, 400.0f - WizardControl::kButtonBarHeight);
}

TEST(WizardControl, StepIndicatorPushesPageDown) {
    Wiz3 z;
    z.w.SetShowStepIndicator(true);
    z.w.Measure(600, 400);
    z.w.Arrange({0, 0, 600, 400});
    const RectDip pb = z.p[0]->Bounds();
    EXPECT_EQ(pb.y, WizardControl::kStepIndicatorHeight);
    EXPECT_EQ(pb.h, 400.0f - WizardControl::kButtonBarHeight -
                        WizardControl::kStepIndicatorHeight);
}

TEST(WizardControl, ButtonsRightAlignedInBarInOrder) {
    Wiz3 z;
    z.w.Measure(600, 400);
    z.w.Arrange({0, 0, 600, 400});
    const RectDip back = z.w.BackButton()->Bounds();
    const RectDip next = z.w.NextButton()->Bounds();
    const RectDip cancel = z.w.CancelButton()->Bounds();
    const float barTop = 400.0f - WizardControl::kButtonBarHeight;
    EXPECT_NEAR(cancel.right(), 600.0f - WizardControl::kBarInsetX, 0.01f);
    EXPECT_NEAR(next.right() + WizardControl::kButtonSpacing, cancel.x, 0.01f);
    EXPECT_NEAR(back.right() + WizardControl::kButtonSpacing, next.x, 0.01f);
    for (const RectDip& r : {back, next, cancel}) {
        EXPECT_TRUE(r.y >= barTop);
        EXPECT_TRUE(r.bottom() <= 400.0f);
        EXPECT_TRUE(r.w >= WizardControl::kButtonMinWidth);
        EXPECT_TRUE(r.h > 0.0f);
    }
}

TEST(WizardControl, HiddenCancelFreesItsSlot) {
    Wiz3 z;
    z.w.SetCancelVisible(false);
    z.w.Measure(600, 400);
    z.w.Arrange({0, 0, 600, 400});
    EXPECT_NEAR(z.w.NextButton()->Bounds().right(), 600.0f - WizardControl::kBarInsetX, 0.01f);
}

TEST(WizardControl, InfiniteHeightMeasureStaysFinite) {
    Wiz3 z;
    z.w.Measure(600, std::numeric_limits<float>::infinity());
    const SizeDip d = z.w.Desired();
    EXPECT_TRUE(std::isfinite(d.w));
    EXPECT_TRUE(std::isfinite(d.h));
    EXPECT_TRUE(d.h >= WizardControl::kButtonBarHeight);
}

TEST(WizardControl, HiddenPageIsNotHitTested) {
    WizardControl w;
    w.SetTransitionStyle(WizardControl::TransitionStyle::None);
    auto* p0 = w.AddPage(L"a", std::make_unique<StackPanel>());
    auto* p1 = w.AddPage(L"b", std::make_unique<StackPanel>());
    auto* leaf0 = p0->Emplace<FocusLeaf>();
    auto* leaf1 = p1->Emplace<FocusLeaf>();
    w.Measure(600, 400);
    w.Arrange({0, 0, 600, 400});
    EXPECT_TRUE(w.HitTestDeep(5, 5) == leaf0);
    w.Next();
    w.Measure(600, 400);
    w.Arrange({0, 0, 600, 400});
    // leaf0 keeps its old bounds but its page is collapsed.
    EXPECT_TRUE(w.HitTestDeep(5, 5) == leaf1);
}

// --- focus -------------------------------------------------------------------

TEST(WizardControl, TabOrderIsPageContentThenButtons) {
    WizardControl w;
    auto* p0 = w.AddPage(L"a", std::make_unique<StackPanel>());
    auto* p1 = w.AddPage(L"b", std::make_unique<StackPanel>());
    auto* leaf0 = p0->Emplace<FocusLeaf>();
    p1->Emplace<FocusLeaf>();
    std::vector<UIElement*> order;
    w.CollectFocusables(order);
    // Back is disabled on page 0, and the hidden page's leaf must not appear.
    EXPECT_EQ(order.size(), size_t(3));
    if (order.size() == 3) {
        EXPECT_TRUE(order[0] == leaf0);
        EXPECT_TRUE(order[1] == w.NextButton());
        EXPECT_TRUE(order[2] == w.CancelButton());
    }
}

TEST(WizardControl, FocusInsideLeavingPageIsCleared) {
    WizardControl w;
    w.SetTransitionStyle(WizardControl::TransitionStyle::None);
    auto* p0 = w.AddPage(L"a", std::make_unique<StackPanel>());
    w.AddPage(L"b", std::make_unique<StackPanel>());
    auto* leaf0 = p0->Emplace<FocusLeaf>();
    FocusRig rig(w);

    rig.fm.SetFocus(leaf0);
    EXPECT_TRUE(rig.fm.Focused() == leaf0);
    w.Next();
    EXPECT_TRUE(rig.fm.Focused() == nullptr);

    // Focus on a still-visible, still-enabled button survives a page change.
    rig.fm.SetFocus(w.NextButton());
    w.Previous();
    EXPECT_TRUE(rig.fm.Focused() == w.NextButton());
    w.DetachFromContext();
}

TEST(WizardControl, FocusOnButtonThatBecomesDisabledIsCleared) {
    WizardControl w;
    w.SetTransitionStyle(WizardControl::TransitionStyle::None);
    w.AddPage(L"a", std::make_unique<StackPanel>());
    w.AddPage(L"b", std::make_unique<StackPanel>());
    w.Next();
    FocusRig rig(w);

    rig.fm.SetFocus(w.BackButton());
    EXPECT_TRUE(rig.fm.Focused() == w.BackButton());
    w.Previous();  // back on page 0: Back becomes disabled
    EXPECT_TRUE(rig.fm.Focused() == nullptr);
    w.DetachFromContext();
}

// The Demo page's shape: a wizard as the single child of a fixed-height Border
// inside a vertical StackPanel (which measures children with infinite height).
// The Border's explicit height must win, so the page area is 360 - bar, not 0.
TEST(WizardControl, InsideFixedHeightBorderInStackGetsRealPageArea) {
    StackPanel stack;
    auto* frame = stack.Emplace<Border>();
    frame->SetHeight(360.0f);
    auto* w = frame->SetChild(std::make_unique<WizardControl>());
    w->SetTransitionStyle(WizardControl::TransitionStyle::None);
    auto* p0 = w->AddPage(L"a", std::make_unique<StackPanel>());
    stack.Measure(600.0f, std::numeric_limits<float>::infinity());
    stack.Arrange({0, 0, 600.0f, 2000.0f});
    EXPECT_NEAR(w->Bounds().h, 360.0f, 0.01f);
    EXPECT_NEAR(p0->Bounds().h, 360.0f - WizardControl::kButtonBarHeight, 0.01f);
    EXPECT_TRUE(w->NextButton()->Bounds().bottom() <= 360.0f + 0.01f);
}

TEST(WizardControl, FocusOnHiddenCancelIsCleared) {
    WizardControl w;
    w.AddPage(L"a", std::make_unique<StackPanel>());
    FocusRig rig(w);
    rig.fm.SetFocus(w.CancelButton());
    w.SetCancelVisible(false);
    EXPECT_TRUE(rig.fm.Focused() == nullptr);
    w.DetachFromContext();
}
