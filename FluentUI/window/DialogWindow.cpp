#include "DialogWindow.h"
#include "../Application.h"
#include "../layout/Panel.h"
#include <algorithm>
#include <limits>

namespace fluent {

namespace {
// Dialog content inset. These are window-level chrome padding, not control
// spacing, which is why they are plain constants rather than theme tokens: the
// theme's spacing tokens describe gaps *between controls*, and borrowing one here
// would couple dialog chrome to a token that exists for a different purpose. If
// dialog chrome ever becomes themeable these move into ThemeTokens together.
constexpr float kDialogPadDip = 16.0f;      // left/right inset
constexpr float kDialogTopGapDip = 8.0f;    // gap below the title bar
constexpr float kDialogBottomPadDip = 24.0f;

// Width an auto-sized dialog offers its content before measuring. Text wraps to
// this, so it sets the shape of every auto-sized dialog: wide enough for a
// sentence, narrow enough to stay a dialog rather than a window.
constexpr float kAutoSizeWidthDip = 360.0f;

// Ceiling on an auto-sized dialog's height, as a fraction of the owner monitor's
// work area. Without it a long message grows the window past the screen and puts
// the action buttons somewhere the user cannot reach or even see.
constexpr float kAutoSizeMaxHeightFraction = 0.8f;
} // namespace

HRESULT DialogWindow::Start(NativeWindowHost& owner) {
    if (IsOpen() || IsInitialized() || !owner.IsOpen()) return E_INVALIDARG;
    ownerWindow_ = &owner;
    owner_ = owner.Hwnd();
    result_ = DialogResult::None;

    // Dialogs inherit the owner's complete theme inputs, not merely the current
    // dark bit. This preserves custom accent and high-contrast policy too.
    SetThemeConfiguration(owner.ThemeConfiguration());
    ownerThemeSub_ = owner.ThemeChanged().Subscribe(
        this, [](void* target, NativeWindowHost& source, ThemeChangedArgs&) {
            static_cast<DialogWindow*>(target)->SetThemeConfiguration(
                source.ThemeConfiguration());
        });

    // Initialization belongs to the dialog class and happens before native
    // creation, matching InitializeComponent/WM_INITDIALOG semantics. PrepareContent
    // enforces the once-per-object rule and the "must have a root" contract.
    //
    // Pass the owner's Application so OwningApplication() works in OnInitialize.
    // Fallback to nullptr for owners that never adopted Application (PrepareContent
    // accepts nullptr and leaves application_ null in that case).
    Application* app = owner.OwningApplication();
    HRESULT hr = PrepareContent(app);
    if (FAILED(hr)) return hr;

    // Inherit the owner's Application so the dialog joins the same frame loop and
    // its modal pump can keep the owner painting. Falls back to the raw HINSTANCE
    // path for hosts that never adopted Application.
    hr = app ? Create(*app, Title().c_str(), ClientWidth(), ClientHeight())
             : Create(owner.Instance(), Title().c_str(), ClientWidth(), ClientHeight());
    if (FAILED(hr)) return hr;

    // Auto-size AFTER Create, never before: TextBlock::Measure needs the DWrite
    // context to measure text, and that context is created by Create. Measuring a
    // text-bearing tree earlier silently falls back to a one-line estimate
    // (fontSize * 1.4), which is why an auto-sized dialog used to come out far too
    // short and clip its buttons.
    if (AutoSize()) ApplyAutoSize();
    return S_OK;
}

// Resize the already-created window so its client area exactly fits the content.
// Runs once, right after Create.
void DialogWindow::ApplyAutoSize() {
    UIElement* root = Root();
    if (!root || !Hwnd()) return;
    SetAutoSize(false);  // Once only; a later user resize must not re-trigger this.

    // Measure against the width a dialog offers its content, with unbounded height.
    // An unbounded WIDTH would make TextBlock report its single-line unwrapped width
    // and every dialog would come out too narrow.
    const float contentW = kAutoSizeWidthDip - kDialogPadDip * 2.0f;
    root->Measure(contentW, std::numeric_limits<float>::infinity());
    const SizeDip desired = root->Desired();

    // Mirror OnLayout's vertical budget exactly: title bar, top gap, content, bottom
    // pad. Anything missing here gets clipped off the bottom, which is where the
    // action buttons live.
    const float chromeH = TitleBarHeightDip() + kDialogTopGapDip + kDialogBottomPadDip;
    const float clientW = std::max(desired.w, contentW) + kDialogPadDip * 2.0f;
    float clientH = chromeH + desired.h;

    // Clamp to the work area. A message long enough to overflow the screen must not
    // push the buttons off it; OnLayout then hands the content a shorter slot and the
    // scroll container the content sits in takes over.
    const float maxClientH = MaxAutoSizeClientHeightDip();
    if (maxClientH > 0.0f && clientH > maxClientH) clientH = maxClientH;

    SetClientSize(clientW, clientH);
    ResizeClientTo(clientW, clientH);
}

// Largest client height an auto-sized dialog may take, in DIPs: a fraction of the
// work area on the monitor the dialog will appear on. Returns 0 when the monitor
// cannot be determined, meaning "no limit".
float DialogWindow::MaxAutoSizeClientHeightDip() const {
    HMONITOR mon = nullptr;
    if (owner_ && IsWindow(owner_))
        mon = MonitorFromWindow(owner_, MONITOR_DEFAULTTONEAREST);
    else if (Hwnd())
        mon = MonitorFromWindow(Hwnd(), MONITOR_DEFAULTTONEAREST);
    if (!mon) return 0.0f;

    MONITORINFO mi = {sizeof(mi)};
    if (!GetMonitorInfo(mon, &mi)) return 0.0f;

    const UINT dpi = Dpi() ? Dpi() : 96;
    const float workHDip =
        static_cast<float>(mi.rcWork.bottom - mi.rcWork.top) * 96.0f / static_cast<float>(dpi);
    return workHDip * kAutoSizeMaxHeightFraction;
}

// Give the window a client area of exactly the requested DIP size.
//
// WM_NCCALCSIZE reserves a ResizeGripOuterPx band on left, right and bottom, so a
// window rect of N is only N - 2*band wide and N - band tall in client terms. The
// bands must be added back here or every dialog ends up 2*band too narrow and one
// band too short -- the same conversion MinTrackSizePx already does for the minimum
// track size.
void DialogWindow::ResizeClientTo(float clientWDip, float clientHDip) {
    if (!Hwnd()) return;
    const UINT dpi = Dpi() ? Dpi() : 96;
    const float scale = static_cast<float>(dpi) / 96.0f;
    const int band = ResizeGripOuterPx(dpi);
    const int wPx = static_cast<int>(clientWDip * scale + 0.5f) + 2 * band;
    const int hPx = static_cast<int>(clientHDip * scale + 0.5f) + band;

    // Keep the dialog centered on its owner after the resize.
    int x = 0, y = 0;
    UINT flags = SWP_NOZORDER | SWP_NOACTIVATE | SWP_NOMOVE;
    RECT ownerRect{};
    if (owner_ && IsWindow(owner_) && GetWindowRect(owner_, &ownerRect)) {
        x = ownerRect.left + ((ownerRect.right - ownerRect.left) - wPx) / 2;
        y = ownerRect.top + ((ownerRect.bottom - ownerRect.top) - hPx) / 2;
        MONITORINFO mi = {sizeof(mi)};
        if (GetMonitorInfo(MonitorFromWindow(owner_, MONITOR_DEFAULTTONEAREST), &mi)) {
            x = std::clamp(x, static_cast<int>(mi.rcWork.left),
                           std::max<int>(mi.rcWork.left, mi.rcWork.right - wPx));
            y = std::clamp(y, static_cast<int>(mi.rcWork.top),
                           std::max<int>(mi.rcWork.top, mi.rcWork.bottom - hPx));
        }
        flags &= ~SWP_NOMOVE;
    }
    SetWindowPos(Hwnd(), nullptr, x, y, wPx, hPx, flags);
}

HRESULT DialogWindow::Show(NativeWindowHost& owner) {
    HRESULT hr = Start(owner);
    if (FAILED(hr)) return hr;
    ShowWindow(Hwnd(), SW_SHOWNORMAL);
    SetForegroundWindow(Hwnd());
    RaiseLoaded();
    return S_OK;
}

DialogResult DialogWindow::ShowDialog(NativeWindowHost& owner) {
    HRESULT hr = Start(owner);
    if (FAILED(hr)) return DialogResult::None;

    ownerWasEnabled_ = IsWindowEnabled(owner.Hwnd()) != FALSE;
    EnableWindow(owner.Hwnd(), FALSE);
    ShowWindow(Hwnd(), SW_SHOWNORMAL);
    SetForegroundWindow(Hwnd());
    RaiseLoaded();

    if (Application* app = OwningApplication()) {
        // Application-driven: its pump services every attached window, so the owner
        // keeps repainting behind the modal dialog.
        app->RunModalUntilClosed(*this);
    } else {
        // Legacy path for hosts without an Application. Each window drives itself
        // via the self-frame timer (UsesExternalFramePump() == false).
        MSG msg;
        while (IsOpen()) {
            const int state = GetMessageW(&msg, nullptr, 0, 0);
            if (state <= 0) {
                if (state == 0) PostQuitMessage(static_cast<int>(msg.wParam));
                break;
            }
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
    }
    // RestoreOwner() removed: owner re-enabling and SetForegroundWindow are now
    // done in RaiseClosing, so they happen for every dismiss path (modal loop exit,
    // non-modal Show, owner-initiated close).
    return result_;
}

void DialogWindow::RestoreOwner() {
    if (ownerWindow_ && ownerWasEnabled_ && ownerWindow_->Hwnd()) {
        EnableWindow(ownerWindow_->Hwnd(), TRUE);
        SetForegroundWindow(ownerWindow_->Hwnd());
    }
    ownerWasEnabled_ = false;
}

bool DialogWindow::Close(DialogResult result) {
    if (!IsOpen() || IsClosing()) return false;
    // Stage the result so RaiseClosing (which runs inside Window::Close) can put it
    // into the dialog args without threading a parameter through the base class.
    pendingResult_ = result;
    return Window::Close();
}

bool DialogWindow::RaiseClosing() {
    // Window channel first, so a handler holding a Window& sees the close and can
    // veto it exactly as it would for any window.
    if (!Window::RaiseClosing()) return false;

    DialogClosingArgs args{pendingResult_, false};
    OnDialogClosing(args);
    dialogClosing_.Raise(*this, args);
    if (args.cancel) return false;

    // Only commit the result once the close is definitely going through — a vetoed
    // close must leave Result() untouched.
    result_ = args.result;
    
    // Transfer foreground back to the owner NOW, while the dialog is still alive.
    // If we wait until RaiseUnloaded/RaiseClosed, the system picks a foreground window
    // itself (often a background app) during the gap, causing visible Z-order flicker
    // (the owner disappears briefly as the background app is raised above it, then
    // reappears when we call SetForegroundWindow later).
    if (ownerWindow_ && ownerWindow_->Hwnd()) {
        EnableWindow(ownerWindow_->Hwnd(), TRUE);
        SetForegroundWindow(ownerWindow_->Hwnd());
        ownerWasEnabled_ = false;  // Mark as already restored
    }
    
    return true;
}

void DialogWindow::RaiseUnloaded() {
    // Owner was already re-enabled and activated in RaiseClosing (before any teardown),
    // so the check here is just a safety net for non-standard dismiss paths (if any).
    if (ownerWindow_ && ownerWasEnabled_ && ownerWindow_->Hwnd()) {
        EnableWindow(ownerWindow_->Hwnd(), TRUE);
        SetForegroundWindow(ownerWindow_->Hwnd());
        ownerWasEnabled_ = false;
    }
    Window::RaiseUnloaded();
}

void DialogWindow::RaiseClosed() {
    Window::RaiseClosed();
    // A dialog dismissed without an explicit result (owner destroyed it, or the
    // system closed it) reads as Cancel rather than the ambiguous None.
    if (result_ == DialogResult::None) result_ = DialogResult::Cancel;
    
    // Owner was already restored in RaiseClosing; ownerWasEnabled_ is false by now.
    
    DialogClosedArgs args{result_};
    OnDialogClosed(args);
    dialogClosed_.Raise(*this, args);
}

// Deliberately synchronous, unlike Window::OnLayout which measures on a worker.
// A dialog's tree is small (a message, a few buttons) and its layout cost is far
// below the ~7ms that motivated async layout, so handing it to a worker would add
// thread-handoff latency and a timeout path to guard, for no measurable win. It
// also never participates in a modal resize drag — the case async layout exists
// for. If a dialog ever grows a large scrolling document, revisit this.
void DialogWindow::OnLayout() {
    UIElement* root = Root();
    if (!root) return;
    const float w = ClientWidthDip();
    const float h = ClientHeightDip();
    const float contentW = std::max(0.0f, w - kDialogPadDip * 2.0f);
    const float titleBar = TitleBarHeightDip();
    // The content slot starts below the title bar plus the top gap and stops at the
    // bottom padding, so its height must subtract BOTH insets. Subtracting only the
    // title bar and bottom pad made the slot kDialogTopGapDip taller than the space
    // actually available, pushing the last row (the action buttons) off the bottom.
    const float contentH =
        std::max(0.0f, h - titleBar - kDialogTopGapDip - kDialogBottomPadDip);
    root->Measure(contentW, contentH);
    root->Arrange({kDialogPadDip, titleBar + kDialogTopGapDip, contentW, contentH});
}

bool DialogWindow::OnAppMessage(UINT msg, WPARAM wp, LPARAM lp, LRESULT& result) {
    UNREFERENCED_PARAMETER(lp);
    if (msg == WM_KEYDOWN && wp == VK_ESCAPE) {
        Close(DialogResult::Cancel);
        result = 0;
        return true;
    }
    if (msg == WM_CLOSE) {
        // Deliberately not delegating to Window::OnAppMessage: its WM_CLOSE path
        // calls Window::Close(), which would bypass the result staging.
        Close(DialogResult::Cancel);
        result = 0;
        return true;
    }
    return false;
}

} // namespace fluent
