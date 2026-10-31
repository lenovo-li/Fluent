#include "Application.h"
#include "window/NativeWindowHost.h"
#include "window/DialogWindow.h"
#include <algorithm>
#include <cassert>

namespace fluent {

// Private messages posted to the message-only window.
//   WM_APP_SHUTDOWN: sets shutdownRequested_ and posts WM_QUIT so every
//     pump — including the system's modal move/resize loop — stops naturally.
//   WM_APP_DISPATCH: the work queue has items; drain it on the UI thread.
static constexpr UINT WM_APP_SHUTDOWN = WM_APP + 1;
static constexpr UINT WM_APP_DISPATCH = WM_APP + 2;

// Window class name for the message-only helper window. Static registration so
// multiple Application objects (rare but legal) share the class.
static constexpr const wchar_t* kAppMsgClassName = L"FluentUI.Application.Msg";

// ---------------------------------------------------------------------------
//  Message-only window WndProc
// ---------------------------------------------------------------------------

/*static*/
LRESULT CALLBACK Application::AppMsgWndProc(HWND hwnd, UINT msg,
                                             WPARAM wp, LPARAM lp) {
    // Retrieve the owning Application* stored at GWLP_USERDATA. During
    // WM_CREATE it is not yet installed, so guard against nullptr.
    auto* self = reinterpret_cast<Application*>(
        ::GetWindowLongPtrW(hwnd, GWLP_USERDATA));

    if (msg == WM_CREATE) {
        // CREATESTRUCT::lpCreateParams carries `this` (passed from
        // CreateWindowExW's last argument). Install it as user data immediately
        // so any message that arrives before WM_CREATE returns can find it.
        auto* cs = reinterpret_cast<CREATESTRUCTW*>(lp);
        ::SetWindowLongPtrW(hwnd, GWLP_USERDATA,
                            reinterpret_cast<LONG_PTR>(cs->lpCreateParams));
        return 0;
    }

    if (!self) return ::DefWindowProcW(hwnd, msg, wp, lp);

    switch (msg) {
    case WM_APP_SHUTDOWN:
        // Record the requested exit code and mark shutdown so PumpOneTurn
        // stops at its next iteration. Also post WM_QUIT: every message pump
        // on this thread — including the system's internal modal drag/resize
        // loop and the legacy GetMessage-only dialog pump — exits when it
        // receives WM_QUIT. This is what PostThreadMessageW(WM_NULL) could not
        // guarantee: thread messages have no HWND so DispatchMessage never
        // reaches a WndProc, meaning the modal loop never sees the request. By
        // using a real HWND the WndProc fires inside whatever pump is running.
        self->shutdownRequested_ = true;
        self->exitCode_ = static_cast<int>(wp);
        ::PostQuitMessage(self->exitCode_);
        return 0;

    case WM_APP_DISPATCH: {
        // Drain the work queue. Swap under the lock so we do not hold it while
        // executing work (which could itself call Post, re-acquiring the lock).
        std::vector<std::function<void()>> local;
        {
            std::lock_guard<std::mutex> lk(self->workMutex_);
            local.swap(self->workQueue_);
        }
        for (auto& fn : local)
            if (fn) fn();
        return 0;
    }

    default:
        return ::DefWindowProcW(hwnd, msg, wp, lp);
    }
}

// ---------------------------------------------------------------------------
//  Construction / destruction
// ---------------------------------------------------------------------------

Application::ComApartment::ComApartment()
    : hr(::CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED |
                                       COINIT_DISABLE_OLE1DDE))
{
    if (FAILED(hr))
        TraceMsg("Application::ComApartment", "CoInitializeEx failed");
}

Application::ComApartment::~ComApartment() {
    if (SUCCEEDED(hr)) ::CoUninitialize();
}

bool Application::ComReady() const {
    return SUCCEEDED(com_.hr);
}

Application::Application(HINSTANCE instance)
    : instance_(instance), threadId_(GetCurrentThreadId())
{
    // Register the helper window class once per process. WNDCLASSEXW keeps it
    // simple — no icon, no cursor, no background. CS_GLOBALCLASS would work too
    // but is not necessary; our WndProc is private.
    WNDCLASSEXW wc{};
    wc.cbSize        = sizeof(wc);
    wc.lpfnWndProc   = AppMsgWndProc;
    wc.hInstance     = instance_;
    wc.lpszClassName = kAppMsgClassName;
    // Ignore ERROR_CLASS_ALREADY_EXISTS — a second Application object (or a
    // restart after teardown) is valid; the class is idempotent.
    RegisterClassExW(&wc);

    // HWND_MESSAGE creates a message-only window: no screen presence, no
    // z-order, not enumerated by EnumWindows. Messages posted to it are queued
    // on the thread's message queue and dispatched through DispatchMessage just
    // like any window message — crucially, including from inside modal loops.
    msgHwnd_ = CreateWindowExW(
        0, kAppMsgClassName, nullptr,
        0, 0, 0, 0, 0,
        HWND_MESSAGE,   // parent = message-only
        nullptr, instance_, this);

    if (!msgHwnd_)
        TraceMsg("Application", "CreateWindowExW(HWND_MESSAGE) failed");
}

Application::~Application() {
    // Window objects are caller-owned. They must close before the coordinator.
    for (const Entry& entry : windows_.Entries())
        if (entry.window) entry.window->application_ = nullptr;

    if (msgHwnd_) {
        DestroyWindow(msgHwnd_);
        msgHwnd_ = nullptr;
    }
}

// ---------------------------------------------------------------------------
//  Window bookkeeping
// ---------------------------------------------------------------------------

bool Application::AttachWindow(NativeWindowHost& window) {
    if (!IsUiThread() || !window.IsOpen()) return false;
    if (window.application_ && window.application_ != this) return false;

    // IsMainWindow() is asked HERE and the answer stored, rather than being
    // re-asked on detach: by the time a window detaches it is mid-destruction and
    // the virtual call would dispatch on an already-sliced object.
    const uint64_t generation = windows_.Add(&window, window.IsMainWindow());
    if (generation == 0) {
        // Already registered — idempotent success, and the existing generation
        // stays valid so outstanding pump snapshots keep matching.
        return windows_.Contains(
            Entry{&window, window.applicationGeneration_, window.IsMainWindow()});
    }
    window.application_ = this;
    window.applicationGeneration_ = generation;
    return true;
}

void Application::DetachWindow(NativeWindowHost& window) {
    windows_.Remove(&window);
    window.application_ = nullptr;
    window.applicationGeneration_ = 0;
}

// ---------------------------------------------------------------------------
//  Shutdown and dispatch
// ---------------------------------------------------------------------------

void Application::Shutdown(int exitCode) {
    // Post to the message-only window rather than using PostThreadMessageW.
    // Thread messages (hwnd == NULL) reach PeekMessage when our own pump is
    // running, but are silently dropped by the system's modal move/resize loop
    // and by GetMessage-based pumps (e.g. the legacy dialog path) because those
    // loops dispatch the message but then discard it without acting on it. A
    // real HWND message goes through DispatchMessage -> WndProc in any pump.
    if (msgHwnd_)
        PostMessage(msgHwnd_, WM_APP_SHUTDOWN, static_cast<WPARAM>(exitCode), 0);
    else {
        // Fallback if the message window was never created (very early/late call).
        shutdownRequested_ = true;
        exitCode_ = exitCode;
    }
}

void Application::Post(std::function<void()> fn) {
    {
        std::lock_guard<std::mutex> lk(workMutex_);
        workQueue_.push_back(std::move(fn));
    }
    // Wake the pump. The message-only window's WndProc drains the queue.
    if (msgHwnd_)
        PostMessage(msgHwnd_, WM_APP_DISPATCH, 0, 0);
}

// ---------------------------------------------------------------------------
//  Pump
// ---------------------------------------------------------------------------

bool Application::WindowCountStop() const {
    return windows_.ShouldStop(shutdownMode_);
}

bool Application::PumpOneTurn(bool stopWhenNoWindows) {
    if (shutdownRequested_) return false;
    if (stopWhenNoWindows && WindowCountStop()) return false;

    // A running animation clock must keep a frame pending, or the wait below would
    // block forever and the animation would never get its next tick. Arming the
    // clock (UpdateAnimationTimer -> SetWanted) does not request a frame by itself;
    // the tick used to do that at the top of every turn. Now that the tick only
    // runs with a due frame (below), the request is made here instead.
    auto snapshot = windows_.Snapshot();
    for (const Entry& entry : snapshot)
        if (windows_.Contains(entry) && entry.window->IsOpen() &&
            entry.window->IsAnimating() && !entry.window->NeedsFrame())
            entry.window->RequestFrame(FrameReason::Animation);

    // Pace on the SOONEST deadline among windows that actually want a frame, as a
    // real number of milliseconds. Whole-millisecond timeouts are rounded up to the
    // system timer period by the wait, which capped a 120Hz panel at ~68fps; see
    // FrameWaiter.h for the measurements.
    double remaining = FrameWaiter::kWaitForever;
    snapshot = windows_.Snapshot();
    for (const Entry& entry : snapshot) {
        if (!windows_.Contains(entry) || !entry.window->IsOpen() || !entry.window->NeedsFrame())
            continue;
        const double windowRemaining = entry.window->FrameWaitRemaining();
        if (remaining == FrameWaiter::kWaitForever || windowRemaining < remaining)
            remaining = windowRemaining;
    }

    // kWaitForever here means nothing is scheduled, so this blocks on input alone
    // and idle stays at zero CPU. Before waiting, tell each window's hang watchdog
    // whether THAT window is idle: a window that paints nothing and receives
    // nothing for longer than the watchdog's threshold is idle, not hung, and its
    // watchdog thread also stops polling for the duration (window/HangDetector.h).
    //
    // The decision is per window, not "is the whole wait unbounded". Each window
    // has its own watchdog, and a quiet window next to a busy one (an
    // indeterminate ProgressBar without a compositor, a long animation in a second
    // window or a modal dialog) used to be neither marked idle — the wait was
    // bounded — nor beaten — the after-wait beat only ran for unbounded waits — so
    // its last beat stayed put and the watchdog reported the healthy UI thread as
    // hung after the threshold. A window with a pending frame is not marked: its
    // wait ends within a frame interval and the beat below follows it.
    for (const Entry& entry : snapshot)
        if (windows_.Contains(entry) && entry.window->IsOpen() && !entry.window->NeedsFrame())
            entry.window->NoteLoopIdle();
    waiter_.Wait(remaining);
    // Beat every window on every wake, bounded wait or not. It clears the idle
    // mark set above (a window whose handler then hangs is judged from this beat,
    // not from its last frame) and covers wakes that dispatch nothing to the
    // window (a thread message, Application::Post work). Beat costs one relaxed
    // store unless the window was idle.
    snapshot = windows_.Snapshot();
    for (const Entry& entry : snapshot)
        if (windows_.Contains(entry)) entry.window->NoteLoopAwake();

    MSG msg;
    while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
        if (msg.message == WM_QUIT) {
            shutdownRequested_ = true;
            exitCode_ = static_cast<int>(msg.wParam);
            return false;
        }
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    snapshot = windows_.Snapshot();
    for (const Entry& entry : snapshot) {
        if (!windows_.Contains(entry) || !entry.window->IsOpen())
            continue;
        // Same predicate FrameWaiter used to decide not to sleep, so "we stopped
        // waiting" and "it is time to paint" can never disagree.
        //
        // The animation tick runs here, immediately before the paint and under the
        // same predicate, not once per turn. A turn ends on ANY input: with a
        // 1000Hz mouse moving over a 120Hz window the loop turns ~1000 times a
        // second but paints ~120, and ticking every turn ran OnAnimationTick (which
        // can re-rasterize a scroll surface) eight times per painted frame for
        // nothing on screen. dt is measured from the real clock, so one tick per
        // painted frame advances the animation exactly as far. Ticking after the
        // drain also means the tick sees this turn's input.
        if (AnimationTickIsDue(entry.window->NeedsFrame(), entry.window->FrameWaitRemaining())) {
            entry.window->PumpAnimations();
            entry.window->RunDueFrame();
        }
    }
    return !shutdownRequested_ && (!stopWhenNoWindows || !WindowCountStop());
}

HRESULT Application::TryRun() {
    // Calling from a non-UI thread or re-entrantly is a programming error, and
    // it is reported on the HRESULT channel — NOT as an exit code. The old code
    // returned ERROR_INVALID_STATE (5023) from `int Run()`, which put a Win32
    // error number into the process exit-code space: the caller could not tell
    // it apart from an app that legitimately exited with 5023, and exitCode_ was
    // never consulted at all.
    if (!IsUiThread()) {
        TraceMsg("Application", "TryRun() called from a non-UI thread");
        return E_ILLEGAL_METHOD_CALL;
    }
    if (running_) {
        TraceMsg("Application", "TryRun() called re-entrantly");
        return E_ILLEGAL_METHOD_CALL;
    }
    running_ = true;
    while (PumpOneTurn(true)) {}
    running_ = false;
    return S_OK;
}

int Application::Run() {
    // Deliberately collapses the failure case to exit code 0: a misuse must not
    // masquerade as a meaningful process exit status. TryRun() is the channel
    // that reports it.
    const HRESULT hr = TryRun();
    if (FAILED(hr)) return 0;
    return exitCode_;
}

void Application::RunModalUntilClosed(DialogWindow& dialog) {
    while (dialog.IsDialogOpen() && !shutdownRequested_)
        if (!PumpOneTurn(false)) break;
}

} // namespace fluent
