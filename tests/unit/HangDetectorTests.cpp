// HangDetectorTests.cpp — 卡死检测的边沿触发、重武装、阈值。
#include "../framework/Test.h"
#include "../../FluentUI/window/HangDetector.h"
#include <chrono>
#include <thread>

using namespace fluent;

TEST(HangDetector, ReportsHangWhenBeatStops) {
    HangDetector hd;
    int callCount = 0;
    double lastReportedMs = 0.0;

    hd.SetOnHang([&](double ms) {
        ++callCount;
        lastReportedMs = ms;
    });

    hd.SetThresholdMs(100.0);
    hd.Beat();  // t=0

    // 模拟已经 200ms 没 beat 了
    hd.SimulateStallForTesting(200.0);

    bool reported = hd.CheckOnce();
    EXPECT_TRUE(reported);
    EXPECT_EQ(callCount, 1);
    EXPECT_TRUE(lastReportedMs >= 100.0);
}

TEST(HangDetector, EdgeTriggeredOnlyReportsOnce) {
    HangDetector hd;
    int callCount = 0;

    hd.SetOnHang([&](double) { ++callCount; });
    hd.SetThresholdMs(100.0);
    hd.Beat();

    hd.SimulateStallForTesting(200.0);

    hd.CheckOnce();  // 第一次应该报
    EXPECT_EQ(callCount, 1);

    hd.CheckOnce();  // 第二次不报（边沿触发，hangReported_ 仍是 true）
    EXPECT_EQ(callCount, 1);

    hd.CheckOnce();  // 第三次还是不报
    EXPECT_EQ(callCount, 1);
}

TEST(HangDetector, RearmsAfterRecovery) {
    HangDetector hd;
    int callCount = 0;

    hd.SetOnHang([&](double) { ++callCount; });
    hd.SetThresholdMs(100.0);
    hd.Beat();

    hd.SimulateStallForTesting(200.0);
    hd.CheckOnce();
    EXPECT_EQ(callCount, 1);

    // 恢复了（beat 又动了）
    hd.Beat();
    hd.CheckOnce();  // 这次检查不报，且应该重新武装

    // 再卡一次
    hd.SimulateStallForTesting(200.0);
    hd.CheckOnce();
    EXPECT_EQ(callCount, 2);  // 第二次报
}

TEST(HangDetector, DoesNotReportBelowThreshold) {
    HangDetector hd;
    int callCount = 0;

    hd.SetOnHang([&](double) { ++callCount; });
    hd.SetThresholdMs(100.0);
    hd.Beat();

    hd.SimulateStallForTesting(50.0);  // 低于阈值
    hd.CheckOnce();
    EXPECT_EQ(callCount, 0);
}

TEST(HangDetector, WorkerThreadReportsHang) {
    // 真实的线程轮询场景：启动检测线程，等它报警。
    HangDetector hd;
    int callCount = 0;

    hd.SetOnHang([&](double) {
        callCount++;
    });

    hd.Start(100.0, 10.0);  // 阈值 100ms，每 10ms 轮询
    EXPECT_TRUE(hd.IsRunning());

    hd.Beat();
    std::this_thread::sleep_for(std::chrono::milliseconds(30));  // 正常

    // 模拟停顿
    hd.SimulateStallForTesting(200.0);

    // 等检测线程发现它
    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    hd.Stop();
    EXPECT_EQ(callCount, 1);
}

TEST(HangDetector, StalledMsReturnsCurrentStall) {
    HangDetector hd;
    hd.Beat();

    double s1 = hd.StalledMs();
    EXPECT_TRUE(s1 < 10.0);  // 刚 beat 过，应该是毫秒级

    hd.SimulateStallForTesting(300.0);
    double s2 = hd.StalledMs();
    EXPECT_TRUE(s2 >= 200.0);
}
