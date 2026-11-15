// SpinnerControl.cpp - Implementation
#include "SpinnerControl.h"

#include "../../FluentUI/automation/AutomationPeer.h"
#include "../../FluentUI/window/WindowServices.h"
#include <d2d1_1.h>
#include <algorithm>
#include <cmath>

namespace fluent::custom {

namespace {
    constexpr float kPi = 3.14159265f;
    constexpr int kArcCount = 8;
    constexpr float kArcStroke = 3.0f;  // line thickness (DIP; scaled to px on the ring)

static bool ColorEquals(const D2D1_COLOR_F& a, const D2D1_COLOR_F& b) {
    return std::fabs(a.r - b.r) < 1e-6f && std::fabs(a.g - b.g) < 1e-6f &&
           std::fabs(a.b - b.b) < 1e-6f && std::fabs(a.a - b.a) < 1e-6f;
}
}

SpinnerControl::SpinnerControl() {
    SetFocusable(false);  // Spinner is not interactive
}

void SpinnerControl::Start() {
    running_ = true;
    UpdateSpinMode();  // try switching to compositor mode if attached
    Invalidate();
}

void SpinnerControl::Stop() {
    running_ = false;
    UpdateSpinMode();  // tear down compositor visual
    Invalidate();
}

bool SpinnerControl::WantsAnimationTick() const {
    if (!IsEffectivelyVisible() || !running_) return false;
    // Compositor mode needs NO UI-thread tick (the compositor drives rotation),
    // EXCEPT when a pending redraw exists (foreground color changed): one tick
    // lets SyncSpinGeometry redraw the ring outside BeginDraw, then go idle again.
    if (compositorActive_) return ringRedrawPending_;
    // Fallback path wants per-frame ticks.
    return true;
}

void SpinnerControl::Measure(float availW, float availH) {
    (void)availW;
    (void)availH;
    // Natural size is diameter_ x diameter_
    float w = IsAuto(Width()) ? diameter_ : Width();
    float h = IsAuto(Height()) ? diameter_ : Height();
    SetDesired(SizeDip(w, h));
}

void SpinnerControl::OnAnimationTick(float dtSec) {
    if (!running_) return;

    // Compositor mode: typically no tick (compositor owns the motion), but when
    // ringRedrawPending_ is true (foreground color changed during Render), sync
    // geometry here (outside BeginDraw) to safely redraw the ring.
    if (compositorActive_) {
        if (ringRedrawPending_) SyncSpinGeometry();
        return;
    }

    // UI-thread fallback: rotate continuously
    rotation_ += speed_ * 2.0f * kPi * dtSec;
    if (rotation_ > 2.0f * kPi) {
        rotation_ -= 2.0f * kPi;
    }

    // Pulse between 0.9 and 1.1
    float pulseTarget = 1.0f + 0.1f * std::sin(rotation_ * 3.0f);
    pulse_.Approach(pulseTarget, dtSec, 0.15f);

    Invalidate();
}

namespace {
// An indeterminate busy indicator, reported the way WinUI reports ProgressRing:
// ProgressBar, no RangeValue (there is no value to report). In the control view
// only while running: a stopped spinner shows nothing worth reading.
class SpinnerPeer final : public automation::AutomationPeer {
public:
    explicit SpinnerPeer(SpinnerControl& s) : AutomationPeer(s) {}
    automation::ControlType GetControlType() const override { return automation::ControlType::ProgressBar; }
    bool IsControlElement() const override {
        return static_cast<const SpinnerControl&>(Owner()).IsRunning();
    }
    std::wstring GetName() const override {
        const std::wstring& n = Owner().AutomationName();
        return n.empty() ? std::wstring(L"Busy") : n;
    }
};
}  // namespace

std::unique_ptr<automation::AutomationPeer> SpinnerControl::CreateAutomationPeer() {
    return std::make_unique<SpinnerPeer>(*this);
}

void SpinnerControl::Render(const DrawingContext& dc) {
    // Draw background (optional, respects Control::Background)
    if (HasBackground()) {
        dc.FillRect(D2D1::RectF(bounds_.x, bounds_.y, bounds_.right(), bounds_.bottom()),
                   EffectiveBackground());
    }

    // Compositor path: the rotating ring is a DComp visual drawn above this
    // content by the compositor thread — draw only the background here (already
    // done above) and let the compositor animate the ring. The UI thread does not
    // touch it per frame.
    //
    // Appearance changes (foreground color) only trigger a redraw when the control
    // is next invalidated (theme change, or another element's redraw intersecting
    // our bounds). To catch foreground changes immediately, check here and flag a
    // pending redraw if the color changed since the ring was last drawn. The flag
    // makes WantsAnimationTick() return true, and OnAnimationTick will call
    // SyncSpinGeometry (outside BeginDraw) to redraw safely.
    if (compositorActive_) {
        const D2D1_COLOR_F fg = EffectiveForeground();
        if (lastRingColor_.a < 0.0f || !ColorEquals(lastRingColor_, fg)) {
            ringRedrawPending_ = true;
        }
        return;
    }

    // Fallback path: no composition device — spin on the UI thread as before.
    float cx = bounds_.x + bounds_.w * 0.5f;
    float cy = bounds_.y + bounds_.h * 0.5f;
    float radius = std::min(bounds_.w, bounds_.h) * 0.4f;

    // Apply pulse scale
    float scale = static_cast<float>(pulse_);
    radius *= scale;

    // Draw 8 arcs with gradient opacity
    D2D1_COLOR_F color = EffectiveForeground();

    for (int i = 0; i < kArcCount; ++i) {
        float angle = rotation_ + (2.0f * kPi / kArcCount) * i;
        float nextAngle = angle + (2.0f * kPi / kArcCount) * 0.6f;

        // Opacity gradient: leading arc is bright, trailing fades
        float opacity = 1.0f - (static_cast<float>(i) / kArcCount);
        D2D1_COLOR_F arcColor = color;
        arcColor.a *= opacity;

        // Arc endpoints
        float x1 = cx + radius * std::cos(angle);
        float y1 = cy + radius * std::sin(angle);
        float x2 = cx + radius * std::cos(nextAngle);
        float y2 = cy + radius * std::sin(nextAngle);

        // Draw arc as a thick line
        dc.DrawLine(D2D1::Point2F(x1, y1), D2D1::Point2F(x2, y2),
                   arcColor, 3.0f);
    }
}

// ---------------------------------------------------------------------------
// Compositor rotation on the DirectComposition compositor thread.
// ---------------------------------------------------------------------------

void SpinnerControl::UpdateSpinMode() {
    const bool shouldRun = running_ && IsAttached() && IsEffectivelyVisible();
    if (shouldRun) {
        WindowServices* win = Window();
        ICompositionBackend* comp = win ? win->Composition() : nullptr;
        if (!comp) { compositorActive_ = false; return; }  // fallback to UI spin

        if (!spinContainer_) {
            spinContainer_ = comp->CreateVisual();
            spinnerVisual_ = comp->CreateVisual();
            if (!spinContainer_ || !spinnerVisual_) {
                spinContainer_.reset();
                spinnerVisual_.reset();
                compositorActive_ = false;
                return;
            }
            spinContainer_->AddChild(spinnerVisual_.get());
            comp->AddToRoot(spinContainer_.get());
            // A fresh visual has no surface yet; draw it even when the size matches
            // the ring a previous (torn-down) visual had.
            ringRedrawPending_ = true;
            lastOpacity_ = -1.0f;  // force opacity push on first sync
        }
        compositorActive_ = true;
        SyncSpinGeometry();
        ApplySpinSpeed();
        comp->RequestCommit();
    } else if (spinContainer_) {
        // Stopping or detaching: tear down the compositor visual.
        if (ICompositionBackend* comp = Window() ? Window()->Composition() : nullptr) {
            spinnerVisual_->StopAnimation(CompositionProperty::Rotation);
            comp->RemoveFromRoot(spinContainer_.get());
            comp->RequestCommit();
        }
        spinContainer_->RemoveChild(spinnerVisual_.get());
        spinnerVisual_.reset();
        spinContainer_.reset();
        compositorActive_ = false;
    } else {
        compositorActive_ = false;
    }
}

void SpinnerControl::SyncSpinGeometry() {
    if (!spinContainer_ || !spinnerVisual_) return;

    // The ring is drawn inside bounds_ with some margin (0.5*diameter - radius).
    // Convert to physical pixels via DPI scale.
    const RectDip clipped = WindowClippedBounds();
    const float s = Context().dpiScale > 0.0f ? Context().dpiScale : 1.0f;

    // No visible area: park a zero-size clip and wait (ProgressBar does the same).
    if (clipped.w <= 0.0f || clipped.h <= 0.0f) {
        spinContainer_->SetOffset(0.0f, 0.0f);
        spinContainer_->SetClip(0.0f, 0.0f, 0.0f, 0.0f);
        if (ICompositionBackend* comp = Window() ? Window()->Composition() : nullptr)
            comp->RequestCommit();
        return;
    }

    // Container: at the clipped origin, clipped to the visible rectangle.
    spinContainer_->SetOffset(clipped.x * s, clipped.y * s);
    spinContainer_->SetClip(0.0f, 0.0f, clipped.w * s, clipped.h * s);

    // Ring: full bounds (diameter_ x diameter_), offset relative to the clipped
    // container so the center lands at the right place. The rotation transform is
    // about the ring visual's own center, which DComp computes after offset.
    const float ringWdip = std::max(1.0f, bounds_.w);
    const float ringHdip = std::max(1.0f, bounds_.h);
    const float offsetX = (bounds_.x - clipped.x) * s;
    const float offsetY = (bounds_.y - clipped.y) * s;
    spinnerVisual_->SetOffset(offsetX, offsetY);

    // Push ancestor opacity to the container visual (the ring itself stays at full
    // opacity; the container fades it). This handles PageHost fade-out and any other
    // ancestor SetOpacity calls. Only update when the value changed to avoid
    // redundant commits.
    const float opacity = EffectiveOpacity();
    if (std::fabs(opacity - lastOpacity_) > 1e-6f) {
        lastOpacity_ = opacity;
        spinContainer_->SetOpacity(opacity);
    }

    // Redraw the ring when its size changed, on the first draw, or when a theme
    // change forced it (ring colors differ but size doesn't).
    const uint32_t newWpx = static_cast<uint32_t>(ringWdip * s + 0.5f);
    const uint32_t newHpx = static_cast<uint32_t>(ringHdip * s + 0.5f);
    const bool sizeChanged =
        newWpx != ringWpx_ || newHpx != ringHpx_;
    if (sizeChanged || ringRedrawPending_) {
        ringWpx_ = newWpx;
        ringHpx_ = newHpx;
        ringRedrawPending_ = false;
        DrawRing();
    }

    if (ICompositionBackend* comp = Window() ? Window()->Composition() : nullptr)
        comp->RequestCommit();
}

HRESULT SpinnerControl::DrawRing() {
    if (!spinnerVisual_ || ringWpx_ == 0 || ringHpx_ == 0) return E_FAIL;

    const D2D1_COLOR_F color = EffectiveForeground();
    lastRingColor_ = color;  // cache for next Render's change detection
    const float s = Context().dpiScale > 0.0f ? Context().dpiScale : 1.0f;
    const float radius = std::min(static_cast<float>(ringWpx_), static_cast<float>(ringHpx_)) * 0.4f;
    const float stroke = kArcStroke * s;

    const float wpx = static_cast<float>(ringWpx_);
    const float hpx = static_cast<float>(ringHpx_);
    const bool ok = spinnerVisual_->DrawSurface(
        ringWpx_, ringHpx_,
        [color, radius, stroke, wpx, hpx](ID2D1DeviceContext* dc, float, float) {
            if (!dc) return;  // headless backend: nothing to rasterize
            dc->Clear(D2D1::ColorF(0, 0, 0, 0));  // transparent outside the ring

            ComPtr<ID2D1SolidColorBrush> brush;
            if (FAILED(dc->CreateSolidColorBrush(color, brush.GetAddressOf()))) return;

            // Draw 8 arcs with gradient opacity, centered in the surface.
            const float cx = wpx * 0.5f;
            const float cy = hpx * 0.5f;

            for (int i = 0; i < kArcCount; ++i) {
                const float angle = (2.0f * kPi / kArcCount) * i;
                const float nextAngle = angle + (2.0f * kPi / kArcCount) * 0.6f;

                // Opacity gradient: leading arc is bright, trailing fades.
                const float opacity = 1.0f - (static_cast<float>(i) / kArcCount);
                D2D1_COLOR_F arcColor = color;
                arcColor.a *= opacity;
                brush->SetColor(arcColor);

                // Arc endpoints (rotation == 0 here; the compositor rotates the whole surface).
                const float x1 = cx + radius * std::cos(angle);
                const float y1 = cy + radius * std::sin(angle);
                const float x2 = cx + radius * std::cos(nextAngle);
                const float y2 = cy + radius * std::sin(nextAngle);

                dc->DrawLine(D2D1::Point2F(x1, y1), D2D1::Point2F(x2, y2),
                            brush.Get(), stroke);
            }
        });

    return ok ? S_OK : S_FALSE;
}

void SpinnerControl::ApplySpinSpeed() {
    if (!spinnerVisual_) return;
    // Speed 0 holds the ring still, as the UI-thread path does (it advances by
    // speed * dt). MakeRotationSweep would otherwise substitute 1 rps for it.
    if (speed_ > 0.0f) spinnerVisual_->StartRotationSweep(speed_);
    else spinnerVisual_->StopAnimation(CompositionProperty::Rotation);
}

void SpinnerControl::UpdateSpinSpeed() {
    if (!compositorActive_ || !spinnerVisual_) return;
    // Restart the rotation at the new speed. The angle restarts at 0, a visible
    // jump of at most one arc step; SetSpeed is a setup call, not something that
    // changes during normal use, so phase continuity is not worth the bookkeeping.
    ApplySpinSpeed();
    if (ICompositionBackend* comp = Window() ? Window()->Composition() : nullptr)
        comp->RequestCommit();
}

void SpinnerControl::OnAttachedToTree() {
    UpdateSpinMode();
}

void SpinnerControl::OnDetachedFromTree() {
    if (spinContainer_) {
        if (ICompositionBackend* comp = Window() ? Window()->Composition() : nullptr) {
            if (spinnerVisual_) spinnerVisual_->StopAnimation(CompositionProperty::Rotation);
            comp->RemoveFromRoot(spinContainer_.get());
        }
        if (spinnerVisual_) spinContainer_->RemoveChild(spinnerVisual_.get());
        spinnerVisual_.reset();
        spinContainer_.reset();
    }
    compositorActive_ = false;
}

void SpinnerControl::OnBoundsChanged() {
    if (compositorActive_) SyncSpinGeometry();
}

void SpinnerControl::OnVisibilityChanged(bool) {
    UpdateSpinMode();
}

void SpinnerControl::OnAncestorVisibilityChanged() {
    UpdateSpinMode();
}

void SpinnerControl::OnThemeChanged() {
    if (compositorActive_ && spinnerVisual_) {
        ringRedrawPending_ = true;  // foreground color may have changed
        SyncSpinGeometry();
    }
}

void SpinnerControl::OnDpiChanged(float dpiScale) {
    UNREFERENCED_PARAMETER(dpiScale);  // Context already refreshed by host
    if (compositorActive_) SyncSpinGeometry();
}

void SpinnerControl::OnDeviceLost() {
    // The device is gone: drop the visuals without calling into it. The host
    // rebuilds the stack and calls OnDeviceRestored, which recreates them.
    spinnerVisual_.reset();
    spinContainer_.reset();
    compositorActive_ = false;
}

void SpinnerControl::OnDeviceRestored() {
    UpdateSpinMode();
}

} // namespace fluent::custom
