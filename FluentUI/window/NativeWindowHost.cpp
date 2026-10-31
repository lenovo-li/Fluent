// NativeWindowHost.cpp

#include "NativeWindowHost.h"
#include "../Application.h"
#include "DirtyRegion.h"  // pure partial-redraw planner
#include "../styling/ThemeTokens.h"
#include "../diagnostics/PerformanceCounters.h"  // QpcNow for the animation clock
#include "HudOverlay.h"                           // off-thread F12 performance HUD
#include <dwmapi.h>
#include <windowsx.h>
#include <shellscalingapi.h>
#include <imm.h>
#include <wincodec.h>
#include <algorithm>

#pragma comment(lib, "dwmapi.lib")
#pragma comment(lib, "user32.lib")
#pragma comment(lib, "shcore.lib")
#pragma comment(lib, "imm32.lib")

namespace fluent {

namespace {
const char* kTag = "NativeWindowHost";
const wchar_t* kClassName = L"FluentUI.NativeWindowHost";

// DWM backdrop / dark-mode / caption attributes now live in WindowAppearance
// — this window drives them through theme_.Appearance().

constexpr float kCaptionBtnWidthDip = 46.0f;
constexpr float kCaptionGlyphDip = 10.0f;
// Above this fraction of the window, a partial redraw is not worth the
// bookkeeping — just clear + repaint the whole frame. Below it, clip + partial
// present. 0.6 keeps the common small-change frames (hover, caret, one control)
// on the cheap path while large scrolls / theme flips take the full path.
constexpr float kPartialRedrawMaxCoverage = 0.6f;
constexpr UINT_PTR kBlinkTimerId = 1;
constexpr UINT_PTR kAnimTimerId = 2;
constexpr UINT_PTR kTooltipTimerId = 3;
constexpr UINT_PTR kSelfFrameTimerId = 4;
constexpr UINT_PTR kDeviceRetryTimerId = 5;  // retry a failed device rebuild
// Period for the LEGACY modal fallback timer (kAnimTimerId), used only when
// ModalFrameHeartbeat could not start — i.e. no waitable timer, pre-Windows 10 1803.
// 10 is USER_TIMER_MINIMUM: asking for less is silently clamped to it, so this is the
// best WM_TIMER can nominally do (~100fps). In practice it delivers far less, because
// WM_TIMER is a low-priority synthesized message whose period is rounded up to the
// system timer resolution (15.6ms default, 5ms measured here) — a 16ms request
// measured ~28.6ms, which is why a press-and-hold on a resize border used to sit at
// ~30-45fps on a 120Hz panel. That limit is intrinsic to WM_TIMER and is the whole
// reason ModalFrameHeartbeat exists; 10 merely stops this path being needlessly worse
// than the API allows.
constexpr UINT kAnimIntervalMs = 10;
constexpr UINT kTooltipDelayMs = 500;  // hover dwell before a tooltip appears
} // namespace

NativeWindowHost::NativeWindowHost() = default;

NativeWindowHost::~NativeWindowHost() {
    // The HUD thread reads hwnd_ geometry and posts to it; join it before the HWND
    // goes. Normally WM_DESTROY already did (this is the safety net, like the rest).
    if (hud_) hud_->Stop();
    if (controlBgBrush_) DeleteObject(controlBgBrush_);
    // A live HWND here means the owner never closed the window before destroying the
    // object (typical of a tray app that keeps the window hidden until process exit,
    // or any Window held in a unique_ptr past the message loop).
    //
    // DestroyWindow() dispatches WM_DESTROY SYNCHRONOUSLY, and we are in the BASE
    // dtor phase: the derived Window (and its root_ element tree) is already gone,
    // so the handler's OnDestroying()/OnDestroyed() virtual calls and its walk over
    // elements_ would touch freed memory. The WM_DESTROY handler therefore bails out
    // when destroying_ is set, and we set it here so the nested dispatch is inert.
    //
    // The teardown that the handler would have done (detach roots, release
    // subscriptions, drop animations, DetachWindow) is done here directly, without
    // any virtual dispatch, and BEFORE the window is destroyed while hwnd_ is valid.
    // Closing the window first (Window::Close()) remains the supported path and
    // makes all of this a no-op; this is the safety net, not the happy path.
    if (hwnd_) {
        if (!destroying_) {
            destroying_ = true;
            KillTimer(hwnd_, kSelfFrameTimerId);
            KillTimer(hwnd_, kDeviceRetryTimerId);
            if (dragDropRegistered_) {
                RevokeDragDrop(hwnd_);
                dragDropRegistered_ = false;
                OleUninitialize();
            }
            dragOverElement_ = nullptr;
            modalHeartbeat_.Stop();
            // elements_ entries are owned by the derived class, which has already
            // been destroyed -- they are dangling now, so they must NOT be
            // dereferenced. Just drop them; each element cleared its own context in
            // ~UIElement (or is gone with the tree).
            elements_.clear();
            // Disconnect UIA (queued element providers, then the root) while hwnd_
            // is still valid, and only AFTER elements_ is empty: with a client
            // attached, UiaDisconnectProvider makes outgoing COM calls and UIA core
            // calls back into us, and while this STA waits on them a cross-thread
            // SendMessage can be dispatched to a handler that walks elements_
            // without a destroying_ guard.
            automation_.OnWindowDestroyed(hwnd_);
            if (application_) application_->DetachWindow(*this);
        }
        HWND doomed = hwnd_;
        hwnd_ = nullptr;   // so the nested WM_DESTROY sees no window to tear down
        DestroyWindow(doomed);
    }
    // Last: the window (destroyed above, or earlier via WM_DESTROY) no longer
    // references the icons, so they can be freed without a dangling WM_SETICON.
    ReleaseWindowIcons();
}

void NativeWindowHost::Close() {
    if (hwnd_ && !destroying_)
        DestroyWindow(hwnd_);
}

// Ask any active popup to close before the window and its elements are torn
// down: while the elements are still alive we invoke the dismiss hook so
// the owning control runs its normal close path (hiding the popup window and
// resetting its Subscription). Idempotent — safe if no popup is open.
void NativeWindowHost::CloseActivePopups() {
    // Teardown: whatever the hook does, nothing may stay registered afterwards.
    DispatchPopupDismiss(PopupDismissReason::Deactivate, nullptr, 0, 0);
    if (popupDismiss_) { popupDismiss_ = nullptr; ++dismissGen_; }
    popupKey_ = nullptr;
}

HRESULT NativeWindowHost::Create(HINSTANCE hInst, const wchar_t* title,
                               float clientWDip, float clientHDip,
                               const WindowState* restore) {
    hInst_ = hInst;
    title_ = title ? title : L"";

    WNDCLASSEXW wc = {sizeof(wc)};
    // CS_DBLCLKS only. Still no CS_HREDRAW/CS_VREDRAW (a DComp window must not be
    // invalidated wholesale on resize) — the two are unrelated flags and
    // adding double-click delivery does not reintroduce that problem.
    //
    // WITHOUT CS_DBLCLKS WINDOWS NEVER SENDS WM_LBUTTONDBLCLK. That is worth stating
    // here because the failure mode is silent and points at the wrong layer: the message
    // handler exists, the routing exists, the control's handler exists, and none of it
    // runs. Time gets spent debugging the click logic instead of the class registration.
    wc.style = CS_DBLCLKS;
    wc.lpfnWndProc = &NativeWindowHost::WndProc;
    wc.hInstance = hInst;
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.lpszClassName = kClassName;
    RegisterClassExW(&wc);  // ignore "already registered"

    // A dialog is created on its owner's monitor, so size it with that window's
    // actual DPI instead of the primary monitor's DPI.
    const HWND ownerHwnd = OwnerHwnd();
    HMONITOR mon = ownerHwnd && IsWindow(ownerHwnd)
        ? MonitorFromWindow(ownerHwnd, MONITOR_DEFAULTTONEAREST)
        : MonitorFromPoint(POINT{0, 0}, MONITOR_DEFAULTTOPRIMARY);
    UINT dx = 96, dy = 96;
    if (ownerHwnd) {
        dx = GetDpiForWindow(ownerHwnd);
        dy = dx;
    } else {
        GetDpiForMonitor(mon, MDT_EFFECTIVE_DPI, &dx, &dy);
    }
    dpi_ = dx ? dx : 96;

    // Read the user's wheel scroll-lines preference (Settings → Bluetooth & devices →
    // Mouse → "Lines to scroll at a time"). Every wheel handler multiplies notches by
    // this; hardcoding 3 ignored the setting entirely. WHEEL_PAGESCROLL (0xFFFFFFFF)
    // means "scroll one page per notch" — controls treat that as a page request.
    {
        UINT lines = 3;
        if (SystemParametersInfoW(SPI_GETWHEELSCROLLLINES, 0, &lines, 0))
            wheelScrollLines_ = lines;
    }

    int w = static_cast<int>(clientWDip * dpi_ / 96.0f + 0.5f);
    int h = static_cast<int>(clientHDip * dpi_ / 96.0f + 0.5f);

    // Center on owner window if present (dialogs should appear in front of their
    // caller, not jump to the primary monitor center when the main window sits
    // elsewhere). Fall back to primary monitor center for top-level windows.
    int x = 0, y = 0;
    RECT ownerRect{};
    const bool haveOwner = ownerHwnd && IsWindow(ownerHwnd) &&
                           GetWindowRect(ownerHwnd, &ownerRect);
    if (haveOwner) {
        x = ownerRect.left + ((ownerRect.right - ownerRect.left) - w) / 2;
        y = ownerRect.top + ((ownerRect.bottom - ownerRect.top) - h) / 2;
        // Clamp to the owner's monitor work area so a dialog spawned from a window
        // near the screen edge doesn't spill off-screen.
        MONITORINFO omi = {sizeof(omi)};
        if (GetMonitorInfo(MonitorFromWindow(ownerHwnd, MONITOR_DEFAULTTONEAREST), &omi)) {
            x = std::clamp(x, static_cast<int>(omi.rcWork.left),
                           std::max<int>(omi.rcWork.left, omi.rcWork.right - w));
            y = std::clamp(y, static_cast<int>(omi.rcWork.top),
                           std::max<int>(omi.rcWork.top, omi.rcWork.bottom - h));
        }
    } else {
        // No owner: center on primary monitor (top-level window).
        MONITORINFO mi = {sizeof(mi)};
        GetMonitorInfo(mon, &mi);
        x = mi.rcWork.left + ((mi.rcWork.right - mi.rcWork.left) - w) / 2;
        y = mi.rcWork.top + ((mi.rcWork.bottom - mi.rcWork.top) - h) / 2;
    }

    RECT initialRect = {x, y, x + w, y + h};
    AdjustInitialWindowRect(initialRect, dpi_);
    // Override the centered rect with the caller-supplied saved placement.
    ApplyRestoreState(initialRect, dpi_, restore);
    w = std::max<int>(1, initialRect.right - initialRect.left);
    h = std::max<int>(1, initialRect.bottom - initialRect.top);

    // WS_EX_NOREDIRECTIONBITMAP so content is fully DComp-owned (no GDI redirection
    // surface underneath).
    //
    // WS_OVERLAPPED (not WS_POPUP) + WS_THICKFRAME: the OVERLAPPED bit is what
    // makes Windows give the window a real sizing frame — including the
    // SM_CXPADDEDBORDER-width resize capture zone that extends a few px PAST
    // the visible edge. WS_POPUP windows get no such outside grip, which is
    // why the user had to hit the exact 8 px strip inside the window (and why
    // that strip then collided with the ScrollViewer's overlay scrollbar).
    // DwmExtendFrameIntoClientArea below already hides the visible frame, so
    // the look is identical; only the hit-test geometry changes.
    DWORD exStyle = WS_EX_NOREDIRECTIONBITMAP | ExtraExStyle();
    DWORD style = WS_OVERLAPPED | WS_THICKFRAME | WS_MINIMIZEBOX | WS_MAXIMIZEBOX | WS_SYSMENU;
    hwnd_ = CreateWindowExW(
        exStyle, kClassName, title_.c_str(),
        style, initialRect.left, initialRect.top, w, h,
        OwnerHwnd(), nullptr, hInst, this);
    if (!hwnd_) {
        Trace(kTag, "CreateWindowExW FAILED", HRESULT_FROM_WIN32(GetLastError()));
        return E_FAIL;
    }
    SetPropW(hwnd_, L"FluentUI.AccessibleName", const_cast<wchar_t*>(title_.c_str()));

    // The window may have been placed (restored) on a monitor whose DPI differs
    // from the primary monitor's `dpi_` computed above. If we keep the primary
    // DPI, InitGraphics and the first Render use the wrong scale and every
    // control is drawn too big/small until a WM_DPICHANGED arrives (which may be
    // too late — the content surface is already sized). Re-read the authoritative
    // per-window DPI now so the initial frame is correct (never a cached global DPI).
    UINT winDpi = GetDpiForWindow(hwnd_);
    if (winDpi) dpi_ = winDpi;

    // The scheduler is the animation CLOCK (running_ flag + QPC dt); it no longer
    // drives an OS timer for normal animation — the main message loop pumps
    // animation once per due frame (PumpAnimations), paced by the frame waiter, so we can
    // run at the display refresh rate. A modal driver is needed ONLY while a system
    // modal loop (drag-resize) blocks our loop; that is armed/disarmed explicitly in
    // WM_ENTERSIZEMOVE / WM_EXITSIZEMOVE. So the arm/disarm edges here only need to
    // cover the case where an animation STARTS mid-drag, and they must not touch the
    // heartbeat's lifetime otherwise (WM_ENTERSIZEMOVE owns that).
    //
    // The heartbeat replaced a SetTimer(kAnimIntervalMs) fallback. That timer was
    // hard-capped at 16ms AND rounded up to the system tick, so a press-and-hold on
    // a resize border ran at 30-45fps on a 120Hz panel with the CPU 70% idle — the
    // measured symptom that motivated ModalFrameHeartbeat.
    frame_.Scheduler().SetCallbacks(
        [this] {
            if (inModalLoop_)
                modalHeartbeat_.Start(hwnd_, RefreshIntervalMs(RefreshHz()));
        },
        [this] { if (!inModalLoop_) modalHeartbeat_.Stop(); });

    // Client size in physical pixels (use GetClientRect; window is not shown yet
    // but WS_OVERLAPPEDWINDOW has a valid frame). Fall back to our computed size.
    RECT rc;
    GetClientRect(hwnd_, &rc);
    pixelW_ = std::max<UINT>(1, rc.right - rc.left);
    pixelH_ = std::max<UINT>(1, rc.bottom - rc.top);
    if (pixelW_ <= 1 || pixelH_ <= 1) { pixelW_ = w; pixelH_ = h; }

    FL_RETURN_IF_FAILED(kTag, InitGraphics(pixelW_, pixelH_));
    RecreateWindowIcon();

    // Build the service context now that the device / DWrite are up, so any
    // element registered in OnCreated is attached with a fully-wired context.
    // Elements read services through Context() instead of the
    // old manual SetDWrite / Attach(WindowServices*) calls.
    context_.window = this;
    context_.dwrite = &graphics_.DWrite();
    context_.animations = &frame_.Anims();
    context_.hwnd = hwnd_;
    context_.dpiScale = DpiScale();
    context_.resourceCache = &graphics_.Cache();
    // Hand the tree the host's stable theme snapshot. The
    // pointer never changes for the window's lifetime — a theme change overwrites
    // the snapshot in place (ThemeManager::Set*) and bumps its generation, so the
    // parked pointer stays valid. The app sets the initial mode via SetDarkMode /
    // FollowSystemTheme before or after Create; the manager defaults to light.
    context_.theme = &theme_.Manager().Snapshot();
    // UI Automation site. Costs nothing until a client sends WM_GETOBJECT; an
    // element creates a peer only when a client reaches it (automation/).
    automation_.Bind(this);
    context_.automation = &automation_;

    // Wire the input routing via InputHost, which encapsulates the
    // InputManager, FocusManager, and ClickCounter.
    input_.Initialize(hwnd_, &context_, &elements_,
                      &NativeWindowHost::InvalidateThunk, this);
    // Run the caret-blink timer only while a blink-wanting element holds focus.
    focusChangedSub_ = input_.Focus().FocusChanged().Subscribe(this,
        [](void* owner, FocusManager&, FocusChangedArgs& e) {
            auto* self = static_cast<NativeWindowHost*>(owner);
            self->UpdateBlinkTimer(e.newFocus);
            // A null check unless a UIA client has connected and is listening.
            self->automation_.OnFocusChanged(e.newFocus);
        });

    theme_.Appearance().SetHwnd(hwnd_);
    ApplyMica();
    ApplyDarkTitleBar();
    theme_.Appearance().SetCornerPreference(CornerPreferenceForRadius(cornerRadiusDip_));
    theme_.Appearance().SetShadowEnabled(shadowEnabled_);
    RecreateControlBrushes();

    // Window-level tooltip service (shares the window's D2D/DWrite devices).
    // A failure here is non-fatal: the window works, just without tooltips.
    tooltip_ = std::make_unique<TooltipService>();
    if (FAILED(tooltip_->Create(hInst_, hwnd_, &graphics_.D2D(), &graphics_.DWrite(),
                                &theme_.Manager().Snapshot()))) {
        TraceMsg(kTag, "Create: TooltipService unavailable");
        tooltip_.reset();
    }

    OnCreated();
    OnLayout();

    // Register OLE drag-drop (must happen after HWND exists, before ShowWindow).
    // Failure is non-fatal — the window works, just without drop support.
    if (SUCCEEDED(OleInitialize(nullptr))) {
        if (SUCCEEDED(RegisterDragDrop(hwnd_, this))) {
            dragDropRegistered_ = true;
            Trace(kTag, "RegisterDragDrop succeeded", S_OK);
        } else {
            TraceMsg(kTag, "RegisterDragDrop failed");
        }
    } else {
        TraceMsg(kTag, "OleInitialize failed — drop unavailable");
    }

    // Draw the first frame, DwmFlush, then ShowWindow, so the window never appears
    // with empty content. Synchronous
    // (RenderNow, not the coalesced Render) — the message loop is not running yet.
    //
    // SWP_FRAMECHANGED before ShowWindow: with WS_OVERLAPPED the window starts
    // life with a real caption frame, and WM_NCCALCSIZE(→0) only strips it when
    // the frame is next recalculated — which, without this call, is the first
    // user resize. Forcing a frame recalc now makes the very first shown frame
    // already caption-free, so the user never sees a transient double title bar
    // (system caption on top, our custom one below).
    SetWindowPos(hwnd_, nullptr, 0, 0, 0, 0,
                 SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE |
                 SWP_FRAMECHANGED);
    RenderNow(FrameReason::Paint);
    DwmFlush();
    ShowWindow(hwnd_, restoreMaximized_ ? SW_SHOWMAXIMIZED : InitialShowCommand());
    UpdateWindow(hwnd_);
    return S_OK;
}

HRESULT NativeWindowHost::InitGraphics(UINT pixelW, UINT pixelH) {
    // The device stack lives in GraphicsHost; this host only wires the frame request.
    FL_RETURN_IF_FAILED(kTag, graphics_.Initialize(hwnd_, pixelW, pixelH,
        [this] { RequestFrame(FrameReason::Paint); }));
    graphicsReady_ = true;
    return S_OK;
}

void NativeWindowHost::ApplyMica() {
    // Resolve the requested material against the live environment (OS build /
    // composition / transparency / high-contrast / RDP) and apply it with a solid
    // fallback. The resolution also tells us whether high-contrast
    // is forcing a solid, opaque window — feed that into the theme so controls
    // paint legibly, and re-seed the dark title bar through the resolver.
    ResolvedBackdrop r = theme_.Appearance().ApplyBackdrop(requestedBackdrop_);
    if (theme_.Manager().Inputs().highContrast != r.suppressDarkTitleBar) {
        // High contrast is detected via the same resolver signal (it suppresses
        // the dark title bar); mirror it into the theme snapshot so controls can
        // switch to a high-contrast-safe path.
        theme_.Manager().SetHighContrast(r.suppressDarkTitleBar);
    }
    if (r.useSolidFallback)
        Trace(kTag, "backdrop: solid fallback in effect (painting base fill)", S_OK);
    // The base fill is part of every frame's pixels, so a resolution change must
    // repaint the whole window — a partial redraw would leave the old base behind
    // outside the dirty rect.
    fullDrawRequired_ = 2u;
    if (graphicsReady_) Render();
}

BaseFillPlan NativeWindowHost::BaseFill() const {
    return ResolveBaseFill(theme_.Appearance().LastResolved(),
                           theme_.Manager().Snapshot().colors.windowBackground,
                           backgroundOpacity_);
}

void NativeWindowHost::SetBackgroundOpacity(float opacity) {
    const float v = (opacity < 0.0f) ? 0.0f : (opacity > 1.0f ? 1.0f : opacity);
    if (backgroundOpacity_ == v) return;
    backgroundOpacity_ = v;
    // Same reasoning as ApplyMica: the base fill covers the whole client area.
    fullDrawRequired_ = 2u;
    if (graphicsReady_) Render();
}

void NativeWindowHost::ApplyDarkTitleBar() {
    theme_.Appearance().SetDarkTitleBar(theme_.Manager().Snapshot().dark);
}

void NativeWindowHost::ApplyTheme() {
    ApplyDarkTitleBar();
    RecreateControlBrushes();
    // Bump the resource-cache epoch so any theme-dependent cached resource is
    // rebuilt on next use rather than reused stale. Cached layouts depend on
    // theme through the typography tokens controls measure with (e.g.
    // Button/CheckBox read typography.bodySize).
    graphics_.Cache().BumpEpoch();
    for (UIElement* e : elements_)
        if (e) e->OnThemeChanged();
    // A theme flip repaints every control + the caption chrome — force
    // a full clear so no partial redraw leaves stale-colored pixels behind.
    fullDrawRequired_ = 2u;
    Render();
    ThemeChangedArgs args;
    themeChanged_.Raise(*this, args);
}

HRESULT NativeWindowHost::Create(Application& application, const wchar_t* title,
                               float clientWDip, float clientHDip,
                               const WindowState* restore) {
    if (!application.IsUiThread() || IsOpen()) return E_INVALIDARG;
    // application_ should already be set by PrepareContent before this is called.
    // If it's not, the caller skipped PrepareContent (a lifecycle violation), but
    // set it here as a fallback so legacy code that never called PrepareContent
    // continues to work — though OwningApplication() won't be valid in OnInitialize
    // for them. New code should always funnel through Window::Show or
    // DialogWindow::Start, both of which call PrepareContent(&app) first.
    //
    // Guard against attaching to a DIFFERENT Application: if application_ is already
    // set and doesn't match the one we're being given, that's a real error.
    if (application_ && application_ != &application) return E_INVALIDARG;
    if (!application_) application_ = &application;
    HRESULT hr = Create(application.Instance(), title, clientWDip, clientHDip, restore);
    if (FAILED(hr)) { application_ = nullptr; return hr; }
    if (!application.AttachWindow(*this)) {
        application_ = nullptr;
        Close();
        return E_FAIL;
    }
    return S_OK;
}

void NativeWindowHost::SetCornerRadius(float radiusDip) {
    cornerRadiusDip_ = std::max(0.0f, radiusDip);
    if (hwnd_)
        theme_.Appearance().SetCornerPreference(CornerPreferenceForRadius(cornerRadiusDip_));
}

void NativeWindowHost::SetShadowEnabled(bool enabled) {
    shadowEnabled_ = enabled;
    if (hwnd_) theme_.Appearance().SetShadowEnabled(enabled);
}

void NativeWindowHost::RecreateControlBrushes() {
    if (controlBgBrush_) {
        DeleteObject(controlBgBrush_);
        controlBgBrush_ = nullptr;
    }
    const ColorTokens& pal = theme_.Manager().Snapshot().colors;
    COLORREF bg = RGB(static_cast<int>(pal.cardFill.r * 255.0f),
                      static_cast<int>(pal.cardFill.g * 255.0f),
                      static_cast<int>(pal.cardFill.b * 255.0f));
    controlBgBrush_ = CreateSolidBrush(bg);
}

void NativeWindowHost::AddElement(UIElement* e) {
    if (!e) return;
    elements_.push_back(e);
    // Attach the element (and its subtree) to the live service context so it gets
    // DWrite / device / popup registration without any manual wiring.
    // Guarded on graphicsReady_ so a stray pre-InitGraphics call is a no-op;
    // AddElement is normally called from OnCreated, after the context is built.
    if (graphicsReady_) e->AttachToContext(context_);
    
    // Install the invalidate callback on tree roots (elements with no parent). Without
    // this, InvalidateDirty walks up to the root, finds no callback, and silently drops
    // the frame request — so DataGrid::RefreshCells, property setters, etc. never repaint
    // until an unrelated event (mouse move, resize) triggers a frame. The symptom is a
    // UI that updates only when you move the mouse over it.
    if (!e->Parent()) {
        e->SetInvalidateCallback(
            [](void* ctx) {
                auto* self = static_cast<NativeWindowHost*>(ctx);
                self->RequestFrame(FrameReason::Paint);
            },
            this);
    }
}

bool NativeWindowHost::RemoveElement(UIElement* e) {
    if (!e) return false;
    auto it = std::find(elements_.begin(), elements_.end(), e);
    if (it == elements_.end()) return false;
    elements_.erase(it);

    // The armed tooltip target and the drag-over target are raw pointers that may
    // point anywhere inside the removed subtree, and walking the subtree to find out
    // costs more than just dropping them. Dropping them is harmless: the next pointer
    // move or DragOver re-derives both from a fresh hit test.
    HideTooltip();
    dragOverElement_ = nullptr;

    // Same order as the WM_DESTROY teardown: clear input state for the root first,
    // then detach the subtree (DetachFromContext notifies input for every
    // descendant on the way down, while the context is still valid).
    input_.Input().OnElementDetached(e);
    if (e->IsAttached()) e->DetachFromContext();
    if (!e->Parent()) e->SetInvalidateCallback(nullptr, nullptr);

    if (IsOpen()) RequestFrame(FrameReason::Layout);
    return true;
}

void NativeWindowHost::SetFocusElement(UIElement* e) {
    // Forward to the single focus authority; the caret-blink timer follows the
    // FocusChanged event (see UpdateBlinkTimer).
    input_.Focus().SetFocus(e);
}

void NativeWindowHost::UpdateBlinkTimer(UIElement* newFocus) {
    // Run a caret-blink timer only while a blink-wanting element is focused.
    bool wantBlink = newFocus && newFocus->WantsBlink();
    if (wantBlink && !blinkTimerOn_) {
        UINT period = GetCaretBlinkTime();
        if (period == 0 || period == INFINITE) period = 530;  // OS default
        SetTimer(hwnd_, kBlinkTimerId, period, nullptr);
        blinkTimerOn_ = true;
    } else if (!wantBlink && blinkTimerOn_) {
        KillTimer(hwnd_, kBlinkTimerId);
        blinkTimerOn_ = false;
    }
}

void NativeWindowHost::UpdateAnimationTimer() {
    // Rebuild the active-animation set from the element tree (a leaf adds itself
    // when it wants a tick; panels recurse). It runs on input events AND at the end
    // of every painted frame (see RenderNow), so it is a whole-tree walk per frame,
    // not O(animating): the per-frame TICK is what stays O(animating), because
    // between Collects the clock ticks only the collected set. An idle window paints
    // nothing and walks nothing. Since the dirty queries are pruned to the dirty
    // path, this is now the one O(tree) walk left in a painted frame.
    //
    // It is deliberately NOT event-driven (elements registering on their
    // WantsAnimationTick rising edge) and NOT pruned by the subtree dirty mark.
    // WantsAnimationTick is a pure query over private state with ~26 overrides,
    // and several have no single setter to hook: SpinnerControl starts true from
    // its constructor (the edge is the attach, not a setter), the scroll-backed
    // controls (ListBox, DataGrid, ComboBox's list, ScrollPanel) answer
    // ScrollViewer::NeedsTick from an embedded model, and an app subclass may flip
    // its own member with no notification at all. Missing one edge means a control
    // that silently never animates. The dirty mark cannot stand in for the edge
    // either: Collect rebuilds the set from scratch, so skipping a clean subtree
    // would drop an element that is mid-animation but did not invalidate.
    // Arm the clock iff something wants a tick; the scheduler owns the edge logic.
    //
    // Auxiliary roots (open PopupHost content) have no FrameHost of their own and
    // share this clock. They MUST go through the same single Collect call:
    // AnimationRegistry::Collect clears the active set first, so a second Collect
    // for the aux roots would wipe everything the main tree just registered.
    // Fast path: no open popup -> collect elements_ directly, no allocation.
    if (auxAnimRoots_.empty()) {
        frame_.Anims().Collect(elements_);
    } else {
        std::vector<UIElement*> roots;
        roots.reserve(elements_.size() + auxAnimRoots_.size());
        roots.insert(roots.end(), elements_.begin(), elements_.end());
        roots.insert(roots.end(), auxAnimRoots_.begin(), auxAnimRoots_.end());
        frame_.Anims().Collect(roots);
    }
    frame_.Scheduler().SetWanted(!frame_.Anims().Empty());
}

// Coalesce a frame request. The old input path called Render()
// which painted immediately; now Render() forwards here and the actual paint
// (RenderNow) happens once per message loop turn via RunDueFrame. Modal paths
// that run the *system* pump (drag-resize, WM_DPICHANGED) still call RenderNow
// directly, because our loop is not spinning during those.
void NativeWindowHost::RequestFrame(FrameReason reason) {
    frame_.Scheduler().RequestFrame(reason);
    if (!application_ && !UsesExternalFramePump() && hwnd_ && !destroying_)
        SetTimer(hwnd_, kSelfFrameTimerId, 1, nullptr);
}

// Service one coalesced frame if the loop has decided to render this turn. The
// scheduler snapshots+clears the pending request (BeginFrame) so a re-invalidate
// during painting is preserved for the next turn, then we paint.
void NativeWindowHost::RunDueFrame() {
    if (!frame_.Scheduler().NeedsFrame()) return;
    FrameReason serviced = frame_.Scheduler().BeginFrame();
    // Resize coalescing: fold any coalesced WM_SIZE into this frame (one ResizeBuffers +
    // OnLayout at most) before painting. No-op when no resize is pending.
    ApplyPendingResize();
    RenderNow(serviced);
    frame_.Scheduler().EndFrame();
}

// THE animation clock. Called once per painted frame, immediately before
// RunDueFrame: by Application::PumpOneTurn only on a turn whose frame is due
// (AnimationTickIsDue), by the self-frame timer, and by the modal-loop frame paths.
// While the clock is armed (something is animating), advance the active set by
// the real QPC elapsed time and request the next frame so the loop keeps
// cycling, paced by the loop's FrameWaitRemaining() wait through FrameWaiter,
// i.e. at the display's refresh rate. (That pacing used to come free from the swap
// chain's blocking Present1; the DComp surface's Commit does not block, so the loop
// must supply the wait itself.) Ticking from our own loop (not only in WM_TIMER)
// means a slider drag or any pointer-capture flood no longer starves the
// animation: the loop keeps running and pumping. Does NOT paint; it only advances
// state and schedules the frame, and RunDueFrame paints right after.
bool NativeWindowHost::PumpAnimations() {
    if (!frame_.Scheduler().Running()) return false;
    const float dt = frame_.Scheduler().ComputeDt(QpcNow());
    // Time the tick and carry it to the next frame's stats. This runs OUTSIDE
    // RenderNow, so cpuFrameMs cannot see it — and it is where the compositor
    // surface refills live (a control's OnAnimationTick re-rasterizes its scroll
    // surface here, which for a long document is the most expensive UI-thread work
    // in the whole scroll path). Without this the overscan refill cost is invisible.
    const int64_t tickStart = QpcNow();
    const bool stillAnimating = frame_.Anims().Tick(dt);
    // Accumulate, do not overwrite: more than one tick can precede one painted frame
    // (inside a system modal loop both the fallback anim timer and the frame
    // heartbeat tick), and the frame's stats must carry all of that cost, not just
    // the last tick's. RenderNow consumes and zeroes it.
    pendingAnimationMs_ += QpcToMs(QpcNow() - tickStart);
    if (stillAnimating) {
        RequestFrame(FrameReason::Animation);
    } else {
        // Falling edge: nothing left to animate. Disarm the clock so the loop
        // returns to the pure-idle input wait (SetWanted handles the edge).
        frame_.Scheduler().SetWanted(false);
    }
    return stillAnimating;
}

void NativeWindowHost::RenderNow(FrameReason reason) {
    if (!graphicsReady_) return;

    // Re-entrancy guard: laying out or painting can call SetOffset/Invalidate,
    // which routes back here through the root callback. Coalesce those into the
    // in-flight frame instead of nesting BeginContentFrame/EndContentFrame. This
    // is checked BEFORE resetting stats/timers so a reentrant call does not zero
    // the in-flight frame's metrics.
    if (rendering_) return;
    rendering_ = true;

    // Record metrics for this frame. The reason/dirty counts are
    // sampled into lastFrameStats_ for a Debug HUD; direct RenderNow callers
    // (OnSize/OnDpiChanged/anim tick) pass their own reason so the HUD shows why
    // each frame happened. cpuFrameMs is timed manually (QPC) so it is finalised
    // BEFORE the diagnostics_.Frames().Push at the end (a ScopedTimer would fire too late).
    lastFrameStats_.Reset();
    lastFrameStats_.frameReason = static_cast<uint32_t>(reason);
    lastFrameStats_.activeAnimations = static_cast<uint32_t>(frame_.Anims().Count());
    // The animation tick that led to this frame ran before RenderNow (PumpAnimations),
    // so its cost is carried over rather than timed here. Consume it so the next frame
    // does not double-report a tick that did not happen.
    lastFrameStats_.animationMs = pendingAnimationMs_;
    pendingAnimationMs_ = 0.0;
    // Same carry-over for ApplyPendingResize, which likewise ran before this frame's
    // clock started (RunDueFrame / ServiceModalFrame apply the resize, then paint).
    lastFrameStats_.resizeMs = pendingResizeMs_;
    pendingResizeMs_ = 0.0;
    graphics_.Cache().ResetFrameStats();
    const int64_t frameStartQpc = QpcNow();

    // --- Dirty analysis -----------------------------------------------------
    // Does anything need a re-measure, and what are the bounds of every element
    // whose own dirty flags are set? The dirty-bounds union drives the
    // partial-redraw decision; its size is the accurate dirtyElements count
    // (every re-rendered element, not just measure roots).
    //
    // These three calls, and the ClearDirtySubtree after the paint, are pruned by
    // the subtree dirty mark (Visual::SubtreeDirty): each skips every subtree no
    // invalidation touched since the last clear. A caret blink or a hover
    // therefore costs the dirty path, not the whole tree; the two layout queries
    // are a single flag test at the root when no Measure/Arrange is pending.
    // FluentUIBench's DirtyWalk scene measures exactly this sequence.
    bool measureDirty = false;
    bool arrangeDirty = false;
    // Member scratch, cleared not reallocated: once it has grown to a typical
    // frame's dirty count, a frame costs no heap allocation here. RenderNow is
    // guarded against re-entry (rendering_), so the one buffer is never shared.
    std::vector<RectDip>& dirtyBounds = dirtyBoundsScratch_;
    dirtyBounds.clear();
    for (UIElement* e : elements_) {
        if (!e) continue;
        if (e->AnyDirtyInSubtree(DirtyFlags::Measure)) measureDirty = true;
        if (e->AnyDirtyInSubtree(DirtyFlags::Arrange)) arrangeDirty = true;
        e->CollectDirtyBounds(dirtyBounds);
    }
    lastFrameStats_.dirtyElements = static_cast<uint32_t>(dirtyBounds.size());

    // Arrange counts as much as Measure. This used to gate on measureDirty alone,
    // so an Arrange-ONLY invalidation never ran layout at all: arrangeDirty was
    // computed but only consulted further down to force a full repaint, which
    // redraws every element at the bounds it already had. Nothing had needed
    // Arrange-without-Measure before ScrollPanel started expressing its scroll
    // offset as child bounds — with the gate as it was, a wheel notch repainted
    // the scrollbar (drawn from the live offset) while the content stayed put,
    // and only a resize (which calls OnLayout unconditionally) appeared to fix it.
    if (measureDirty || arrangeDirty) {
        ScopedTimer layoutTimer(lastFrameStats_.layoutMs);
        OnLayout();
    }

    // Union the per-element dirty rects into one region (window DIPs).
    RectDip dirtyDip;
    for (const RectDip& r : dirtyBounds) dirtyDip = RectDip::Union(dirtyDip, r);

    const float wDip = ClientWidthDip();
    const float hDip = ClientHeightDip();
    const float s = DpiScale();

    // Caption hover/press is host chrome, invisible to the element dirty walk —
    // a change there forces a full redraw so the caption glyphs repaint.
    const bool captionChanged =
        (hotCaption_ != prevHotCaption_) || (pressedCaption_ != prevPressedCaption_);

    // --- Decide full vs partial redraw -------------------------------------
    // Delegated to the pure PlanRedraw helper (DirtyRegion.h) so the decision is
    // testable headless. Partial is unsafe on warm-up frames, on any layout change
    // (Measure/Arrange can move bounds anywhere), or when the caption chrome
    // changed — collapse those into forceFull. The HUD is deliberately NOT a reason:
    // it lives on its own composition target (HudOverlay.h), so it neither needs
    // nor may cause a full redraw — that would make it measure a different program.
    const bool forceFull =
        (fullDrawRequired_ > 0) || measureDirty || arrangeDirty || captionChanged;
    const RedrawPlan plan = PlanRedraw(dirtyDip, wDip, hDip, s,
                                       pixelW_, pixelH_, kPartialRedrawMaxCoverage,
                                       forceFull);
    const bool partial = plan.partial;
    const RectDip redrawDip = plan.redrawDip;
    const RECT dirtyPx = plan.dirtyPx;

    ID2D1DeviceContext* dc = nullptr;
    POINT netOffset = {};
    // The content is now a DComp surface (not a swap chain): pass the dirty rect in
    // pixels on a partial frame so only that region is redrawn (the surface is
    // persistent). netOffset folds the surface's atlas tile origin (minus the
    // update-rect origin) into our transform below.
    HRESULT beginHr = graphics_.Comp().BeginContentFrame(
        &dc, &netOffset, /*fullClear=*/!partial, partial ? &dirtyPx : nullptr);
    if (FAILED(beginHr) || !dc) {
        rendering_ = false;
        // A lost device surfaces here (D2DERR_RECREATE_TARGET / device removed):
        // rebuild the stack and repaint. A transient failure just skips this frame.
        if (deviceLost_.NotifyFrameResult(ClassifyDeviceHResult(beginHr)))
            RecoverDevice();
        return;
    }

    // Paint in DIPs: DPI scale, composed onto the surface's net atlas translation
    // so DIP (0,0) lands at the surface tile origin. Row-vector order: the scale is
    // applied first, then the pixel-space translation — Scale(s) * Translation(net).
    dc->SetTransform(D2D1::Matrix3x2F::Scale(s, s) *
                     D2D1::Matrix3x2F::Translation(static_cast<float>(netOffset.x),
                                                   static_cast<float>(netOffset.y)));

    // One reusable solid brush per frame, shared by every element via the
    // DrawingContext: controls draw through typed methods
    // that set this one brush's color, instead of each creating its own per frame.
    ComPtr<ID2D1SolidColorBrush> brush;
    if (FAILED(dc->CreateSolidColorBrush(D2D1::ColorF(0, 0, 0), brush.GetAddressOf()))) {
        dc->SetTransform(D2D1::Matrix3x2F::Identity());
        graphics_.Comp().EndContentFrame();
        rendering_ = false;
        return;
    }

    // On a partial frame, scope everything to the dirty rect: clear only that
    // sub-region to transparent (Mica shows through) then repaint the elements
    // that intersect it. The DrawingContext carries the dirty region as its
    // ClipHint so Panel::Render culls children outside it. On a full frame the
    // clip hint is unbounded (nullptr) so every element is visited.
    {
        uint32_t drawOps = 0;
        ClipGuard partialClip;
        const RectDip* hint = nullptr;
        if (partial) {
            partialClip = ClipGuard(dc, D2D1::RectF(redrawDip.x, redrawDip.y,
                                                    redrawDip.right(), redrawDip.bottom()));
            dc->Clear(D2D1::ColorF(0, 0, 0, 0));  // transparent, dirty region only
            hint = &redrawDip;
        }

        // Window BASE FILL. The content surface is premultiplied
        // alpha over the DWM material, so a cleared frame is see-through by
        // design — correct only while a material actually exists. Without one
        // (pre-Win11, composition off, RDP, high contrast, or BackdropKind::None)
        // there is no material AND no redirection bitmap, so the desktop shows
        // through the whole window and every material-authored translucent
        // control fill loses the opaque base it was designed against. Paint that
        // base here, over exactly the region being redrawn.
        const BaseFillPlan base = BaseFill();
        if (base.fill) {
            const RectDip fillArea = partial ? redrawDip : RectDip{0, 0, wDip, hDip};
            brush->SetColor(base.color);
            dc->FillRectangle(D2D1::RectF(fillArea.x, fillArea.y, fillArea.right(),
                                          fillArea.bottom()),
                              brush.Get());
        }

        DrawingContext rc{dc, brush.Get(), s, &drawOps, hint};

        // Time the element traversal + D2D draw (fills renderMs) so the HUD can
        // separate "build the frame" cost from present cost — key for diagnosing
        // resize frame drops (is layout, render, or present the bottleneck?).
        ScopedTimer renderTimer(lastFrameStats_.renderMs);
        OnRender(dc);
        for (UIElement* e : elements_)
            if (e) e->RenderWithOpacity(rc);
        DrawWindowIcon(dc);
        DrawStandardTitle(dc);
        DrawCaptionButtons(dc, s);
        lastFrameStats_.drawOps = drawOps;
    }  // partialClip pops here (before SetTransform / present)

    dc->SetTransform(D2D1::Matrix3x2F::Identity());
    // Time EndContentFrame (D2D EndDraw + Present1) + DComp Commit (fills
    // presentMs).
    HRESULT endHr;
    {
        ScopedTimer presentTimer(lastFrameStats_.presentMs);
        endHr = partial ? graphics_.Comp().EndContentFrame(&dirtyPx, 1)
                        : graphics_.Comp().EndContentFrame();
        graphics_.Comp().Commit();
    }
    lastFrameStats_.dirtyRects = partial ? 1u : 0u;

    // Frame serviced: clear the tree's accumulated dirty flags.
    for (UIElement* e : elements_)
        if (e) e->ClearDirtySubtree();

    prevHotCaption_ = hotCaption_;
    prevPressedCaption_ = pressedCaption_;
    if (fullDrawRequired_ > 0) --fullDrawRequired_;

    // Sample resource-cache diagnostics for this frame.
    const CacheStats& cs = graphics_.Cache().Stats();
    lastFrameStats_.textLayoutsNew = cs.misses;
    lastFrameStats_.textLayoutCacheHits = cs.hits;

    // Finalise the CPU frame time and feed the P95/P99 ring.
    const int64_t frameEndQpc = QpcNow();
    lastFrameStats_.cpuFrameMs = QpcToMs(frameEndQpc - frameStartQpc);
    diagnostics_.Frames().Push(static_cast<float>(lastFrameStats_.cpuFrameMs));

    ++paintedFrames_;

    // Session diagnostic capture (in-memory, zero-alloc, no I/O; never requests a
    // frame). Uses the interval since the previous painted frame as the real
    // cadence; the phase is derived from the current modal/animation state.
    //
    // THE IDLE GUARD IS THE SAME PREDICATE THE INTERVAL RING USES, deliberately.
    // This call site used to carry its own copy of the rejected `gap <= 500.0`
    // threshold, which FrameIntervalIsCadenceSample exists to replace (see the
    // reasoning at FramePacing.h and at the interval-ring push below): a threshold
    // cannot distinguish "the window idled 3s" from "this frame COST 3s", and it
    // resolves the ambiguity the wrong way on exactly the workload the log is read
    // for. The fix reached the ring but not this line, and the comment here still
    // claimed the two matched — so a genuinely 3-second frame was recorded as
    // intervalMs = 0 and vanished from the jank list and the phase average. The log
    // is written on close to prove timing regressions after the fact, which made the
    // *worst* hitches the ones guaranteed to be absent from it.
    //
    // A frame that is not a cadence sample is passed as intervalMs = 0, which
    // RecordFrame reads as "no sample": the frame is counted, but it enters neither
    // the phase's average interval nor the jank list.
    if (sessionLogEnabled_) {
        float intervalMs = 0.0f;
        if (FrameIntervalIsCadenceSample(lastFrameQpc_ != 0, loopWasContinuous_))
            intervalMs = static_cast<float>(QpcToMs(frameEndQpc - lastFrameQpc_));
        diagnostics_.Log().RecordFrame(lastFrameStats_, intervalMs,
                                CurrentSessionPhase(reason), SessionClockSec());
    }

    // Feed the frame-INTERVAL ring (real cadence for FPS/jank on the HUD): the gap
    // since the previous painted frame. Skip the first frame (no prior timestamp),
    // and skip a gap that spans an IDLE period — the loop blocking on input is not a
    // cadence sample and would read as one enormous stutter when the HUD opens.
    //
    // WHY THIS TESTS STATE AND NOT THE GAP'S SIZE. This guard used to be
    // `intervalMs <= 500.0`: treat anything big as an idle resume. That cannot
    // distinguish the two cases it has to distinguish —
    //
    //     the window slept 3s waiting for input   -> not a sample, discard
    //     this frame genuinely COST 3s            -> the most important sample there is
    //
    // — and it silently chose wrong on exactly the workload where the numbers matter.
    // Dragging the edge of a window holding a large document, every real interval
    // exceeded 500ms and was discarded as "idle", leaving the ring holding only the
    // cheap frames in between. The HUD then reported four-digit FPS against a window
    // that was visibly redrawing about once per second: the measurement excluded the
    // entire cost it existed to measure.
    //
    // The honest question is whether the loop was CONTINUOUSLY BUSY across the gap,
    // which is a property of the scheduler state at the end of the previous frame, not
    // of the gap's magnitude. `loopWasContinuous_` records it there (see below), so a
    // 3-second frame is now kept and a 3-second idle is still dropped — no threshold,
    // and no load at which the two get confused.
    if (FrameIntervalIsCadenceSample(lastFrameQpc_ != 0, loopWasContinuous_)) {
        const double intervalMs = QpcToMs(frameEndQpc - lastFrameQpc_);
        diagnostics_.Intervals().Push(static_cast<float>(intervalMs));
    }
    lastFrameQpc_ = frameEndQpc;

    // Watchdog heartbeat. One relaxed store; the detector thread (if started) reads
    // it to answer "is the UI thread responding RIGHT NOW", which neither ring above
    // can answer — they are written on this thread, so a stall silently produces no
    // samples at all rather than a large one. Unconditional: the beat is nanoseconds
    // and gating it on sessionLogEnabled_ would leave the watchdog blind in exactly
    // the default configuration a user hits a hang in.
    diagnostics_.Hang().Beat();

    // Hand the finished frame to the HUD thread (no-op while it is off). After both
    // ring pushes, so the HUD sees this frame's cost and interval.
    if (hud_) PublishHud(frameEndQpc);

    // PACING REFERENCE: the START of this frame, deliberately NOT frameEndQpc.
    //
    // The deadline for the next frame is "one refresh after this one began". Pacing
    // from the END instead adds this frame's own cost to every cycle: the loop woke
    // at start = prevEnd + interval - margin, spent cpuFrameMs painting, and set the
    // next reference to prevEnd + interval - margin + cpuFrameMs. The frame cost is
    // then OUTSIDE the interval rather than inside it, so the cadence runs slow by
    // exactly cpuFrameMs every frame — a permanent, load-dependent shortfall rather
    // than a transient one. On a 120Hz panel with the HUD open (cpuFrameMs ~0.8ms)
    // that measured 9.1ms intervals = 110fps instead of 120, reproducible every run.
    //
    // Anchoring to the start makes the frame's own cost fit INSIDE the interval,
    // which is what "one frame per refresh" means. It also degrades correctly: if a
    // frame overruns the interval the next deadline is already past, FrameIsDue is
    // true immediately, and the loop paints back-to-back until it catches up instead
    // of compounding the delay.
    //
    // lastFrameQpc_ (end-based) is kept, but ONLY for diagnostics: the interval ring
    // and the HUD measure end-to-end, which has the same mean in steady state and is
    // the number a user comparing against a frame budget expects. Every PACING
    // accessor reads lastFrameStartQpc_ instead — see FrameWaitTimeoutMs and
    // FrameWaitRemaining, which deliberately share this one reference point so the
    // coarse and precise paths can never disagree about whether a frame is due.
    lastFrameStartQpc_ = frameStartQpc;

    // Will the loop go straight into another frame, or block waiting for input? This
    // is what decides whether the NEXT frame's interval is a cadence sample (see the
    // interval-ring guard above). A pending
    // frame, a live animation or an unapplied resize each mean the next frame follows
    // directly and the gap to it is real cadence rather than a wait for input.
    //
    // INSIDE THE SYSTEM MODAL LOOP, ALSO QUEUED INPUT. During a resize drag the next
    // WM_SIZE has not been generated yet when this frame ends: DefWindowProc turns the
    // already-queued mouse move into it only after we return. So resizePending_ reads
    // false at exactly the moment the drag is busiest, every interval of the drag was
    // classified as idle and dropped, and the ring kept only the pre-drag frames. A
    // stress-page drag measured rsz 327ms per frame while the HUD said FPS 66 and
    // interval Max 18ms. A mouse move already waiting in the queue is the honest
    // signal that the drag continues. Scoped to the modal loop because there the next
    // mouse move is certain to become a WM_SIZE; in the normal loop a queued move over
    // static content may not produce a frame, and counting it would fold the wait
    // into the next sample.
    const bool modalInputQueued =
        inModalLoop_ && HIWORD(GetQueueStatus(QS_MOUSEMOVE | QS_MOUSEBUTTON)) != 0;
    loopWasContinuous_ = frame_.Scheduler().NeedsFrame() || IsAnimating() ||
                         resizePending_ || modalInputQueued;

    // Rebuild the animation set at the frame boundary, not only on input events.
    // A PROGRAMMATIC state change (e.g. TabControl::SetSelectedIndex from code, or
    // a control starting an animation from a timer) can begin an animation with no
    // pointer/key ever reaching the window, and UpdateAnimationTimer otherwise
    // runs only on input — so that animation is never collected, never ticked,
    // and any state gated on it (the tab indicator position, the content fade)
    // freezes at its start value. The frame boundary is the reliable sync point:
    // every state change schedules a frame, so re-collecting here guarantees the
    // set reflects reality. Collect is a tree walk of WantsAnimationTick, cheap
    // relative to the paint that just ran, and SetWanted disarms the clock when
    // nothing wants a tick, so idle stays zero-cost.
    UpdateAnimationTimer();

    rendering_ = false;

    // Classify the present result: a lost device triggers a rebuild
    // on the way out of the frame (not mid-paint). Done after rendering_ is
    // cleared so RecoverDevice's follow-up repaint is not suppressed as reentrant.
    if (deviceLost_.NotifyFrameResult(ClassifyDeviceHResult(endHr)))
        RecoverDevice();
}

void NativeWindowHost::SetHudVisible(bool on) {
    if (hudVisible_ == on) return;
    hudVisible_ = on;
    // No frame request either way: the HUD draws on its own target and thread, so
    // this window's content is unaffected by it appearing or disappearing.
    if (on) {
        hud_ = std::make_unique<HudOverlay>();
        hud_->Start(hwnd_);
        // Seed it with the last frame so an idle window does not show "waiting for
        // first frame" until something happens to repaint.
        if (paintedFrames_ > 0) PublishHud(lastFrameQpc_);
    } else if (hud_) {
        hud_->Stop();  // joins; the thread releases its target, removing the HUD
        hud_.reset();
    }
}

void NativeWindowHost::PublishHud(int64_t frameEndQpc) {
    HudSnapshot snap;
    snap.stats = lastFrameStats_;
    snap.cpu = diagnostics_.Frames();
    snap.interval = diagnostics_.Intervals();
    snap.frameCount = paintedFrames_;
    snap.lastFrameEndQpc = frameEndQpc;
    snap.titleBarHeightDip = TitleBarHeightDip();
    hud_->Publish(snap);
}

// --- Session diagnostic log ------------------------------------------------

double NativeWindowHost::SessionClockSec() const {
    if (!sessionLogEnabled_ || sessionStartQpc_ == 0) return 0.0;
    return QpcToMs(QpcNow() - sessionStartQpc_) / 1000.0;
}

SessionPhase NativeWindowHost::CurrentSessionPhase(FrameReason reason) const {
    // Modal system loop: distinguish an active resize from a position-only move.
    if (inModalLoop_)
        return resizePending_ ? SessionPhase::Resizing : SessionPhase::Moving;
    if (HasReason(reason, FrameReason::Resize)) return SessionPhase::Resizing;
    if (HasReason(reason, FrameReason::Dpi)) return SessionPhase::Dpi;
    if (frame_.Anims().Count() > 0 || HasReason(reason, FrameReason::Animation))
        return SessionPhase::Animating;
    return SessionPhase::Idle;
}

int NativeWindowHost::RefreshHz() {
    if (refreshHz_ == 0) {
        // The window's OWN monitor. This used to pass nullptr to EnumDisplaySettings,
        // which reads the primary display: a window on a 240Hz secondary panel next
        // to a 60Hz primary was paced at 60, and the reverse painted four frames for
        // every one the panel could show.
        refreshMonitor_ = hwnd_ ? MonitorFromWindow(hwnd_, MONITOR_DEFAULTTONEAREST) : nullptr;
        MONITORINFOEXW mi{};
        mi.cbSize = sizeof(mi);
        DEVMODEW dm{};
        dm.dmSize = sizeof(dm);
        const wchar_t* device =
            (refreshMonitor_ && GetMonitorInfoW(refreshMonitor_, &mi)) ? mi.szDevice : nullptr;
        if (EnumDisplaySettingsW(device, ENUM_CURRENT_SETTINGS, &dm))
            refreshHz_ = static_cast<int>(dm.dmDisplayFrequency);
        // 0 and 1 mean "hardware default"; pace at 60 rather than not at all.
        if (refreshHz_ <= 1) refreshHz_ = 60;
        FL_TRACEF(kTag, "refresh %d Hz (%ls)", refreshHz_, device ? device : L"primary");
    }
    // Cap at 144Hz ONLY inside the system modal move/size loop. That is where the
    // cap came from: during a resize drag the frame includes a full relayout, 5-6ms
    // frames overrun a 240Hz budget (4.17ms) every time, and DWM then stops
    // delivering WM_SIZE, producing 80-400ms pauses instead of motion. It used to
    // apply everywhere, which held a 240Hz panel to 144 for scrolling and animation
    // too, where frames are a fraction of a millisecond and the budget is easily met.
    if (inModalLoop_ && refreshHz_ > 144) return 144;
    return refreshHz_;
}

void NativeWindowHost::TrackRefreshMonitor() {
    // MonitorFromWindow is a cheap user-mode lookup; WM_MOVE is not a hot path at
    // the rate that matters (one per move step), so checking every time is fine and
    // catches moves that never change DPI, which WM_DPICHANGED alone missed.
    if (!hwnd_ || refreshHz_ == 0) return;
    if (MonitorFromWindow(hwnd_, MONITOR_DEFAULTTONEAREST) == refreshMonitor_) return;
    const int oldHz = RefreshHz();
    InvalidateRefreshHz();
    // Dragged across a monitor boundary mid-drag (same DPI, so no WM_DPICHANGED ends
    // the modal loop): the heartbeat would keep ticking at the old panel's period for
    // the rest of the drag while the yield test compares against the new one.
    if (inModalLoop_ && modalHeartbeat_.IsRunning() && RefreshHz() != oldHz) {
        modalHeartbeat_.Stop();
        modalHeartbeat_.Start(hwnd_, RefreshIntervalMs(RefreshHz()));
    }
}

unsigned NativeWindowHost::FrameWaitTimeoutMs() const {
    // No previous frame (first paint): render immediately, nothing to pace against.
    if (lastFrameStartQpc_ == 0) return 0u;
    // Same reference point as FrameWaitRemaining — the previous frame's START. Two
    // pacing accessors that disagreed about "when did the last frame happen" would
    // be a silent source of drift: the coarse one would think a frame was overdue
    // while the precise one still wanted to wait, or vice versa. There is one answer
    // to that question and both read it.
    const double elapsed = QpcToMs(QpcNow() - lastFrameStartQpc_);
    // RefreshHz() caches, so this is a plain read on all but the first call. The
    // const_cast keeps the accessor const for callers (it is a memo, not state).
    const int hz = const_cast<NativeWindowHost*>(this)->RefreshHz();
    return FrameWaitMs(elapsed, hz);
}

double NativeWindowHost::FrameWaitRemaining() const {
    // First paint: due immediately.
    if (lastFrameStartQpc_ == 0) return 0.0;
    // Measured from the previous frame's START, not its end — see the comment at
    // lastFrameStartQpc_'s assignment in RenderNow. Using the end would push each
    // frame's own paint cost outside the refresh interval, making the cadence run
    // permanently slow by cpuFrameMs (the measured 110-vs-120fps shortfall).
    const double elapsed = QpcToMs(QpcNow() - lastFrameStartQpc_);
    const int hz = const_cast<NativeWindowHost*>(this)->RefreshHz();
    return FrameWaitRemainingMs(elapsed, hz);
}

void NativeWindowHost::EnableSessionLog(bool on) {
    if (sessionLogEnabled_ == on) return;
    sessionLogEnabled_ = on;
    if (on) {
        sessionStartQpc_ = QpcNow();
        const int refreshHz = RefreshHz();   // cached; also used for frame pacing
        diagnostics_.Log().Start(static_cast<int>(DpiScale() * 100.0f + 0.5f),
                          static_cast<int>(pixelW_), static_cast<int>(pixelH_),
                          ClientWidthDip(), ClientHeightDip(), refreshHz);
        diagnostics_.Log().RecordEvent("session", "launch", SessionClockSec());
    }
}

void NativeWindowHost::EnableHangDetector(bool on, double thresholdMs,
                                          double pollIntervalMs) {
    if (on) {
        diagnostics_.Hang().Start(thresholdMs, pollIntervalMs);
    } else {
        diagnostics_.Hang().Stop();
    }
}

void NativeWindowHost::LogSessionEvent(const char* category,
                                     const std::string& detail) {
    if (sessionLogEnabled_) diagnostics_.Log().RecordEvent(category, detail, SessionClockSec());
}

bool NativeWindowHost::RecoverDevice() {
    if (!deviceLost_.BeginRebuild()) return deviceLost_.IsHealthy();

    HRESULT reason = graphics_.D2D().DeviceRemovedReason();
    Trace(kTag, "device lost — rebuilding stack", reason);

    // Tell the tree the device is gone (controls release any device-bound
    // resource; today none do, but panels propagate for future controls).
    for (UIElement* e : elements_)
        if (e) e->OnDeviceLost();

    // Drop cached GPU/factory resources and any transient overlays — a popup /
    // tooltip recreates its own composition target from the fresh device on next open.
    graphics_.Cache().Clear();
    CloseActivePopups();
    if (tooltip_) tooltip_->Hide();

    // Release the old stack and rebuild IN PLACE so borrowers (PopupHost /
    // TooltipService holding d2d_ pointers) stay valid.
    graphicsReady_ = false;
    titleIconBitmap_.Reset();
    graphics_.Reset();

    HRESULT hr = graphics_.Rebuild(hwnd_, pixelW_, pixelH_);
    if (FAILED(hr)) {
        Trace(kTag, "device rebuild FAILED", hr);
        deviceLost_.EndRebuild(false);  // back to Lost
        // Schedule the retry explicitly. "A later frame" never comes: RenderNow returns
        // at `!graphicsReady_` before any HRESULT is classified, and NotifyFrameResult
        // only reports a Healthy -> Lost edge anyway, so nothing else calls this again
        // and the window stayed blank (no content surface) until restart.
        ++deviceRebuildFailures_;
        if (hwnd_ && !destroying_)
            SetTimer(hwnd_, kDeviceRetryTimerId,
                     DeviceRebuildRetryDelayMs(deviceRebuildFailures_), nullptr);
        return false;
    }
    deviceRebuildFailures_ = 0;

    // Re-point the context at the fresh cache.
    context_.resourceCache = &graphics_.Cache();
    graphicsReady_ = true;
    RecreateWindowIcon();

    // Notify the tree the device is back. A full repaint follows below (RenderNow
    // repaints the whole tree unconditionally), so no explicit re-dirty is needed.
    for (UIElement* e : elements_)
        if (e) e->OnDeviceRestored();

    deviceLost_.EndRebuild(true);
    Trace(kTag, "device rebuilt", S_OK);
    // Fresh content surface — force a full clear for the next 2 frames.
    fullDrawRequired_ = 2u;
    RenderNow(FrameReason::Paint);
    return true;
}

// ---------------------------------------------------------------------------
// Window procedure
// ---------------------------------------------------------------------------

LRESULT CALLBACK NativeWindowHost::WndProc(HWND hwnd, UINT msg, WPARAM wp,
                                         LPARAM lp) {
    NativeWindowHost* self = nullptr;
    if (msg == WM_NCCREATE) {
        auto* cs = reinterpret_cast<CREATESTRUCTW*>(lp);
        self = static_cast<NativeWindowHost*>(cs->lpCreateParams);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
        self->hwnd_ = hwnd;
    } else {
        self = reinterpret_cast<NativeWindowHost*>(
            GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    }
    if (!self) return DefWindowProcW(hwnd, msg, wp, lp);

    // Hang watchdog bookkeeping (window/HangDetector.h). Any message reaching us
    // proves the UI thread is pumping, so it is a beat. That matters inside loops
    // we do not own (a system move/resize or menu loop, a MessageBox): there our
    // pump is not running and frames may stop, but messages still arrive here.
    //
    // Those foreign loops then BLOCK waiting for input, exactly like our own idle
    // wait, with nobody to say so. Two signals stand in for that: between
    // WM_ENTER/EXITSIZEMOVE and WM_ENTER/EXITMENULOOP every nested message ends by
    // marking idle again (the loop goes straight back to waiting), and WM_ENTERIDLE,
    // which a modal dialog or menu sends its owner each time its queue empties,
    // means "waiting now" by definition. A handler that hangs still hangs before it
    // returns, so marking idle on the way OUT never hides a real stall.
    //
    // The idle decision is taken BEFORE dispatch: the outer message that runs the
    // whole drag (WM_NCLBUTTONDOWN / WM_SYSCOMMAND) arrives with no loop open and
    // returns after the loop closed, so it never marks idle and the next beat comes
    // from our own pump as usual.
    self->diagnostics_.Hang().Beat();
    switch (msg) {
        case WM_ENTERSIZEMOVE:
        case WM_ENTERMENULOOP:
            ++self->foreignModalLoops_;
            break;
        case WM_EXITSIZEMOVE:
        case WM_EXITMENULOOP:
            if (self->foreignModalLoops_ > 0) --self->foreignModalLoops_;
            break;
        default:
            break;
    }
    const bool idleAfter = msg == WM_ENTERIDLE || self->foreignModalLoops_ > 0;
    const LRESULT result = self->HandleMessage(msg, wp, lp);
    // A handler may have destroyed the window, and an owner reacting to Closed may
    // have deleted the host object with it (~NativeWindowHost destroys the HWND it
    // still holds), so `self` is only touched again while the HWND is alive and
    // still maps to it.
    if (idleAfter && IsWindow(hwnd) &&
        reinterpret_cast<NativeWindowHost*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA)) == self)
        self->diagnostics_.Hang().EnterIdle();
    return result;
}

LRESULT NativeWindowHost::HandleMessage(UINT msg, WPARAM wp, LPARAM lp) {
    LRESULT appResult = 0;
    if (OnAppMessage(msg, wp, lp, appResult))
        return appResult;

    switch (msg) {
        case WM_SETFOCUS:
            // The window got OS focus back (Alt+Tab, click from another app). The
            // focus manager kept its element while inactive; UIA must hear about it
            // again or Narrator stays silent on return. A null check without a client.
            automation_.OnWindowFocused();
            break;  // DefWindowProc as before

        case automation::WM_FLUENT_UIA_FLUSH:
            // Queued UIA actions, provider disconnects and events (AutomationHost.h).
            automation_.OnFlush();
            return 0;

        case WM_GETOBJECT: {
            // UI Automation asks for our provider with UiaRootObjectId; every other
            // object id (MSAA OBJID_CLIENT etc.) falls through to DefWindowProc and
            // the system proxy. Nothing UIA-related exists before this message.
            LRESULT uiaResult = 0;
            if (automation_.OnGetObject(hwnd_, wp, lp, destroying_, uiaResult))
                return uiaResult;
            break;
        }

        case WM_NCCALCSIZE:
            if (wp == TRUE) {
                auto* params = reinterpret_cast<NCCALCSIZE_PARAMS*>(lp);
                // Reserve NC bands on left/right/bottom only. Top cannot have an NC inset
                // without triggering the system title bar (DWM draws its own caption when
                // the top edge is non-client), producing a double-title-bar artifact.
                //
                // The DComp content surface follows the client origin (verified by probe:
                // 40px left+top NC inset shifted content 40px right+down), so asymmetric
                // insets pose no coordinate-system issue — the surface adjusts automatically.
                const int insetPx = ResizeGripOuterPx(dpi_);
                params->rgrc[0].left += insetPx;
                // params->rgrc[0].top — unchanged; top edge remains client rect boundary
                params->rgrc[0].right -= insetPx;
                params->rgrc[0].bottom -= insetPx;
                return 0;
            }
            break;

        case WM_NCHITTEST: {
            POINT pt = {GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
            const LRESULT ht = HitTestNca(pt);
            // Timestamp the moment the cursor was last reported over a resize
            // border. The gap from here to WM_ENTERSIZEMOVE is time spent inside
            // the SYSTEM (deciding drag vs click, DWM scheduling) and is invisible
            // to every frame-pacing knob we own.
            if (ht >= HTSIZEFIRST && ht <= HTSIZELAST) borderHitQpc_ = QpcNow();
            return ht;
        }

        case WM_GETMINMAXINFO: {
            // Clamp how small the user can drag the window. Without this the system
            // floor (~SM_CXMIN) applies and the layout collapses. Recomputed from DIPs
            // on every message so the limit tracks the current monitor's DPI — the
            // window may have been dragged to a different one since it was set.
            // (The very first WM_GETMINMAXINFO precedes WM_NCCREATE, so GWLP_USERDATA
            // is still null and WndProc sends it to DefWindowProc — this handler only
            // ever runs once the instance is wired up.)
            auto* mmi = reinterpret_cast<MINMAXINFO*>(lp);
            POINT minTrack = mmi->ptMinTrackSize;
            if (MinTrackSizePx(minClientWDip_, minClientHDip_, dpi_, minTrack))
                mmi->ptMinTrackSize = minTrack;
            return 0;
        }

        case WM_SIZE:
            if (wp != SIZE_MINIMIZED) {
                // Wall-clock gap since the previous WM_SIZE we were handed. A large
                // value here means the STALL WAS BEFORE US: the system did not
                // deliver a size for that long, so no amount of frame pacing on our
                // side could have painted anything in between.
                if (inModalLoop_) {
                    const int64_t now = QpcNow();
                    const double sinceLast =
                        lastSizeMsgQpc_ ? QpcToMs(now - lastSizeMsgQpc_) : -1.0;
                    lastSizeMsgQpc_ = now;
                    FL_TRACEF("Modal", "WM_SIZE %dx%d  (+%.1fms since previous WM_SIZE)",
                              LOWORD(lp), HIWORD(lp), sinceLast);
                }
                OnSize(LOWORD(lp), HIWORD(lp));
                // Resize coalescing: WM_SIZE arrives densely inside the system modal resize
                // loop where our main loop (and the low-priority WM_TIMER heartbeat)
                // is starved. Service a frame right here so the coalesced resize is
                // applied and animation advances smoothly while dragging the edge.
                if (inModalLoop_) ServiceModalFrame(/*pace*/ true);
            }
            return 0;

        case WM_MOVING:
            // Window being dragged (position only, no size change): the modal loop
            // owns the pump, so drive animation from these frequent messages too so
            // a running animation (e.g. an indeterminate ProgressBar) keeps moving
            // while the window is dragged. No resize is pending, so this paints only
            // when something is actually animating.
            if (inModalLoop_) ServiceModalFrame(/*pace*/ true);
            break;  // let DefWindowProc handle the actual move

        case WM_MOVE:
            // Moved onto another monitor (possibly another refresh rate, same DPI).
            TrackRefreshMonitor();
            // Dismiss active popup when the parent window moves.
            if (popupDismiss_) {
                DispatchPopupDismiss(PopupDismissReason::Move, nullptr, 0, 0);
            }
            return 0;

        case WM_ENTERSIZEMOVE:
            // User started resizing/moving the window — dismiss popup immediately.
            if (popupDismiss_) {
                DispatchPopupDismiss(PopupDismissReason::Resize, nullptr, 0, 0);
            }
            // Entering a SYSTEM modal move/resize loop: our message loop (and thus
            // PumpAnimations / RunDueFrame) stops running. Arm the heartbeat timer
            // UNCONDITIONALLY (not just when animating) — with resize coalescing, WM_SIZE no
            // longer paints synchronously, so the heartbeat is now what applies the
            // coalesced resize and repaints the window during the drag. It also
            // keeps any active animation ticking. Disarmed on WM_EXITSIZEMOVE.
            {
                const double gapMs = borderHitQpc_ ? QpcToMs(QpcNow() - borderHitQpc_) : -1.0;
                FL_TRACEF("Modal", "ENTERSIZEMOVE (+%.1fms since last border hit)", gapMs);
            }
            inModalLoop_ = true;
            context_.inModalResize = true;
            // UIContext is stored BY VALUE on every element (UIElement::context_), so
            // mutating the host's copy alone leaves the tree reading a stale flag —
            // exactly the same hazard UpdateContextDpi exists for. Push it down now, so
            // a composition-backed control (none in FluentUI today; see UIContext.h)
            // sees it from the drag's first frame and can skip its BeginDraw. Cheap:
            // one bool write per element, no layout, no invalidation.
            for (UIElement* e : elements_)
                if (e) e->UpdateContextModalResize(true);
            // Forget the previous drag's opportunity spacing: it described that
            // drag's mouse rate and message mix, and starting a new drag with a
            // stale (possibly much wider) estimate would let the first frames
            // overshoot the panel before the smoothing pulls it back.
            ResetModalGapEstimate();
            modalTickYields_ = 0;
            // Precise tick from a worker thread posting WM_FLUENT_MODAL_TICK. This
            // is the primary driver now, and the reason a held-but-motionless resize
            // border no longer drops to ~30fps: WM_TIMER could not deliver better
            // than ~28.6ms here (low priority + rounded to the system timer
            // resolution), while this paces at the actual refresh interval.
            {
                const double period = RefreshIntervalMs(RefreshHz());
                modalHeartbeat_.Start(hwnd_, period);
                // Keep the OS timer armed as a FALLBACK only when the heartbeat could
                // not start (no waitable timer on this OS). Arming both would double the
                // tick rate for no benefit — ServiceModalFrame would just skip the
                // extra, but it is wasted wakeups.
                const bool heartbeatOk = modalHeartbeat_.IsRunning();
                FL_TRACEF("Modal", "heartbeat %s (%.2fms), fallback timer %s",
                          heartbeatOk ? "started" : "FAILED",
                          period,
                          heartbeatOk ? "off" : "armed");
                if (!heartbeatOk)
                    SetTimer(hwnd_, kAnimTimerId, kAnimIntervalMs, nullptr);
            }
            if (sessionLogEnabled_)
                diagnostics_.Log().RecordEvent("modal", "enter size/move loop",
                                        SessionClockSec());
            break;

        case WM_EXITSIZEMOVE:
            // Modal loop done — the main loop resumes driving PumpAnimations /
            // RunDueFrame. Apply any resize the heartbeat had not yet folded in and
            // paint the final exact size synchronously (crisp last frame), then drop
            // the heartbeat timer.
            inModalLoop_ = false;
            context_.inModalResize = false;
            // Clear the flag on the tree BEFORE the convergence layout below. Order is
            // load-bearing: that layout is what rasterizes the final pixels, and it can
            // only do so if every element has already seen the falling edge (a control
            // that deferred rasterization during the drag re-arms it there).
            for (UIElement* e : elements_)
                if (e) e->UpdateContextModalResize(false);
            // Stop the tick BEFORE the final paint: Stop() joins the worker, so no
            // further WM_FLUENT_MODAL_TICK can be posted after this point and the
            // synchronous final frame below is the last one of the drag.
            modalHeartbeat_.Stop();
            // Convergence layout: run a precise layout at the final size if a WM_SIZE
            // is still unconsumed.
            if (resizePending_) {
                frame_.Scheduler().BeginFrame();
                ApplyPendingResize();  // inModalLoop_ is false now → full OnLayout
                RenderNow(FrameReason::Resize);
                frame_.Scheduler().EndFrame();
            } else {
                // No pending size, but we still ran a tree update in the drag, and a
                // control that deferred work while inModalResize was set needs one
                // more frame to converge to the exact final pixels. (The flag update
                // itself invalidates nothing; this layout + paint is the convergence.)
                frame_.Scheduler().BeginFrame();
                OnLayout();
                RenderNow(FrameReason::Resize);
                frame_.Scheduler().EndFrame();
            }
            KillTimer(hwnd_, kAnimTimerId);
            // Once per drag: how many heartbeat ticks deferred to queued input. Nonzero
            // means frames ran past the refresh interval at some point in the drag.
            FL_TRACEF("Modal", "EXITSIZEMOVE (%d heartbeat tick(s) yielded to input)",
                      modalTickYields_);
            if (sessionLogEnabled_) {
                char buf[96];
                _snprintf_s(buf, sizeof(buf), _TRUNCATE, "exit; final %ux%upx",
                            pixelW_, pixelH_);
                diagnostics_.Log().RecordEvent("modal", buf, SessionClockSec());
            }
            break;

        case WM_ACTIVATE:
            // Dismiss popup when the window loses activation to another app (not our
            // own popup). WA_INACTIVE with lp=popup HWND means popup is showing.
            if (LOWORD(wp) == WA_INACTIVE) {
                // Release any pointer capture with a canceling release so a control
                // pressed when focus left doesn't stay stuck pressed.
                input_.Input().OnWindowDeactivated();
                // Break the click streak. Otherwise clicking away and quickly back to
                // the same spot resumes the old streak, so one click in the new
                // activation reads as a double- or triple-click.
                input_.Clicks().Reset();
                if (popupDismiss_) {
                    HWND other = reinterpret_cast<HWND>(lp);
                    // Pass the other HWND so the callback can check if it's the popup.
                    DispatchPopupDismiss(PopupDismissReason::Deactivate, other, 0, 0);
                }
            }
            break;

        case WM_CAPTURECHANGED:
            // Another window or a caption button stole capture. Clear our internal
            // captured_ state so the next press doesn't incorrectly route to the old
            // element. Don't call ::ReleaseCapture — we don't hold it anymore.
            input_.Input().OnCaptureStolen();
            break;

        case WM_DPICHANGED: {
            UINT newDpi = HIWORD(wp);
            OnDpiChanged(newDpi, reinterpret_cast<const RECT*>(lp));
            return 0;
        }

        case WM_SETTINGCHANGE: {
            // The user changed a system-wide setting. Re-read the wheel scroll-lines
            // preference: it is broadcast under SPI_SETWHEELSCROLLLINES, but the wparam
            // is unreliable across shells, so just re-query unconditionally. Cheap
            // (a registry-backed cached read) and only fires on real setting changes.
            UINT lines = 3;
            if (SystemParametersInfoW(SPI_GETWHEELSCROLLLINES, 0, &lines, 0))
                wheelScrollLines_ = lines;
            break;  // fall through to DefWindowProc — other subsystems may care too
        }

        case WM_MOUSEMOVE: {
            TRACKMOUSEEVENT tme = {sizeof(tme), TME_LEAVE, hwnd_, 0};
            TrackMouseEvent(&tme);
            int px = GET_X_LPARAM(lp), py = GET_Y_LPARAM(lp);
            float s = 96.0f / dpi_;
            // Caption-button hover is a window-level concern (not an element):
            // track it here and suppress element hover while over a caption button.
            int caption = CaptionButtonAt(px * s, py * s);
            if (caption != hotCaption_) { hotCaption_ = caption; Render(); }
            if (caption) {
                input_.Input().PointerLeftWindow();  // clear element hover under the button
            } else {
                input_.Input().PointerMoved(ToDip(px, py), CurrentModifiers());
                ArmTooltip(input_.Input().Hot());
            }
            // A move may change hover (fade/expand a scrollbar) OR, while a control
            // has captured the pointer (dragging a Slider), retarget an animation on
            // a DIFFERENT element — e.g. the Slider's ValueChanged drives a bound
            // ProgressBar's determinate ease. So the active-animation set must be
            // re-collected on drag-moves too; skipping it (an earlier optimization)
            // left the ProgressBar un-ticked until drag end. Collect walks only the
            // registered roots and is cheap relative to the per-move layout/paint.
            UpdateAnimationTimer();
            return 0;
        }
        case WM_MOUSELEAVE:
            input_.Input().PointerLeftWindow();
            if (hotCaption_) { hotCaption_ = 0; Render(); }
            HideTooltip();  // pointer left the window entirely
            UpdateAnimationTimer();  // leaving may start a scrollbar fade-out
            return 0;
        // Both press messages take the SAME path — the only difference is the click
        // count, which the counter derives. Windows sends DOWN, DBLCLK, DOWN, DBLCLK…
        // for a rapid series, so a handler that keyed off the message type alone would
        // see every third click as a fresh single press; counting time+position instead
        // is what makes triple-click possible at all (Win32 has no triple-click message).
        case WM_LBUTTONDOWN:
        case WM_LBUTTONDBLCLK: {
            HideTooltip();  // any click dismisses a visible tooltip
            int px = GET_X_LPARAM(lp), py = GET_Y_LPARAM(lp);
            float s = 96.0f / dpi_;
            // A caption button press is window chrome, not an element event.
            int caption = CaptionButtonAt(px * s, py * s);
            if (caption) {
                // Chrome interaction breaks the streak: a double-click that lands half
                // on the caption and half in the text must not select a word.
                input_.Clicks().Reset();
                pressedCaption_ = caption;
                SetCapture(hwnd_);
                Render();
                return 0;
            }
            // GetMessageTime, not GetTickCount: it is when the message was POSTED, so a
            // slow frame between two clicks cannot stretch the measured interval and
            // break a genuine double-click. System thresholds because the user can change
            // double-click speed and pointer tolerance (see ClickCounter.h).
            const int clickCount = input_.Clicks().Register(
                static_cast<float>(px), static_cast<float>(py),
                static_cast<uint32_t>(GetMessageTime()), GetDoubleClickTime(),
                static_cast<float>(GetSystemMetrics(SM_CXDOUBLECLK)));
            // Traced because "double-click does nothing" and "double-click was never
            // delivered" look identical on screen. Seeing clickCount here separates
            // a missing CS_DBLCLKS / routing problem from a control-logic problem,
            // which is otherwise a long detour through the wrong layer.
            //
            // FL_TRACEF, not a local buffer + TraceMsg: this runs on EVERY mouse press,
            // and with a debugger attached OutputDebugStringA blocks until the debugger
            // drains it. The macro removes the formatting too, so a Release build costs
            // literally nothing here.
            FL_TRACEF("Input", "press clickCount=%d at (%d,%d)", clickCount, px, py);
            // Parent-window light-dismiss for no-activate popups first (the popup
            // decides whether to close), then route the press into the tree.
            LightDismissAt(px, py);
            input_.Input().PointerPressed(ToDip(px, py), PointerButton::Left, CurrentModifiers(),
                                  clickCount);
            UpdateAnimationTimer();
            return 0;
        }
        case WM_RBUTTONDOWN: {
            HideTooltip();
            int px = GET_X_LPARAM(lp), py = GET_Y_LPARAM(lp);
            // Close any already-open popup first (same light-dismiss as a left
            // click), then open the context menu — otherwise a menu opened by an
            // earlier click stays visible under the new one.
            LightDismissAt(px, py);
            float s = 96.0f / dpi_;
            POINT screen = {px, py};
            ClientToScreen(hwnd_, &screen);
            // Per-control context menu: the element under the
            // cursor (or the nearest ancestor that owns one) opens its own menu.
            // Only if nothing in the chain has a menu do we fall back to the
            // subclass hook (a window-level background menu).
            if (UIElement* owner = input_.Input().ContextMenuOwnerAt(Point{px * s, py * s})) {
                // Give the element a chance to refresh item state and adjust its own
                // selection first — MenuItem::enabled is captured by SetItems, so a menu
                // with live conditions has to rebuild here or it shows stale state.
                owner->OnContextMenuOpening(px * s, py * s);
                owner->ContextMenu()->ShowAt(screen.x, screen.y);
            } else {
                OnClientRightClick(px * s, py * s, screen.x, screen.y);
            }
            return 0;
        }
        case WM_MBUTTONDOWN:
        case WM_MBUTTONDBLCLK: {
            HideTooltip();  // dismiss tooltip like any other click
            int px = GET_X_LPARAM(lp), py = GET_Y_LPARAM(lp);
            // Light-dismiss any open popup (same as left/right click), but do NOT
            // break the click streak — middle-click is a pan gesture, not a selection,
            // and should not interfere with double/triple-click tracking.
            LightDismissAt(px, py);
            // Route as a middle-button press. Unlike left-click, middle-click does NOT
            // change keyboard focus — it is a navigation gesture, not an editing one.
            // Also does NOT capture: autoscroll is a toggle, not a press-and-hold drag.
            input_.Input().PointerPressed(ToDip(px, py), PointerButton::Middle, CurrentModifiers(),
                                          1);  // clickCount 1: no multi-click for middle button
            // Cursor updates immediately on WM_MOUSEMOVE once autoscroll starts, so no
            // explicit RefreshCursor call needed here (unlike the old pan mode).
            UpdateAnimationTimer();
            return 0;
        }
        case WM_LBUTTONUP: {
            int px = GET_X_LPARAM(lp), py = GET_Y_LPARAM(lp);
            float s = 96.0f / dpi_;
            if (pressedCaption_) {
                int pressed = pressedCaption_;
                pressedCaption_ = 0;
                ReleaseCapture();
                Render();
                if (CaptionButtonAt(px * s, py * s) == pressed) {
                    switch (pressed) {
                        case HTMINBUTTON:
                            SendMessageW(hwnd_, WM_SYSCOMMAND, SC_MINIMIZE, 0);
                            break;
                        case HTMAXBUTTON:
                            SendMessageW(hwnd_, WM_SYSCOMMAND,
                                         IsZoomed(hwnd_) ? SC_RESTORE : SC_MAXIMIZE, 0);
                            break;
                        case HTCLOSE:
                            SendMessageW(hwnd_, WM_SYSCOMMAND, SC_CLOSE, 0);
                            break;
                    }
                }
                return 0;
            }
            // A press not on any element still reaches the subclass hook when the
            // release lands on empty space (nothing was captured).
            bool hadCapture = input_.Input().Captured() != nullptr;
            input_.Input().PointerReleased(ToDip(px, py), PointerButton::Left, CurrentModifiers());
            // The release handler may have destroyed the window (an app Close
            // button): don't touch the torn-down window past this point.
            if (destroying_) return 0;
            if (!hadCapture && !input_.Input().HitTest(ToDip(px, py)))
                OnClientClick(px * s, py * s);
            if (destroying_) return 0;  // OnClientClick could also close the window
            UpdateAnimationTimer();  // drag-end starts the idle fade countdown
            return 0;
        }
        case WM_MBUTTONUP: {
            int px = GET_X_LPARAM(lp), py = GET_Y_LPARAM(lp);
            input_.Input().PointerReleased(ToDip(px, py), PointerButton::Middle,
                                           CurrentModifiers());
            if (destroying_) return 0;
            // Autoscroll is a toggle: middle-up doesn't necessarily end it (only if
            // the user clicked again to stop). Cursor updates via WM_SETCURSOR naturally.
            UpdateAnimationTimer();
            return 0;
        }
        case WM_MOUSEWHEEL: {
            HideTooltip();  // scrolling dismisses a visible tooltip
            POINT pt = {GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
            ScreenToClient(hwnd_, &pt);
            
            // Forward to an active popup FIRST (WS_EX_NOACTIVATE popups never receive
            // WM_MOUSEWHEEL themselves — the focused window gets it, which is us). The
            // popup callback checks whether the wheel is over its HWND and routes the
            // event to its own content if so. Only when no popup consumes it do we
            // route to our own element tree (so wheel-over-main-window still works).
            int delta = GET_WHEEL_DELTA_WPARAM(wp);
            if (popupWheel_) {
                POINT screenPt = {GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
                if (popupWheel_(screenPt.x, screenPt.y, delta)) {
                    UpdateAnimationTimer();  // popup may have started a smooth scroll
                    return 0;
                }
            }
            
            // Normalize WHEEL_PAGESCROLL (0xFFFFFFFF) to 0 before passing down — the
            // element tree reads 0 as "page per notch" without multiply-by-height confusion.
            UINT lines = wheelScrollLines_;
            if (lines == WHEEL_PAGESCROLL) lines = 0;
            input_.Input().PointerWheel(ToDip(pt.x, pt.y), delta,
                                        CurrentModifiers(), lines);
            OnMouseWheel(pt.x * (96.0f / dpi_), pt.y * (96.0f / dpi_), delta);
            UpdateAnimationTimer();  // a wheel may have kicked off a smooth scroll
            return 0;
        }

        case WM_KEYDOWN:
        case WM_SYSKEYDOWN: {
            UINT vk = static_cast<UINT>(wp);
            // F12 toggles the on-screen performance HUD. A global
            // diagnostics shortcut handled before element routing, so it works
            // regardless of focus.
            if (vk == VK_F12) { ToggleHud(); return 0; }
            // An active no-activate popup session (menu/combo) consumes keys first.
            // A context menu opened by right-click has no focused element, so it
            // cannot rely on the focused-element route inside InputManager.
            if (popupKey_ && popupKey_(vk)) { UpdateAnimationTimer(); return 0; }
            if (input_.Input().KeyDown(vk, CurrentModifiers())) {
                // A key may have kicked off an animation (Space/Enter toggling a
                // CheckBox/ToggleSwitch, arrow keys nudging a Slider): re-collect
                // the active set and arm the clock, same as the mouse paths.
                UpdateAnimationTimer();
                return 0;
            }
            break;  // fall through to DefWindowProc for unhandled keys
        }

        case WM_CHAR:
            input_.Input().TextInput(static_cast<wchar_t>(wp));
            return 0;

        case WM_SETCURSOR:
            // Let the element under the pointer choose its cursor (e.g. I-beam
            // for a text box). Only for the client area hit code.
            if (LOWORD(lp) == HTCLIENT) {
                POINT pt;
                GetCursorPos(&pt);
                ScreenToClient(hwnd_, &pt);
                // A captured element owns the cursor for its whole gesture, even when
                // the pointer is outside it (a pan that dragged off the control must
                // keep the pan arrow).
                if (UIElement* cap = input_.Input().Captured()) {
                    if (HCURSOR c = cap->Cursor()) {
                        SetCursor(c);
                        return TRUE;
                    }
                }
                if (UIElement* e = input_.Input().HitTest(ToDip(pt.x, pt.y))) {
                    if (HCURSOR c = e->Cursor()) {
                        SetCursor(c);
                        return TRUE;
                    }
                }
            }
            break;  // default arrow / resize cursors

        case WM_FLUENT_HUD_PING:
            // Liveness ping from the HUD thread (HudFeed.h). Acknowledge and return
            // without touching the frame scheduler: answering must not itself cause
            // work, or the HUD would perturb the idle it is reporting on.
            if (hud_) hud_->AcknowledgePing(wp);
            return 0;

        case WM_FLUENT_MODAL_TICK:
            // High-resolution modal heartbeat (ModalFrameHeartbeat.h). Posted from a
            // waitable-timer thread at the display refresh rate while a SYSTEM modal
            // move/resize loop owns the pump. This exists because the WM_TIMER
            // heartbeat below cannot go faster than ~16ms (USER_TIMER_MINIMUM is
            // 10ms and the tick is rounded up to the system timer period), which
            // capped a held-but-motionless resize border at ~30-45fps even though
            // the frame itself costs under 3ms.
            //
            // Stale ticks are expected and harmless: Stop() cannot retract messages
            // already in the queue, so a few may arrive after the modal loop ended.
            // ServiceModalFrame is safe then (it paints only if there is pending
            // work), but gate on inModalLoop_ anyway so a post-drag tick cannot
            // paint outside the state it was meant for.
            //
            // Release the heartbeat's single in-flight slot FIRST, before painting.
            // The worker suppresses a new tick while one is unconsumed (so a long
            // frame cannot queue a backlog), and "consumed" means dispatched here —
            // not "finished painting". Decrementing after the paint would idle the
            // heartbeat for the whole duration of a slow frame, which is exactly when
            // a motionless drag needs the next tick to already be on its way.
            modalHeartbeat_.OnTickConsumed();
            if (!inModalLoop_) return 0;
            //
            // Let queued input go first. Posted messages outrank input in GetMessage,
            // so a slow frame would otherwise always find the next tick already queued
            // and the mouse would never be read — the drag freezes until frames get
            // cheap again (ModalTickShouldYieldToInput has the measured case). The
            // input's own WM_SIZE / WM_MOVING then paints. GetQueueStatus is used
            // rather than PeekMessage(PM_NOREMOVE) because the latter dispatches
            // pending sent messages and would re-enter this WndProc. It also resets
            // the queue's "new input" bits, which is harmless: the main loop waits
            // with MWMO_INPUTAVAILABLE, and this runs only inside the modal loop.
            {
                const bool inputPending =
                    HIWORD(GetQueueStatus(QS_MOUSEMOVE | QS_MOUSEBUTTON | QS_KEY)) != 0;
                const double sinceFrameEnd =
                    lastFrameQpc_ ? QpcToMs(QpcNow() - lastFrameQpc_) : 0.0;
                if (ModalTickShouldYieldToInput(inputPending, lastFrameQpc_ != 0,
                                                sinceFrameEnd, RefreshHz())) {
                    ++modalTickYields_;
                    return 0;
                }
            }
            //
            // pace=false for the same reason as the WM_TIMER heartbeat: this tick IS
            // the pacing (the waitable timer already fires at the refresh interval),
            // so applying the skip-if-not-due test on top would double-pace and drop
            // every other frame.
            ServiceModalFrame(/*pace*/ false);
            return 0;

        case WM_FLUENT_POPUP_OPENED:
            // A PopupHost owned by this window just became visible (menu, drop-down,
            // calendar). Sent synchronously from PopupHost::Open.
            OnPopupOpened(reinterpret_cast<HWND>(wp));
            return 0;

        case WM_TIMER:
            if (wp == kDeviceRetryTimerId) {
                // One-shot: RecoverDevice re-arms it with a longer delay if this attempt
                // fails too.
                KillTimer(hwnd_, kDeviceRetryTimerId);
                RecoverDevice();
                return 0;
            }
            if (wp == kBlinkTimerId && input_.Focus().Focused() && input_.Focus().Focused()->WantsBlink()) {
                input_.Focus().Focused()->OnBlink();
                Render();
            } else if (wp == kTooltipTimerId) {
                // Hover delay elapsed: show the tooltip for the tracked element.
                KillTimer(hwnd_, kTooltipTimerId);
                tooltipTimerOn_ = false;
                UIElement* armed = tooltipArming_.Armed();
                if (tooltip_ && armed && armed->HasTooltip()) {
                    POINT pt;
                    GetCursorPos(&pt);
                    tooltip_->Show(armed->Tooltip(), pt.x, pt.y);
                }
            } else if (wp == kAnimTimerId) {
                // FALLBACK animation tick, only load-bearing inside a SYSTEM modal
                // loop (drag-resize / menu), where our message loop is not running
                // and PumpAnimations() is therefore not being called each turn.
                // Normally the main loop drives PumpAnimations() and this timer is
                // redundant (it may still fire, but PumpAnimations already advanced
                // the shared QPC clock, so ComputeDt here returns a near-zero dt and
                // the extra Tick is a no-op-sized step — no double-advance).
                //
                // Paint SYNCHRONOUSLY (RenderNow): in the modal loop a coalesced
                // RequestFrame would sit unserviced until the drag ends, freezing
                // the animation. PumpAnimations advances state + requests a frame;
                // BeginFrame/EndFrame consume it and paint right here.
                //
                // THIS IS NOW A FALLBACK ONLY. WM_ENTERSIZEMOVE arms it solely when
                // ModalFrameHeartbeat failed to start (no waitable timer, pre-1803);
                // otherwise the precise WM_FLUENT_MODAL_TICK path drives the drag and
                // this timer is never armed. It remains because it is the only
                // mechanism available on those older systems, and a motionless drag
                // must still repaint there — just at WM_TIMER's ~28ms cadence rather
                // than the panel's refresh.
                //
                // pace=false: this timer is already self-limiting at kAnimIntervalMs
                // (and then some, given WM_TIMER's rounding), and it is the ONLY
                // driver while the mouse holds still — pacing it would risk skipping
                // to the next period and halving an already-slow rate.
                ServiceModalFrame(/*pace*/ false);
            } else if (wp == kSelfFrameTimerId) {
                // Secondary windows have no dedicated application-loop entry.
                // Drive only their pending work here, then remove the timer as
                // soon as they become idle. A continuing animation re-arms at a
                // roughly display-rate interval; ordinary invalidation gets one
                // prompt frame and stops.
                // NOTE ON PACING: this path is capped by WM_TIMER, whose minimum
                // period is ~10ms (USER_TIMER_MINIMUM) regardless of what we ask
                // for — so a window driven from here cannot exceed ~100fps and on a
                // 120Hz panel will pace at roughly every other refresh. That is
                // accepted, not overlooked: this timer only runs for a secondary
                // window with NO Application (UsesExternalFramePump() == false),
                // which is the legacy path. Any window attached to an Application
                // is paced by Application::PumpOneTurn through FrameWaiter, which
                // uses a high-resolution waitable timer and does reach full refresh.
                // Fixing this path properly means giving it its own message loop,
                // which is precisely what Application exists to avoid duplicating.
                KillTimer(hwnd_, kSelfFrameTimerId);
                PumpAnimations();
                RunDueFrame();
                if (IsAnimating() || NeedsFrame())
                    SetTimer(hwnd_, kSelfFrameTimerId,
                             std::max<UINT>(1u, FrameWaitTimeoutMs()), nullptr);
            }
            return 0;

        case WM_IME_STARTCOMPOSITION:
            if (UIElement* f = input_.Focus().Focused()) {
                f->OnImeStartComposition(hwnd_);
                UpdateImePosition();
                return 0;  // we draw the composition string ourselves
            }
            break;
        case WM_IME_COMPOSITION:
            if (UIElement* f = input_.Focus().Focused()) {
                f->OnImeComposition(hwnd_, lp);
                UpdateImePosition();
                Render();
                return 0;
            }
            break;
        case WM_IME_ENDCOMPOSITION:
            if (UIElement* f = input_.Focus().Focused()) {
                f->OnImeEndComposition(hwnd_);
                Render();
                return 0;
            }
            break;

        case WM_DISPLAYCHANGE:
            // A display mode switch (e.g. the user changed 240Hz -> 60Hz) keeps the
            // window on the same monitor, so only an explicit re-query notices it.
            InvalidateRefreshHz();
            break;

        case WM_DEVICECHANGE:
            OnDeviceChange(wp, lp);
            return TRUE;

        case WM_CTLCOLOREDIT:
        case WM_CTLCOLORSTATIC: {
            const ColorTokens& pal = theme_.Manager().Snapshot().colors;
            HDC hdc = reinterpret_cast<HDC>(wp);
            SetTextColor(hdc, RGB(static_cast<int>(pal.textPrimary.r * 255.0f),
                                  static_cast<int>(pal.textPrimary.g * 255.0f),
                                  static_cast<int>(pal.textPrimary.b * 255.0f)));
            SetBkColor(hdc, RGB(static_cast<int>(pal.cardFill.r * 255.0f),
                                static_cast<int>(pal.cardFill.g * 255.0f),
                                static_cast<int>(pal.cardFill.b * 255.0f)));
            return reinterpret_cast<LRESULT>(controlBgBrush_ ? controlBgBrush_ : GetStockObject(NULL_BRUSH));
        }

        case WM_ERASEBKGND:
            return 1;  // DComp owns the content; no GDI erase

        case WM_PAINT: {
            PAINTSTRUCT ps;
            BeginPaint(hwnd_, &ps);
            EndPaint(hwnd_, &ps);
            return 0;
        }

        case WM_DESTROY: {
            // Guard against reentrancy: ~NativeWindowHost() calls DestroyWindow(hwnd_)
            // from the base-class dtor phase when the derived class (Window, elements_)
            // is already destroyed. That second WM_DESTROY would call OnDestroying/
            // OnDestroyed (pure virtual dispatch -> UB) and detach already-freed elements.
            // Bail immediately on the second pass; the first pass already did the work.
            if (destroying_) {
                hwnd_ = nullptr;  // Mark as destroyed so dtor sees hwnd_==nullptr
                return 0;
            }
            // Mark teardown so an input message that synchronously triggered this
            // (e.g. a Close button's handler) skips its trailing member access
            // after unwinding back out of the nested WM_DESTROY.
            destroying_ = true;
            KillTimer(hwnd_, kSelfFrameTimerId);
            KillTimer(hwnd_, kDeviceRetryTimerId);
            // Revoke the OLE drop registration while hwnd_ is still valid. OLE holds
            // a BORROWED pointer to this object (AddRef/Release are no-ops — see the
            // header), so this must happen before the window object can go away or
            // the OS would be left with a dangling IDropTarget.
            if (dragDropRegistered_) {
                RevokeDragDrop(hwnd_);
                dragDropRegistered_ = false;
                OleUninitialize();
            }
            dragOverElement_ = nullptr;
            // Stop the modal heartbeat BEFORE the HWND is cleared. Its worker posts
            // to hwnd_, so a tick in flight past this point would target a destroyed
            // window. Stop() joins the worker, so once it returns no further post can
            // happen. Normally WM_EXITSIZEMOVE already stopped it, but a window can be
            // destroyed from inside a drag (an owner closing it), so this is the
            // backstop rather than the usual path.
            modalHeartbeat_.Stop();
            // Same for the HUD thread: it posts pings to hwnd_ and owns a composition
            // target bound to it, which must be released before the window goes.
            if (hud_) hud_->Stop();
            // Close any active popup while the owning elements are still alive and
            // the HWND is valid, so a control never tears down mid-dismiss
            // (single deterministic shutdown path).
            CloseActivePopups();
            // UIA, step 1: stop serving WM_GETOBJECT (destroying_ is set too) and drop
            // queued UIA actions/events before any element goes away.
            automation_.OnWindowDestroying();
            OnDestroying();
            // Detach every registered root while it is still alive and the window
            // members (anim registry, device) are valid. This
            // releases context-scoped subscriptions and drops animations now, and
            // clears each element's context so a later element destructor (if it
            // outlives the window) never dereferences the by-then-dead registry.
            for (UIElement* e : elements_) {
                if (!e) continue;
                // Clear the element from the input/focus state before it detaches
                // so no manager keeps a dangling hot/captured/focused pointer.
                input_.Input().OnElementDetached(e);
                if (e->IsAttached()) e->DetachFromContext();
            }
            // UIA, step 2: the detach above froze and queued every element
            // provider; disconnect them and then the root while hwnd_ is valid.
            automation_.OnWindowDestroyed(hwnd_);
            hwnd_ = nullptr;
            OnDestroyed();
            if (application_) application_->DetachWindow(*this);
            else if (ShouldPostQuitOnDestroy()) PostQuitMessage(0);
            return 0;
        }
    }
    return DefWindowProcW(hwnd_, msg, wp, lp);
}

LRESULT NativeWindowHost::HitTestNca(POINT ptScreen) {
    RECT rc;
    GetWindowRect(hwnd_, &rc);
    // WHERE THE BAND ACTUALLY IS. WM_NCCALCSIZE shrinks only the CLIENT rect; the WINDOW
    // rect is untouched. So the reserved non-client band lies INSIDE the window rect,
    // between the window edge and the client edge:
    //
    //     window rect   [rc.left,          rc.right)
    //     client rect   [rc.left + inset,  rc.right - inset)
    //     right NC band [rc.right - inset, rc.right)          <-- inside rc
    //
    // The band must therefore be claimed on the INSIDE of each edge. An earlier revision
    // tested [rc.right - 1, rc.right + outerPx) — i.e. 1px of the real band plus a stretch
    // of pure outside — which left inset-1 px of genuine non-client area falling through
    // to HTCLIENT at the bottom of this function, and leaned on an outside band that a
    // frameless window never receives WM_NCHITTEST for. That is why the sides felt so much
    // thinner than the bottom despite identical arithmetic: only ~1px of each was live.
    //
    // `tolerancePx` extends each band a little past the window edge. It costs nothing when
    // those coordinates are not delivered, and helps on the configurations where they are.
    const int insetPx = ResizeGripOuterPx(dpi_);
    constexpr int tolerancePx = 4;

    bool left = false, right = false, top = false, bottom = false;

    // Left/right/bottom: the full reserved band, inside the window rect, plus tolerance.
    // Top: no NC band exists (a top inset makes DWM draw its own caption over ours — the
    // double-title-bar artifact), so the top grip has to live inside the CLIENT area and
    // compete with the title bar. It is kept at insetPx for consistency of feel.
    if (ptScreen.x >= rc.left - tolerancePx && ptScreen.x < rc.left + insetPx)
        left = true;
    if (ptScreen.x >= rc.right - insetPx && ptScreen.x < rc.right + tolerancePx)
        right = true;
    if (ptScreen.y >= rc.top && ptScreen.y < rc.top + insetPx)
        top = true;
    if (ptScreen.y >= rc.bottom - insetPx && ptScreen.y < rc.bottom + tolerancePx)
        bottom = true;

    if (top && left) return HTTOPLEFT;
    if (top && right) return HTTOPRIGHT;
    if (bottom && left) return HTBOTTOMLEFT;
    if (bottom && right) return HTBOTTOMRIGHT;
    if (left) return HTLEFT;
    if (right) return HTRIGHT;
    if (top) return HTTOP;
    if (bottom) return HTBOTTOM;

    // Title bar drag region (top strip, excluding any element hit areas).
    int titleBarPx = static_cast<int>(TitleBarHeightDip() * DpiScale() + 0.5f);
    if (ptScreen.y >= rc.top && ptScreen.y < rc.top + titleBarPx) {
        // If an interactive element sits here, let it receive the click.
        float dipX = (ptScreen.x - rc.left) * 96.0f / dpi_;
        float dipY = (ptScreen.y - rc.top) * 96.0f / dpi_;
        if (CaptionButtonAt(dipX, dipY)) return HTCLIENT;
        if (input_.Input().HitTest(Point{dipX, dipY})) return HTCLIENT;
        return HTCAPTION;
    }
    return HTCLIENT;
}

void NativeWindowHost::OnSize(UINT pixelW, UINT pixelH) {
    if (!graphicsReady_) return;
    // Resize coalescing: a drag fires WM_SIZE at ~per-pixel frequency;
    // doing a full ResizeBuffers + OnLayout + synchronous RenderNow on each one
    // repaints every intermediate size and starves everything else (the animation
    // fallback timer, in particular). Instead, just record the latest size and
    // request a Resize frame. ApplyPendingResize() does the real work once per
    // painted frame — from RunDueFrame() (normal loop) or, inside the system modal
    // resize loop where our loop is not spinning, from the kAnimTimerId heartbeat
    // (see WM_TIMER) and WM_EXITSIZEMOVE. Result: at most one ResizeBuffers + one
    // OnLayout + one paint per frame regardless of WM_SIZE burst rate.
    pendingPixelW_ = std::max<UINT>(1, pixelW);
    pendingPixelH_ = std::max<UINT>(1, pixelH);
    resizePending_ = true;
    RequestFrame(FrameReason::Resize);
}

void NativeWindowHost::ApplyPendingResize() {
    if (!resizePending_ || !graphicsReady_) return;

    const int64_t applyStart = QpcNow();

    // Time this and carry it to the next frame's stats. It runs BEFORE RenderNow
    // starts its own clock, so without this the whole cost of a resize — the surface
    // resize plus the OnLayout it forces, which for a text control means re-wrapping
    // at the new width — is absent from every number on the HUD. See
    // FrameStats::resizeMs.
    ScopedTimer resizeTimer(pendingResizeMs_);
    resizePending_ = false;
    // Coalesce: only the most recent size matters. Skip the swap-chain work if the
    // committed size already matches (e.g. a maximize that lands on the same rect).
    const bool sizeChanged = (pendingPixelW_ != pixelW_) || (pendingPixelH_ != pixelH_);
    pixelW_ = pendingPixelW_;
    pixelH_ = pendingPixelH_;

    const int64_t beforeCompResize = QpcNow();
    if (sizeChanged)
        graphics_.Comp().Resize(pixelW_, pixelH_);  // resize the surface in place, no rebuild
    const int64_t afterCompResize = QpcNow();

    // A resized content surface has undefined pixels in the newly exposed
    // area — force a full clear for the next 2 frames so no partial redraw trusts them.
    fullDrawRequired_ = 2u;

    // Layout runs every resize frame. The GPU-transform alternatives (uniform root
    // scale, or 1:1 anchoring with a blank strip) were implemented, measured, and
    // deleted: both were fast but looked wrong — scale blurred and squashed the
    // content, anchoring exposed white edges on the right/bottom. What made the
    // per-frame layout affordable instead is UIContext::inModalResize, which skips
    // the DComp surface rasterization inside ScrollContentHost during the drag and
    // converges on WM_EXITSIZEMOVE. That kept the geometry exact at every frame and
    // took layout from 7-13ms to 2-3ms, so there is nothing left to trade away here.
    const int64_t beforeLayout = QpcNow();
    OnLayout();
    const int64_t afterLayout = QpcNow();

    // 细分打印：graphics_.Comp().Resize 和 OnLayout 分别的耗时
    const double compResizeMs = QpcToMs(afterCompResize - beforeCompResize);
    const double layoutMs = QpcToMs(afterLayout - beforeLayout);
    const double totalMs = QpcToMs(afterLayout - applyStart);

    if (!resizeTraceLabel_.empty()) {
        // measure/arrange come from Window::OnLayout via FrameStats — read them right
        // after the OnLayout above so they describe THIS resize, not a later frame.
        // Printed on the same line as the total so no manual correlation between two
        // trace streams is needed: one line says how long, which half, and how many
        // elements were actually re-measured.
        const FrameStats& fs = lastFrameStats_;
        FL_TRACEF("Resize",
                  "Page=[%ls] size=%ux%u  total=%.3fms (surface=%.3f + layout=%.3f "
                  "[measure=%.3f arrange=%.3f]) measured=%d hits=%d",
                  resizeTraceLabel_.c_str(), pixelW_, pixelH_,
                  totalMs, compResizeMs, layoutMs, fs.measureMs, fs.arrangeMs,
                  LayoutCostProbe::GetCount(LayoutCountKey::MeasureCalls),
                  LayoutCostProbe::GetCount(LayoutCountKey::MeasureCacheHits));
        // Per-control breakdown for whichever suspects are instrumented. Silent when
        // nothing recorded, so an uninstrumented page costs one loop over 6 ints.
        LayoutCostProbe::Report("    ");
    }
}

double NativeWindowHost::SampleModalGap() {
    const int64_t now = QpcNow();
    if (modalLastOpportunityQpc_ != 0)
        modalGapMs_ = UpdateOpportunityGap(
            modalGapMs_, QpcToMs(now - modalLastOpportunityQpc_));
    modalLastOpportunityQpc_ = now;
    return modalGapMs_;
}

void NativeWindowHost::ServiceModalFrame(bool pace) {
    if (!graphicsReady_) return;
    // Measure the opportunity spacing BEFORE any early return, and for EVERY caller
    // (heartbeat included, paced or not). The estimate must describe how often we are
    // being given the chance to paint, which is a property of the message stream, not
    // of the frames we chose to accept. Sampling only where the decision is made
    // would feed it the intervals between PAINTS instead — a circular estimate that
    // converges on the refresh interval and stops adapting to the mouse rate.
    const double gapMs = SampleModalGap();
    // Advance animation on the real QPC clock, then paint once — SYNCHRONOUSLY,
    // because inside a modal loop a coalesced RequestFrame sits unserviced until
    // the drag ends (RunDueFrame is not being called). Paint only when there is
    // something to show: a pending resize, a live animation, or an already-pending
    // frame. A pure window *move* with nothing animating must not repaint at all.
    const bool anim = PumpAnimations();
    if (!resizePending_ && !anim && !frame_.Scheduler().NeedsFrame()) return;

    // Pace to the display, like the main loop does. This path CANNOT wait (it runs
    // synchronously inside the system's modal pump, and blocking here would add
    // latency to the drag), so instead skip a frame that is not due yet. The pending
    // request is deliberately left unconsumed, so the next message — or the heartbeat,
    // or WM_EXITSIZEMOVE's final synchronous paint — renders it once it is due.
    // Nothing is dropped; it is only deferred to the refresh boundary.
    //
    // Only for the MOUSE-driven callers: WM_SIZE / WM_MOVING arrive at the mouse's
    // report rate, unrelated to vsync, so a drag otherwise paints once per mouse event
    // (a measured session showed the Moving phase at 132fps on a 120Hz panel; a
    // 500/1000Hz mouse wastes proportionally more). The heartbeat must NOT be paced:
    // it is already self-limiting at kAnimIntervalMs, and Win32 timers are coarse
    // (~15.6ms) and fire early, so on a 60Hz panel a heartbeat landing at 15ms would
    // see 1.67ms remaining, skip, and wait a whole further period — halving a
    // motionless drag to ~32fps. It is the only driver when the mouse holds still.
    //
    // WHY THIS IS NOT FrameIsDue(FrameWaitRemaining()). That predicate is right for
    // the main loop, which can WAIT until the deadline and then paint exactly on it.
    // Here the opportunities are DISCRETE — one per mouse message — and we cannot
    // wait, so a frame that is "not due yet" is not deferred by the fraction of a
    // millisecond that remains: it is deferred by a whole mouse-report interval.
    // Applying the main loop's threshold here aliases badly against the mouse rate.
    // Simulated over a second of dragging on a 120Hz panel with a 125Hz mouse:
    //
    //   jitter          0.0ms  0.5ms  1.0ms  2.0ms
    //   old (>= 7.33ms)   125    120     93     85
    //   main-loop rule    125     78     78     78    <- worse, threshold even tighter
    //   nearest-opp       125    125    125    124
    //
    // FrameIsNearestOpportunity instead picks whichever of "paint now, early by `remaining`"
    // and "paint at the next opportunity, late by `gap - remaining`" lands closer to
    // the deadline. It is self-tuning: a 1000Hz mouse gets a tight threshold (so we
    // do not paint faster than the panel), a 125Hz mouse a loose one (so jitter never
    // costs a whole frame). Passing gap == 0 degrades to "always paint", which is
    // exactly the unpaced heartbeat behaviour, so the pace==false path is unchanged.
    //
    // Exception: the FIRST opportunity of a drag (gapMs == 0, no estimate yet) must
    // never be skipped — doing so defers the first visible resize by a whole mouse
    // period, producing a jarring stall before the window starts following the cursor.
    // Once the estimate is seeded (second opportunity onward), pacing kicks in.
    if (pace && gapMs > 0.0 &&
        !FrameIsNearestOpportunity(FrameWaitRemaining(), gapMs))
        return;

    FrameReason r = frame_.Scheduler().BeginFrame();
    ApplyPendingResize();
    RenderNow(AnyReason(r) ? r : FrameReason::Animation);
    frame_.Scheduler().EndFrame();
}

void NativeWindowHost::OnDpiChanged(UINT newDpi, const RECT* suggested) {
    const UINT oldDpi = dpi_;
    dpi_ = newDpi ? newDpi : 96;
    if (suggested) {
        FL_TRACEF(kTag, "DPI: %u -> %u scale=%.3f suggested=(%ld,%ld,%ldx%ld)",
                  oldDpi, dpi_, DpiScale(), suggested->left, suggested->top,
                  suggested->right - suggested->left,
                  suggested->bottom - suggested->top);
    } else {
        FL_TRACEF(kTag, "DPI: %u -> %u scale=%.3f suggested=(none)",
                  oldDpi, dpi_, DpiScale());
    }
    // A DPI change usually means the window moved to another monitor, which may run at
    // a different refresh rate — re-query it so frame pacing follows the new panel.
    InvalidateRefreshHz();
    // Keep the injected context's DPI current so anything reading Context() after
    // a monitor change sees the new scale. Existing attached
    // elements cache the context by value; they still get OnDpiChanged below for
    // the per-DPI resource refresh.
    context_.dpiScale = DpiScale();
    // Bump the resource-cache epoch on a DPI change too: DIP-space
    // layouts are DPI-independent so nothing rebuilds today, but a future
    // rasterized/realized resource keyed by DPI would — the backstop is free.
    graphics_.Cache().BumpEpoch();
    // A DPI change repositions/resizes and rebuilds layout — force a
    // full clear for the next 2 frames (same reason as OnSize).
    fullDrawRequired_ = 2u;
    if (suggested) {
        // CLAUDE.md: honor the suggested rect.
        SetWindowPos(hwnd_, nullptr, suggested->left, suggested->top,
                     suggested->right - suggested->left,
                     suggested->bottom - suggested->top,
                     SWP_NOZORDER | SWP_NOACTIVATE);
    }
    // Every element stores UIContext by value. Refresh those copies BEFORE the
    // notification: composited controls read Context().dpiScale while rebuilding
    // their pixel-space visuals in OnDpiChanged.
    for (UIElement* e : elements_) {
        if (!e) continue;
        e->UpdateContextDpi(DpiScale());
        e->OnDpiChanged(DpiScale());
    }
    // The icon frames were picked for the old DPI; reload so the title bar keeps
    // drawing 1:1 and the shell gets the matching small/big sizes.
    RecreateWindowIcon();
    // WM_DPICHANGED runs in a system modal path; paint synchronously and consume
    // any coalesced request so the on-demand loop doesn't repaint a stale frame.
    // Resize coalescing: the SetWindowPos above fired a synchronous WM_SIZE that only set
    // resizePending_ (deferred). Commit it here — ApplyPendingResize does the
    // surface Resize + OnLayout — so the content surface matches the new size/DPI before
    // we paint. If no size actually changed, force the layout the DPI change needs.
    frame_.Scheduler().BeginFrame();
    if (resizePending_) ApplyPendingResize();
    else OnLayout();
    RenderNow(FrameReason::Dpi);
    frame_.Scheduler().EndFrame();
}

RectDip NativeWindowHost::CaptionButtonRect(int hit) const {
    float w = static_cast<float>(pixelW_) / DpiScale();
    float h = TitleBarHeightDip();
    float x = w;
    switch (hit) {
        case HTCLOSE:    x -= kCaptionBtnWidthDip; break;
        case HTMAXBUTTON: x -= kCaptionBtnWidthDip * 2.0f; break;
        case HTMINBUTTON: x -= kCaptionBtnWidthDip * 3.0f; break;
        default: return {};
    }
    return {x, 0.0f, kCaptionBtnWidthDip, h};
}

int NativeWindowHost::CaptionButtonAt(float dipX, float dipY) const {
    if (CaptionButtonRect(HTCLOSE).contains(dipX, dipY)) return HTCLOSE;
    if (CaptionButtonRect(HTMAXBUTTON).contains(dipX, dipY)) return HTMAXBUTTON;
    if (CaptionButtonRect(HTMINBUTTON).contains(dipX, dipY)) return HTMINBUTTON;
    return 0;
}

void NativeWindowHost::DrawCaptionButtons(ID2D1DeviceContext* dc, float) {
    const ColorTokens& pal = theme_.Manager().Snapshot().colors;
    ComPtr<ID2D1SolidColorBrush> brush;
    if (FAILED(dc->CreateSolidColorBrush(pal.textPrimary, brush.GetAddressOf()))) return;

    auto fillButton = [&](int hit) {
        RectDip r = CaptionButtonRect(hit);
        if (r.w <= 0) return;
        if (pressedCaption_ == hit) {
            brush->SetColor(hit == HTCLOSE
                ? D2D1::ColorF(0.76f, 0.12f, 0.12f, 0.85f)
                : pal.controlFillPressed);
            dc->FillRectangle(D2D1::RectF(r.x, r.y, r.right(), r.bottom()), brush.Get());
        } else if (hotCaption_ == hit) {
            brush->SetColor(hit == HTCLOSE
                ? D2D1::ColorF(0.91f, 0.15f, 0.15f, 0.95f)
                : pal.controlFillHover);
            dc->FillRectangle(D2D1::RectF(r.x, r.y, r.right(), r.bottom()), brush.Get());
        }
    };

    fillButton(HTMINBUTTON);
    fillButton(HTMAXBUTTON);
    fillButton(HTCLOSE);

    auto glyphColor = [&](int hit) -> D2D1_COLOR_F {
        if (hit == HTCLOSE && (hotCaption_ == hit || pressedCaption_ == hit))
            return D2D1::ColorF(1, 1, 1, 1);
        return pal.textPrimary;
    };

    auto center = [](const RectDip& r) {
        return D2D1::Point2F(r.x + r.w * 0.5f, r.y + r.h * 0.5f);
    };

    // Minimize: horizontal line.
    RectDip minR = CaptionButtonRect(HTMINBUTTON);
    D2D1_POINT_2F c = center(minR);
    brush->SetColor(glyphColor(HTMINBUTTON));
    dc->DrawLine(D2D1::Point2F(c.x - kCaptionGlyphDip * 0.5f, c.y + 4.0f),
                 D2D1::Point2F(c.x + kCaptionGlyphDip * 0.5f, c.y + 4.0f),
                 brush.Get(), 1.25f);

    // Maximize/restore: square outline.
    RectDip maxR = CaptionButtonRect(HTMAXBUTTON);
    c = center(maxR);
    brush->SetColor(glyphColor(HTMAXBUTTON));
    bool zoomed = IsZoomed(hwnd_) != FALSE;
    if (zoomed) {
        dc->DrawRectangle(D2D1::RectF(c.x - 3.0f, c.y - 5.0f, c.x + 5.0f, c.y + 3.0f), brush.Get(), 1.0f);
        dc->DrawRectangle(D2D1::RectF(c.x - 5.0f, c.y - 3.0f, c.x + 3.0f, c.y + 5.0f), brush.Get(), 1.0f);
    } else {
        dc->DrawRectangle(D2D1::RectF(c.x - 5.0f, c.y - 5.0f, c.x + 5.0f, c.y + 5.0f), brush.Get(), 1.0f);
    }

    // Close: X.
    RectDip closeR = CaptionButtonRect(HTCLOSE);
    c = center(closeR);
    brush->SetColor(glyphColor(HTCLOSE));
    dc->DrawLine(D2D1::Point2F(c.x - 5.0f, c.y - 5.0f),
                 D2D1::Point2F(c.x + 5.0f, c.y + 5.0f), brush.Get(), 1.25f);
    dc->DrawLine(D2D1::Point2F(c.x + 5.0f, c.y - 5.0f),
                 D2D1::Point2F(c.x - 5.0f, c.y + 5.0f), brush.Get(), 1.25f);
}

void NativeWindowHost::SetIconResource(UINT resourceId) {
    if (iconResourceId_ == resourceId) return;
    iconResourceId_ = resourceId;
    FL_TRACEF(kTag, "icon resource set id=%u hwnd=%p", resourceId, hwnd_);
    titleIconBitmap_.Reset();
    if (hwnd_) {
        RecreateWindowIcon();
        Render();
    }
}

void NativeWindowHost::RecreateWindowIcon() {
    titleIconBitmap_.Reset();
    if (!hwnd_ || !hInst_) {
        FL_TRACEF(kTag, "icon skipped id=%u hwnd=%p instance=%p",
                  iconResourceId_, hwnd_, hInst_);
        return;
    }

    // Load separate small (16px) and big (32px) icons at the current DPI so they
    // are crisp at any scale. LR_DEFAULTSIZE would pick only one size (usually 32px),
    // forcing the system to downscale it for the taskbar and forcing D2D to downscale
    // it again for the title bar — both with quality loss.
    HICON iconSmall = nullptr, iconBig = nullptr;
    if (iconResourceId_) {
        const int smallPx = WindowIconPx(kSmallIconDip, dpi_);
        const int bigPx = WindowIconPx(kBigIconDip, dpi_);
        iconSmall = static_cast<HICON>(LoadImageW(
            hInst_, MAKEINTRESOURCEW(iconResourceId_), IMAGE_ICON,
            smallPx, smallPx, LR_DEFAULTCOLOR));
        iconBig = static_cast<HICON>(LoadImageW(
            hInst_, MAKEINTRESOURCEW(iconResourceId_), IMAGE_ICON,
            bigPx, bigPx, LR_DEFAULTCOLOR));
        FL_TRACEF(kTag, "icon id=%u dpi=%u small=%dpx(%p) big=%dpx(%p) err=%lu",
                  iconResourceId_, dpi_, smallPx, iconSmall, bigPx, iconBig, GetLastError());
    }
    SendMessageW(hwnd_, WM_SETICON, ICON_BIG, reinterpret_cast<LPARAM>(iconBig));
    SendMessageW(hwnd_, WM_SETICON, ICON_SMALL, reinterpret_cast<LPARAM>(iconSmall));
    // The window no longer references the previous pair; free it.
    ReleaseWindowIcons();
    iconSmall_ = iconSmall;
    iconBig_ = iconBig;
    HICON icon = iconSmall;  // the title bar draws the 16 DIP frame 1:1
    if (!icon || !graphics_.D2D().DC()) {
        TraceMsg(kTag, "icon bitmap skipped: icon or D2D context unavailable");
        return;
    }

    ComPtr<IWICImagingFactory> factory;
    ComPtr<IWICBitmap> bitmap;
    ComPtr<IWICFormatConverter> converter;
    HRESULT hr = CoCreateInstance(CLSID_WICImagingFactory, nullptr,
                                  CLSCTX_INPROC_SERVER,
                                  IID_PPV_ARGS(factory.GetAddressOf()));
    Trace(kTag, "icon WIC factory", hr);
    if (SUCCEEDED(hr)) {
        hr = factory->CreateBitmapFromHICON(icon, bitmap.GetAddressOf());
        Trace(kTag, "icon CreateBitmapFromHICON", hr);
    }
    if (SUCCEEDED(hr)) {
        UINT width = 0, height = 0;
        bitmap->GetSize(&width, &height);
        FL_TRACEF(kTag, "icon WIC size=%ux%u", width, height);
        hr = factory->CreateFormatConverter(converter.GetAddressOf());
        Trace(kTag, "icon CreateFormatConverter", hr);
    }
    if (SUCCEEDED(hr)) {
        hr = converter->Initialize(bitmap.Get(), GUID_WICPixelFormat32bppPBGRA,
                                   WICBitmapDitherTypeNone, nullptr, 0.0,
                                   WICBitmapPaletteTypeMedianCut);
        Trace(kTag, "icon converter Initialize(PBGRA)", hr);
    }
    if (SUCCEEDED(hr)) {
        hr = graphics_.D2D().DC()->CreateBitmapFromWicBitmap(converter.Get(), nullptr,
                                                  titleIconBitmap_.GetAddressOf());
        Trace(kTag, "icon CreateBitmapFromWicBitmap", hr);
    }
    FL_TRACEF(kTag, "icon ready=%d bitmap=%p", titleIconBitmap_ ? 1 : 0,
              titleIconBitmap_.Get());
}

void NativeWindowHost::DrawWindowIcon(ID2D1DeviceContext* dc) {
    if (!titleIconBitmap_ || !dc) return;
    // Draw the bitmap 1:1: its pixel size was chosen for the current DPI in
    // RecreateWindowIcon, and the origin is snapped to whole pixels, so D2D maps
    // each source pixel onto exactly one device pixel. NEAREST_NEIGHBOR then
    // guarantees no filtering even if a rounding edge case leaves a sub-pixel
    // remainder (a blur there is what made thin strokes disappear).
    const D2D1_SIZE_U px = titleIconBitmap_->GetPixelSize();
    const float s = DpiScale();
    const POINT o = TitleIconOriginPx(kTitleIconLeftDip, TitleBarHeightDip(),
                                      static_cast<int>(px.height), dpi_);
    const float x = o.x / s;
    const float y = o.y / s;
    dc->DrawBitmap(titleIconBitmap_.Get(),
                   D2D1::RectF(x, y, x + px.width / s, y + px.height / s),
                   1.0f, D2D1_BITMAP_INTERPOLATION_MODE_NEAREST_NEIGHBOR);
}

void NativeWindowHost::ReleaseWindowIcons() {
    if (iconSmall_) DestroyIcon(iconSmall_);
    if (iconBig_) DestroyIcon(iconBig_);
    iconSmall_ = nullptr;
    iconBig_ = nullptr;
}

void NativeWindowHost::DrawStandardTitle(ID2D1DeviceContext* dc) {
    if (!UsesStandardTitleBar() || !dc) return;
    const wchar_t* text = TitleText();
    if (!text || !*text) return;

    const auto& theme = theme_.Manager().Snapshot();
    IDWriteTextFormat* format = graphics_.DWrite().Format(
        theme.typography.subtitleSize, DWRITE_FONT_WEIGHT_SEMI_BOLD,
        DWRITE_TEXT_ALIGNMENT_LEADING, DWRITE_PARAGRAPH_ALIGNMENT_CENTER,
        DWRITE_WORD_WRAPPING_NO_WRAP);
    if (!format) return;

    ComPtr<ID2D1SolidColorBrush> brush;
    if (FAILED(dc->CreateSolidColorBrush(theme.colors.textPrimary, brush.GetAddressOf()))) return;
    const float left = titleIconBitmap_ ? 34.0f : 12.0f;
    const float right = std::max(left, ClientWidthDip() - kCaptionBtnWidthDip * 3.0f);
    dc->DrawTextW(text, static_cast<UINT32>(wcslen(text)), format,
                  D2D1::RectF(left, 0.0f, right, TitleBarHeightDip()), brush.Get());
}

void NativeWindowHost::UpdateNativeTitle(const std::wstring& title) {
    title_ = title;
    if (!hwnd_) return;
    SetWindowTextW(hwnd_, title_.c_str());
    SetPropW(hwnd_, L"FluentUI.AccessibleName", const_cast<wchar_t*>(title_.c_str()));
    Render();
}

// ---------------------------------------------------------------------------
// Input translation: window -> InputManager
// ---------------------------------------------------------------------------

Point NativeWindowHost::ToDip(int px, int py) const {
    float s = 96.0f / dpi_;
    return Point{px * s, py * s};
}

ModifierKeys NativeWindowHost::CurrentModifiers() {
    ModifierKeys m = ModifierKeys::None;
    if (GetKeyState(VK_CONTROL) & 0x8000) m |= ModifierKeys::Ctrl;
    if (GetKeyState(VK_SHIFT) & 0x8000) m |= ModifierKeys::Shift;
    if (GetKeyState(VK_MENU) & 0x8000) m |= ModifierKeys::Alt;
    return m;
}

void NativeWindowHost::InvalidateThunk(void* ctx) {
    static_cast<NativeWindowHost*>(ctx)->Render();
}

void NativeWindowHost::ArmTooltip(UIElement* hovered) {
    // Same element, same text: Keep — whatever state we're in (a pending timer
    // counting down or a tooltip already shown) continues. The text is part of the
    // target because a composite control (NavRail) is one element whose tooltip
    // changes per region; comparing the element alone never re-armed there. While
    // a popup is open, or the pointer is still on the element a popup opened from,
    // TooltipArming never arms.
    switch (tooltipArming_.OnHover(hovered, InteractivePopupOpen())) {
        case TooltipArming::Action::Keep:
            return;
        case TooltipArming::Action::Hide:
            CancelTooltip();
            return;
        case TooltipArming::Action::HideAndArm:
            // A new target hides any visible tooltip and cancels a pending one,
            // then the delay restarts for the new target.
            CancelTooltip();
            if (tooltip_) {
                SetTimer(hwnd_, kTooltipTimerId, kTooltipDelayMs, nullptr);
                tooltipTimerOn_ = true;
            }
            return;
    }
}

void NativeWindowHost::CancelTooltip() {
    if (tooltipTimerOn_) {
        KillTimer(hwnd_, kTooltipTimerId);
        tooltipTimerOn_ = false;
    }
    if (tooltip_ && tooltip_->IsVisible()) tooltip_->Hide();
}

void NativeWindowHost::HideTooltip() {
    CancelTooltip();
    tooltipArming_.OnDismissed();
}

void NativeWindowHost::OnPopupOpened(HWND popup) {
    if (popup && std::find(openPopups_.begin(), openPopups_.end(), popup) == openPopups_.end())
        openPopups_.push_back(popup);
    // Query the hit target now rather than trusting Armed(): a button's flyout
    // opens on release, and the press before it already cleared the armed target.
    const bool wasVisible = tooltip_ && tooltip_->IsVisible();
    CancelTooltip();
    tooltipArming_.OnPopupOpened(input_.Input().Hot());
    FL_TRACEF(kTag, "Tooltip: popup opened, tooltip %s, suppressed until pointer moves on",
              wasVisible ? "dismissed" : "not visible");
}

bool NativeWindowHost::InteractivePopupOpen() {
    // Called on every pointer move; the list is almost always empty.
    openPopups_.erase(std::remove_if(openPopups_.begin(), openPopups_.end(),
                                     [](HWND h) { return !IsWindow(h) || !IsWindowVisible(h); }),
                      openPopups_.end());
    return !openPopups_.empty();
}

void NativeWindowHost::LightDismissAt(int px, int py) {
    if (!popupDismiss_) return;
    POINT pt = {px, py};
    ClientToScreen(hwnd_, &pt);
    DispatchPopupDismiss(PopupDismissReason::Click, nullptr, pt.x, pt.y);
}

bool NativeWindowHost::DispatchPopupDismiss(PopupDismissReason reason, HWND other,
                                            int x, int y) {
    // The hook used to be called in place and cleared with `if (hook(...)) hook =
    // nullptr;`. Two things were wrong with that once a control's closed event may
    // reopen the popup from inside the hook (the close now finishes before the event
    // is raised, so a reopen succeeds):
    //   * the reopen registers a NEW hook in this slot, and the unconditional clear
    //     on return wiped it, so the reopened popup could no longer be dismissed by a
    //     click outside, a window move or deactivation;
    //   * the control's own Subscription reset empties the slot while the closure in
    //     it is still executing, destroying a running std::function.
    // So the hook runs from a local copy, and the slot is only touched afterwards if
    // nobody re-registered or unregistered during the call (dismissGen_ unchanged).
    return InvokeOwnedHook(popupDismiss_, dismissGen_, reason, other, x, y);
}

void NativeWindowHost::RefreshCursor() {
    POINT pt;
    if (!GetCursorPos(&pt)) return;
    ScreenToClient(hwnd_, &pt);
    HCURSOR c = nullptr;
    if (UIElement* cap = input_.Input().Captured()) c = cap->Cursor();
    if (!c) {
        if (UIElement* e = input_.Input().HitTest(ToDip(pt.x, pt.y))) c = e->Cursor();
    }
    static HCURSOR arrow = LoadCursor(nullptr, IDC_ARROW);
    SetCursor(c ? c : arrow);
}

void NativeWindowHost::UpdateImePosition() {
    UIElement* f = input_.Focus().Focused();
    if (!f) return;
    RectDip caret;
    if (!f->CaretRectDip(caret)) return;

    // Convert the caret (window DIPs) to client pixels and ask the IME to put
    // its composition + candidate window at the caret's bottom-left.
    float s = DpiScale();
    HIMC himc = ImmGetContext(hwnd_);
    if (!himc) return;

    COMPOSITIONFORM cf = {};
    cf.dwStyle = CFS_POINT;
    cf.ptCurrentPos.x = static_cast<LONG>(caret.x * s + 0.5f);
    cf.ptCurrentPos.y = static_cast<LONG>(caret.y * s + 0.5f);
    ImmSetCompositionWindow(himc, &cf);

    // Also place the candidate list right below the caret.
    CANDIDATEFORM candf = {};
    candf.dwIndex = 0;
    candf.dwStyle = CFS_CANDIDATEPOS;
    candf.ptCurrentPos.x = cf.ptCurrentPos.x;
    candf.ptCurrentPos.y = static_cast<LONG>(caret.bottom() * s + 0.5f);
    ImmSetCandidateWindow(himc, &candf);

    ImmReleaseContext(hwnd_, himc);
}

// ---------------------------------------------------------------------------
// Window state capture / restore (plain data; persistence is the caller's job)
// ---------------------------------------------------------------------------

void NativeWindowHost::ApplyRestoreState(RECT& rect, UINT dpi,
                                       const WindowState* restore) {
    if (!restore || !restore->valid) return;

    // `dpi` is the PRIMARY monitor's DPI (resolved before any window exists). But
    // the saved rect may live on a DIFFERENT monitor — rescaling by the primary
    // DPI then wrongly grows/shrinks the frame (the size looks right on the
    // primary but is doubled/halved on the actual target monitor). Use the DPI of
    // the monitor the saved rect lands on as the rescale target (per-window DPI, never
    // a global one).
    UNREFERENCED_PARAMETER(dpi);
    RECT savedRect = {restore->x, restore->y,
                      restore->x + restore->width, restore->y + restore->height};
    HMONITOR mon = MonitorFromRect(&savedRect, MONITOR_DEFAULTTONULL);
    if (!mon) {  // saved rect off-screen (monitor unplugged): keep the fallback.
        TraceMsg(kTag, "saved window rect off-screen, using centered default");
        return;
    }
    UINT targetDpi = 96, ydpi = 96;
    if (FAILED(GetDpiForMonitor(mon, MDT_EFFECTIVE_DPI, &targetDpi, &ydpi)) || !targetDpi)
        targetDpi = 96;

    UINT savedDpi = restore->dpi ? restore->dpi : 96;
    int w = (targetDpi != savedDpi) ? MulDiv(restore->width, targetDpi, savedDpi)
                                    : restore->width;
    int h = (targetDpi != savedDpi) ? MulDiv(restore->height, targetDpi, savedDpi)
                                    : restore->height;

    POINT minSize{0, 0};
    if (MinTrackSizePx(minClientWDip_, minClientHDip_, targetDpi, minSize)) {
        w = std::max(w, static_cast<int>(minSize.x));
        h = std::max(h, static_cast<int>(minSize.y));
    }

    WindowState candidate = *restore;
    candidate.width = w;
    candidate.height = h;

    // Validate the placement is still on a connected display; if the monitor is
    // gone, keep the centered fallback rect that was passed in.
    if (!candidate.OnMonitor()) {
        TraceMsg(kTag, "saved window rect off-screen, using centered default");
        return;
    }

    // Clamp into the target monitor's work area so the caption bar stays visible
    // and grabbable. A window dragged mostly off-screen (only an edge showing)
    // otherwise leaves no reachable title bar to drag — and it would restore to
    // that same stuck spot on the next launch. If the window is larger than the
    // work area, align its top-left so at least the caption is on-screen.
    int rx = restore->x, ry = restore->y;
    MONITORINFO mi = {sizeof(mi)};
    if (GetMonitorInfo(mon, &mi)) {
        const RECT& wa = mi.rcWork;
        if (rx + w > wa.right)  rx = wa.right - w;
        if (rx < wa.left)       rx = wa.left;
        if (ry + h > wa.bottom) ry = wa.bottom - h;
        if (ry < wa.top)        ry = wa.top;
    }

    rect = {rx, ry, rx + w, ry + h};
    restoreMaximized_ = restore->maximized;
}

void NativeWindowHost::SetMinClientSizeDip(float wDip, float hDip) {
    minClientWDip_ = wDip > 0.0f ? wDip : 0.0f;
    minClientHDip_ = hDip > 0.0f ? hDip : 0.0f;
    if (!hwnd_) return;  // applied by WM_GETMINMAXINFO once the window exists

    // A new limit larger than the current size would otherwise not bite until the user
    // next dragged an edge, leaving the window sitting below its own minimum. Grow it
    // now. Skipped while maximized/minimized: the placement is not the user's restored
    // size, and WM_GETMINMAXINFO will enforce the limit when it is restored.
    if (IsIconic(hwnd_) || IsZoomed(hwnd_)) return;
    POINT minPx = {0, 0};
    if (!MinTrackSizePx(minClientWDip_, minClientHDip_, dpi_, minPx)) return;
    RECT rc;
    if (!GetWindowRect(hwnd_, &rc)) return;
    const LONG w = rc.right - rc.left, h = rc.bottom - rc.top;
    const LONG newW = w < minPx.x ? minPx.x : w;
    const LONG newH = h < minPx.y ? minPx.y : h;
    if (newW == w && newH == h) return;
    SetWindowPos(hwnd_, nullptr, 0, 0, newW, newH,
                 SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
}

WindowState NativeWindowHost::CaptureWindowState() const {
    WindowState s;
    if (!hwnd_) return s;

    // GetWindowPlacement gives the normal (restored) rect plus the maximized
    // flag, so we persist the un-maximized size even when closing maximized.
    WINDOWPLACEMENT wp = {sizeof(wp)};
    if (!GetWindowPlacement(hwnd_, &wp)) return s;

    const RECT& n = wp.rcNormalPosition;
    s.x = n.left;
    s.y = n.top;
    s.width = std::max<int>(1, n.right - n.left);
    s.height = std::max<int>(1, n.bottom - n.top);
    s.maximized = (wp.showCmd == SW_SHOWMAXIMIZED) || (IsZoomed(hwnd_) != FALSE);
    s.dpi = dpi_;
    s.valid = true;
    return s;
}

// ────────────────────────────────────────────────────────────────────────────
// IDropTarget implementation — routes OS drag-drop to element handlers
// ────────────────────────────────────────────────────────────────────────────

STDMETHODIMP NativeWindowHost::QueryInterface(REFIID riid, void** ppv) {
    if (riid == IID_IUnknown || riid == IID_IDropTarget) {
        *ppv = static_cast<IDropTarget*>(this);
        AddRef();
        return S_OK;
    }
    *ppv = nullptr;
    return E_NOINTERFACE;
}

Point NativeWindowHost::DropPointToClientDip(POINTL pt) const {
    POINT client = {pt.x, pt.y};
    ScreenToClient(hwnd_, &client);
    const float scale = DpiScale();
    return {client.x / scale, client.y / scale};
}

UIElement* NativeWindowHost::FindDropTarget(Point dip) const {
    // Reverse-walk so the top-most (last-registered) root gets first hit, matching
    // the paint order the same way HitTestDeep does.
    for (auto it = elements_.rbegin(); it != elements_.rend(); ++it) {
        if (!*it) continue;
        if (UIElement* target = (*it)->HitTestDropTarget(dip.x, dip.y))
            return target;
    }
    return nullptr;
}

// The proposed effect for a drag, given what the source allows. Copy is
// preferred (the common file-drop case); Move and Link are accepted when Copy
// is not on offer. A source that allows nothing yields None.
static DragDropEffect PreferredEffect(DWORD allowed) {
    if (allowed & DROPEFFECT_COPY) return DragDropEffect::Copy;
    if (allowed & DROPEFFECT_MOVE) return DragDropEffect::Move;
    if (allowed & DROPEFFECT_LINK) return DragDropEffect::Link;
    return DragDropEffect::None;
}

STDMETHODIMP NativeWindowHost::DragEnter(IDataObject* data, DWORD /*keyState*/,
                                        POINTL pt, DWORD* effect) {
    if (!effect) return E_POINTER;

    // Cache the data object for the whole gesture: DragOver does NOT receive it,
    // and a target that wants to decide its effect from the payload (accept files
    // but not text) needs it on every move, not only on enter and drop.
    input_.DragData() = data;

    const Point dip = DropPointToClientDip(pt);
    UIElement* target = FindDropTarget(dip);
    dragOverElement_ = target;

    if (target && target->DropTarget()) {
        DragEventArgs args{dip, PreferredEffect(*effect), data};
        *effect = static_cast<DWORD>(target->DropTarget()->OnDragEnter(args));
    } else {
        *effect = DROPEFFECT_NONE;
    }
    return S_OK;
}

STDMETHODIMP NativeWindowHost::DragOver(DWORD /*keyState*/, POINTL pt, DWORD* effect) {
    if (!effect) return E_POINTER;

    const Point dip = DropPointToClientDip(pt);
    UIElement* target = FindDropTarget(dip);

    // Crossing from one target to another must look like a leave + an enter to the
    // two elements involved — the OS only reports coordinates, so the transition is
    // ours to synthesize. Without this, an element keeps whatever hover affordance
    // it drew when the pointer moves to a sibling.
    if (target != dragOverElement_) {
        if (dragOverElement_ && dragOverElement_->DropTarget())
            dragOverElement_->DropTarget()->OnDragLeave();
        dragOverElement_ = target;
        if (target && target->DropTarget()) {
            DragEventArgs args{dip, PreferredEffect(*effect), input_.DragData().Get()};
            *effect = static_cast<DWORD>(target->DropTarget()->OnDragEnter(args));
            return S_OK;   // enter already reported the effect for this position
        }
    }

    if (target && target->DropTarget()) {
        DragEventArgs args{dip, PreferredEffect(*effect), input_.DragData().Get()};
        *effect = static_cast<DWORD>(target->DropTarget()->OnDragOver(args));
    } else {
        *effect = DROPEFFECT_NONE;
    }
    return S_OK;
}

STDMETHODIMP NativeWindowHost::DragLeave() {
    if (dragOverElement_ && dragOverElement_->DropTarget())
        dragOverElement_->DropTarget()->OnDragLeave();
    dragOverElement_ = nullptr;
    input_.DragData().Reset();
    return S_OK;
}

STDMETHODIMP NativeWindowHost::Drop(IDataObject* data, DWORD /*keyState*/, POINTL pt,
                                   DWORD* effect) {
    if (!effect) return E_POINTER;

    const Point dip = DropPointToClientDip(pt);
    UIElement* target = FindDropTarget(dip);

    if (target && target->DropTarget()) {
        DragEventArgs args{dip, PreferredEffect(*effect), data};
        target->DropTarget()->OnDrop(args);
        *effect = static_cast<DWORD>(args.effect);
        // A drop mutates content (text inserted, files loaded), and nothing on this
        // path went through a property setter — schedule the frame explicitly.
        RequestFrame(FrameReason::Paint);
    } else {
        *effect = DROPEFFECT_NONE;
    }

    // No DragLeave arrives after a Drop; clear the gesture state here.
    dragOverElement_ = nullptr;
    input_.DragData().Reset();
    return S_OK;
}



} // namespace fluent
