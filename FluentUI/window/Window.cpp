#include "Window.h"
#include "../diagnostics/LayoutCostProbe.h"
#include <shellapi.h>  // Shell_NotifyIconW, NOTIFYICONDATAW

namespace fluent {
namespace {

// Gate for the per-phase layout trace, read once from the environment.
//
// Off by default and env-gated rather than compiled in, for the reason stated in
// CLAUDE.md's frame-pipeline notes: a trace on the layout path is itself a cost
// (OutputDebugStringA serializes on a system-wide lock and a debugger makes it far
// worse), so a diagnostic that is always on distorts exactly the numbers it reports.
//
// Set FLUENTUI_TRACE_LAYOUT=1 to enable. In Visual Studio that goes in the project's
// Debugging → Environment (FluentUIDemo.vcxproj.user already carries it); a `set` in
// a separate shell does not reach an F5 launch.
bool LayoutPhaseTraceEnabled() {
    static const bool enabled = [] {
        char buf[8]{};
        return GetEnvironmentVariableA("FLUENTUI_TRACE_LAYOUT", buf, sizeof(buf)) > 0 &&
               buf[0] == '1';
    }();
    return enabled;
}

}  // namespace

void Window::SetTitle(std::wstring title) {
    title_ = std::move(title);
    if (IsOpen()) UpdateNativeTitle(title_);
}

void Window::SetStandardTitleBar(bool enabled) {
    if (standardTitleBar_ == enabled) return;
    standardTitleBar_ = enabled;
    if (IsOpen()) {
        OnLayout();
        Render();
    }
}

void Window::SetTitleBarHeight(float heightDip) {
    const float value = heightDip > 0.0f ? heightDip : 0.0f;
    if (titleBarHeightDip_ == value) return;
    titleBarHeightDip_ = value;
    if (IsOpen()) {
        OnLayout();
        Render();
    }
}

HRESULT Window::PrepareContent(Application* app) {
    // Initialization is once per object: NativeWindowHost instances are not reusable
    // after their HWND is destroyed, so a second Show would build a second tree
    // onto a dead window.
    if (initialized_ || IsOpen()) return E_INVALIDARG;
    SetOwningApplication(app);  // Set BEFORE OnInitialize, so OwningApplication() works
    OnInitialize();
    initialized_ = true;
    // A window with no root would create successfully and then paint nothing,
    // which is a confusing failure to debug. Fail loudly at the source instead.
    return root_ ? S_OK : E_UNEXPECTED;
}

HRESULT Window::Show(Application& application, const WindowState* restore) {
    HRESULT hr = PrepareContent(&application);
    if (FAILED(hr)) return hr;
    hr = Create(application, title_.c_str(), widthDip_, heightDip_, restore);
    if (FAILED(hr)) return hr;
    RaiseLoaded();
    return S_OK;
}

bool Window::Close() {
    if (!IsOpen() || closing_) return false;
    if (!RaiseClosing()) return false;
    closing_ = true;
    NativeWindowHost::Close();
    return true;
}

void Window::RaiseLoaded() {
    if (loadedRaised_ || !IsOpen()) return;
    loadedRaised_ = true;
    WindowEventArgs args;
    OnLoaded(args);
    loaded_.Raise(*this, args);
}

bool Window::RaiseClosing() {
    WindowClosingArgs args;
    OnClosing(args);
    closingEvent_.Raise(*this, args);
    return !args.cancel;
}

void Window::RaiseUnloaded() {
    if (unloadedRaised_) return;
    unloadedRaised_ = true;
    WindowEventArgs args;
    OnUnloaded(args);
    unloaded_.Raise(*this, args);
}

void Window::RaiseClosed() {
    if (closedRaised_) return;
    closedRaised_ = true;
    WindowEventArgs args;
    OnClosed(args);
    closed_.Raise(*this, args);
}

void Window::OnCreated() {
    // Register the root first so the service context reaches it, then let the
    // subclass add anything that lives outside the root tree. Order matters: a
    // subclass may want to reference already-attached root children.
    AddElement(root_.get());
    OnContentCreated();
}

void Window::OnLayout() {
    if (!root_) return;
    // Per-pass, not per-session: the probe accumulates, so without this every
    // reported total is the sum over every resize frame since launch.
    LayoutCostProbe::Reset();
    const float top = standardTitleBar_ ? titleBarHeightDip_ : 0.0f;
    const float height = std::max(0.0f, ClientHeightDip() - top);
    const float width = ClientWidthDip();

    // Synchronous, and deliberately so. This ran on a background worker for several
    // commits (AsyncLayoutWorker, now deleted); measurement on the Gallery is what
    // ended that. Per-phase traces of an Off-mode resize drag showed:
    //
    //   spawn=0.8ms   creating the single-shot worker thread, every WM_SIZE
    //   wait=7-17ms   the UI thread BLOCKED on the worker for the whole measure
    //   publish=0.1ms walking the tree to copy async fields into the real ones
    //   arrange=4-12ms UI thread, which async measure cannot touch by construction
    //
    // The measure cost never went anywhere — it was accounted to a different thread
    // while the UI thread waited for exactly as long, plus the spawn. And with
    // arrange at 4-12ms, even a perfect non-blocking measure could not have made the
    // drag smooth.
    //
    // The real fix for a slow resize: skip the work instead of moving it.
    // UIContext::inModalResize (set by NativeWindowHost on WM_ENTERSIZEMOVE) defers
    // DComp surface rasterization until WM_EXITSIZEMOVE, which took resize layout
    // from 7-13ms to 2-3ms — geometry updates every frame, pixels converge once.
    const int64_t tMeasureStart = QpcNow();
    // Apply root element's Margin in BOTH Measure and Arrange passes.
    // Without this, a Grid with Margin={20,20,20,20} at the window root would:
    // - receive too much space in Measure (leading to inflated Star-row constraints)
    // - have its bottom margin ignored in Arrange (visual clipping)
    float availW = width;
    float availH = height;
    if (auto* rootFE = dynamic_cast<FrameworkElement*>(root_.get())) {
        const Thickness& m = rootFE->Margin();
        availW = std::max(0.0f, availW - m.horizontal());
        availH = std::max(0.0f, availH - m.vertical());
    }
    root_->Measure(availW, availH);
    const int64_t tMeasured = QpcNow();
    RectDip arrangeRect = {0.0f, top, width, height};
    if (auto* rootFE = dynamic_cast<FrameworkElement*>(root_.get())) {
        const Thickness& m = rootFE->Margin();
        arrangeRect.x += m.left;
        arrangeRect.y += m.top;
        arrangeRect.w = std::max(0.0f, arrangeRect.w - m.horizontal());
        arrangeRect.h = std::max(0.0f, arrangeRect.h - m.vertical());
    }
    root_->Arrange(arrangeRect);
    const int64_t tArranged = QpcNow();

    // Publish the split unconditionally. It is two QPC reads and two stores on a
    // path that already costs milliseconds, and the numbers are what let the resize
    // trace (NativeWindowHost::ApplyPendingResize) name the expensive half without a
    // second trace line to correlate by hand. Only the TRACE below is gated — the
    // measurement is not, because a diagnostic that requires an environment variable
    // to be set correctly is a diagnostic that is missing exactly when it is needed.
    FrameStats& stats = MutableFrameStats();
    stats.measureMs = QpcToMs(tMeasured - tMeasureStart);
    stats.arrangeMs = QpcToMs(tArranged - tMeasured);

    if (LayoutPhaseTraceEnabled()) {
        FL_TRACEF("Layout", "phases: measure=%.3f arrange=%.3f total=%.3fms",
                  stats.measureMs, stats.arrangeMs,
                  QpcToMs(tArranged - tMeasureStart));
        LayoutCostProbe::Report("  ");
    }
}

void Window::OnDestroying() {
    // Runs while the tree is still attached and the HWND is still valid, so a
    // handler can still read control state (that is the whole point of Unloaded
    // being distinct from Closed).
    RaiseUnloaded();
}

void Window::OnDestroyed() {
    // Runs after roots have detached and hwnd_ has been cleared.
    RaiseClosed();
}

bool Window::OnAppMessage(UINT msg, WPARAM wp, LPARAM lp, LRESULT& result) {
    UNREFERENCED_PARAMETER(wp);
    if (msg == WM_CLOSE) {
        // Route the system close button through the cancelable path rather than
        // letting DefWindowProc destroy the window behind the app's back.
        Close();
        result = 0;
        return true;
    }

    if (msg == WM_FLUENT_TRAY_CALLBACK) {
        // GetCursorPos rather than decoding lParam: under the classic notify-icon
        // contract lParam carries only the mouse message, with no coordinates. The
        // cursor has not moved between the shell posting this and us handling it —
        // the tray is not a path where a frame of lag can accumulate.
        POINT pt{};
        GetCursorPos(&pt);
        TrayIconEventArgs args;
        args.screenX = pt.x;
        args.screenY = pt.y;
        args.rawMessage = static_cast<UINT>(lp);
        args.action = DecodeTrayAction(lp);
        RaiseTrayAction(args.action, args);
        result = 0;
        return true;
    }

    // Explorer restarted and dropped every tray icon. Compared only when this
    // window actually registered one (taskbarCreatedMsg_ stays 0 otherwise), so a
    // window with no tray icon pays one integer compare against a value no real
    // message can equal — RegisterWindowMessageW never returns 0.
    if (taskbarCreatedMsg_ != 0 && msg == taskbarCreatedMsg_) {
        if (trayIcon_) trayIcon_->HandleTaskbarCreated();
        // Deliberately NOT consumed: TaskbarCreated is a broadcast and other
        // subsystems (or a subclass) may legitimately want to see it too. Returning
        // false lets it continue to the host's own switch and DefWindowProc.
        return false;
    }

    return false;
}

// --- Tray icon ----------------------------------------------------------------

HRESULT Window::SetTrayIcon(HICON icon, const std::wstring& tooltip) {
    // An HWND is required, and this is the one failure mode worth being loud
    // about: SetTrayIcon in OnInitialize is the natural mistake, since that is
    // where every other piece of window setup goes.
    if (!IsOpen()) {
        TraceMsg("Window", "SetTrayIcon before the window exists — call it from "
                           "OnContentCreated/OnLoaded, not OnInitialize");
        return HRESULT_FROM_WIN32(ERROR_INVALID_WINDOW_HANDLE);
    }
    if (!trayIcon_) {
        trayIcon_ = std::make_unique<TrayIcon>(Hwnd());
        // Interned process-wide by the shell; resolving it here rather than at
        // startup keeps the cost on windows that actually want a tray icon.
        taskbarCreatedMsg_ = RegisterWindowMessageW(L"TaskbarCreated");
    }
    return trayIcon_->Set(icon, tooltip);
}

void Window::RemoveTrayIcon() {
    if (trayIcon_) trayIcon_->Remove();
}

bool Window::HasTrayIcon() const {
    return trayIcon_ && trayIcon_->IsAdded();
}

HRESULT Window::SetTrayTooltip(const std::wstring& tooltip) {
    if (!trayIcon_) return E_FAIL;
    return trayIcon_->SetTooltip(tooltip);
}

HRESULT Window::ShowTrayBalloon(const std::wstring& title, const std::wstring& text,
                                DWORD iconFlags) {
    if (!trayIcon_) return E_FAIL;
    return trayIcon_->ShowBalloon(title, text, iconFlags);
}

void Window::HideToTray() {
    if (!IsOpen()) return;
    ShowWindow(Hwnd(), SW_HIDE);
}

void Window::RestoreFromTray() {
    if (!IsOpen()) return;
    const HWND hwnd = Hwnd();
    // SW_SHOW alone is not enough for a window hidden while minimized: it becomes
    // visible but stays minimized, so the user sees a taskbar button instead of a
    // window and has to click again. SW_RESTORE covers both cases (it shows AND
    // un-minimizes), and the IsIconic check keeps a normal show from animating a
    // restore it does not need.
    ShowWindow(hwnd, IsIconic(hwnd) ? SW_RESTORE : SW_SHOW);
    SetForegroundWindow(hwnd);
}

void Window::RaiseTrayAction(TrayAction action, const TrayIconEventArgs& args) {
    // Events take a mutable ref (a handler may annotate the args), but the decoded
    // input is ours — copy per dispatch so one handler cannot alter what the next
    // hook sees.
    TrayIconEventArgs local = args;
    switch (action) {
        case TrayAction::Click:
            OnTrayIconClick(local);
            trayClick_.Raise(*this, local);
            break;
        case TrayAction::DoubleClick:
            OnTrayIconDoubleClick(local);
            trayDoubleClick_.Raise(*this, local);
            break;
        case TrayAction::RightClick:
            OnTrayIconRightClick(local);
            trayRightClick_.Raise(*this, local);
            break;
        case TrayAction::MiddleClick:
            OnTrayIconMiddleClick(local);
            trayMiddleClick_.Raise(*this, local);
            break;
        case TrayAction::None:
            // Mouse-move over the icon, balloon shown/dismissed, and similar
            // callbacks land here. Surfacing them would mean an event that fires
            // continuously while the cursor crosses the tray.
            break;
    }
}

} // namespace fluent
