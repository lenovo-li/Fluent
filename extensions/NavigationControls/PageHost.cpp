// PageHost.cpp
#include "PageHost.h"
#include "../../FluentUI/input/FocusManager.h"
#include "../../FluentUI/styling/ThemeTokens.h"
#include <algorithm>
#include <string>

namespace fluent {

int PageHost::AddPage(std::unique_ptr<FrameworkElement> page) {
    if (!page) return -1;
    const int index = static_cast<int>(ChildCount());
    page->SetVisible(false);
    page->SetOpacity(1.0f);
    Add(std::move(page));
    return index;
}

FrameworkElement* PageHost::Page(int index) const {
    if (index < 0 || index >= PageCount()) return nullptr;
    return ChildAt(index);
}

bool PageHost::SystemAnimationEnabled() {
    BOOL enabled = TRUE;
    if (!SystemParametersInfoW(SPI_GETCLIENTAREAANIMATION, 0, &enabled, 0))
        return true;
    return enabled != FALSE;
}

void PageHost::ClearFocusIn(FrameworkElement* page) {
    if (!page) return;
    FocusManager* fm = Context().focus;
    if (!fm) return;
    UIElement* focused = fm->Focused();
    if (!focused) return;
    // Walk up the parent chain to see if focused element is inside page.
    for (Visual* v = focused; v; v = v->Parent()) {
        if (v == page) {
            fm->ClearFocus();
            return;
        }
    }
}

void PageHost::FinishTransition() {
    if (leavingIndex_ >= 0 && leavingIndex_ < PageCount()) {
        ChildAt(leavingIndex_)->SetVisible(false);
        ChildAt(leavingIndex_)->SetOpacity(1.0f);
    }
    leavingIndex_ = -1;
    fadeOut_.SetImmediate(1.0f);
    slideOffset_.SetImmediate(0.0f);
    InvalidateDirty(DirtyFlags::Arrange);
    OutputDebugStringA("[NavigationControls.PageHost] transition complete\n");
}


void PageHost::ShowPage(int index) {
    if (index < 0 || index >= PageCount()) return;
    // Already current (or already the incoming page of a running transition).
    if (index == currentIndex_) return;

    const bool animate = (transition_ != PageTransition::None) &&
                         SystemAnimationEnabled() &&
                         IsAttached() &&
                         (currentIndex_ >= 0);

    if (!animate) {
        // Instant switch.
        for (int i = 0; i < PageCount(); ++i) {
            auto* page = ChildAt(i);
            page->SetVisible(i == index);
            page->SetOpacity(1.0f);
        }
        if (currentIndex_ >= 0 && currentIndex_ != index)
            ClearFocusIn(ChildAt(currentIndex_));
        currentIndex_ = index;
        leavingIndex_ = -1;
        fadeOut_.SetImmediate(1.0f);
        fadeIn_.SetImmediate(1.0f);
        slideOffset_.SetImmediate(0.0f);
        InvalidateDirty(DirtyFlags::Arrange);
        OutputDebugStringA(("[NavigationControls.PageHost] instant to page " +
                            std::to_string(index) + "\n").c_str());
        return;
    }

    // Animated transition.
    const int oldCurrent = currentIndex_;
    currentIndex_ = index;

    // Snap old leaving page if mid-transition.
    if (leavingIndex_ >= 0 && leavingIndex_ < PageCount() && leavingIndex_ != index) {
        ChildAt(leavingIndex_)->SetVisible(false);
        ChildAt(leavingIndex_)->SetOpacity(1.0f);
    }

    leavingIndex_ = (oldCurrent >= 0 && oldCurrent != index) ? oldCurrent : -1;

    // Clear focus from leaving page.
    if (leavingIndex_ >= 0 && leavingIndex_ < PageCount())
        ClearFocusIn(ChildAt(leavingIndex_));

    // Set up fade from current opacity for smooth chaining.
    if (leavingIndex_ >= 0 && leavingIndex_ < PageCount()) {
        ChildAt(leavingIndex_)->SetVisible(true);
        fadeOut_.SetImmediate(ChildAt(leavingIndex_)->Opacity());
    } else {
        fadeOut_.SetImmediate(1.0f);
    }

    // A collapsed page starts from 0; the page that was leaving (user went back)
    // resumes from its current opacity, which reverses the fade.
    FrameworkElement* incoming = ChildAt(index);
    fadeIn_.SetImmediate(incoming->IsVisible() ? incoming->Opacity() : 0.0f);
    incoming->SetVisible(true);
    incoming->SetOpacity(fadeIn_.value);
    if (leavingIndex_ >= 0) ChildAt(leavingIndex_)->SetOpacity(fadeOut_.value);

    if (transition_ == PageTransition::FadeSlide) {
        slideOffset_.SetImmediate(slideDistanceDip_ * (1.0f - fadeIn_.value));
    } else {
        slideOffset_.SetImmediate(0.0f);
    }

    InvalidateDirty(DirtyFlags::Arrange);
    OutputDebugStringA(("[NavigationControls.PageHost] transition " +
                        std::to_string(leavingIndex_) + " -> " +
                        std::to_string(index) + "\n").c_str());
}

SizeDip PageHost::MeasureOverride(float availW, float availH) {
    // Measure all pages so they're ready when shown.
    SizeDip largest{0, 0};
    for (int i = 0; i < PageCount(); ++i) {
        auto* child = ChildAt(i);
        if (!child) continue;
        child->MeasureCached(availW, availH);
        const SizeDip& d = child->Desired();
        largest.w = std::max(largest.w, d.w);
        largest.h = std::max(largest.h, d.h);
    }
    return largest;
}

void PageHost::ArrangeOverride(const RectDip& content) {
    for (int i = 0; i < PageCount(); ++i) {
        auto* child = ChildAt(i);
        if (!child || !child->IsVisible()) continue;

        RectDip slot = content;
        // Apply slide offset to the incoming page during FadeSlide.
        if (i == currentIndex_ && transition_ == PageTransition::FadeSlide &&
            leavingIndex_ >= 0) {
            slot.y += slideOffset_.value;
        }
        ArrangeChild(child, slot);
    }
}

bool PageHost::WantsAnimationTick() const {
    if (leavingIndex_ < 0) return false;
    return fadeOut_.Animating(0.0f, 0.01f) || fadeIn_.Animating(1.0f, 0.01f) ||
           (transition_ == PageTransition::FadeSlide && slideOffset_.Animating(0.0f, 0.5f));
}

void PageHost::OnAnimationTick(float dtSec) {
    if (leavingIndex_ < 0) return;

    const float tau = durationSec_ * 0.25f;
    const float tauOut = (transition_ == PageTransition::FadeSlide) ? tau * 0.5f : tau;
    fadeOut_.Approach(0.0f, dtSec, tauOut, 0.01f);
    fadeIn_.Approach(1.0f, dtSec, tau, 0.01f);
    if (transition_ == PageTransition::FadeSlide) {
        slideOffset_.Approach(0.0f, dtSec, tau, 0.5f);
    }

    if (leavingIndex_ >= 0 && leavingIndex_ < PageCount())
        ChildAt(leavingIndex_)->SetOpacity(fadeOut_.value);
    if (currentIndex_ >= 0 && currentIndex_ < PageCount())
        ChildAt(currentIndex_)->SetOpacity(fadeIn_.value);

    InvalidateDirty(DirtyFlags::Arrange);

    if (!fadeIn_.Animating(1.0f, 0.01f)) {
        FinishTransition();
    }
}

UIElement* PageHost::HitTestDeep(float dipX, float dipY) {
    if (!HitTest(dipX, dipY)) return nullptr;
    // Only the current page receives input.
    if (currentIndex_ >= 0 && currentIndex_ < PageCount()) {
        auto* page = ChildAt(currentIndex_);
        if (page && page->IsVisible()) {
            UIElement* hit = page->HitTestDeep(dipX, dipY);
            if (hit) return hit;
        }
    }
    return nullptr;  // empty area: let the click fall through
}

void PageHost::CollectFocusables(std::vector<UIElement*>& out) {
    // Only the current page's children are tabbable.
    if (currentIndex_ >= 0 && currentIndex_ < PageCount()) {
        auto* page = ChildAt(currentIndex_);
        if (page && page->IsVisible())
            page->CollectFocusables(out);
    }
}

}  // namespace fluent
