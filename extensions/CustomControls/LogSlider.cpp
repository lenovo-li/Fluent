// LogSlider.cpp — Simplified implementation
#include "CustomControls.h"
#include <cmath>
#include <algorithm>

namespace fluent::custom {

float LogSlider::CoerceValue(float v) const {
    v = Slider::CoerceValue(v);
    if (v <= 0.0f) return Minimum();
    
    float logVal = std::log10f(v);
    int level = static_cast<int>(std::roundf(logVal));
    float snapped = std::powf(10.0f, static_cast<float>(level));
    
    return std::clamp(snapped, Minimum(), Maximum());
}

// Step one decade per arrow key. Slider::StepBy adds +/-Step() and CoerceValue
// above rounds the result back to the nearest power of ten, so the base arrows
// can never leave the current value (100 + 1 -> 100, 1 - 1 -> 1). Multiplying
// or dividing by ten lands exactly on the neighbouring snap point; SetValue then
// clamps at the ends and only raises ValueChanged when the value really moved.
void LogSlider::OnKeyDownRouted(KeyEventArgs& e) {
    switch (e.vk) {
        case VK_RIGHT:
        case VK_UP:
            SetValue(Value() * 10.0f);
            e.handled = true;
            return;
        case VK_LEFT:
        case VK_DOWN:
            SetValue(Value() / 10.0f);
            e.handled = true;
            return;
        default:
            Slider::OnKeyDownRouted(e);  // Home / End and anything else
            return;
    }
}

void LogSlider::Render(const DrawingContext& dc) {
    Slider::Render(dc);
    
    // Draw simple tick marks at powers of 10
    int minLevel = LogLevel(Minimum());
    int maxLevel = LogLevel(Maximum());
    
    for (int level = minLevel; level <= maxLevel; ++level) {
        float tickValue = std::powf(10.0f, static_cast<float>(level));
        if (tickValue < Minimum() || tickValue > Maximum()) continue;
        
        float tickPos = ValueToTrackPos(tickValue);
        float tickY = TrackCenterline();
        
        dc.DrawLine(D2D1::Point2F(tickPos, tickY - 4.0f),
                   D2D1::Point2F(tickPos, tickY + 4.0f),
                   EffectiveForeground(), 1.0f);
    }
}

int LogSlider::LogLevel(float v) const {
    if (v <= 0.0f) return 0;
    return static_cast<int>(std::roundf(std::log10f(v)));
}

} // namespace fluent::custom
