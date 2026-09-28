// InheritanceExamples.h — Concrete inheritance examples for Calendar, Button, Slider
//
// These are WORKING, COMPILABLE examples demonstrating the Phase 1 inheritance APIs.
// Place this file in FluentUIDemo/ or examples/ to integrate into the demo app.
#pragma once

#include "../FluentUI/controls/Calendar.h"
#include "../FluentUI/controls/Button.h"
#include "../FluentUI/controls/Slider.h"
#include <cmath>

namespace fluent {

// =============================================================================
// Example 1: LunarCalendar
// =============================================================================
// Adds lunar date display below each Gregorian day number. Demonstrates:
// - Calling base RenderCell to reuse selection/today/day-number rendering
// - Using CellInfo struct to get pre-computed cell state
// - Using DWriteContext to draw additional text

class LunarCalendar : public Calendar {
protected:
    void RenderCell(const DrawingContext& dc, const CellInfo& cell) override {
        // First, call base to draw the standard cell (background, selection ring,
        // today indicator, day number)
        Calendar::RenderCell(dc, cell);
        
        // Then add lunar date below the day number (only for in-month, enabled dates)
        if (!cell.inViewedMonth || !cell.isEnabled) return;
        
        std::wstring lunar = GetLunarDate(cell.year, cell.month, cell.day);
        if (lunar.empty()) return;
        
        DWriteContext* dw = Dwrite();
        if (!dw) return;
        
        // Small font, bottom half of the cell
        if (auto* fmt = dw->Format(9.0f, DWRITE_FONT_WEIGHT_NORMAL,
                                   DWRITE_TEXT_ALIGNMENT_CENTER,
                                   DWRITE_PARAGRAPH_ALIGNMENT_CENTER)) {
            D2D1_RECT_F lunarRect = D2D1::RectF(
                cell.rect.x, cell.rect.y + cell.rect.h * 0.6f,
                cell.rect.right(), cell.rect.bottom());
            
            // Use secondary text color, slightly faded
            D2D1_COLOR_F lunarColor = Theme().colors.textSecondary;
            lunarColor.a *= 0.8f;
            
            dc.DrawText(lunar.c_str(), static_cast<UINT32>(lunar.size()),
                       fmt, lunarRect, lunarColor);
        }
    }

private:
    // Simplified lunar calendar conversion (real implementation would use a library)
    std::wstring GetLunarDate(int year, int month, int day) const {
        // Placeholder: just show "初N" for demo purposes
        // Real implementation: call a lunar calendar library or lookup table
        UNREFERENCED_PARAMETER(year);
        UNREFERENCED_PARAMETER(month);
        int lunarDay = (day % 30) + 1;  // fake
        if (lunarDay == 1) return L"初一";
        if (lunarDay < 11) return L"初" + std::to_wstring(lunarDay);
        if (lunarDay < 20) return L"十" + std::to_wstring(lunarDay - 10);
        if (lunarDay == 20) return L"二十";
        return L"廿" + std::to_wstring(lunarDay - 20);
    }
};

// =============================================================================
// Example 2: BigButton
// =============================================================================
// A 64dp-tall button with icon (top), main title (middle), subtitle (bottom).
// Demonstrates:
// - Overriding Measure to change size
// - Overriding RenderContent to draw custom layout
// - Reading CurrentKind/TintOpacity to sync with base button's state
// - Calling ButtonTextColor helper to match base text color logic

class BigButton : public Button {
public:
    void SetIcon(std::wstring icon) { 
        icon_ = std::move(icon); 
        Invalidate(); 
    }
    void SetSubtitle(std::wstring sub) { 
        subtitle_ = std::move(sub); 
        Invalidate(); 
    }

    // Measure stays PUBLIC: it is public on Button, and redeclaring an override under
    // protected: would narrow its access so a parent Panel could no longer call it.
    void Measure(float availW, float availH) override {
        UNREFERENCED_PARAMETER(availH);
        // Fixed height 64dp, width adapts to available or caps at 200dp
        float w = IsAuto(Width()) ? std::min(availW, 200.0f) : Width();
        SetDesired(SizeDip(w, 64.0f));
    }

protected:
    void RenderContent(const DrawingContext& dc) override {
        DWriteContext* dw = Dwrite();
        if (!dw) return;
        
        // Get the color that matches base button's logic (onAccent for Accent, etc.)
        D2D1_COLOR_F textColor = ButtonTextColor(CurrentKind(), State(),
                                                 AppearanceForRender(),
                                                 Theme().colors);
        
        // Icon (top third, 24dp font)
        if (!icon_.empty()) {
            if (auto* iconFmt = dw->Format(24.0f, DWRITE_FONT_WEIGHT_NORMAL,
                                           DWRITE_TEXT_ALIGNMENT_CENTER,
                                           DWRITE_PARAGRAPH_ALIGNMENT_CENTER)) {
                D2D1_RECT_F iconRect = D2D1::RectF(
                    bounds_.x, bounds_.y + 8,
                    bounds_.right(), bounds_.y + 32);
                dc.DrawText(icon_.c_str(), static_cast<UINT32>(icon_.size()),
                           iconFmt, iconRect, textColor);
            }
        }
        
        // Main title (middle, 14dp SemiBold)
        std::wstring mainText = Text();  // from ContentControl
        if (!mainText.empty()) {
            if (auto* titleFmt = dw->Format(14.0f, DWRITE_FONT_WEIGHT_SEMI_BOLD,
                                            DWRITE_TEXT_ALIGNMENT_CENTER,
                                            DWRITE_PARAGRAPH_ALIGNMENT_CENTER)) {
                D2D1_RECT_F titleRect = D2D1::RectF(
                    bounds_.x, bounds_.y + 28,
                    bounds_.right(), bounds_.y + 44);
                dc.DrawText(mainText.c_str(), static_cast<UINT32>(mainText.size()),
                           titleFmt, titleRect, textColor);
            }
        }
        
        // Subtitle (bottom, 11dp Normal, secondary color)
        if (!subtitle_.empty()) {
            if (auto* subFmt = dw->Format(11.0f, DWRITE_FONT_WEIGHT_NORMAL,
                                          DWRITE_TEXT_ALIGNMENT_CENTER,
                                          DWRITE_PARAGRAPH_ALIGNMENT_CENTER)) {
                D2D1_RECT_F subRect = D2D1::RectF(
                    bounds_.x, bounds_.y + 44,
                    bounds_.right(), bounds_.y + 60);
                D2D1_COLOR_F subColor = Theme().colors.textSecondary;
                dc.DrawText(subtitle_.c_str(), static_cast<UINT32>(subtitle_.size()),
                           subFmt, subRect, subColor);
            }
        }
    }

private:
    std::wstring icon_;
    std::wstring subtitle_;
};

// =============================================================================
// Example 3: LogSlider
// =============================================================================
// Logarithmic slider: drag linearly → output values snap to 1, 10, 100, 1000.
// Demonstrates:
// - Overriding CoerceValue to implement custom snapping
// - Reading IsDragging to distinguish drag (preview) from keyboard/SetValue (commit)
// - Using ValueToTrackPos to draw tick marks at snapped positions

class LogSlider : public Slider {
public:
    LogSlider() {
        SetMinimum(1.0f);
        SetMaximum(10000.0f);
        SetValue(100.0f);
        SetStep(1.0f);  // step is ignored by CoerceValue, set to 1 for consistency
    }

protected:
    // Snap to nearest power of 10
    float CoerceValue(float v) const override {
        if (v < 1.0f) return 1.0f;
        if (v > 10000.0f) return 10000.0f;
        float logVal = std::log10(v);
        float rounded = std::round(logVal);
        return std::pow(10.0f, rounded);
    }
    
    void OnValueChanged(float oldVal, float newVal) override {
        // Always call base first so ValueChanged/ValueChanging events fire
        Slider::OnValueChanged(oldVal, newVal);
        
        // Distinguish drag (real-time preview) from keyboard/programmatic (final commit)
        if (IsDragging()) {
            // Mid-drag: update a live preview (e.g., volume indicator) but don't save
            UpdatePreview(newVal);
        } else {
            // Committed: write to settings/config
            CommitToSettings(newVal);
        }
    }
    
    void Render(const DrawingContext& dc) override {
        // Call base to draw track, thumb, fill
        Slider::Render(dc);
        
        // Overlay tick marks at 1, 10, 100, 1000, 10000
        DrawTickMarks(dc);
    }

private:
    void UpdatePreview(float v) {
        // Placeholder: update a label or visual indicator
        UNREFERENCED_PARAMETER(v);
        // e.g., volumeLabel_->SetText(std::to_wstring(static_cast<int>(v)));
    }
    
    void CommitToSettings(float v) {
        // Placeholder: write to persistent storage
        UNREFERENCED_PARAMETER(v);
        // e.g., settings_.volume = static_cast<int>(v); SaveSettings();
    }
    
    void DrawTickMarks(const DrawingContext& dc) {
        const ColorTokens& colors = Theme().colors;
        D2D1_COLOR_F tickColor = colors.textSecondary;
        tickColor.a *= 0.5f;
        
        // Draw ticks at 1, 10, 100, 1000, 10000
        for (float val : {1.0f, 10.0f, 100.0f, 1000.0f, 10000.0f}) {
            float pos = ValueToTrackPos(val);  // map value to screen DIP
            float centerline = TrackCenterline();
            
            // Small vertical line below the track
            dc.DrawLine(D2D1::Point2F(pos, centerline + 6.0f),
                       D2D1::Point2F(pos, centerline + 10.0f),
                       tickColor, 1.0f);
        }
    }
};

} // namespace fluent
