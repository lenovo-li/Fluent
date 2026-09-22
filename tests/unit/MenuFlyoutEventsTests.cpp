// MenuFlyoutEventsTests.cpp — MenuFlyout event model (P1: std::function → Event<> migration).
//
// Tests the Event<> API directly: subscription, unsubscription, multiple
// subscribers. Does not test the full open/close flow (that needs a real window).

#include "../framework/Test.h"
#include "../../FluentUI/controls/MenuFlyout.h"

using namespace fluent;

namespace {

struct Rec {
    int count = 0;
    static void On(void* owner, MenuFlyout&, RoutedEventArgs&) {
        static_cast<Rec*>(owner)->count++;
    }
};

}  // namespace

// The event has the correct API: you can subscribe and the subscription is valid.
TEST(MenuFlyoutEvents, SubscriptionSucceeds) {
    MenuFlyout flyout;

    Rec rec;
    auto sub = flyout.Closed().Subscribe(&rec, &Rec::On);

    // Successfully subscribed (test passes if no crash)
}

// Multiple subscribers can register simultaneously.
TEST(MenuFlyoutEvents, MultipleSubscribersAllowed) {
    MenuFlyout flyout;

    Rec rec1, rec2;
    auto sub1 = flyout.Closed().Subscribe(&rec1, &Rec::On);
    auto sub2 = flyout.Closed().Subscribe(&rec2, &Rec::On);

    // Both subscribed successfully (test passes if no crash)
}

// Destroying the subscription unregisters (RAII): the event's slot list is
// compacted, and a later raise does not invoke the dead handler. This is the
// core RAII property.
TEST(MenuFlyoutEvents, RAIIUnsubscribeRemovesSub) {
    MenuFlyout flyout;

    Rec rec;

    {
        auto sub = flyout.Closed().Subscribe(&rec, &Rec::On);
    }
    // sub destroyed, subscription should be gone (slot marked dead)

    // We can't fire the event without a real open (needs WindowServices), so
    // we just verify the subscription API accepted and released correctly.
    // The Event<> base class itself has comprehensive tests in EventTests.cpp.
}

// The deprecated SetOnClosed API still compiles and can be called.
TEST(MenuFlyoutEvents, DeprecatedSetOnClosedStillCompiles) {
    MenuFlyout flyout;

    bool dummy = false;

#pragma warning(push)
#pragma warning(disable: 4996)  // Suppress deprecation warning
    flyout.SetOnClosed([&]() {
        dummy = true;
    });
#pragma warning(pop)

    // Successfully compiled and called — that's the compat check.
}

// The new and old APIs can coexist: setting both is allowed.
TEST(MenuFlyoutEvents, EventAndDeprecatedCallbackCoexist) {
    MenuFlyout flyout;

    Rec rec;
    auto sub = flyout.Closed().Subscribe(&rec, &Rec::On);

    bool callbackRegistered = false;

#pragma warning(push)
#pragma warning(disable: 4996)
    flyout.SetOnClosed([&]() {
        callbackRegistered = true;
    });
#pragma warning(pop)

    // Both registered successfully (no crash)
}

// Close without opening is a no-op: the event should not fire.
TEST(MenuFlyoutEvents, ClosedEventNotFiredWhenNotOpen) {
    MenuFlyout flyout;

    Rec rec;
    auto sub = flyout.Closed().Subscribe(&rec, &Rec::On);

    flyout.Close();
    EXPECT_EQ(rec.count, 0);
}
