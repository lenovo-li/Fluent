// SpinnerControl.cpp - Implementation
#include "SpinnerControl.h"

#include "../../FluentUI/automation/AutomationPeer.h"
#include <cmath>

namespace fluent::custom {

namespace {
    constexpr float kPi = 3.14159265f;
    constexpr int kArcCount = 8;
}

SpinnerControl::SpinnerControl() {
    SetFocusable(false);  // Spinner is not interactive
}

void SpinnerControl::Start() { running_ = true; Invalidate(); }

void SpinnerControl::Stop() { running_ = false; Invalidate(); }

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
    
    // Rotate continuously
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
    
    // Center point
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

} // namespace fluent::custom
