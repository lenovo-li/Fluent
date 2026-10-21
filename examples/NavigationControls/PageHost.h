// PageHost.h — page container that animates between pages.
//
//   auto* host = grid->Emplace<fluent::PageHost>();
//   host->AddPage(std::move(firmwarePage));   // index 0
//   host->AddPage(std::move(aboutPage));      // index 1
//   host->SetTransition(fluent::PageTransition::Fade);  // optional, default FadeSlide
//   host->ShowPage(0);                        // first page: always instant
//
// MODES.
//   None       instant cut.
//   Fade       old page fades out while the new one fades in, in place.
//   FadeSlide  WinUI "Entrance": old page fades out quickly, new page fades in
//              while sliding up from SlideDistance (24 DIP) below. Default.
// Easing is exponential (AnimatedValue::Approach), so it decelerates naturally;
// TransitionDuration is the approximate settle time (~4 time constants).
//
// The transition is skipped (instant cut) when the system setting "Animation
// effects" is off (SPI_GETCLIENTAREAANIMATION), when the host is not attached yet,
// and for the very first ShowPage.
//
// INPUT. Only the current (incoming) page is hit-tested and contributes Tab stops;
// the leaving page only paints. Focus inside the leaving page is cleared. Calling
// ShowPage mid-transition chains from the current opacities (going back to the
// leaving page reverses the fade instead of restarting it).
#pragma once

#include "../../FluentUI/layout/Panel.h"
#include "../../FluentUI/animation/AnimatedValue.h"
#include <memory>
#include <vector>

namespace fluent {

enum class PageTransition { None, Fade, FadeSlide };

class PageHost : public Panel {
public:
    PageHost() = default;

    // Append a page (initially collapsed). Returns its index, -1 for null.
    int AddPage(std::unique_ptr<FrameworkElement> page);
    FrameworkElement* Page(int index) const;
    int PageCount() const { return static_cast<int>(ChildCount()); }

    // Switch to `index`. Out of range, or already current, is a no-op.
    void ShowPage(int index);
    int CurrentPage() const { return currentIndex_; }
    bool IsTransitioning() const { return leavingIndex_ >= 0; }

    void SetTransition(PageTransition t) { transition_ = t; }
    PageTransition Transition() const { return transition_; }
    void SetTransitionDuration(float seconds) { durationSec_ = seconds > 0.01f ? seconds : 0.01f; }
    float TransitionDuration() const { return durationSec_; }
    void SetSlideDistance(float dip) { slideDistanceDip_ = dip; }
    float SlideDistance() const { return slideDistanceDip_; }

    // Panel / UIElement overrides.
    UIElement* HitTestDeep(float dipX, float dipY) override;
    void CollectFocusables(std::vector<UIElement*>& out) override;
    bool WantsAnimationTick() const override;
    void OnAnimationTick(float dtSec) override;

protected:
    SizeDip MeasureOverride(float availW, float availH) override;
    void ArrangeOverride(const RectDip& content) override;

private:
    // Pages go through AddPage only.
    using Panel::Add;
    using Panel::Emplace;
    using Panel::InsertAt;
    using Panel::RemoveAt;
    using Panel::Clear;

    static bool SystemAnimationEnabled();
    void ClearFocusIn(FrameworkElement* page);
    void FinishTransition();

    PageTransition transition_ = PageTransition::FadeSlide;
    int currentIndex_ = -1;   // visible page receiving input
    int leavingIndex_ = -1;   // page fading out; < 0 when idle
    AnimatedValue fadeOut_{1.0f};
    AnimatedValue fadeIn_{1.0f};
    AnimatedValue slideOffset_{0.0f};  // DIP below the resting position
    float durationSec_ = 0.25f;
    float slideDistanceDip_ = 24.0f;
};

}  // namespace fluent
