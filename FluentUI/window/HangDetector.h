// HangDetector.h — 独立线程观测 UI 线程是否卡住。
//
// 为什么这件事不能由 FPS 仪器兼任：
// interval ring 和 CPU ring 都跑在 UI 线程上，UI 线程卡住的时候它们一起卡住，
// 卡顿期间没有任何样本被写入——所以事后看日志只能看到「有一段时间没有帧」，
// 而卡住的当时没有任何东西能报告「现在正卡着，已经 N ms」。
//
// | 仪器                | 回答的问题                  | 线程   |
// |---------------------|-----------------------------|--------|
// | interval ring → FPS | UI 线程每秒产出几帧（追溯） | UI     |
// | CPU ring → P50/Max  | 建一帧花多少 CPU            | UI     |
// | HangDetector        | 现在是不是正卡着，卡了多久  | 独立   |
//
// 用法：UI 线程每帧调 Beat()（一次 relaxed store，纳秒级）；检测线程按
// pollInterval 轮询，发现 now - lastBeat > threshold 就调一次 onHang。
//
//   HangDetector hd;
//   hd.SetOnHang([](double stalledMs) {
//       TraceMsg("hang", "UI thread unresponsive for %.0f ms", stalledMs);
//   });
//   hd.Start(5000.0, 500.0);   // 阈值 5s，每 500ms 查一次
//   ...
//   hd.Beat();                 // 每帧末尾
//
// 回调在检测线程上执行，不在 UI 线程上——实现里不要碰 UI 状态，也不要假设
// 它和帧循环有任何同步关系。一次 hang 只报一次（边沿触发，不是每次轮询都报），
// 恢复后重新武装，所以一次长卡顿不会刷屏。
#pragma once
#include <atomic>
#include <functional>
#include <thread>
#include <windows.h>

namespace fluent {

class HangDetector {
public:
    HangDetector() = default;
    ~HangDetector() { Stop(); }

    HangDetector(const HangDetector&) = delete;
    HangDetector& operator=(const HangDetector&) = delete;

    // 卡死回调，在检测线程上调用，参数是已停顿的毫秒数。
    // 必须在 Start() 之前设置——Start() 之后设置的话，检测线程可能已经在跑了。
    using HangCallback = std::function<void(double stalledMs)>;
    void SetOnHang(HangCallback cb) { onHang_ = std::move(cb); }

    // UI 线程每帧调用。relaxed store 就够了：我们只需要检测线程最终能看到
    // 新值，不需要它和其他内存操作有顺序关系。
    void Beat() {
        lastBeatQpc_.store(NowQpc(), std::memory_order_relaxed);
    }

    // thresholdMs 停顿多久算卡死；pollIntervalMs 检测线程多久查一次。
    // pollInterval 应该明显小于 threshold，否则报出来的 stalledMs 会有一个
    // 轮询周期的量化误差。重复 Start() 是幂等的（已在跑就直接返回）。
    void Start(double thresholdMs = 5000.0, double pollIntervalMs = 500.0) {
        if (running_) return;
        thresholdMs_ = thresholdMs;

        // 第一拍必须在启动线程之前打，否则检测线程可能先跑起来、看到
        // lastBeatQpc_ == 0，把「还没开始产帧」当成「卡了很久」立刻误报。
        Beat();
        hangReported_.store(false, std::memory_order_relaxed);

        stopEvent_ = CreateEventW(nullptr, TRUE, FALSE, nullptr);  // 手动重置
        if (!stopEvent_) return;

        running_ = true;
        const DWORD pollMs = static_cast<DWORD>(pollIntervalMs > 1.0 ? pollIntervalMs : 1.0);
        worker_ = std::thread([this, pollMs] { PollLoop(pollMs); });
    }

    // 停线程并 join。幂等。析构自动调用。
    void Stop() {
        if (stopEvent_) SetEvent(stopEvent_);
        if (worker_.joinable()) worker_.join();
        if (stopEvent_) {
            CloseHandle(stopEvent_);
            stopEvent_ = nullptr;
        }
        running_ = false;
    }

    bool IsRunning() const { return running_; }

    // 当前停顿时长（毫秒）。任何线程都可以读，主要给测试用。HUD 不用它：
    // 空闲窗口本来就不产帧，「距上一帧多久」分不清空闲和卡死，HUD 改用 ping
    // （diagnostics/HudFeed.h）。
    double StalledMs() const {
        const int64_t last = lastBeatQpc_.load(std::memory_order_relaxed);
        if (last == 0) return 0.0;
        return QpcDeltaMs(last, NowQpc());
    }

    // 上一次 Start()/恢复以来是否已经报过一次 hang。测试用来确认边沿触发。
    bool HangReported() const { return hangReported_.load(std::memory_order_relaxed); }

    // 不开线程、直接跑一次判定，返回是否报了 hang。测试用：把整个时序
    // 从 sleep 里解出来，否则「等 5 秒确认它报警」就成了 5 秒的测试。
    bool CheckOnce() {
        const int64_t last = lastBeatQpc_.load(std::memory_order_relaxed);
        if (last == 0) return false;
        const double stalled = QpcDeltaMs(last, NowQpc());

        if (stalled >= thresholdMs_) {
            // 边沿触发：一次卡顿只报一次，否则轮询周期决定报警条数。
            if (!hangReported_.exchange(true, std::memory_order_relaxed)) {
                if (onHang_) onHang_(stalled);
                return true;
            }
            return false;
        }
        // 恢复了（又有新的 beat 了）→ 重新武装。
        hangReported_.store(false, std::memory_order_relaxed);
        return false;
    }

    // 测试用：直接设置阈值，免得为了改阈值重启线程。
    void SetThresholdMs(double ms) { thresholdMs_ = ms; }
    double ThresholdMs() const { return thresholdMs_; }

    // 测试用：把上一拍往前挪，伪造一次停顿，不用真的 sleep。
    void SimulateStallForTesting(double stalledMs) {
        LARGE_INTEGER freq{};
        QueryPerformanceFrequency(&freq);
        const int64_t ticks = static_cast<int64_t>(stalledMs * freq.QuadPart / 1000.0);
        lastBeatQpc_.store(NowQpc() - ticks, std::memory_order_relaxed);
    }

private:
    static int64_t NowQpc() {
        LARGE_INTEGER now{};
        QueryPerformanceCounter(&now);
        return now.QuadPart;
    }

    static double QpcDeltaMs(int64_t from, int64_t to) {
        LARGE_INTEGER freq{};
        QueryPerformanceFrequency(&freq);
        if (freq.QuadPart == 0) return 0.0;
        return static_cast<double>(to - from) * 1000.0 / static_cast<double>(freq.QuadPart);
    }

    void PollLoop(DWORD pollMs) {
        for (;;) {
            // 等 stop 或者超时。WaitForSingleObject 而不是 sleep_for：
            // Stop() 要能立刻叫醒它，不然 join 得等满一个轮询周期。
            const DWORD r = WaitForSingleObject(stopEvent_, pollMs);
            if (r != WAIT_TIMEOUT) break;  // stop 了，或者出错了
            CheckOnce();
        }
    }

    std::thread worker_;
    HANDLE stopEvent_ = nullptr;
    bool running_ = false;

    // UI 线程写，检测线程读。这是唯一的共享可变状态。
    std::atomic<int64_t> lastBeatQpc_{0};
    // 边沿触发标记，两个线程都可能碰（CheckOnce 可以被测试从任意线程调）。
    std::atomic<bool> hangReported_{false};

    double thresholdMs_ = 5000.0;
    HangCallback onHang_;
};

}  // namespace fluent
