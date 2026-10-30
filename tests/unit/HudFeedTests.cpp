// HudFeedTests.cpp — the pure half of the off-thread HUD (diagnostics/HudFeed.h).
// The HUD thread and its GPU objects need a real window and device, so only the
// hand-off, the ping bookkeeping and the status line are tested here. Whether the
// HUD actually appears, stays on top and keeps updating during a stall is a
// hardware check.

#include "../framework/Test.h"
#include "../../FluentUI/diagnostics/HudFeed.h"

#include <string>

using namespace fluent;

// Nothing published yet -> Read reports no data instead of a zeroed snapshot that
// would draw as "FPS 0".
TEST(HudFeed, ReadBeforePublishReportsNoData) {
    HudFeed feed;
    HudSnapshot out;
    EXPECT_FALSE(feed.Read(out));
}

// Read returns the latest publish whole, rings included.
TEST(HudFeed, ReadReturnsLatestPublish) {
    HudFeed feed;
    HudSnapshot a;
    a.frameCount = 1;
    a.cpu.Push(3.0f);
    feed.Publish(a);
    HudSnapshot b;
    b.frameCount = 2;
    b.cpu.Push(7.0f);
    b.interval.Push(16.0f);
    feed.Publish(b);

    HudSnapshot out;
    EXPECT_TRUE(feed.Read(out));
    EXPECT_EQ(out.frameCount, 2u);
    EXPECT_EQ(out.cpu.Count(), 1u);
    EXPECT_NEAR(out.cpu.Max(), 7.0f, 0.001f);
    EXPECT_NEAR(out.interval.P50(), 16.0f, 0.001f);
}

// One ping in flight at most: a stuck UI thread must not collect a backlog of
// pings that it then drains in a burst.
TEST(HudPingTracker, OnlyOnePingInFlight) {
    HudPingTracker t;
    const uint32_t first = t.NextPing(0.0);
    EXPECT_TRUE(first != 0u);
    EXPECT_EQ(t.NextPing(100.0), 0u);
    EXPECT_EQ(t.NextPing(200.0), 0u);
    EXPECT_NEAR(t.PendingMs(300.0), 300.0, 0.001);
}

// The answer releases the slot and records latency from post to acknowledgement.
TEST(HudPingTracker, AckRecordsLatencyAndAllowsNextPing) {
    HudPingTracker t;
    const uint32_t seq = t.NextPing(1000.0);
    t.Observe(seq, 1004.5);
    EXPECT_TRUE(t.HasLatency());
    EXPECT_NEAR(t.LastLatencyMs(), 4.5, 0.001);
    EXPECT_NEAR(t.PendingMs(2000.0), 0.0, 0.001);
    const uint32_t next = t.NextPing(1100.0);
    EXPECT_TRUE(next != 0u);
    EXPECT_TRUE(next != seq);
}

// An older sequence (or the reserved 0 before anything is answered) is not an
// answer to the current ping, so the stall keeps counting.
TEST(HudPingTracker, StaleAckIgnored) {
    HudPingTracker t;
    const uint32_t s1 = t.NextPing(0.0);
    t.Observe(s1, 1.0);
    t.NextPing(10.0);
    t.Observe(s1, 500.0);  // stale
    t.Observe(0u, 500.0);  // reserved
    EXPECT_NEAR(t.PendingMs(600.0), 590.0, 0.001);
    EXPECT_NEAR(t.LastLatencyMs(), 1.0, 0.001);
}

// A failed post must not leave the tracker "in flight" forever, which would read
// as a permanent stall.
TEST(HudPingTracker, CancelInFlightFreesSlot) {
    HudPingTracker t;
    t.NextPing(0.0);
    t.CancelInFlight();
    EXPECT_NEAR(t.PendingMs(1000.0), 0.0, 0.001);
    EXPECT_TRUE(t.NextPing(1000.0) != 0u);
}

// Past the threshold the line says STALLED and that the numbers above are stale;
// just under it, it does not.
TEST(HudStatusLine, StallAtThreshold) {
    const std::string stalled = FormatHudStatusLine(kHudStallThresholdMs, 1.0, 50.0);
    EXPECT_TRUE(stalled.find("UI STALLED 250 ms") != std::string::npos);
    EXPECT_TRUE(stalled.find("stale") != std::string::npos);
    const std::string ok = FormatHudStatusLine(kHudStallThresholdMs - 1.0, 1.0, 50.0);
    EXPECT_TRUE(ok.find("STALLED") == std::string::npos);
    EXPECT_TRUE(ok.find("UI ping 1.0ms") != std::string::npos);
}

// No frame for a while is "idle", not a stall — an idle window paints nothing by
// design, which is why liveness comes from the ping and not from frame age.
TEST(HudStatusLine, IdleAndNoData) {
    const std::string idle = FormatHudStatusLine(0.0, 0.3, 2500.0);
    EXPECT_TRUE(idle.find("idle 2.5s") != std::string::npos);
    EXPECT_TRUE(idle.find("STALLED") == std::string::npos);
    const std::string recent = FormatHudStatusLine(0.0, 0.3, 40.0);
    EXPECT_TRUE(recent.find("last frame 40ms ago") != std::string::npos);
    const std::string none = FormatHudStatusLine(0.0, -1.0, -1.0);
    EXPECT_TRUE(none.find("UI ping --") != std::string::npos);
    EXPECT_TRUE(none.find("no frame yet") != std::string::npos);
}
