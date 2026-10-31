// Calendar.cpp
#include "Calendar.h"
#include "../graphics/DrawingContext.h"
#include "../styling/ThemeTokens.h"
#include "../graphics/DWriteContext.h"
#include <sstream>
#include <algorithm>
#include <tuple>

namespace fluent {

// --- Date helpers ------------------------------------------------------------

int CalDayOfWeek(int y, int m, int d) {
    // Tomohiko Sakamoto's algorithm: returns 0=Sun, 1=Mon, ..., 6=Sat.
    static const int t[] = {0, 3, 2, 5, 0, 3, 5, 1, 4, 6, 2, 4};
    y -= (m < 3);
    return (y + y/4 - y/100 + y/400 + t[m-1] + d) % 7;
}

void CalAddMonths(int& y, int& m, int& d, int delta) {
    int totalMonths = y * 12 + (m - 1) + delta;
    y = totalMonths / 12;
    m = (totalMonths % 12) + 1;
    // Clamp day to valid range for the new month.
    int maxDay = CalDaysInMonth(y, m);
    if (d > maxDay) d = maxDay;
}

void CalAddDays(int& y, int& m, int& d, int delta) {
    // Walk whole months rather than day-by-day, so a large delta (a 42-cell grid
    // start offset, PageUp by a month) costs a handful of iterations instead of
    // `delta` of them. The month step here deliberately does NOT go through
    // CalAddMonths: that one clamps the day, which is exactly what breaks day
    // normalization (see the note in the header).
    d += delta;

    // Carry forward: while the day is past the end of its month, subtract that
    // month's length and advance one month.
    while (d > CalDaysInMonth(y, m)) {
        d -= CalDaysInMonth(y, m);
        if (++m > 12) { m = 1; ++y; }
    }

    // Borrow backward: while the day is before the 1st, step to the previous
    // month and add ITS length. The order matters — the length added must be the
    // previous month's, not the one we came from.
    while (d < 1) {
        if (--m < 1) { m = 12; --y; }
        d += CalDaysInMonth(y, m);
    }
}

// --- Calendar ----------------------------------------------------------------

Calendar::Calendar() {
    // Initialize to today.
    SYSTEMTIME st;
    GetLocalTime(&st);
    selYear_ = st.wYear;
    selMonth_ = st.wMonth;
    selDay_ = st.wDay;
    viewYear_ = selYear_;
    viewMonth_ = selMonth_;
    viewDay_ = selDay_;

    SetFocusable(true);
}

void Calendar::SetSelectedDate(int year, int month, int day) {
    // Reject a date the constraints forbid (min/max range, blackout dates),
    // programmatic caller or not.
    // The alternative — accept it and let Render fade it — would leave the control
    // reporting a selection the user can never reach by clicking, and would let a
    // stale SetSelectedDate silently defeat a min/max set afterwards.
    if (!IsDateEnabled(year, month, day)) return;

    const int oy = viewYear_, om = viewMonth_;
    selYear_ = year;
    selMonth_ = month;
    selDay_ = day;
    viewYear_ = year;
    viewMonth_ = month;
    viewDay_ = day;
    Invalidate();
    // Selecting a date in a month that is not the one on screen pages the view to
    // it — clicking a trailing spill-over cell does exactly that. The view really
    // did change, so it is reported; selecting within the visible month does not
    // reach the raise (NotifyViewChanged compares first).
    NotifyViewChanged(oy, om);
}

void Calendar::GetSelectedDate(int& year, int& month, int& day) const {
    year = selYear_;
    month = selMonth_;
    day = selDay_;
}

// Raise DisplayMonthChanged if the view moved off (oldYear, oldMonth). Every path
// that writes viewYear_/viewMonth_ ends here, so the "only on an actual change"
// rule is in one place rather than repeated at three call sites.
void Calendar::NotifyViewChanged(int oldYear, int oldMonth) {
    if (viewYear_ == oldYear && viewMonth_ == oldMonth) return;
    DisplayMonthChangedArgs args{viewYear_, viewMonth_};
    displayMonthChanged_.Raise(*this, args);
}

void Calendar::SetViewedMonth(int year, int month) {
    const int oy = viewYear_, om = viewMonth_;
    viewYear_ = year;
    viewMonth_ = month;
    Invalidate();
    NotifyViewChanged(oy, om);
}

void Calendar::PreviousMonth() {
    const int oy = viewYear_, om = viewMonth_;
    CalAddMonths(viewYear_, viewMonth_, viewDay_, -1);
    Invalidate();
    NotifyViewChanged(oy, om);
}

void Calendar::NextMonth() {
    const int oy = viewYear_, om = viewMonth_;
    CalAddMonths(viewYear_, viewMonth_, viewDay_, 1);
    Invalidate();
    NotifyViewChanged(oy, om);
}

// --- Date constraints: min/max range and blackout dates ----------------------

void Calendar::SetMinDate(int year, int month, int day) {
    hasMinDate_ = true;
    minYear_ = year;
    minMonth_ = month;
    minDay_ = day;
    Invalidate();
}

void Calendar::SetMaxDate(int year, int month, int day) {
    hasMaxDate_ = true;
    maxYear_ = year;
    maxMonth_ = month;
    maxDay_ = day;
    Invalidate();
}

void Calendar::ClearMinDate() {
    hasMinDate_ = false;
    Invalidate();
}

void Calendar::ClearMaxDate() {
    hasMaxDate_ = false;
    Invalidate();
}

void Calendar::GetMinDate(int& year, int& month, int& day) const {
    year = minYear_;
    month = minMonth_;
    day = minDay_;
}

void Calendar::GetMaxDate(int& year, int& month, int& day) const {
    year = maxYear_;
    month = maxMonth_;
    day = maxDay_;
}

void Calendar::SetBlackoutDates(const std::vector<std::tuple<int,int,int>>& dates) {
    blackoutDates_ = dates;
    std::sort(blackoutDates_.begin(), blackoutDates_.end());
    Invalidate();
}

void Calendar::ClearBlackoutDates() {
    blackoutDates_.clear();
    Invalidate();
}

bool Calendar::IsDateBlackedOut(int y, int m, int d) const {
    auto it = std::lower_bound(blackoutDates_.begin(), blackoutDates_.end(),
                               std::make_tuple(y, m, d));
    return it != blackoutDates_.end() && *it == std::make_tuple(y, m, d);
}

bool Calendar::IsDateEnabled(int y, int m, int d) const {
    // Check min/max bounds.
    if (hasMinDate_) {
        if (y < minYear_ || (y == minYear_ && m < minMonth_) ||
            (y == minYear_ && m == minMonth_ && d < minDay_)) {
            return false;
        }
    }
    if (hasMaxDate_) {
        if (y > maxYear_ || (y == maxYear_ && m > maxMonth_) ||
            (y == maxYear_ && m == maxMonth_ && d > maxDay_)) {
            return false;
        }
    }
    // Check blackout list.
    if (IsDateBlackedOut(y, m, d)) {
        return false;
    }
    return true;
}

void Calendar::CellDate(int i, int& y, int& m, int& d) const {
    if (i < 0 || i >= 42) {
        y = m = d = 0;
        return;
    }

    // The grid starts on the Sunday on or before the 1st of the viewed month, so
    // cell i is (1st of month) + (i - dow) days, where dow is the 1st's weekday.
    // CalAddDays handles both signs and every month/year boundary, so there is no
    // normalization loop here to get wrong (an earlier version normalized by hand
    // through CalAddMonths and produced Aug 31 for every cell past the overflow —
    // CalAddMonths clamps the day, which is right for month navigation and wrong
    // for day arithmetic. See the comment on CalAddDays).
    y = viewYear_;
    m = viewMonth_;
    d = 1;
    CalAddDays(y, m, d, i - CalDayOfWeek(viewYear_, viewMonth_, 1));
}

int Calendar::CellIndexOf(int y, int m, int d) const {
    for (int i = 0; i < 42; ++i) {
        int cy, cm, cd;
        CellDate(i, cy, cm, cd);
        if (cy == y && cm == m && cd == d) return i;
    }
    return -1;
}

RectDip Calendar::CellRect(int cellIndex) const {
    if (cellIndex < 0 || cellIndex >= 42) return RectDip();
    int row = cellIndex / 7;
    int col = cellIndex % 7;
    float x = bounds_.x + col * cellW_;
    float y = bounds_.y + navH_ + hdrH_ + row * cellH_;
    return RectDip(x, y, cellW_, cellH_);
}

int Calendar::HitTestCell(float dipX, float dipY) const {
    // dipX/dipY are control-relative.
    float gridY = dipY - navH_ - hdrH_;
    if (gridY < 0.0f) return -1;
    if (dipX < 0.0f || dipX >= 7 * cellW_) return -1;
    if (gridY >= 6 * cellH_) return -1;

    int col = static_cast<int>(dipX / cellW_);
    int row = static_cast<int>(gridY / cellH_);
    if (col < 0 || col >= 7 || row < 0 || row >= 6) return -1;
    return row * 7 + col;
}

namespace {
// Natural (minimum) cell metrics. The grid never shrinks below these: at 36x32 a
// two-digit day number plus the today ring is already tight.
constexpr float kNaturalCellW = 36.0f;
constexpr float kNaturalCellH = 32.0f;
constexpr int kCols = 7;
constexpr int kRows = 6;
}  // namespace

void Calendar::Measure(float availW, float availH) {
    UNREFERENCED_PARAMETER(availW);
    UNREFERENCED_PARAMETER(availH);
    // Desired size = the natural grid, unless the app pinned Width/Height. Stretch
    // beyond natural happens in Arrange: a parent that hands the calendar more room
    // (Grid star cell, SetWidth(360)) gets a proportionally larger grid instead of a
    // 252-wide grid pinned to the left edge of a 360-wide background — the old
    // behavior, where the nav arrows sat at the far edges and no longer lined up
    // with the first/last columns.
    const float natW = kCols * kNaturalCellW;
    const float natH = navH_ + hdrH_ + kRows * kNaturalCellH;
    SetDesired(SizeDip(IsAuto(width_) ? natW : width_,
                       IsAuto(height_) ? natH : height_));
}

void Calendar::Arrange(const RectDip& finalRect) {
    FrameworkElement::Arrange(finalRect);  // CRITICAL: sets animPrimed_ on first layout
    // Derive the cell size from the arranged box so paint (CellRect) and hit-test
    // (HitTestCell) always cover exactly the bounds. Floor at the natural size so a
    // too-small slot clips rather than crushing the digits — Render clips to bounds_
    // to make that true; the floor alone let the grid paint past the edge.
    cellW_ = std::max(kNaturalCellW, finalRect.w / kCols);
    cellH_ = std::max(kNaturalCellH, (finalRect.h - navH_ - hdrH_) / kRows);
}

void Calendar::Render(const DrawingContext& dc) {
    // Orchestration only: every piece of the paint lives in a protected virtual
    // hook (Calendar.h, "Extension points") so a subclass can replace one part
    // without re-implementing the rest. Keep this body free of drawing detail.
    const ColorTokens& colors = Theme().colors;
    const float corner = EffectiveCornerRadius();

    // Clip to bounds_. Arrange floors the cells at their natural size, so in a slot
    // narrower or shorter than that the grid extends past bounds_ — over the
    // neighbor, and outside the dirty rect (bounds_, no VisualOverflowDip), so a
    // hover or selection drawn there was never cleared and stayed as a ghost. The
    // subclass hooks below draw inside this clip too.
    ClipGuard clip = dc.PushClip(
        D2D1::RectF(bounds_.x, bounds_.y, bounds_.right(), bounds_.bottom()));

    // Background.
    D2D1_ROUNDED_RECT bg = D2D1::RoundedRect(
        D2D1::RectF(bounds_.x, bounds_.y, bounds_.right(), bounds_.bottom()),
        corner, corner);
    dc.FillRoundedRect(bg, EffectiveBackground(colors.cardFill));

    RenderNavBar(dc);
    RenderWeekdayHeader(dc);

    // Today is sampled once per frame, not per cell (42 GetLocalTime calls).
    SYSTEMTIME today;
    GetLocalTime(&today);

    for (int i = 0; i < 42; ++i) {
        CellInfo cell{};
        cell.index = i;
        CellDate(i, cell.year, cell.month, cell.day);
        cell.rect = CellRect(i);
        cell.inViewedMonth = (cell.year == viewYear_ && cell.month == viewMonth_);
        cell.isToday = (cell.year == today.wYear && cell.month == today.wMonth &&
                        cell.day == today.wDay);
        cell.isSelected = (cell.year == selYear_ && cell.month == selMonth_ &&
                           cell.day == selDay_);
        cell.isEnabled = IsDateEnabled(cell.year, cell.month, cell.day);
        cell.isHovered = (i == hoverCell_);
        RenderCell(dc, cell);
    }
}

void Calendar::OnPointerPressed(PointerEventArgs& e) {
    if (e.handled) return;

    float relX = e.position.x - bounds_.x;
    float relY = e.position.y - bounds_.y;

    // Navigation arrows: the same rects RenderNavBar paints into (Prev/NextArrowRect),
    // so the painted arrow and its hit zone cannot drift apart.
    if (PrevArrowRect().contains(e.position.x, e.position.y)) {
        PreviousMonth();
        e.handled = true;
        return;
    }
    if (NextArrowRect().contains(e.position.x, e.position.y)) {
        NextMonth();
        e.handled = true;
        return;
    }

    // Check day cells. A disabled date (outside min/max or blacked out) swallows
    // the click without changing the selection — the same "click lands, nothing
    // happens" feel as a disabled button, rather than letting the click fall
    // through to whatever is behind the calendar.
    int cell = HitTestCell(relX, relY);
    if (cell >= 0) {
        int y, m, d;
        CellDate(cell, y, m, d);
        if (IsDateEnabled(y, m, d)) {
            SetSelectedDate(y, m, d);
            RoutedEventArgs args;
            args.source = this;
            args.originalSource = this;
            selectionChanged_.Raise(*this, args);
        }
        e.handled = true;
    }
}

void Calendar::OnKeyDownRouted(KeyEventArgs& e) {
    if (e.handled) return;

    int delta = 0;
    switch (e.vk) {
    case VK_LEFT:  delta = -1; break;
    case VK_RIGHT: delta = 1; break;
    case VK_UP:    delta = -7; break;
    case VK_DOWN:  delta = 7; break;
    case VK_RETURN: {
        RoutedEventArgs args;
        args.source = this;
        args.originalSource = this;
        selectionChanged_.Raise(*this, args);
        e.handled = true;
        return;
    }
    default:
        return;
    }

    // Move selection by delta days. Skip over disabled dates — keep stepping
    // until we land on an enabled one (or hit a sanity limit to prevent infinite
    // loops when all dates are disabled). Same feel as skipping disabled items
    // in a ComboBox dropdown.
    int y = selYear_, m = selMonth_, d = selDay_;
    int steps = 0;
    const int MAX_STEPS = 365;  // sanity limit to prevent infinite loop
    do {
        CalAddDays(y, m, d, delta);
        steps++;
    } while (!IsDateEnabled(y, m, d) && steps < MAX_STEPS);

    // Only commit the move if we found an enabled date.
    if (IsDateEnabled(y, m, d)) {
        SetSelectedDate(y, m, d);
    }
    e.handled = true;
}

bool Calendar::WantsAnimationTick() const {
    float target = (hoverCell_ >= 0) ? 1.0f : 0.0f;
    return hoverFade_.Animating(target);
}

void Calendar::OnPointerMoved(PointerEventArgs& e) {
    if (e.handled) return;

    float relX = e.position.x - bounds_.x;
    float relY = e.position.y - bounds_.y;
    int cell = HitTestCell(relX, relY);

    // A disabled cell reads as "no hover" rather than "hovered but not painted".
    // Render already skips the highlight for a disabled date, but leaving
    // hoverCell_ pointing at one keeps WantsAnimationTick() true for a fade that
    // never becomes visible — a tick per frame while the pointer rests on a
    // blacked-out day, against the "idle costs zero" rule.
    if (cell >= 0) {
        int y, m, d;
        CellDate(cell, y, m, d);
        if (!IsDateEnabled(y, m, d)) cell = -1;
    }

    if (cell != hoverCell_) {
        hoverCell_ = cell;
        Invalidate();
    }
}

void Calendar::OnPointerLeft() {
    if (hoverCell_ >= 0) {
        hoverCell_ = -1;
        Invalidate();
    }
}

void Calendar::OnAnimationTick(float dtSec) {
    float target = (hoverCell_ >= 0) ? 1.0f : 0.0f;
    hoverFade_.Approach(target, dtSec, Theme().motion.tintTau);
    Invalidate();
}

// --- Extension-point implementations (protected virtual) ---------------------

RectDip Calendar::NavBarRect() const {
    return RectDip{bounds_.x, bounds_.y, bounds_.w, navH_};
}

RectDip Calendar::WeekdayHeaderRect() const {
    float y = bounds_.y + navH_;
    return RectDip{bounds_.x, y, bounds_.w, hdrH_};
}

RectDip Calendar::PrevArrowRect() const {
    return RectDip{bounds_.x, bounds_.y, 40.0f, navH_};
}

RectDip Calendar::NextArrowRect() const {
    return RectDip{bounds_.x + bounds_.w - 40.0f, bounds_.y, 40.0f, navH_};
}

void Calendar::RenderNavBar(const DrawingContext& dc) {
    const ColorTokens& colors = Theme().colors;
    float navY = bounds_.y;
    
    // Month/year title: "2026年8月"
    std::wostringstream oss;
    oss << viewYear_ << L"年" << viewMonth_ << L"月";
    std::wstring headerText = oss.str();
    
    if (DWriteContext* dw = Dwrite()) {
        if (auto* fmt = dw->Format(14.0f, DWRITE_FONT_WEIGHT_SEMI_BOLD,
                                   DWRITE_TEXT_ALIGNMENT_CENTER,
                                   DWRITE_PARAGRAPH_ALIGNMENT_CENTER)) {
            D2D1_RECT_F textRect = D2D1::RectF(
                bounds_.x + 40, navY,
                bounds_.right() - 40, navY + navH_);
            dc.DrawText(headerText.c_str(), static_cast<UINT32>(headerText.size()),
                       fmt, textRect, EffectiveForeground(colors.textPrimary));
        }
        
        // Navigation arrows
        if (auto* arrowFmt = dw->Format(16.0f, DWRITE_FONT_WEIGHT_NORMAL,
                                        DWRITE_TEXT_ALIGNMENT_CENTER,
                                        DWRITE_PARAGRAPH_ALIGNMENT_CENTER)) {
            const D2D1_COLOR_F arrowColor = EffectiveForeground(colors.textPrimary);
            
            RectDip prev = PrevArrowRect();
            D2D1_RECT_F prevRect = D2D1::RectF(prev.x, prev.y, prev.right(), prev.bottom());
            dc.DrawText(L"<", 1, arrowFmt, prevRect, arrowColor);
            
            RectDip next = NextArrowRect();
            D2D1_RECT_F nextRect = D2D1::RectF(next.x, next.y, next.right(), next.bottom());
            dc.DrawText(L">", 1, arrowFmt, nextRect, arrowColor);
        }
    }
}

void Calendar::RenderWeekdayHeader(const DrawingContext& dc) {
    const ColorTokens& colors = Theme().colors;
    const wchar_t* weekdays[] = {L"日", L"一", L"二", L"三", L"四", L"五", L"六"};
    float hdrY = bounds_.y + navH_;
    
    if (DWriteContext* dw = Dwrite()) {
        if (auto* fmt = dw->Format(12.0f, DWRITE_FONT_WEIGHT_NORMAL,
                                   DWRITE_TEXT_ALIGNMENT_CENTER,
                                   DWRITE_PARAGRAPH_ALIGNMENT_CENTER)) {
            const D2D1_COLOR_F weekdayColor = EffectiveForeground(colors.textSecondary);
            for (int col = 0; col < 7; ++col) {
                D2D1_RECT_F r = D2D1::RectF(
                    bounds_.x + col * cellW_, hdrY,
                    bounds_.x + (col + 1) * cellW_, hdrY + hdrH_);
                dc.DrawText(weekdays[col], 1, fmt, r, weekdayColor);
            }
        }
    }
}

void Calendar::RenderCell(const DrawingContext& dc, const CellInfo& cell) {
    const ColorTokens& colors = Theme().colors;
    
    D2D1_ROUNDED_RECT cellRR = D2D1::RoundedRect(
        D2D1::RectF(cell.rect.x, cell.rect.y,
                   cell.rect.right(), cell.rect.bottom()),
        4.0f, 4.0f);
    
    // Background for selected date (only if enabled)
    if (cell.isSelected && cell.isEnabled) {
        dc.FillRoundedRect(cellRR, colors.accent);
    }
    // Hover highlight (only on enabled dates)
    else if (cell.isEnabled && cell.isHovered && hoverFade_ > 0.01f) {
        D2D1_COLOR_F hoverColor = colors.controlFillHover;
        hoverColor.a *= static_cast<float>(hoverFade_);
        dc.FillRoundedRect(cellRR, hoverColor);
    }
    
    // Ring for today (when not selected). Inset by half the stroke so the whole
    // stroke lies inside the cell: the edge cells are flush with bounds_, and Render
    // clips there, so a stroke centered on the cell edge lost its outer half on the
    // last column and the bottom row.
    if (cell.isToday && !cell.isSelected && cell.isEnabled) {
        D2D1_ROUNDED_RECT ring = cellRR;
        ring.rect.left += 0.5f;  ring.rect.top += 0.5f;
        ring.rect.right -= 0.5f; ring.rect.bottom -= 0.5f;
        dc.DrawRoundedRect(ring, EffectiveAccentColor(colors.accent), 1.0f);
    }
    
    // Day number text
    std::wstring dayText = std::to_wstring(cell.day);
    D2D1_RECT_F textRect = D2D1::RectF(
        cell.rect.x, cell.rect.y,
        cell.rect.right(), cell.rect.bottom());
    
    // Text color: disabled dates are faded gray, others follow original logic
    D2D1_COLOR_F textColor;
    if (!cell.isEnabled) {
        textColor = colors.textSecondary;
        textColor.a *= 0.5f;  // 50% opacity for disabled
    } else if (HasForeground()) {
        textColor = EffectiveForeground();
    } else if (cell.isSelected) {
        textColor = D2D1::ColorF(D2D1::ColorF::White);
    } else if (cell.inViewedMonth) {
        textColor = colors.textPrimary;
    } else {
        textColor = colors.textSecondary;
    }
    
    if (DWriteContext* dw = Dwrite()) {
        if (auto* fmt = dw->Format(13.0f, DWRITE_FONT_WEIGHT_NORMAL,
                                   DWRITE_TEXT_ALIGNMENT_CENTER,
                                   DWRITE_PARAGRAPH_ALIGNMENT_CENTER)) {
            dc.DrawText(dayText.c_str(), static_cast<UINT32>(dayText.size()),
                       fmt, textRect, textColor);
        }
    }
    
    // Strikethrough for disabled dates
    if (!cell.isEnabled) {
        float midY = cell.rect.y + cell.rect.h * 0.5f;
        D2D1_COLOR_F strikeColor = colors.textSecondary;
        strikeColor.a *= 0.5f;
        dc.DrawLine(
            D2D1::Point2F(cell.rect.x + 6, midY),
            D2D1::Point2F(cell.rect.right() - 6, midY),
            strikeColor, 1.0f);
    }
}

} // namespace fluent
