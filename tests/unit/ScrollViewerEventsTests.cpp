// ScrollViewerEventsTests.cpp — ScrollViewer.ScrollChanged.
//
// Covers the direct setters, both axes, the "no event when the offset did not move"
// case, and the animation path — which is the one with a performance cliff on it:
// Tick() advances the offset every frame, so a naive implementation would raise
// dozens of events for one smooth scroll. The AnimationRaisesOncePerSettle test is
// what pins that down.

#include "../framework/Test.h"
#include "../../FluentUI/layout/ScrollViewer.h"

using namespace fluent;

namespace {
struct Rec {
    std::vector<ScrollChangedArgs> seen;
    static void On(void* owner, ScrollViewer&, ScrollChangedArgs& a) {
        static_cast<Rec*>(owner)->seen.push_back(a);
    }
};

// A ScrollViewer with a live vertical axis: 1000 DIP of content in a 300 DIP box.
void SetUpVertical(ScrollViewer& sv) {
    sv.Arrange({0, 0, 400.0f, 300.0f});
    sv.SetContentHeight(1000.0f);
}
}  // namespace

TEST(ScrollViewerEvents, SetOffsetRaisesWithOldAndNew) {
    ScrollViewer sv;
    SetUpVertical(sv);

    Rec rec;
    auto sub = sv.ScrollChanged().Subscribe(&rec, &Rec::On);

    sv.SetOffset(100.0f);
    EXPECT_EQ(rec.seen.size(), size_t{1});
    EXPECT_EQ(rec.seen[0].offsetOld, 0.0f);
    EXPECT_EQ(rec.seen[0].offsetNew, 100.0f);
    EXPECT_EQ(rec.seen[0].offsetXOld, 0.0f);
    EXPECT_EQ(rec.seen[0].offsetXNew, 0.0f);
}

TEST(ScrollViewerEvents, ScrollByRaisesRelativeToCurrent) {
    ScrollViewer sv;
    SetUpVertical(sv);
    sv.SetOffset(50.0f);

    Rec rec;
    auto sub = sv.ScrollChanged().Subscribe(&rec, &Rec::On);

    sv.ScrollBy(25.0f);
    EXPECT_EQ(rec.seen.size(), size_t{1});
    EXPECT_EQ(rec.seen[0].offsetOld, 50.0f);
    EXPECT_EQ(rec.seen[0].offsetNew, 75.0f);
}

// A set that lands on the current offset is not a scroll. Without this, a caller
// that re-asserts the same offset each frame (a sync-two-scrollers handler) would
// raise forever and, with two viewers subscribed to each other, never terminate.
TEST(ScrollViewerEvents, NoRaiseWhenOffsetUnchanged) {
    ScrollViewer sv;
    SetUpVertical(sv);
    sv.SetOffset(100.0f);

    Rec rec;
    auto sub = sv.ScrollChanged().Subscribe(&rec, &Rec::On);

    sv.SetOffset(100.0f);
    EXPECT_EQ(rec.seen.size(), size_t{0});
    sv.ScrollBy(0.0f);
    EXPECT_EQ(rec.seen.size(), size_t{0});
}

// Clamping happens before the comparison, so scrolling past the end raises once
// (0 -> MaxOffset) and then stops, rather than once per attempt.
TEST(ScrollViewerEvents, ClampedOffsetRaisesOnlyWhileItMoves) {
    ScrollViewer sv;
    SetUpVertical(sv);
    const float maxOff = sv.MaxOffset();   // 1000 - 300

    Rec rec;
    auto sub = sv.ScrollChanged().Subscribe(&rec, &Rec::On);

    sv.SetOffset(99999.0f);
    EXPECT_EQ(rec.seen.size(), size_t{1});
    EXPECT_EQ(rec.seen[0].offsetNew, maxOff);

    sv.SetOffset(99999.0f);               // already pinned at the end
    EXPECT_EQ(rec.seen.size(), size_t{1});
}

TEST(ScrollViewerEvents, SetOffsetXRaisesHorizontalAxis) {
    ScrollViewer sv;
    sv.Arrange({0, 0, 400.0f, 300.0f});
    sv.SetContentWidth(2000.0f);

    Rec rec;
    auto sub = sv.ScrollChanged().Subscribe(&rec, &Rec::On);

    sv.SetOffsetX(150.0f);
    EXPECT_EQ(rec.seen.size(), size_t{1});
    EXPECT_EQ(rec.seen[0].offsetXOld, 0.0f);
    EXPECT_EQ(rec.seen[0].offsetXNew, 150.0f);
    EXPECT_EQ(rec.seen[0].offsetOld, 0.0f);
    EXPECT_EQ(rec.seen[0].offsetNew, 0.0f);
}

TEST(ScrollViewerEvents, BothAxesMove) {
    ScrollViewer sv;
    sv.Arrange({0, 0, 400.0f, 300.0f});
    sv.SetContentHeight(1000.0f);
    sv.SetContentWidth(2000.0f);

    Rec rec;
    auto sub = sv.ScrollChanged().Subscribe(&rec, &Rec::On);

    sv.SetOffset(80.0f);
    sv.SetOffsetX(120.0f);
    EXPECT_EQ(rec.seen.size(), size_t{2});
    EXPECT_EQ(rec.seen[0].offsetNew, 80.0f);
    EXPECT_EQ(rec.seen[1].offsetXNew, 120.0f);
}

// AnimateTo starts an animation; Tick() advances it frame-by-frame. The event
// should fire once at the END of the animation (when settled), not every frame.
// This test calls Tick() 100 times to step through the animation — a naive raise
// from Tick() would produce 100 events.
TEST(ScrollViewerEvents, AnimationRaisesOnceWhenSettled) {
    ScrollViewer sv;
    SetUpVertical(sv);

    Rec rec;
    auto sub = sv.ScrollChanged().Subscribe(&rec, &Rec::On);

    sv.AnimateTo(200.0f);
    const float dt = 1.0f / 60.0f;  // 60fps frame
    for (int i = 0; i < 100; ++i) {
        sv.Tick(dt);
        if (sv.Offset() == 200.0f) break;  // animation settled
    }

    // Should have raised exactly once, when the offset reached the target.
    EXPECT_EQ(rec.seen.size(), size_t{1});
    EXPECT_EQ(rec.seen[0].offsetNew, 200.0f);
}

// Unsubscribing via RAII: the Subscription destructor unsubscribes, so a later
// SetOffset does not call the handler.
TEST(ScrollViewerEvents, SubscriptionRAII) {
    ScrollViewer sv;
    SetUpVertical(sv);

    Rec rec;
    {
        auto sub = sv.ScrollChanged().Subscribe(&rec, &Rec::On);
        sv.SetOffset(50.0f);
        EXPECT_EQ(rec.seen.size(), size_t{1});
    }  // sub destroyed -> unsubscribe

    sv.SetOffset(100.0f);
    EXPECT_EQ(rec.seen.size(), size_t{1});  // no new event
}

