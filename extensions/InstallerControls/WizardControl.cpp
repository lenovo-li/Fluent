// WizardControl.cpp
#include "WizardControl.h"
#include "../../FluentUI/automation/AutomationPeer.h"
#include "../../FluentUI/styling/ThemeTokens.h"
#include "../../FluentUI/input/FocusManager.h"
#include "../../FluentUI/core/UIContext.h"
#include <algorithm>
#include <cmath>

namespace fluent {
namespace wizard {

namespace {
// The wizard is a Group named after where the user is ("Options, step 2 of 4"),
// so a screen reader entering it hears the step; its children are the current page
// (hidden pages are not in the tree) and the Back / Next / Cancel buttons, which
// are real Buttons with their own peers. A page change invalidates the wizard, so
// the Name change is raised by the ordinary re-check.
class WizardPeer final : public automation::AutomationPeer {
public:
    explicit WizardPeer(WizardControl& w) : AutomationPeer(w) {}
    automation::ControlType GetControlType() const override { return automation::ControlType::Group; }
    bool IsControlElement() const override { return true; }
    std::wstring GetName() const override {
        const std::wstring& n = Owner().AutomationName();
        if (!n.empty()) return n;
        const auto& w = static_cast<const WizardControl&>(Owner());
        const int cur = w.CurrentPage();
        if (cur < 0) return {};
        return w.PageTitle(cur) + L", " + std::to_wstring(cur + 1) + L"/" + std::to_wstring(w.PageCount());
    }
};
}  // namespace

std::unique_ptr<automation::AutomationPeer> WizardControl::CreateAutomationPeer() {
    return std::make_unique<WizardPeer>(*this);
}

namespace {

// Replace every occurrence of `token` in `s` with `value`.
void ReplaceAll(std::wstring& s, const std::wstring& token, const std::wstring& value) {
    if (token.empty()) return;
    size_t pos = 0;
    while ((pos = s.find(token, pos)) != std::wstring::npos) {
        s.replace(pos, token.size(), value);
        pos += value.size();  // never rescan the inserted value (a title may contain "{title}")
    }
}

// True when `e` is `root` or lives under it.
bool IsWithin(const Visual* e, const Visual* root) {
    for (const Visual* p = e; p; p = p->Parent())
        if (p == root) return true;
    return false;
}

}  // namespace

WizardControl::WizardControl() {
    // The buttons are created here, so they precede every page in children_. That
    // order is irrelevant for painting and hit-testing (Render and HitTestDeep are
    // explicit), and CollectFocusables is overridden so Tab still visits the page
    // content first, then Back / Next / Cancel.
    btnBack_ = Emplace<Button>();
    btnNext_ = Emplace<Button>();
    btnNext_->SetKind(Button::Kind::Accent);
    btnCancel_ = Emplace<Button>();
    for (Button* b : {btnBack_, btnNext_, btnCancel_}) b->SetMinWidth(kButtonMinWidth);

    // Member-function subscriptions; stored so they live as long as the control.
    backSub_ = btnBack_->Click().Subscribe<&WizardControl::OnBackClick>(this);
    nextSub_ = btnNext_->Click().Subscribe<&WizardControl::OnNextClick>(this);
    cancelSub_ = btnCancel_->Click().Subscribe<&WizardControl::OnCancelClick>(this);

    btnCancel_->SetText(labelCancel_);
    UpdateChrome();
}

const std::wstring& WizardControl::PageTitle(int index) const {
    static const std::wstring kEmpty;
    if (index < 0 || index >= PageCount()) return kEmpty;
    return pages_[index].title;
}

FrameworkElement* WizardControl::PageContent(int index) const {
    if (index < 0 || index >= PageCount()) return nullptr;
    return pages_[index].content;
}

bool WizardControl::CanGoNext() const {
    return currentPage_ >= 0 && currentPage_ < PageCount() - 1;
}

bool WizardControl::CanGoPrevious() const { return currentPage_ > 0; }

bool WizardControl::IsLastPage() const {
    return currentPage_ >= 0 && currentPage_ == PageCount() - 1;
}

bool WizardControl::ConfirmLeave(int to) {
    WizardNavigatingArgs args;
    args.from = currentPage_;
    args.to = to;
    navigating_.Raise(*this, args);
    if (args.cancel) return false;
    return ValidateCurrentPage();
}

void WizardControl::Next() {
    if (navigationLocked_ || !CanGoNext()) return;
    if (!ConfirmLeave(currentPage_ + 1)) return;
    GoToPage(currentPage_ + 1);
}

void WizardControl::Previous() {
    if (navigationLocked_ || !CanGoPrevious()) return;
    GoToPage(currentPage_ - 1);
}

void WizardControl::Advance() {
    if (navigationLocked_ || currentPage_ < 0) return;
    if (!IsLastPage()) { Next(); return; }
    if (!ConfirmLeave(WizardNavigatingArgs::kFinish)) return;
    RoutedEventArgs args;
    args.source = this;
    finishing_.Raise(*this, args);
}

void WizardControl::Cancel() {
    if (navigationLocked_) return;
    RoutedEventArgs args;
    args.source = this;
    cancelled_.Raise(*this, args);
}

void WizardControl::GoToPage(int index) {
    if (index < 0 || index >= PageCount() || index == currentPage_) return;

    const int oldPage = currentPage_;
    if (oldPage >= 0) DropFocusFrom(pages_[oldPage].content);
    currentPage_ = index;
    for (int i = 0; i < PageCount(); ++i)
        pages_[i].content->SetVisible(i == currentPage_);

    // The very first page is not a transition: it must be fully visible on the
    // first frame, never fade in from nothing when the window opens.
    pageFade_.SetImmediate((oldPage >= 0 && FadeEnabled()) ? 0.0f : 1.0f);

    UpdateChrome();
    // Measure: the new page has not been measured in this slot.
    InvalidateDirty(DirtyFlags::Measure);

    OnPageChanged(oldPage, index);
    int payload = index;
    pageChanged_.Raise(*this, payload);
}

void WizardControl::SetButtonLabels(std::wstring back, std::wstring next,
                                    std::wstring cancel, std::wstring finish) {
    labelBack_ = std::move(back);
    labelNext_ = std::move(next);
    labelCancel_ = std::move(cancel);
    labelFinish_ = std::move(finish);
    btnCancel_->SetText(labelCancel_);
    UpdateChrome();
}

void WizardControl::SetNavigationLocked(bool locked) {
    if (navigationLocked_ == locked) return;
    navigationLocked_ = locked;
    UpdateChrome();
}

void WizardControl::SetCancelVisible(bool visible) {
    if (btnCancel_->IsVisible() == visible) return;
    if (!visible) DropFocusFrom(btnCancel_);
    btnCancel_->SetVisible(visible);
    InvalidateDirty(DirtyFlags::Measure);
}

void WizardControl::SetShowStepIndicator(bool show) {
    if (showStepIndicator_ == show) return;
    showStepIndicator_ = show;
    if (show && !stepIndicator_) {
        stepIndicator_ = Emplace<TextBlock>();
        stepIndicator_->SetDimmed(true);
        stepIndicator_->SetVAlign(VAlign::Center);
    }
    if (stepIndicator_) stepIndicator_->SetVisible(show);
    UpdateChrome();
    InvalidateDirty(DirtyFlags::Measure);
}

void WizardControl::SetStepIndicatorFormat(std::wstring format) {
    stepFormat_ = std::move(format);
    UpdateChrome();
}

std::wstring WizardControl::StepIndicatorText() const {
    if (!showStepIndicator_ || currentPage_ < 0) return {};
    std::wstring s = stepFormat_;
    // {title} last: a title containing "{current}" must stay literal.
    ReplaceAll(s, L"{current}", std::to_wstring(currentPage_ + 1));
    ReplaceAll(s, L"{total}", std::to_wstring(PageCount()));
    ReplaceAll(s, L"{title}", pages_[currentPage_].title);
    return s;
}

void WizardControl::SetTransitionStyle(TransitionStyle style) {
    transitionStyle_ = style;
    // Switching to None mid-fade must not leave the page half transparent.
    if (style == TransitionStyle::None && pageFade_.Animating(1.0f)) {
        pageFade_.SetImmediate(1.0f);
        Invalidate();
    }
}

bool WizardControl::FadeEnabled() const {
    if (transitionStyle_ != TransitionStyle::Fade) return false;
    if (!respectSystemAnimations_) return true;
    BOOL on = TRUE;
    if (!SystemParametersInfoW(SPI_GETCLIENTAREAANIMATION, 0, &on, 0)) return true;
    return on != FALSE;
}

void WizardControl::UpdateChrome() {
    const bool unlocked = !navigationLocked_;
    btnBack_->SetText(labelBack_);
    btnNext_->SetText(IsLastPage() ? labelFinish_ : labelNext_);
    btnBack_->SetEnabled(unlocked && CanGoPrevious());
    btnNext_->SetEnabled(unlocked && currentPage_ >= 0);
    btnCancel_->SetEnabled(unlocked);
    // A disabled button must not keep focus (Space would still reach it).
    for (Button* b : {btnBack_, btnNext_, btnCancel_})
        if (!b->IsFocusable()) DropFocusFrom(b);
    if (stepIndicator_) stepIndicator_->SetText(StepIndicatorText());
}

void WizardControl::DropFocusFrom(FrameworkElement* subtree) {
    if (!subtree || !IsAttached()) return;
    const UIContext& ctx = Context();
    FocusManager* fm = ctx.focus;
    if (!fm) return;
    if (UIElement* f = fm->Focused(); f && IsWithin(f, subtree)) fm->ClearFocus();
}

void WizardControl::CollectFocusables(std::vector<UIElement*>& out) {
    // Page content first, then the bar: the natural reading order of a wizard.
    if (FrameworkElement* page = PageContent(currentPage_); page && page->IsVisible())
        page->CollectFocusables(out);
    for (Button* b : {btnBack_, btnNext_, btnCancel_})
        if (b->IsVisible()) b->CollectFocusables(out);
}

SizeDip WizardControl::MeasureOverride(float availW, float availH) {
    const float indH = IndicatorHeight();
    const float chromeH = kButtonBarHeight + indH;
    // A container asking "what do you need" passes infinity; keep it infinite
    // rather than subtracting (inf - x is inf anyway, but a NaN must never leak).
    const float pageAvailH =
        std::isfinite(availH) ? std::max(0.0f, availH - chromeH) : availH;

    SizeDip pageSize{};
    if (FrameworkElement* page = PageContent(currentPage_)) {
        page->MeasureCached(availW, pageAvailH);
        pageSize = page->Desired();
    }

    // Hidden pages are measured ONLY when something inside them is Measure-dirty,
    // and their result never sizes the wizard. The host clears dirty flags across
    // the whole tree at the end of every frame (ClearDirtySubtree ignores
    // visibility). A leaf that changes inside a hidden page drops only its own
    // measure cache; its ancestors learn about it solely through the dirty bit
    // (Panel::NeedsRemeasure). Skip the page on that frame and the bit is cleared
    // unserviced: when the page is shown at an unchanged width, its intermediate
    // panels hand back their old cached sizes and the changed leaf keeps stale
    // layout until the next resize. Measuring the page on the frame its content
    // changes consumes the bit while it still exists; a plain resize still skips
    // every hidden page (PageHost does the same for the same reason).
    for (int i = 0; i < PageCount(); ++i) {
        if (i == currentPage_) continue;
        FrameworkElement* hidden = pages_[i].content;
        if (!hidden || hidden->IsVisible() || !hidden->NeedsRemeasure()) continue;
        hidden->MeasureCached(availW, pageAvailH);
    }

    float barW = kBarInsetX * 2.0f;
    int shown = 0;
    for (Button* b : {btnBack_, btnNext_, btnCancel_}) {
        if (!b->IsVisible()) continue;
        b->MeasureCached(availW, kButtonBarHeight);
        barW += std::max(b->Desired().w, kButtonMinWidth);
        ++shown;
    }
    if (shown > 1) barW += kButtonSpacing * static_cast<float>(shown - 1);

    float indW = 0.0f;
    if (stepIndicator_ && showStepIndicator_) {
        const float w = std::isfinite(availW) ? std::max(0.0f, availW - kBarInsetX * 2.0f)
                                              : availW;
        stepIndicator_->MeasureCached(w, indH);
        indW = stepIndicator_->Desired().w + kBarInsetX * 2.0f;
    }

    return SizeDip{std::max({pageSize.w, barW, indW}), pageSize.h + chromeH};
}

void WizardControl::ArrangeOverride(const RectDip& content) {
    const float indH = IndicatorHeight();

    if (stepIndicator_ && showStepIndicator_) {
        ArrangeChild(stepIndicator_,
                     RectDip{content.x + kBarInsetX, content.y,
                             std::max(0.0f, content.w - kBarInsetX * 2.0f), indH});
    }

    if (FrameworkElement* page = PageContent(currentPage_)) {
        ArrangeChild(page, RectDip{content.x, content.y + indH, content.w,
                                   std::max(0.0f, content.h - indH - kButtonBarHeight)});
    }

    const float barTop = std::max(content.y + indH, content.bottom() - kButtonBarHeight);
    dividerY_ = barTop;
    dividerX0_ = content.x;
    dividerX1_ = content.right();

    // Right-aligned, laid out right to left: Cancel, Next/Finish, Back.
    float right = content.right() - kBarInsetX;
    for (Button* b : {btnCancel_, btnNext_, btnBack_}) {
        if (!b->IsVisible()) continue;
        const float w = std::max(b->Desired().w, kButtonMinWidth);
        const float h = b->Desired().h;
        const float y = barTop + (kButtonBarHeight - h) * 0.5f;
        ArrangeChild(b, RectDip{right - w, y, w, h});
        right -= w + kButtonSpacing;
    }
}

void WizardControl::Render(const DrawingContext& dc) {
    OnRenderBackground(dc);

    // Hairline between the page and the button bar.
    dc.DrawLine(D2D1::Point2F(dividerX0_, dividerY_ + 0.5f),
                D2D1::Point2F(dividerX1_, dividerY_ + 0.5f),
                Theme().colors.controlStrokeDefault, 1.0f);

    if (stepIndicator_ && showStepIndicator_) stepIndicator_->RenderWithOpacity(dc);

    if (FrameworkElement* page = PageContent(currentPage_)) {
        const float fade = pageFade_;
        if (fade >= 1.0f) {
            page->RenderWithOpacity(dc);
        } else if (fade > 0.0f) {
            page->RenderWithOpacity(dc.WithOpacity(fade));
        }
        // fade == 0 is the first frame of a transition. NativeWindowHost re-collects
        // animations at every frame boundary, so the tick starts on the next frame
        // even for a programmatic GoToPage with no input.
    }

    // The bar never fades: the buttons are the same on every page.
    for (Button* b : {btnBack_, btnNext_, btnCancel_}) b->RenderWithOpacity(dc);
}

bool WizardControl::WantsAnimationTick() const {
    return pageFade_.Animating(1.0f, 0.01f);
}

void WizardControl::OnAnimationTick(float dtSec) {
    pageFade_.Approach(1.0f, dtSec, Theme().motion.fadeTau, 0.01f);
    // Render-only: opacity never changes layout. The wizard's own bounds cover
    // the page, so invalidating ourselves repaints it.
    Invalidate();
}

} // namespace wizard
} // namespace fluent
