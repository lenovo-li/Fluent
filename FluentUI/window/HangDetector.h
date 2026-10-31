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
// 用法：UI 线程每次「醒着干活」时调 Beat()——每帧末尾、消息循环每次从等待中
// 醒来、每次进入窗口过程（都是一次 relaxed store，纳秒级）；检测线程按
// pollInterval 轮询，发现 now - lastBeat > threshold 就调一次 onHang。
//
// 空闲不是卡死。按需渲染的窗口空闲时既不出帧也不收消息，UI 线程阻塞在
// INFINITE 的等待里，响应完全正常。只看「距上一拍多久」的话，任何超过阈值的
// 空闲都会被报成 hang（Demo 静置 5 秒就出一条假的 unresponsive）。所以 UI 线程
// 在进入无限期等待之前调 EnterIdle()：检测线程看到空闲标记就不判定，而且
// 自己也改成无限期等待，直到下一次 Beat() 把它叫醒——空闲时两个线程都不占 CPU，
// 保住「idle costs zero」。下一次 Beat() 同时清掉空闲标记、重新开始计时。
//
// 每个窗口一个检测器，空闲也按窗口判断。Application::PumpOneTurn 在等待之前
// 给「自己没有待出的帧」的窗口标空闲，不管别的窗口忙不忙：另一个窗口里有个
// 一直在转的 ProgressBar 时，循环每轮都是有界等待，但安静的那个窗口既不出帧
// 也不收消息，按全局「有没有人要出帧」判断的话它的拍子会一直停在原地，阈值
// 一到就误报。醒来后每个窗口都打一拍（等待有没有上限都打）。
//
// 哪些等待会标空闲：
//   * Application::PumpOneTurn（Run / RunModalUntilClosed 都走它）——按窗口，见上。
//   * DialogWindow::ShowDialog 在没有 Application 时用的 GetMessageW 循环——
//     GetMessageW 之前给对话框和 owner 里没有待出帧的那个标空闲，返回后打拍子。
//   * 系统模态循环（菜单、MessageBox、拖动边框）在我们的消息循环之外等待，宿主
//     用 WM_ENTERIDLE 和 WM_ENTERSIZEMOVE/WM_ENTERMENULOOP 把这些等待也标成空闲
//     （NativeWindowHost::WndProc）。
//
// 盲区：检测线程只认 EnterIdle/Beat 这两个信号，所以下面这些等待不会标空闲，
// 静置超过阈值就会被报成 hang：
//   * 不用 Application 的宿主自己写的消息循环（GetMessageW / WaitMessage）。
//     框架看不到这种循环，没有地方插 EnterIdle；这类宿主要么不开检测器，要么
//     在自己的 GetMessageW 之前调用 EnterIdle、返回后调用 Beat。self-frame
//     timer（没有 Application 时的 kSelfFrameTimerId）只负责出帧，它不是循环，
//     同样标不了空闲。
//   * 不发 WM_ENTERIDLE、又长时间不给我们派发任何消息的外部模态循环。
//
//   HangDetector hd;
//   hd.SetOnHang([](double stalledMs) {
//       TraceMsg("hang", "UI thread unresponsive for %.0f ms", stalledMs);
//   });
//   hd.Start(5000.0, 500.0);   // 阈值 5s，每 500ms 查一次
//   ...
//   hd.Beat();                 // 每帧末尾、每次醒来
//   hd.EnterIdle();            // 即将无限期阻塞等输入
//
// 回调在检测线程上执行，不在 UI 线程上——实现里不要碰 UI 状态，也不要假设
// 它和帧循环有任何同步关系。一次 hang 只报一次（边沿触发，不是每次轮询都报），
// 恢复后重新武装，所以一次长卡顿不会刷屏。
#pragma once
#include <atomic>
#include <cstdint>
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

    // UI 线程每次醒着干活时调用。拍子本身 relaxed store 就够了：我们只需要
    // 检测线程最终能看到新值。
    //
    // 从空闲恢复时多做两件事：清空闲标记（release，保证检测线程看到「不空闲」时
    // 一定也看到这次的新拍子，不会拿空闲前的旧拍子算出一个巨大的停顿），并叫醒
    // 正在无限期等待的检测线程。先 relaxed load 再 exchange：常态下（不空闲）
    // 只多一次普通读，不付带锁的 exchange。
    //
    // 只在检测线程真的停在无限期等待里（parked_）时才 SetEvent。一个窗口每轮循环都
    // 可能「标空闲 → 醒来 Beat」一次（别的窗口在动画、或者鼠标在快速移动时每秒上百
    // 次），如果每次都 SetEvent，检测线程每次都被叫醒、再立刻睡回去，空闲零开销就
    // 名存实亡。检测线程只有在空闲持续了一整个轮询周期之后才会 park，所以短暂的
    // 空闲不再产生任何跨线程唤醒，最多每个轮询周期一次。
    void Beat() {
        lastBeatQpc_.store(NowQpc(), std::memory_order_relaxed);
        if (idle_.load(std::memory_order_relaxed) &&
            idle_.exchange(false, std::memory_order_seq_cst)) {
            if (parked_.load(std::memory_order_seq_cst) && wakeEvent_) {
                wakeSignals_.fetch_add(1, std::memory_order_relaxed);
                SetEvent(wakeEvent_);
            }
        }
    }

    // UI 线程即将无限期阻塞（没有待出的帧、没有动画，只等输入）时调用。
    // 空闲期间不判定 hang，检测线程也停止轮询；下一次 Beat() 结束空闲。
    // 「空闲」是对这个检测器所属的窗口而言：它自己没有待出的帧。循环这一轮的
    // 等待可能因为别的窗口有帧而是有界的，这不影响——这个窗口在等待期间同样
    // 什么都不做，醒来就 Beat。有帧待出的窗口不标：它的等待几毫秒就结束，
    // 醒来出帧时 Beat。
    void EnterIdle() { idle_.store(true, std::memory_order_release); }

    bool IsIdle() const { return idle_.load(std::memory_order_acquire); }

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
        idle_.store(false, std::memory_order_relaxed);

        stopEvent_ = CreateEventW(nullptr, TRUE, FALSE, nullptr);  // 手动重置
        if (!stopEvent_) return;
        // 自动重置：一次空闲→忙碌的跳变叫醒一次。Beat() 只在跳变时 Set，所以它
        // 不会被每帧都 Set 一遍；残留的一次信号最多让检测线程提前醒一轮，无害。
        wakeEvent_ = CreateEventW(nullptr, FALSE, FALSE, nullptr);
        if (!wakeEvent_) {
            CloseHandle(stopEvent_);
            stopEvent_ = nullptr;
            return;
        }

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
        // Beat() 可能在 Stop() 之后仍被 UI 线程调用（检测器关掉了，宿主照样打拍子），
        // 所以句柄关掉后必须清零，Beat() 里的空指针检查才有意义。两者在同一个
        // UI 线程上，不存在并发。
        if (wakeEvent_) {
            CloseHandle(wakeEvent_);
            wakeEvent_ = nullptr;
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
        // 空闲 = UI 线程正常地在等输入，不是卡死。顺带重新武装：空闲前报过的
        // 那次 hang 已经结束了（能进入空闲说明 UI 线程回到了消息循环）。
        if (idle_.load(std::memory_order_acquire)) {
            hangReported_.store(false, std::memory_order_relaxed);
            return false;
        }
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

    // 测试用：检测线程按周期醒来做过几次判定。空闲期间它应该一直不变。
    uint64_t PollCountForTesting() const { return pollCount_.load(std::memory_order_relaxed); }

    // 测试用：检测线程进入过几次无限期（空闲）等待。见 PollLoop。
    uint64_t IdleWaitsForTesting() const { return idleWaits_.load(std::memory_order_acquire); }
    uint64_t WakeupsForTesting() const { return wakeups_.load(std::memory_order_relaxed); }
    uint64_t WakeSignalsForTesting() const { return wakeSignals_.load(std::memory_order_relaxed); }

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
        const HANDLE handles[2] = {stopEvent_, wakeEvent_};
        for (;;) {
            // 等 stop、wake 或者超时。等待内核对象而不是 sleep_for：Stop() 要能
            // 立刻叫醒它，不然 join 得等满一个轮询周期。
            //
            // 空闲时无限期等待：UI 线程空闲期间没有任何可判定的东西，按周期醒来
            // 只是白白耗电。EnterIdle() 到这里的竞态是安全的：如果读到空闲之后、
            // 进入等待之前 UI 线程就 Beat 了，wakeEvent_ 已经是有信号状态，
            // 等待立即返回。
            //
            // park 的顺序是承重的：先置 parked_，再重读 idle_（都是 seq_cst）。Beat 那边
            // 先清 idle_ 再读 parked_。两边的读写落在同一个全序上，所以要么这里重读到
            // 「已经不空闲」而不进入无限期等待，要么 Beat 读到 parked_ 为真而 SetEvent，
            // 不存在两边都错过的交错。多出来的一次 SetEvent（读到 parked_ 时本线程其实
            // 已经决定不睡了）只会让下一轮等待立即返回一次，无害。
            DWORD timeout = pollMs;
            if (idle_.load(std::memory_order_acquire)) {
                parked_.store(true, std::memory_order_seq_cst);
                if (idle_.load(std::memory_order_seq_cst)) timeout = INFINITE;
                else parked_.store(false, std::memory_order_seq_cst);
            }
            // 进入无限期等待之前计一次数。之后直到被 wake/stop 叫醒，这个线程
            // 不可能再给 pollCount_ 加 1，所以测试看到这个计数变化以后采样
            // pollCount_，得到的就是空闲期间的确定值，不用靠 sleep 去猜检测线程
            // 什么时候走完了空闲之前那一轮有界等待。
            if (timeout == INFINITE) idleWaits_.fetch_add(1, std::memory_order_release);
            const DWORD r = WaitForMultipleObjects(2, handles, FALSE, timeout);
            parked_.store(false, std::memory_order_seq_cst);
            if (r == WAIT_OBJECT_0 + 1) {
                wakeups_.fetch_add(1, std::memory_order_relaxed);
                continue;  // 从空闲恢复，重新开始一个轮询周期
            }
            if (r != WAIT_TIMEOUT) break;          // stop 了，或者出错了
            pollCount_.fetch_add(1, std::memory_order_relaxed);
            CheckOnce();
        }
    }

    std::thread worker_;
    HANDLE stopEvent_ = nullptr;
    HANDLE wakeEvent_ = nullptr;  // Beat() 结束空闲时叫醒检测线程
    bool running_ = false;

    // UI 线程写，检测线程读。这是唯一的共享可变状态。
    std::atomic<int64_t> lastBeatQpc_{0};
    // 边沿触发标记，两个线程都可能碰（CheckOnce 可以被测试从任意线程调）。
    std::atomic<bool> hangReported_{false};
    // UI 线程写（EnterIdle 置位、Beat 清除），检测线程读。
    std::atomic<bool> idle_{false};
    std::atomic<uint64_t> pollCount_{0};
    std::atomic<uint64_t> idleWaits_{0};
    // 检测线程是否正停在无限期等待里。只有这时 Beat 才需要叫醒它。
    std::atomic<bool> parked_{false};
    // 被 wakeEvent_ 叫醒的次数（测试用：确认短暂空闲不产生跨线程唤醒）。
    std::atomic<uint64_t> wakeups_{0};
    // Beat() 发出的 SetEvent 次数（测试用）。和 wakeups_ 不同，它不会被自动重置事件
    // 合并：200 次多余的 SetEvent 只叫醒检测线程一次，但这里记 200。
    std::atomic<uint64_t> wakeSignals_{0};

    double thresholdMs_ = 5000.0;
    HangCallback onHang_;
};

}  // namespace fluent
