// DatePicker.h — button that pops a Calendar for date selection.
//
// Displays the selected date as text ("2026/08/15"). Click opens a PopupHost with a
// Calendar; selecting a date in the calendar closes the popup and updates the
// button text. Keyboard arrows adjust the date by ±1 day while focused.
//
// Design rationale: DatePicker is a thin shell around Calendar. The calendar does
// all the heavy lifting (rendering, navigation, date arithmetic); this wraps it in
// a button + PopupHost (the same popup primitive ComboBox uses). Inherits ButtonBase
// for the standard click gesture (pointer + Space/Enter) and state animation.
#pragma once

#include "primitives/ButtonBase.h"
#include "../base/Event.h"
#include "../input/RoutedEvent.h"
#include "../core/Subscription.h"
#include <memory>
#include <string>

namespace fluent {

class Calendar;
class PopupHost;

class DatePicker : public ButtonBase {
public:
    DatePicker();
    ~DatePicker();

    // Selected date. Defaults to today at construction.
    void SetSelectedDate(int year, int month, int day);
    int SelectedYear() const { return year_; }
    int SelectedMonth() const { return month_; }
    int SelectedDay() const { return day_; }

    // Raised when the user selects a date (via the calendar or keyboard arrows).
    Event<DatePicker, RoutedEventArgs>& DateChanged() { return dateChanged_; }

    // Raised after the calendar popup opens / after it closes, by any path (click,
    // Space/Enter, a date pick, light dismiss, or a detach that closes it). Each
    // fires once per transition — the open/close helpers return early when the popup
    // is already in that state.
    Event<DatePicker, RoutedEventArgs>& CalendarOpened() { return calendarOpened_; }
    Event<DatePicker, RoutedEventArgs>& CalendarClosed() { return calendarClosed_; }

    void Measure(float availW, float availH) override;
    void Arrange(const RectDip& finalRect) override;
    void Render(const DrawingContext& dc) override;
    void OnKeyDownRouted(KeyEventArgs& e) override;

protected:
    void OnActivate() override;  // ButtonBase: click or Space/Enter
    void OnAttachedToTree() override;
    void OnDetachedFromTree() override;
    void OnThemeChanged() override;

private:
    void OpenCalendar();
    void CloseCalendar();
    void AdjustDate(int daysDelta);

    int year_  = 2000;
    int month_ = 1;
    int day_   = 1;

    // Popup infrastructure (ComboBox pattern: PopupHost + content element).
    std::unique_ptr<PopupHost> popup_;
    std::shared_ptr<Calendar> calendar_;
    // Calendar.SelectionChanged, held for the CALENDAR's lifetime rather than as a
    // context subscription. A context subscription is released on detach but was only
    // made when popup_ was created, so a re-attached picker (TabControl page switch)
    // opened a calendar wired to nothing. Declared after calendar_ so it unregisters
    // before the calendar is destroyed.
    Subscription calendarSelectionSub_;
    bool popupOpen_ = false;
    WindowServices* window_ = nullptr;

    Event<DatePicker, RoutedEventArgs> dateChanged_;
    Event<DatePicker, RoutedEventArgs> calendarOpened_;
    Event<DatePicker, RoutedEventArgs> calendarClosed_;
};

} // namespace fluent
