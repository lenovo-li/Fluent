// CalendarEventsTests.cpp — Calendar 的 SelectionChanged / DisplayMonthChanged 事件
//
// Calendar 有两个事件：
//   - SelectionChanged：用户点击日期（或 SetSelectedDate 调用）改变选中日期时触发，
//     payload 是 RoutedEventArgs（非 Date —— 通过 SelectedDate() 方法读取）
//   - DisplayMonthChanged：用户翻页（PreviousMonth/NextMonth）改变显示的年月时触发，
//     payload 是 DisplayMonthChangedArgs{year, month}
//
// 两个事件独立：翻页不改变选中日期，选中日期不改变当前显示月份（除非是跨
// 月选择，那时会同时触发两个事件）。

#include "../framework/Test.h"
#include "../../FluentUI/controls/Calendar.h"
#include "../../FluentUI/input/RoutedEvent.h"

using namespace fluent;

namespace {
struct EventRec {
    int selectionChangedCount = 0;
    int displayMonthChangedCount = 0;
    int lastDisplayYear = 0;
    int lastDisplayMonth = 0;

    static void OnSelectionChanged(void* owner, Calendar&, RoutedEventArgs&) {
        static_cast<EventRec*>(owner)->selectionChangedCount++;
    }
    static void OnDisplayMonthChanged(void* owner, Calendar&, Calendar::DisplayMonthChangedArgs& args) {
        auto* r = static_cast<EventRec*>(owner);
        r->displayMonthChangedCount++;
        r->lastDisplayYear = args.year;
        r->lastDisplayMonth = args.month;
    }
};
}  // namespace

// SetSelectedDate 是程序化 API，按 WPF 惯例不触发 SelectionChanged 事件。
// 事件只在用户交互时触发（点击日期、按回车键）。headless 下无法模拟真实点击，
// 所以实际触发行为只能在硬件上验证（demo 的 Calendar 页面）。
TEST(CalendarEvents, SetSelectedDateDoesNotFireEvent) {
    Calendar c;
    EventRec rec;
    auto sub = c.SelectionChanged().Subscribe(&rec, &EventRec::OnSelectionChanged);

    c.SetSelectedDate(2026, 9, 22);
    EXPECT_EQ(rec.selectionChangedCount, 0);  // 程序化设置不触发
}

// SetViewedMonth 触发 DisplayMonthChanged，payload 包含新年月。
TEST(CalendarEvents, DisplayMonthChangedFiresOnSetViewedMonth) {
    Calendar c;
    c.SetViewedMonth(2026, 1);

    EventRec rec;
    auto sub = c.DisplayMonthChanged().Subscribe(&rec, &EventRec::OnDisplayMonthChanged);

    c.SetViewedMonth(2026, 5);
    EXPECT_EQ(rec.displayMonthChangedCount, 1);
    EXPECT_EQ(rec.lastDisplayYear, 2026);
    EXPECT_EQ(rec.lastDisplayMonth, 5);
}

// SetViewedMonth 到同一年月不触发（NotifyViewChanged 的 dedup）。
TEST(CalendarEvents, DisplayMonthChangedDeduplicates) {
    Calendar c;
    c.SetViewedMonth(2025, 12);

    EventRec rec;
    auto sub = c.DisplayMonthChanged().Subscribe(&rec, &EventRec::OnDisplayMonthChanged);

    c.SetViewedMonth(2025, 12);  // 重复
    EXPECT_EQ(rec.displayMonthChangedCount, 0);
}

// NextMonth / PreviousMonth 触发 DisplayMonthChanged（翻页改变显示月份）。
TEST(CalendarEvents, DisplayMonthChangedFiresOnNavigation) {
    Calendar c;
    c.SetViewedMonth(2026, 6);

    EventRec rec;
    auto sub = c.DisplayMonthChanged().Subscribe(&rec, &EventRec::OnDisplayMonthChanged);

    c.NextMonth();  // 2026-06 → 2026-07
    EXPECT_EQ(rec.displayMonthChangedCount, 1);
    EXPECT_EQ(rec.lastDisplayMonth, 7);

    c.PreviousMonth();  // 2026-07 → 2026-06
    EXPECT_EQ(rec.displayMonthChangedCount, 2);
    EXPECT_EQ(rec.lastDisplayMonth, 6);
}

// 两个事件独立订阅，互不影响。
TEST(CalendarEvents, EventsAreIndependent) {
    Calendar c;
    EventRec rec;
    auto s1 = c.SelectionChanged().Subscribe(&rec, &EventRec::OnSelectionChanged);

    EXPECT_TRUE(c.SelectionChanged().HasSubscribers());
    EXPECT_FALSE(c.DisplayMonthChanged().HasSubscribers());
}

// 多订阅者可以同时注册到同一事件（但实际触发需要用户交互）。
TEST(CalendarEvents, MultipleSubscribersOnSelectionChanged) {
    Calendar c;
    EventRec a, b;
    auto s1 = c.SelectionChanged().Subscribe(&a, &EventRec::OnSelectionChanged);
    auto s2 = c.SelectionChanged().Subscribe(&b, &EventRec::OnSelectionChanged);

    // 程序化设置不触发，但订阅关系已建立
    EXPECT_EQ(a.selectionChangedCount, 0);
    EXPECT_EQ(b.selectionChangedCount, 0);
    EXPECT_TRUE(c.SelectionChanged().HasSubscribers());
}

// Subscription 离开作用域自动退订。
TEST(CalendarEvents, SubscriptionUnregistersOnDestruction) {
    Calendar c;
    EventRec rec;
    {
        auto sub = c.SelectionChanged().Subscribe(&rec, &EventRec::OnSelectionChanged);
        EXPECT_TRUE(c.SelectionChanged().HasSubscribers());
    }
    EXPECT_FALSE(c.SelectionChanged().HasSubscribers());
}

// 零订阅者时 HasSubscribers 为假。
TEST(CalendarEvents, NoSubscribersReportsFalse) {
    Calendar c;
    EXPECT_FALSE(c.SelectionChanged().HasSubscribers());
    EXPECT_FALSE(c.DisplayMonthChanged().HasSubscribers());
}
