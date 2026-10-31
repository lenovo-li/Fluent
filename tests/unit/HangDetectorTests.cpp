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

// 空闲不是卡死：按需渲染的窗口静置时既不出帧也不收消息，UI 线程停在无限期
// 等待里。只要宣告过空闲，停顿再长也不能报 hang（Demo 静置 5 秒误报的回归）。
TEST(HangDetector, IdleWaitIsNotReportedAsHang) {
    HangDetector hd;
    int callCount = 0;
    hd.SetOnHang([&](double) { ++callCount; });
    hd.SetThresholdMs(100.0);
    hd.Beat();

    hd.EnterIdle();
    hd.SimulateStallForTesting(10000.0);  // 远超阈值的空闲
    EXPECT_FALSE(hd.CheckOnce());
    EXPECT_EQ(callCount, 0);
    EXPECT_TRUE(hd.IsIdle());
}

// 醒来后的第一拍结束空闲；之后再停顿就是真的卡死，照常报。
TEST(HangDetector, BeatAfterIdleResumesDetection) {
    HangDetector hd;
    int callCount = 0;
    hd.SetOnHang([&](double) { ++callCount; });
    hd.SetThresholdMs(100.0);
    hd.Beat();

    hd.EnterIdle();
    hd.Beat();
    EXPECT_FALSE(hd.IsIdle());
    // 刚醒的这一拍是新的，不能拿空闲前的旧拍子算停顿。
    EXPECT_FALSE(hd.CheckOnce());

    hd.SimulateStallForTesting(200.0);
    EXPECT_TRUE(hd.CheckOnce());
    EXPECT_EQ(callCount, 1);
}

// 短暂空闲（一个轮询周期之内又 Beat）不叫醒检测线程。一个窗口每轮循环都可能
// 「标空闲 → 醒来 Beat」一次；原来每次 Beat 都 SetEvent，别的窗口在动画时检测线程
// 每秒被叫醒上百次。现在只有真正 park 了才叫醒。轮询周期设成 1s，200 次切换在一个
// 周期内完成；最多允许 1 次，覆盖线程刚启动时恰好读到空闲而 park 的情况。
// 断言的是 Beat 发出的 SetEvent 次数，不是线程被叫醒的次数：自动重置事件会把 200
// 次信号合并成一次唤醒，只看唤醒次数分不出「每次都发」和「只在 park 时发」。
TEST(HangDetector, BriefIdleDoesNotWakeTheWorker) {
    HangDetector hd;
    hd.Start(10000.0, 1000.0);
    for (int i = 0; i < 200; ++i) {
        hd.EnterIdle();
        hd.Beat();
    }
    EXPECT_TRUE(hd.WakeSignalsForTesting() <= 1u);
    EXPECT_TRUE(hd.WakeupsForTesting() <= 1u);
    hd.Stop();
}

// 空闲期间检测线程不按周期醒来（idle costs zero），下一拍又恢复轮询。
TEST(HangDetector, WorkerStopsPollingWhileIdle) {
    HangDetector hd;
    int callCount = 0;
    hd.SetOnHang([&](double) { ++callCount; });
    hd.Start(10000.0, 2.0);  // 每 2ms 轮询；阈值很大，不会真的报

    // 先确认它确实在轮询（轮询线程起来需要一点时间，给足余量）。
    for (int i = 0; i < 200 && hd.PollCountForTesting() == 0; ++i)
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    EXPECT_TRUE(hd.PollCountForTesting() > 0);

    const uint64_t idleWaitsBefore = hd.IdleWaitsForTesting();
    hd.EnterIdle();
    // 等检测线程真正进入无限期等待，而不是睡一个固定时长去猜：空闲之前那一轮
    // 2ms 的有界等待在负载高的机器上可能 30ms 以后才走完，那时它还会给
    // pollCount 加 1，落在采样之后就是偶发失败。IdleWaitsForTesting 在进入
    // INFINITE 等待之前加 1，看到它变了，pollCount 在下一次 Beat 之前就不会再动。
    for (int i = 0; i < 2000 && hd.IdleWaitsForTesting() == idleWaitsBefore; ++i)
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    EXPECT_TRUE(hd.IdleWaitsForTesting() > idleWaitsBefore);
    const uint64_t idleStart = hd.PollCountForTesting();
    std::this_thread::sleep_for(std::chrono::milliseconds(60));
    EXPECT_EQ(hd.PollCountForTesting(), idleStart);  // 空闲 60ms，按 2ms 周期本该 ~30 次

    hd.Beat();  // 结束空闲，叫醒检测线程
    for (int i = 0; i < 200 && hd.PollCountForTesting() == idleStart; ++i)
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    EXPECT_TRUE(hd.PollCountForTesting() > idleStart);

    hd.Stop();
    EXPECT_EQ(callCount, 0);
}
