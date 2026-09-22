// ComboBoxEventsTests.cpp — ComboBox 的 SelectionChanged / DropDownOpened / DropDownClosed 事件
//
// ComboBox 继承 Selector<wstring>，Selector 提供 SelectionChanged() Event。
// ComboBox 新增两个弹出状态事件：DropDownOpened / DropDownClosed（WPF parity）。
//
// 关键约束：弹出窗口需要真实的 WindowServices，headless 测试只能验证事件
// 的订阅/退订机制和 SelectionChanged 的触发（SetSelectedIndex 走纯内存路径）。
// DropDownOpened / DropDownClosed 的触发需要真机验证（demo 的 ComboBox 页面）。

#include "../framework/Test.h"
#include "../../FluentUI/controls/ComboBox.h"
#include "../../FluentUI/input/RoutedEvent.h"

using namespace fluent;

namespace {
struct EventRec {
    int selectionChangedCount = 0;
    int lastSelectedIndex = -999;
    int dropDownOpenedCount = 0;
    int dropDownClosedCount = 0;

    static void OnSelectionChanged(void* owner, ComboBox&, int& index) {
        auto* r = static_cast<EventRec*>(owner);
        r->selectionChangedCount++;
        r->lastSelectedIndex = index;
    }
    static void OnDropDownOpened(void* owner, ComboBox&, RoutedEventArgs&) {
        static_cast<EventRec*>(owner)->dropDownOpenedCount++;
    }
    static void OnDropDownClosed(void* owner, ComboBox&, RoutedEventArgs&) {
        static_cast<EventRec*>(owner)->dropDownClosedCount++;
    }
};
}  // namespace

// SelectionChanged 在调用 SetSelectedIndex 后触发，payload 是新索引。
TEST(ComboBoxEvents, SelectionChangedFiresOnSetSelectedIndex) {
    ComboBox cb;
    cb.SetItems({L"Alpha", L"Beta", L"Gamma"});

    EventRec rec;
    auto sub = cb.SelectionChanged().Subscribe(&rec, &EventRec::OnSelectionChanged);

    cb.SetSelectedIndex(1);
    EXPECT_EQ(rec.selectionChangedCount, 1);
    EXPECT_EQ(rec.lastSelectedIndex, 1);
}

// SetSelectedIndex 到同一索引不触发事件（Selector 的 dedup）。
TEST(ComboBoxEvents, SelectionChangedDeduplicates) {
    ComboBox cb;
    cb.SetItems({L"One", L"Two"});
    cb.SetSelectedIndex(0);

    EventRec rec;
    auto sub = cb.SelectionChanged().Subscribe(&rec, &EventRec::OnSelectionChanged);

    cb.SetSelectedIndex(0);  // 重复设置
    EXPECT_EQ(rec.selectionChangedCount, 0);
}

// 三个事件都能订阅，返回的 Subscription 正常持有。
TEST(ComboBoxEvents, AllThreeEventsAreSubscribable) {
    ComboBox cb;
    EventRec rec;
    auto s1 = cb.SelectionChanged().Subscribe(&rec, &EventRec::OnSelectionChanged);
    auto s2 = cb.DropDownOpened().Subscribe(&rec, &EventRec::OnDropDownOpened);
    auto s3 = cb.DropDownClosed().Subscribe(&rec, &EventRec::OnDropDownClosed);

    EXPECT_TRUE(cb.SelectionChanged().HasSubscribers());
    EXPECT_TRUE(cb.DropDownOpened().HasSubscribers());
    EXPECT_TRUE(cb.DropDownClosed().HasSubscribers());
}

// 多订阅者可以同时注册到同一事件（Event<> 的多播能力）。
TEST(ComboBoxEvents, MultipleSubscribersOnSelectionChanged) {
    ComboBox cb;
    cb.SetItems({L"Item1", L"Item2"});

    EventRec a, b;
    auto s1 = cb.SelectionChanged().Subscribe(&a, &EventRec::OnSelectionChanged);
    auto s2 = cb.SelectionChanged().Subscribe(&b, &EventRec::OnSelectionChanged);

    cb.SetSelectedIndex(1);
    EXPECT_EQ(a.selectionChangedCount, 1);
    EXPECT_EQ(b.selectionChangedCount, 1);
    EXPECT_EQ(a.lastSelectedIndex, 1);
    EXPECT_EQ(b.lastSelectedIndex, 1);
}

// Subscription 是 RAII：离开作用域即退订。
TEST(ComboBoxEvents, SubscriptionUnregistersOnDestruction) {
    ComboBox cb;
    EventRec rec;
    {
        auto sub = cb.SelectionChanged().Subscribe(&rec, &EventRec::OnSelectionChanged);
        EXPECT_TRUE(cb.SelectionChanged().HasSubscribers());
    }
    EXPECT_FALSE(cb.SelectionChanged().HasSubscribers());
}

// 零订阅者时 HasSubscribers 为假。
TEST(ComboBoxEvents, NoSubscribersReportsFalse) {
    ComboBox cb;
    EXPECT_FALSE(cb.SelectionChanged().HasSubscribers());
    EXPECT_FALSE(cb.DropDownOpened().HasSubscribers());
    EXPECT_FALSE(cb.DropDownClosed().HasSubscribers());
}

// SetItems 改变列表不触发 SelectionChanged（它不改变当前选中索引的值，
// 只是可能让一个已选中的索引越界 —— Selector 会 clamp 到 -1，但那是
// silent coerce，不算用户操作）。
TEST(ComboBoxEvents, SetItemsDoesNotFireSelectionChanged) {
    ComboBox cb;
    cb.SetItems({L"A", L"B"});
    cb.SetSelectedIndex(1);

    EventRec rec;
    auto sub = cb.SelectionChanged().Subscribe(&rec, &EventRec::OnSelectionChanged);

    cb.SetItems({L"X", L"Y", L"Z"});  // 列表变了，但选中索引的【值】没变
    EXPECT_EQ(rec.selectionChangedCount, 0);
}

// 三个事件是独立的槽位，订阅一个不影响另一个。
TEST(ComboBoxEvents, EventsAreIndependent) {
    ComboBox cb;
    EventRec rec;
    auto sub = cb.SelectionChanged().Subscribe(&rec, &EventRec::OnSelectionChanged);

    EXPECT_TRUE(cb.SelectionChanged().HasSubscribers());
    EXPECT_FALSE(cb.DropDownOpened().HasSubscribers());
    EXPECT_FALSE(cb.DropDownClosed().HasSubscribers());
}
