// LunarCalendar.cpp — Implementation
#include "CustomControls.h"

namespace fluent::custom {

void LunarCalendar::RenderCell(const DrawingContext& dc, const CellInfo& cell) {
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

std::wstring LunarCalendar::GetLunarDate(int year, int month, int day) const {
    // STUB: Real implementation would use a lunar calendar library.
    // For now, just show day-of-month as "初N" (Chinese lunar day prefix).
    //
    // A production version would:
    // 1. Convert Gregorian (year, month, day) to lunar (year, month, day, isLeap)
    // 2. Map lunar day [1..30] to Chinese strings: 初一, 初二, ..., 廿九, 三十
    // 3. Handle leap months and month names (正月, 二月, etc.)
    //
    // Libraries: ChineseCalendar (https://github.com/9p4/libastro),
    //            or ship a lookup table for years 1900-2100.
    
    (void)year;
    (void)month;
    
    if (day <= 0 || day > 31) return L"";
    
    // Simple placeholder: show day number as lunar date
    return L"Day " + std::to_wstring(day);
}

} // namespace fluent::custom
