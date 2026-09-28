// CustomControls.h — Public API for user-defined FluentUI extensions
//
// This is a SEPARATE LIBRARY that demonstrates how users can extend FluentUI
// by inheriting Calendar, Button, Slider without modifying FluentUI source.
//
// Integration:
//   1. Build FluentUI.lib
//   2. Build this CustomControls.lib (links FluentUI.lib)
//   3. Your app links both
//
// Users can fork/modify this library freely — it is NOT part of FluentUI core.
#pragma once

#include "../../FluentUI/controls/Calendar.h"
#include "../../FluentUI/controls/Button.h"
#include "../../FluentUI/controls/Slider.h"

namespace fluent::custom {

// =============================================================================
// LunarCalendar: Adds lunar date display below Gregorian day numbers
// =============================================================================
// Demonstrates:
// - Calling base RenderCell to reuse standard cell rendering
// - Reading CellInfo struct for pre-computed state
// - Drawing additional text via DWriteContext
//
// Use case: Chinese lunar calendar, Islamic calendar, holiday markers, etc.

class LunarCalendar : public Calendar {
protected:
    void RenderCell(const DrawingContext& dc, const CellInfo& cell) override;

private:
    // Stub: returns "初一", "初二", etc. Real implementation would use a proper
    // lunar calendar library (e.g., libastro, ChineseCalendar.cpp).
    std::wstring GetLunarDate(int year, int month, int day) const;
};

// =============================================================================
// BigButton: 64dp tall button with icon + two-line text
// =============================================================================
// Demonstrates:
// - Overriding Measure() to change natural size
// - Reading CurrentKind() and TintOpacity() to sync with base state
// - Calling RenderBackground() / RenderBorder() for base chrome
// - Overriding RenderContent() for custom layout
//
// Use case: Command palette, hero buttons, toolbar buttons with subtitles

class BigButton : public Button {
public:
    BigButton() = default;

    // Two-line text: main label + subtitle
    void SetSubtitle(const std::wstring& subtitle) { 
        subtitle_ = subtitle; 
        Invalidate(); 
    }
    const std::wstring& Subtitle() const { return subtitle_; }

    // Optional icon (Unicode character or emoji)
    void SetIcon(const std::wstring& icon) { 
        icon_ = icon; 
        InvalidateMeasure(); 
    }
    const std::wstring& Icon() const { return icon_; }

protected:
    void Measure(float availW, float availH) override;
    void RenderContent(const DrawingContext& dc) override;

private:
    std::wstring subtitle_;
    std::wstring icon_;
};

// =============================================================================
// LogSlider: Logarithmic slider (linear drag → exponential value)
// =============================================================================
// Demonstrates:
// - Overriding CoerceValue() to snap to logarithmic steps
// - Reading IsDragging() to distinguish user input
// - Using TrackStart() / ValueToTrackPos() for custom tick marks
//
// Use case: Volume controls (dB), frequency (Hz), zoom levels (1x, 10x, 100x)

class LogSlider : public Slider {
public:
    LogSlider() {
        // Default range: [1, 10000] maps to powers of 10
        SetMinimum(1.0f);
        SetMaximum(10000.0f);
        SetValue(100.0f);
    }

protected:
    // Snap to nearest power of 10
    float CoerceValue(float v) const override;

    // Draw logarithmic tick marks at 1, 10, 100, 1000
    void Render(const DrawingContext& dc) override;

private:
    // Helper: log10(v) rounded to nearest integer power
    int LogLevel(float v) const;
};

} // namespace fluent::custom

#include "SpinnerControl.h"
