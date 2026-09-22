// MenuItemEventsTests.cpp — MenuItem.Invoked() (event_completion_plan §4, 方案B).
//
// MenuItem went from a POD struct with a public `std::function<void()> onInvoke`
// field to a class owning an Event<>. The interesting property is not "does the
// event fire" but the one that makes the change safe at all:
//
//   Event<> is non-movable because subscriptions capture its address. A menu is
//   authored as std::vector<MenuItem>, which relocates its elements on growth and
//   on push_back(std::move(...)). So the event lives behind a unique_ptr, and the
//   address a Subscription remembers must survive every one of those moves.
//
// AddressStableAcrossVectorGrowth and MovedItemKeepsItsHandler are the tests that
// pin that down. Without the indirection they do not crash — the subscription just
// silently stops firing, which is exactly the failure mode worth a regression test.

#include "../framework/Test.h"
#include "../../FluentUI/controls/MenuFlyout.h"

#include <vector>

using namespace fluent;

namespace {

struct Rec {
    int count = 0;
    MenuItem* lastSender = nullptr;
    static void On(void* owner, MenuItem& sender, RoutedEventArgs&) {
        auto* r = static_cast<Rec*>(owner);
        r->count++;
        r->lastSender = &sender;
    }
};

}  // namespace

TEST(MenuItemEvents, InvokedRaisesToSubscriber) {
    MenuItem item;
    item.text = L"New";

    Rec rec;
    auto sub = item.Invoked().Subscribe(&rec, &Rec::On);

    item.RaiseInvoked();
    EXPECT_EQ(rec.count, 1);
    // The sender is the item itself, not a copy — a handler may read its text/state.
    EXPECT_EQ(rec.lastSender, &item);
}

// The single `std::function` field could hold exactly one callback; the whole point
// of the migration is that a framework default and an app handler can coexist.
TEST(MenuItemEvents, MultipleSubscribersAllFire) {
    MenuItem item;
    Rec a, b;
    auto s1 = item.Invoked().Subscribe(&a, &Rec::On);
    auto s2 = item.Invoked().Subscribe(&b, &Rec::On);

    item.RaiseInvoked();
    EXPECT_EQ(a.count, 1);
    EXPECT_EQ(b.count, 1);
}

TEST(MenuItemEvents, DroppedSubscriptionStopsFiring) {
    MenuItem item;
    Rec rec;
    {
        auto sub = item.Invoked().Subscribe(&rec, &Rec::On);
        item.RaiseInvoked();
        EXPECT_EQ(rec.count, 1);
    }
    item.RaiseInvoked();
    EXPECT_EQ(rec.count, 1);   // unchanged: RAII unsubscribed
}

// Nothing subscribed: RaiseInvoked must be a cheap no-op, not an allocation or a
// null deref. This is the common case — most menu rows have no handler at all.
TEST(MenuItemEvents, RaiseWithNoSubscriberIsNoOp) {
    MenuItem item;
    EXPECT_FALSE(item.HasInvokeHandler());
    item.RaiseInvoked();        // must not crash
    EXPECT_FALSE(item.HasInvokeHandler());
}

// THE load-bearing test. A vector reallocates when it grows past capacity; each
// growth moves every MenuItem. A subscription taken before the growth must still
// fire after it. If the Event<> were held by value, the remembered address would
// point into freed storage and the handler would silently never run.
TEST(MenuItemEvents, AddressStableAcrossVectorGrowth) {
    std::vector<MenuItem> items;
    items.reserve(1);           // force a reallocation on the second push_back

    MenuItem first;
    first.text = L"First";
    items.push_back(std::move(first));

    Rec rec;
    auto sub = items[0].Invoked().Subscribe(&rec, &Rec::On);

    // Grow well past the initial capacity: several reallocations, each one moving
    // items[0] to fresh storage.
    for (int i = 0; i < 32; ++i) {
        MenuItem filler;
        filler.text = L"Filler";
        items.push_back(std::move(filler));
    }

    items[0].RaiseInvoked();
    EXPECT_EQ(rec.count, 1);
    EXPECT_EQ(rec.lastSender, &items[0]);
}

// Same invariant, isolated to a single move rather than a vector reallocation.
TEST(MenuItemEvents, MovedItemKeepsItsHandler) {
    MenuItem source;
    source.text = L"Cut";

    Rec rec;
    auto sub = source.Invoked().Subscribe(&rec, &Rec::On);

    MenuItem moved = std::move(source);
    moved.RaiseInvoked();

    EXPECT_EQ(rec.count, 1);
    EXPECT_EQ(rec.lastSender, &moved);
}

// AddInvokeHandler is the authoring convenience that replaced `onInvoke = [...]`.
// It owns the closure, so the caller stores no Subscription.
TEST(MenuItemEvents, AddInvokeHandlerRunsWithoutStoredSubscription) {
    MenuItem item;
    int calls = 0;
    item.AddInvokeHandler([&calls] { calls++; });

    EXPECT_TRUE(item.HasInvokeHandler());
    item.RaiseInvoked();
    EXPECT_EQ(calls, 1);
}

// Unlike the old single field (where a second assignment overwrote the first),
// AddInvokeHandler is additive.
TEST(MenuItemEvents, AddInvokeHandlerIsAdditive) {
    MenuItem item;
    int a = 0, b = 0;
    item.AddInvokeHandler([&a] { a++; });
    item.AddInvokeHandler([&b] { b++; });

    item.RaiseInvoked();
    EXPECT_EQ(a, 1);
    EXPECT_EQ(b, 1);
}

// AddInvokeHandler's closures live in a heap node for the same address-stability
// reason as the event itself, so they must survive a move too.
TEST(MenuItemEvents, AddInvokeHandlerSurvivesMove) {
    MenuItem source;
    int calls = 0;
    source.AddInvokeHandler([&calls] { calls++; });

    std::vector<MenuItem> items;
    items.push_back(std::move(source));
    for (int i = 0; i < 16; ++i) items.push_back(MenuItem{});   // force reallocation

    items[0].RaiseInvoked();
    EXPECT_EQ(calls, 1);
}

// Sep() items are non-interactive dividers; they are pushed into the same vector as
// real rows, so they must be movable and safe to raise (MenuFlyout never invokes one,
// but nothing should break if it did).
TEST(MenuItemEvents, SeparatorIsMovableAndInert) {
    std::vector<MenuItem> items;
    items.push_back(MenuItem::Sep());

    EXPECT_TRUE(items[0].separator);
    EXPECT_FALSE(items[0].Selectable());
    EXPECT_FALSE(items[0].HasInvokeHandler());
    items[0].RaiseInvoked();    // must not crash
}
