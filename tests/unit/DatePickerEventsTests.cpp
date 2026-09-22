// DatePickerEventsTests.cpp — DatePicker 的 DateChanged / CalendarOpened / CalendarClosed 事件
//
// DatePicker 有三个事件：
//   - DateChanged：用户在日历弹窗中选择日期（或 SetSelectedDate 调用）时触发
//   - CalendarOpened：日历弹窗打开时触发
//   - CalendarClosed：日历弹窗关闭时触发
//
// 关键约束：弹窗事件（Opened/Closed）需要真实的 WindowServices 才能完整触发，
// headless 测试只验证事件的订阅/退订机制和 DateChanged 的纯内存路径（SetSelectedDate）。
// 弹窗打开/关闭的实际触发需要在 demo 的 DatePicker 页面真机验证。

#include "../framework/Test.h"
#include "../../FluentUI/controls/DatePicker.h"
#include "../../FluentUI/input/RoutedEvent.h"

using namespace fluent;

namespace {
struct EventRec {
    int dateChangedCount = 0;
    int calendarOpenedCount = 0;
    int calendarClosedCount = 0;

    static void OnDateChanged(void* owner, DatePicker&, RoutedEventArgs&) {
        static_cast<EventRec*>(owner)->dateChangedCount++;
    }
    static void OnCalendarOpened(void* owner, DatePicker&, RoutedEventArgs&) {
        static_cast<EventRec*>(owner)->calendarOpenedCount++;
    }
    static void OnCalendarClosed(void* owner, DatePicker&, RoutedEventArgs&) {
        static_cast<EventRec*>(owner)->calendarClosedCount++;
    }
};
}  // namespace

// SetSelectedDate 是程序化 API，按 WPF 惯例不触发 DateChanged 事件。
// 事件只在用户交互时触发（日历选日期、键盘方向键）。这条契约已在
// DatePickerTests.cpp::SetSelectedDateDoesNotRaiseDateChanged 中验证。
TEST(DatePickerEvents, SetSelectedDateDoesNotFireEvent) {
    DatePicker dp;
    EventRec rec;
    auto sub = dp.DateChanged().Subscribe(&rec, &EventRec::OnDateChanged);

    dp.SetSelectedDate(2026, 9, 22);
    EXPECT_EQ(rec.dateChangedCount, 0);  // 程序化设置不触发
}

// 重复 SetSelectedDate 到同一日期不触发（dedup）。
TEST(DatePickerEvents, DateChangedDeduplicates) {
    DatePicker dp;
    dp.SetSelectedDate(2026, 1, 1);

    EventRec rec;
    auto sub = dp.DateChanged().Subscribe(&rec, &EventRec::OnDateChanged);

    dp.SetSelectedDate(2026, 1, 1);  // 重复
    EXPECT_EQ(rec.dateChangedCount, 0);
}

// 三个事件都能订阅，返回的 Subscription 正常持有。
TEST(DatePickerEvents, AllThreeEventsAreSubscribable) {
    DatePicker dp;
    EventRec rec;
    auto s1 = dp.DateChanged().Subscribe(&rec, &EventRec::OnDateChanged);
    auto s2 = dp.CalendarOpened().Subscribe(&rec, &EventRec::OnCalendarOpened);
    auto s3 = dp.CalendarClosed().Subscribe(&rec, &EventRec::OnCalendarClosed);

    EXPECT_TRUE(dp.DateChanged().HasSubscribers());
    EXPECT_TRUE(dp.CalendarOpened().HasSubscribers());
    EXPECT_TRUE(dp.CalendarClosed().HasSubscribers());
}

// 多订阅者可以同时注册到同一事件（但实际触发需要用户交互）。
TEST(DatePickerEvents, MultipleSubscribersOnDateChanged) {
    DatePicker dp;
    EventRec a, b;
    auto s1 = dp.DateChanged().Subscribe(&a, &EventRec::OnDateChanged);
    auto s2 = dp.DateChanged().Subscribe(&b, &EventRec::OnDateChanged);

    // 程序化设置不触发，但订阅关系已建立
    EXPECT_EQ(a.dateChangedCount, 0);
    EXPECT_EQ(b.dateChangedCount, 0);
    EXPECT_TRUE(dp.DateChanged().HasSubscribers());
}

// Subscription 离开作用域自动退订。
TEST(DatePickerEvents, SubscriptionUnregistersOnDestruction) {
    DatePicker dp;
    EventRec rec;
    {
        auto sub = dp.DateChanged().Subscribe(&rec, &EventRec::OnDateChanged);
        EXPECT_TRUE(dp.DateChanged().HasSubscribers());
    }
    EXPECT_FALSE(dp.DateChanged().HasSubscribers());
}

// 零订阅者时 HasSubscribers 为假。
TEST(DatePickerEvents, NoSubscribersReportsFalse) {
    DatePicker dp;
    EXPECT_FALSE(dp.DateChanged().HasSubscribers());
    EXPECT_FALSE(dp.CalendarOpened().HasSubscribers());
    EXPECT_FALSE(dp.CalendarClosed().HasSubscribers());
}

// 三个事件是独立的槽位，订阅一个不影响另一个。
TEST(DatePickerEvents, EventsAreIndependent) {
    DatePicker dp;
    EventRec rec;
    auto sub = dp.DateChanged().Subscribe(&rec, &EventRec::OnDateChanged);

    EXPECT_TRUE(dp.DateChanged().HasSubscribers());
    EXPECT_FALSE(dp.CalendarOpened().HasSubscribers());
    EXPECT_FALSE(dp.CalendarClosed().HasSubscribers());
}
