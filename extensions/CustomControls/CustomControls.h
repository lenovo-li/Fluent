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
// - Reading CurrentKind() and State() to sync text colour with base state
// - Keeping Button's RenderBackground() / RenderBorder() chrome untouched
// - Overriding RenderContent() for custom layout
//
// LAYOUT. Up to three centred rows, top to bottom: the icon (a glyph string,
// drawn at 24 DIP), the main label (Text(), semibold body size) and the
// subtitle (caption size, secondary colour). Empty rows take no space and the
// remaining rows are centred vertically as a block, so a button with only a
// label looks like before. The row arithmetic is the static LayoutRows() so it
// can be tested without a device.
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

    // Optional icon (Unicode character or emoji). Render-only: the button is a
    // fixed 64 DIP tall and LayoutRows() fits all three rows inside it, so an icon
    // never changes the desired size and needs no Measure pass.
    void SetIcon(const std::wstring& icon) { 
        icon_ = icon; 
        Invalidate(); 
    }
    const std::wstring& Icon() const { return icon_; }

    static constexpr float kHeight = 64.0f;
    static constexpr float kIconRowH = 24.0f;
    static constexpr float kTitleRowH = 20.0f;
    static constexpr float kSubtitleRowH = 16.0f;

    // Where each row goes inside `bounds`. A row that is not shown gets a
    // zero-height rect (and RenderContent skips it).
    struct Rows {
        RectDip icon;
        RectDip title;
        RectDip subtitle;
    };
    static Rows LayoutRows(const RectDip& bounds, bool hasIcon, bool hasTitle,
                           bool hasSubtitle);

protected:
    void Measure(float availW, float availH) override;
    void RenderContent(const DrawingContext& dc) override;

private:
    std::wstring subtitle_;
    std::wstring icon_;
};

// =============================================================================
// LogSlider: Slider whose value snaps to powers of ten (1, 10, 100, ...)
// =============================================================================
// Demonstrates:
// - Overriding CoerceValue() to snap to logarithmic steps
// - Overriding OnKeyDownRouted() when the base step no longer fits the grid
// - Using TrackStart() / ValueToTrackPos() for custom tick marks
//
// The track itself stays LINEAR in value (Slider's pointer mapping is private and
// not an extension point), so dragging reaches 1000 and 10000 easily while 1, 10
// and 100 share the leftmost percent of the track. A real logarithmic drag would
// store the exponent as the RangeBase value and convert at the edges; this sample
// keeps to the snapping and the keyboard.
//
// KEYBOARD. Slider's arrow keys add +/-Step() (1 by default). Every power of ten
// is at least 1 away from its neighbour on a log scale, so 100 +/- 1 rounds back
// to 100 and the arrows did nothing. LogSlider steps a decade instead: Right/Up
// multiply by ten, Left/Down divide by ten; Home/End fall through to the base.
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

    void OnKeyDownRouted(KeyEventArgs& e) override;

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
