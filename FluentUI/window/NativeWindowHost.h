// NativeWindowHost.h — borderless top-level window with a custom title bar,
// DWM Mica backdrop, dark/light title bar, and a DComp content root.
//
// FLUENTUI_INTERNAL — this is an implementation header, not a supported public
// API. It must be installed alongside the static library because Window.h
// (the supported authoring base) derives from this class and exposes its
// protected virtuals. Consumers should subclass Window, not NativeWindowHost.
// Direct NativeWindowHost subclassing is unsupported: types, virtual signatures,
// and data layout here may change between minor versions.
//
// Follows the DComp pitfalls in the global CLAUDE.md:
//   * WS_EX_NOREDIRECTIONBITMAP, window-class style = 0 (no CS_HREDRAW/VREDRAW),
//   * placed on the primary monitor center at startup (no CW_USEDEFAULT),
//   * first frame drawn then DwmFlush() before ShowWindow(),
//   * WM_SIZE resizes the content surface in place, never rebuilds visuals,
//   * WM_DPICHANGED repositions to the suggested rect and re-renders immediately.
//
// Subclasses (or the demo) override OnRender to paint content in DIPs and
// register Elements for hit-testing.
#pragma once

#include "../fl_common.h"
#include "GraphicsHost.h"
#include "InputHost.h"
#include "FrameHost.h"
#include "ThemeHost.h"
#include "DiagnosticsHost.h"
#include "../graphics/DeviceLostController.h"
#include "../core/UIElement.h"
#include "../core/UIContext.h"
#include "WindowServices.h"
#include "WindowState.h"
#include "../services/TooltipService.h"
#include "FramePacing.h"
#include "ModalFrameHeartbeat.h"
#include "../diagnostics/PerformanceCounters.h"
#include "../input/DragDrop.h"
#include "../automation/AutomationHost.h"
#include <vector>
#include <functional>
#include <string>
#include <memory>
#include <ole2.h>
#include <wrl/client.h>

namespace fluent {

struct ThemeChangedArgs {};
class Application;
class HudOverlay;

class NativeWindowHost : public WindowServices,
                         public IDropTarget,
                         private automation::IAutomationWindow {
public:
    // Out of line (with the destructor) because hud_ is a unique_ptr to a type this
    // header only forward-declares; an implicit constructor would need its destructor
    // in every translation unit that constructs a window.
    NativeWindowHost();
    virtual ~NativeWindowHost();

    // Create and show the window. clientWDip/clientHDip are the initial client
    // size in DIPs (scaled by the startup monitor DPI). If `restore` is non-null
    // and valid, its saved placement (rescaled for the target DPI and validated
    // against current monitors) overrides the centered default. The window does
    // not store `restore` — the caller owns the data and its persistence.
    HRESULT Create(HINSTANCE hInst, const wchar_t* title, float clientWDip,
                   float clientHDip, const WindowState* restore = nullptr);
    HRESULT Create(Application& application, const wchar_t* title,
                   float clientWDip, float clientHDip,
                   const WindowState* restore = nullptr);
    Application* OwningApplication() const { return application_; }

    // Close the native window. The normal WM_DESTROY lifecycle still runs, so
    // attached elements are detached and popup registrations are released before
    // the HWND disappears. This is deliberately separate from the destructor so
    // a modeless owner can close a window without destroying its C++ object yet.
    void Close();
    bool IsOpen() const { return hwnd_ != nullptr && !destroying_; }

    HWND Hwnd() const override { return hwnd_; }
    HINSTANCE Instance() const override { return hInst_; }
    float DpiScale() const override { return dpi_ / 96.0f; }
    UINT Dpi() const { return dpi_; }
    UINT WheelScrollLines() const override { return wheelScrollLines_; }

    // Use an icon embedded in the application's module for the custom title bar,
    // taskbar and Alt+Tab. Call before Create/Show; 0 clears the icon.
    //
    // The window loads two sizes at its CURRENT DPI and reloads them on every
    // WM_DPICHANGED: 16 DIP (ICON_SMALL and the title bar, drawn 1:1 with no
    // resampling) and 32 DIP (ICON_BIG, Alt+Tab). For crisp results the .ico
    // should contain frames for the sizes those map to at the DPIs you support —
    // 16/20/24/32 px for the small icon (100/125/150/200%) and 32/40/48/64 px for
    // the big one. A missing frame is resampled once by LoadImageW from the
    // nearest one, which softens thin strokes at small sizes.
    void SetIconResource(UINT resourceId);
    UINT IconResource() const { return iconResourceId_; }

    // Current client area size in DIPs (physical pixels / DPI scale).
    float ClientWidthDip() const { return pixelW_ * 96.0f / dpi_; }
    float ClientHeightDip() const { return pixelH_ * 96.0f / dpi_; }

    // Smallest size the user may drag the window to, in DIPs (0 = no limit, the
    // default). Without this the system floor applies — roughly SM_CXMIN, ~136px —
    // and a fixed layout collapses: controls overlap and anything measured with
    // min(avail, content) starts clipping. Stored in DIPs and converted with the
    // window's CURRENT DPI on every WM_GETMINMAXINFO, so the limit means the same
    // physical amount of layout room on a 200% monitor as on a 100% one.
    // Set it to the narrowest width the app's layout is designed to survive.
    void SetMinClientSizeDip(float wDip, float hDip);
    float MinClientWidthDip() const { return minClientWDip_; }
    float MinClientHeightDip() const { return minClientHDip_; }

    // Capture the current placement as plain data (normal/restored rect + the
    // maximized flag + current DPI), for the caller to persist however it likes.
    // Analogous to WPF's Window.RestoreBounds. Safe to call any time after the
    // HWND exists; returns an invalid state otherwise.
    WindowState CaptureWindowState() const;

    D2DContext& D2D() override { return graphics_.D2D(); }
    DWriteContext& DWrite() override { return graphics_.DWrite(); }
    DCompHost& Comp() { return graphics_.Comp(); }

    // Composition layer. The backend wraps comp_ (DCompHost) +
    // d2d_ and forwards commits to RequestFrame(Paint). Null until graphics are up
    // (a control then falls back to its UI-thread path); it is created once in
    // InitGraphics and survives device-loss rebuilds (comp_/d2d_ rebuild in place,
    // controls recreate their own visuals). See ICompositionBackend.
    ICompositionBackend* Composition() override {
        return graphicsReady_ ? graphics_.CompBackend() : nullptr;
    }

    // Register an element for hit-testing / event routing (not owned).
    void AddElement(UIElement* e);

    // Undo AddElement for an element the caller is about to destroy or re-home.
    // elements_ holds raw pointers, so an element that is freed while still listed
    // is a use-after-free on the next frame, hit test or WM_DESTROY walk. This drops
    // the pointer, clears every host-side reference that could point into the
    // subtree (input hot/capture/focus via OnElementDetached, the armed tooltip
    // target, the drag-over target), detaches the subtree from the service context
    // while the context is still valid, and removes the root invalidate callback that
    // AddElement installed so a stray Invalidate on a re-homed element no longer
    // schedules frames on this window. No-op for an element that was never added.
    // Schedules a layout frame when the window is live, since what was painted for
    // the element is now stale. Returns whether the element was registered.
    bool RemoveElement(UIElement* e);

    // The window's service context, valid once graphics are up.
    // Tree-registered elements are attached automatically by AddElement; this
    // accessor is for non-tree helpers a host builds by hand (e.g. a standalone
    // context-menu MenuFlyout) so they can be given the same services without the
    // old manual SetDWrite / Attach wiring.
    const UIContext& Context() const { return context_; }

    // Give keyboard focus to `e` (must be a registered, focusable element), or
    // nullptr to clear focus. Forwards to the FocusManager (the single focus
    // authority); kept as a public entry point so apps can set initial
    // focus without reaching into the manager.
    void SetFocusElement(UIElement* e);
    UIElement* FocusedElement() const { return input_.Focus().Focused(); }

    // Register a callback to dismiss an active popup when the window is
    // deactivated, moved, or enters resize. The callback receives the event type
    // and optionally screen-click coordinates (for mouse-down dismiss). Return
    // true to dismiss, false to keep the popup open.
    //
    // PopupDismissReason now lives at namespace scope (WindowServices.h); this
    // alias keeps existing NativeWindowHost::PopupDismissReason references valid.
    using PopupDismissReason = fluent::PopupDismissReason;

    // Install the callback and return a Subscription whose destruction clears the
    // slot — but only if it still owns that registration. A
    // generation counter guards against a stale subscription (whose slot was
    // already replaced by a newer Register* call) clearing the newer callback.
    // These are the only registration paths: a control holds the Subscription and
    // the slot is cleared automatically when the control (or its popup) goes away.
    Subscription RegisterActivePopupDismiss(
        std::function<bool(PopupDismissReason, HWND, int, int)> cb) override {
        popupDismiss_ = std::move(cb);
        unsigned gen = ++dismissGen_;
        return Subscription([this, gen] {
            if (dismissGen_ == gen) { popupDismiss_ = nullptr; ++dismissGen_; }
        });
    }
    Subscription RegisterActivePopupKeyHandler(
        std::function<bool(UINT)> cb) override {
        popupKey_ = std::move(cb);
        unsigned gen = ++keyGen_;
        return Subscription([this, gen] {
            if (keyGen_ == gen) { popupKey_ = nullptr; ++keyGen_; }
        });
    }
    Subscription RegisterActivePopupWheelHandler(
        std::function<bool(int, int, int)> cb) override {
        popupWheel_ = std::move(cb);
        unsigned gen = ++wheelGen_;
        return Subscription([this, gen] {
            if (wheelGen_ == gen) { popupWheel_ = nullptr; ++wheelGen_; }
        });
    }
    // Unlike the three single-slot hooks above, aux roots are a LIST (a menu
    // cascade can have several popups open), so there is no generation guard:
    // each Subscription erases exactly its own entry. A generation guard here
    // would turn every older popup's unregister into a no-op and leave a
    // dangling root in the list.
    Subscription RegisterAuxAnimationRoot(UIElement* root) override {
        if (!root) return Subscription();
        auxAnimRoots_.push_back(root);
        return Subscription([this, root] {
            auto it = std::find(auxAnimRoots_.begin(), auxAnimRoots_.end(), root);
            if (it != auxAnimRoots_.end()) auxAnimRoots_.erase(it);
            frame_.Anims().Remove(root);  // never tick a closed popup's content
        });
    }

    // Expose the animation registry for popup content injection.
    // PopupHost sets ctx.animations to this so UIElement can
    // remove itself on detach. Non-owning pointer; stable for the host's life.
    AnimationRegistry* Anims() override { return &frame_.Anims(); }
    // Popup input arrives on the popup's own WndProc, bypassing this window's
    // message handlers (which are what normally re-collect the animation set).
    void RequestAnimationUpdate() override { UpdateAnimationTimer(); }

    // Re-apply theme (tokens + dark title bar) and repaint.
    void ApplyTheme();

    // Theme control. Replaces the old global
    // Theme::Instance().SetDark / FollowSystem / IsDark: the window owns the
    // ThemeManager, so an app flips the theme through the window. SetDarkMode /
    // FollowSystemTheme rebuild the snapshot (in place) and call ApplyTheme so the
    // title bar + tree refresh; IsDarkMode reads the current snapshot.
    void SetDarkMode(bool dark) { theme_.Manager().SetDark(dark); ApplyTheme(); }
    void FollowSystemTheme() { theme_.Manager().SetDark(SystemUsesDarkMode()); ApplyTheme(); }
    bool IsDarkMode() const { return theme_.Manager().Snapshot().dark; }
    // The current immutable theme snapshot (colors / spacing / typography). A
    // subclass paints host chrome (OnRender) through this instead of the old
    // Theme::Instance().Colors().
    const ThemeSnapshot& Theme() const { return theme_.Manager().Snapshot(); }
    const ThemeInputs& ThemeConfiguration() const { return theme_.Manager().Inputs(); }
    void SetThemeConfiguration(const ThemeInputs& inputs) {
        theme_.Manager().SetInputs(inputs);
        if (hwnd_) ApplyTheme();
    }
    // Replace the entire theme snapshot in one shot. Use this to load a theme
    // constructed offline (from JSON, from a file, computed per user) rather than
    // building it incrementally with SetDark/SetCornerRadius/... For the common
    // "flip one field" cases, the convenience setters below are simpler.
    //
    // The `generation` field in `snap` is IGNORED: the manager stamps its own
    // generation on rebuild, which is exactly what downstream caches (text layouts,
    // geometries) need to invalidate correctly. Passing a snapshot built elsewhere
    // with a stale generation would not cause cache thrash — the manager's generation
    // wins.
    void SetTheme(const ThemeSnapshot& snap) {
        theme_.Manager().SetInputs(ThemeInputsFromSnapshot(snap));
        if (hwnd_) ApplyTheme();
    }
    // Convenience override entry points (Win11 polish A0). They set one field
    // of the current inputs' override layer and rebuild; everything else in
    // the inputs (dark mode, existing overrides) is preserved. SetCornerRadius
    // maps to cornerRadiusSmall+Normal; SetControlHeight to controlHeightNormal.
    void SetCornerRadiusDip(float smallDip, float normalDip) {
        ThemeInputs in = theme_.Manager().Inputs();
        in.spacing.cornerRadiusSmall = smallDip;
        in.spacing.cornerRadiusNormal = normalDip;
        theme_.Manager().SetInputs(in);
        if (hwnd_) ApplyTheme();
    }
    void SetControlHeightDip(float normalDip) {
        ThemeInputs in = theme_.Manager().Inputs();
        in.spacing.controlHeightNormal = normalDip;
        theme_.Manager().SetInputs(in);
        if (hwnd_) ApplyTheme();
    }
    Event<NativeWindowHost, ThemeChangedArgs>& ThemeChanged() { return themeChanged_; }

    // Window material policy. SetBackdropKind changes the
    // requested material (Auto/None/Mica/MicaAlt/Acrylic) and re-applies it with
    // the environment-aware fallback. Appearance() exposes the WindowAppearance
    // for corner/caption/border tweaks and the last resolution (LastResolved()).
    void SetBackdropKind(BackdropKind kind) { requestedBackdrop_ = kind; ApplyMica(); }
    WindowAppearance& Appearance() { return theme_.Appearance(); }
    const WindowAppearance& Appearance() const { return theme_.Appearance(); }

    // Application-facing radius in DIPs. Windows 11 only exposes native corner
    // presets, so this maps to square/small/normal; pre-Windows 11 safely remains
    // square. The requested value is retained for API consistency and inspection.
    void SetCornerRadius(float radiusDip);
    float CornerRadius() const { return cornerRadiusDip_; }
    void SetShadowEnabled(bool enabled);
    bool ShadowEnabled() const { return shadowEnabled_; }

    // Window background opacity [0..1], default 1.0 (opaque). This is the window
    // half of the opacity interface; UIElement::SetOpacity covers controls.
    //
    // What it does depends on whether a material is active (see ResolveBaseFill):
    //   * No material (Win10, composition off, RDP, high contrast, None) — the
    //     window paints its own opaque base, and this dials that base's alpha.
    //     1.0 keeps the window opaque, which is what makes a pre-Win11 build look
    //     right instead of showing the desktop through everything.
    //   * Material active (Mica/Acrylic on Win11) — 1.0 leaves the material
    //     untouched (filling over it would erase the Mica look); below 1.0 tints
    //     the material with windowBackground at that alpha.
    void SetBackgroundOpacity(float opacity);
    float BackgroundOpacity() const { return backgroundOpacity_; }

    // The base fill this window paints under its content each frame, resolved from
    // the last backdrop resolution + the theme's windowBackground + the requested
    // background opacity. `fill == false` means "the DWM material is the base;
    // draw nothing". Public so a test or a subclass can assert the policy.
    BaseFillPlan BaseFill() const;

    // Request a repaint for the next frame. This is the normal
    // entry point — it coalesces: many calls between two frames merge into one
    // pending frame serviced by the message loop (via RunDueFrame).
    void Render() { RequestFrame(FrameReason::Paint); }

    // Merge a frame request of a given reason. Cheap; safe to call repeatedly.
    void RequestFrame(FrameReason reason);

    // The DXGI frame-latency waitable for pacing, or nullptr if
    // unavailable. Always nullptr since the content became a DComp surface instead of
    // a swap chain: there is no swap chain to pace against. Kept for source
    // compatibility; pace on FrameWaitRemaining() through FrameWaiter instead.
    HANDLE FrameLatencyWaitable() const { return graphics_.Comp().FrameLatencyWaitable(); }

    // Milliseconds until this window's next frame is due, as a real number so the
    // loop can program a high-resolution timer with sub-millisecond accuracy.
    // <= 0 means the frame is due now. Includes the measured wake margin
    // (FramePacing.h). This — not FrameWaitTimeoutMs — is what the message loop
    // should pace on: a whole-millisecond timeout gets rounded up to the system
    // timer period and caps a 120Hz panel at ~68fps. See FrameWaiter.h.
    double FrameWaitRemaining() const;

    // FrameWaitRemaining() rounded up to whole milliseconds, for a wait API that
    // only takes integers. NOT what a message loop should pace on: the rounding plus
    // the system timer period capped a 120Hz panel at ~68fps (FrameWaiter.h).
    // Application::PumpOneTurn paces on FrameWaitRemaining() through FrameWaiter; the
    // one remaining caller is the legacy self-frame WM_TIMER of a window without an
    // Application, whose own ~10ms minimum period dominates anyway.
    //
    // This replaces the pacing the swap chain used to provide for free: Commit() does
    // not block on vsync the way Present1 did, so without a timeout the loop renders
    // as fast as the CPU allows — a measured 36s session hit ~1927 fps on a 120Hz
    // panel, invisible on screen but burning a core and corrupting every FPS number in
    // the diagnostics.
    unsigned FrameWaitTimeoutMs() const;

    // True if a frame has been requested and not yet serviced. The message loop
    // polls this to decide whether to render this iteration.
    bool NeedsFrame() const { return graphicsReady_ && frame_.Scheduler().NeedsFrame(); }

    // Service a pending frame now: consume the coalesced request and paint once.
    // The message loop calls this after waking. No-op if nothing is pending.
    void RunDueFrame();

    // Advance active animations by the real elapsed time (QPC) and, if anything is
    // still animating, request the next frame so the message loop keeps cycling.
    // This is THE animation clock now: the loop calls it once per painted frame, so animation
    // ticks at the display refresh rate (paced by FrameWaitRemaining through
    // FrameWaiter) instead of being throttled to — and starved by — the
    // low-priority WM_TIMER. Application::PumpOneTurn calls it only on a turn whose
    // frame is due, right before RunDueFrame (AnimationTickIsDue). The old
    // WM_TIMER path survives only as a fallback while a system modal loop
    // (drag-resize) blocks our loop. Returns true if animation is still running.
    // Cheap + idempotent when idle: ComputeDt is QPC-based so calling it twice in
    // quick succession just splits one dt into two tiny ones (no double-advance).
    bool PumpAnimations();

    // True while at least one animation is active (the loop uses this to keep a
    // frame pending, so it waits for the next frame instead of blocking on input
    // alone).
    bool IsAnimating() const { return frame_.Scheduler().Running(); }

    // Metrics recorded for the most recently painted frame: phase
    // timings, the serviced FrameReason, dirty-element count, drawOps, and
    // dirtyRects. Populated by RenderNow; read by a Debug HUD / diagnostics.
    // Zeroed until the first paint.
    const FrameStats& LastFrameStats() const { return lastFrameStats_; }

    // Ring buffer of the last 120 frame CPU times. ~2 s at 60 fps.
    // Call FrameRingStats().P95() / .P99() for latency percentile stats.
    const FrameRing<120>& FrameRingStats() const { return diagnostics_.Frames(); }

    // Ring of the last 120 frame INTERVALS (ms) — real cadence for FPS/jank.
    const FrameRing<120>& FrameIntervalStats() const { return diagnostics_.Intervals(); }

    // Toggle / query the on-screen performance HUD (F12). The HUD runs on its own
    // thread with its own composition target (window/HudOverlay.h): showing it
    // neither keeps this window repainting nor forces full redraws, so the numbers
    // describe the app's real frames. It updates ~10x a second even while the UI
    // thread is stalled, and then says so.
    void SetHudVisible(bool on);
    bool HudVisible() const { return hudVisible_; }
    void ToggleHud() { SetHudVisible(!hudVisible_); }

    // --- Session diagnostic log (diagnostics/SessionLog.h) ----------------
    // Capture operations + per-frame performance in memory for a whole run, so
    // the app can write a report on close and issues (resize frame drops,
    // misplaced compositor visuals) are diagnosable from a file. Enable early
    // (e.g. in OnCreated). The log NEVER requests a frame and does no I/O — it
    // only records frames the window already paints (idle-zero-refresh intact).
    // The app persists BuildSessionReport() itself (the library stays file-free).
    void EnableSessionLog(bool on);
    bool SessionLogEnabled() const { return sessionLogEnabled_; }
    // Record a semantic operation into the session log (no-op if disabled). Apps
    // call this for control-level events (e.g. "checkbox toggled") the window
    // cannot name; the window records its own resize/dpi/device events itself.
    void LogSessionEvent(const char* category, const std::string& detail);
    // Serialize the captured session to a human-readable report (empty if the
    // log was never enabled). The app writes this to disk on close.
    std::string BuildSessionReport() const { return diagnostics_.Log().Serialize(); }

    // --- Hang watchdog (window/HangDetector.h) ----------------------------
    // Start a background thread that reports when this window's UI thread stops
    // responding for longer than thresholdMs: no frame, no dispatched message and no
    // pump wake, while NOT parked in an idle wait (an idle window paints nothing and
    // is not hung; see HangDetector.h). Independent of the rings above and of
    // the session log: those are written ON the UI thread, so a stall produces no
    // samples rather than a visible one, and nothing can report a hang WHILE it is
    // happening. The callback runs on the detector thread — it must not touch UI
    // state. Off by default; Stop is automatic on window destruction.
    void EnableHangDetector(bool on, double thresholdMs = 5000.0,
                            double pollIntervalMs = 500.0);
    bool HangDetectorEnabled() const { return diagnostics_.Hang().IsRunning(); }
    void SetOnHang(HangDetector::HangCallback cb) {
        diagnostics_.Hang().SetOnHang(std::move(cb));
    }
    // How long the UI thread has been unresponsive right now, in ms. Safe from any
    // thread; 0 before the first painted frame.
    double HangStalledMs() const { return diagnostics_.Hang().StalledMs(); }

protected:
    // ArmTooltip's re-arm rule (see ArmTooltip below), pure so it can be tested
    // without a window: a different element, or the same element whose tooltip
    // text changed since it was armed, is a new hover target.
    static bool TooltipTargetChanged(const UIElement* armed, const std::wstring& armedText,
                                     const UIElement* hovered) {
        if (hovered != armed) return true;
        return hovered && hovered->Tooltip() != armedText;
    }

    // The tooltip's hover state machine, kept free of HWNDs and timers so it can be
    // tested headless. The host feeds it pointer events and carries out the returned
    // action (kill/start the hover timer, hide the popup card); it holds no other
    // tooltip state of its own.
    //
    // Two pieces of state:
    //   armed      the target the hover timer is counting down for, or whose tooltip
    //              is on screen. Same meaning as before popups were considered.
    //   suppressed the target that was under the pointer when a popup (menu,
    //              drop-down, calendar) opened. It may not arm again until the pointer
    //              reaches a different target (TooltipTargetChanged's rule).
    //
    // Why both a "popup is open" gate (OnHover's popupOpen) and the suppressed target:
    //   - The gate is what keeps a tooltip off an OPEN popup. Suppression alone is not
    //     enough: a context menu opens at the cursor, so the main window gets
    //     WM_MOUSELEAVE without the pointer moving, and a pointer that wanders from the
    //     flyout's button to its neighbour would arm the neighbour over the menu.
    //   - Suppression is what keeps the tooltip away AFTER the popup closes while the
    //     pointer still rests on the element that opened it (pick a menu item with
    //     the keyboard, Esc, click the button again). WinUI does not re-show there
    //     either; the user has already acted on that element.
    //
    // A plain press or wheel (no popup) only clears `armed`, exactly as before: the
    // tooltip hides and the next pointer move re-arms it. Leaving the window does the
    // same and deliberately does NOT clear suppression — see the context-menu case
    // above, where the leave is caused by the popup itself.
    //
    // Pointers are identity only. `armed` is dereferenced by the host (to show the
    // text), never by this class; `suppressed` is never dereferenced at all.
    class TooltipArming {
    public:
        enum class Action {
            Keep,        // nothing changed: leave the timer / visible tooltip alone
            Hide,        // cancel the timer and hide the tooltip
            HideAndArm,  // hide, then start the hover timer for Armed()
        };

        // Pointer moved; `hovered` is the hit-tested element (may be null).
        // `popupOpen` is whether any popup announced by WM_FLUENT_POPUP_OPENED is
        // still visible.
        Action OnHover(UIElement* hovered, bool popupOpen) {
            if (suppressing_ && TooltipTargetChanged(suppressed_, suppressedText_, hovered)) {
                suppressing_ = false;
                suppressed_ = nullptr;
                suppressedText_.clear();
            }
            if (popupOpen || suppressing_) {
                // Clear `armed` so the first move after the block lifts is a target
                // change and arms normally.
                const bool wasArmed = armed_ != nullptr;
                armed_ = nullptr;
                armedText_.clear();
                return wasArmed ? Action::Hide : Action::Keep;
            }
            if (!TooltipTargetChanged(armed_, armedText_, hovered)) return Action::Keep;
            armed_ = hovered;
            armedText_ = hovered ? hovered->Tooltip() : std::wstring{};
            return (hovered && hovered->HasTooltip()) ? Action::HideAndArm : Action::Hide;
        }

        // A popup opened. The caller hides the tooltip and cancels the timer.
        // `hovered` is what is under the pointer right now — not Armed(), which a
        // press has already cleared by the time a button's flyout opens on release.
        void OnPopupOpened(UIElement* hovered) {
            armed_ = nullptr;
            armedText_.clear();
            suppressing_ = true;
            suppressed_ = hovered;
            suppressedText_ = hovered ? hovered->Tooltip() : std::wstring{};
        }

        // Press, wheel, or pointer left the window. The caller hides the tooltip and
        // cancels the timer. Suppression survives (see the class comment).
        void OnDismissed() {
            armed_ = nullptr;
            armedText_.clear();
        }

        UIElement* Armed() const { return armed_; }
        bool Suppressing() const { return suppressing_; }

    private:
        UIElement* armed_ = nullptr;
        std::wstring armedText_;        // armed_'s tooltip text when it was armed
        const UIElement* suppressed_ = nullptr;
        std::wstring suppressedText_;   // suppressed_'s tooltip text when the popup opened
        bool suppressing_ = false;      // separate flag: a null target can be suppressed
    };

    // Paint window content (background card etc.) before elements are drawn.
    // Coordinates are DIPs; multiply by DpiScale() for pixels — but the content
    // DC is already set to a DPI transform, so paint in DIPs directly.
    virtual void OnRender(ID2D1DeviceContext* dc) {}
    // Called after a successful Create (elements can be built here).
    virtual void OnCreated() {}
    // Lets subclasses replace the centered startup rect with a validated saved
    // physical-pixel rect before the HWND is created.
    virtual void AdjustInitialWindowRect(RECT& rect, UINT dpi) { UNREFERENCED_PARAMETER(rect); UNREFERENCED_PARAMETER(dpi); }
    virtual int InitialShowCommand() const { return SW_SHOW; }
    virtual void OnDestroying() {}
    // Runs after registered roots have detached and the HWND has been cleared.
    // DialogWindow uses this for its Closed lifecycle, which must be later than
    // Unloaded/OnDestroying rather than merely another pre-destroy callback.
    virtual void OnDestroyed() {}
    virtual bool ShouldPostQuitOnDestroy() const { return true; }
    // Returns true for primary application windows (Window subclasses); false
    // for dialogs (DialogWindow subclasses). Application::AttachWindow uses this
    // to track the main-window count for ShutdownMode::OnMainWindowClose. The
    // default is true so raw NativeWindowHost subclasses are treated as primary.
    virtual bool IsMainWindow() const { return true; }
    // Main windows are normally paced by the application's outer loop. Secondary
    // windows can opt into a private on-demand timer so modeless/modal windows
    // continue rendering without requiring the application to enumerate them.
    // The timer exists only while a frame or animation is pending, preserving
    // zero-refresh idle behavior.
    virtual bool UsesExternalFramePump() const { return true; }
    // WS_POPUP windows use this as their owner. The main application window has
    // no owner; DialogWindow overrides it so Windows keeps the dialog above its
    // parent without requiring a second message-pump implementation.
    virtual HWND OwnerHwnd() const { return nullptr; }

    // Additional extended window styles (WS_EX_*) to OR with the base set.
    // NativeWindowHost always uses WS_EX_NOREDIRECTIONBITMAP; subclasses can add more.
    // DialogWindow overrides this to add WS_EX_APPWINDOW so dialogs appear in the
    // taskbar even when they have an owner (Win32 default: owned windows have no
    // taskbar button and minimize to the screen corner).
    virtual DWORD ExtraExStyle() const { return 0; }

    virtual void OnLayout() {}
    // Called for a client click that did not hit a registered Element.
    virtual void OnClientClick(float dipX, float dipY) {}
    // Called on right-button-down (screen coordinates for popup placement).
    virtual void OnClientRightClick(float dipX, float dipY,
                                    int screenX, int screenY) {
        UNREFERENCED_PARAMETER(dipX); UNREFERENCED_PARAMETER(dipY);
        UNREFERENCED_PARAMETER(screenX); UNREFERENCED_PARAMETER(screenY);
    }
    virtual void OnMouseWheel(float dipX, float dipY, int delta) { UNREFERENCED_PARAMETER(dipX); UNREFERENCED_PARAMETER(dipY); UNREFERENCED_PARAMETER(delta); }
    virtual void OnDeviceChange(WPARAM wp, LPARAM lp) { UNREFERENCED_PARAMETER(wp); UNREFERENCED_PARAMETER(lp); }
    virtual bool OnAppMessage(UINT msg, WPARAM wp, LPARAM lp, LRESULT& result) { UNREFERENCED_PARAMETER(msg); UNREFERENCED_PARAMETER(wp); UNREFERENCED_PARAMETER(lp); UNREFERENCED_PARAMETER(result); return false; }
    // Title bar height in DIPs.
    virtual float TitleBarHeightDip() const { return 32.0f; }
    virtual const wchar_t* TitleText() const { return title_.c_str(); }
    virtual bool UsesStandardTitleBar() const { return false; }
    void UpdateNativeTitle(const std::wstring& title);

    std::vector<UIElement*> elements_;

    // Set the owning Application. Called by Window::PrepareContent before
    // OnInitialize, so OwningApplication() is valid during initialization.
    void SetOwningApplication(Application* app) { application_ = app; }

    // Mutable access to the in-progress frame's stats, for a subclass phase that
    // records its own counters. Window::OnLayout uses this to log async-measure
    // attempts and timeouts: the async path lives in Window (that is where root_
    // is), but the stats live here, and the alternative — passing a FrameStats& down
    // through OnLayout's signature — would change a virtual that DialogWindow and
    // every app subclass override.
    //
    // Only valid during a frame. Outside one it refers to the last completed frame's
    // record, so a write would be attributed to a frame that has already been
    // sampled; LastFrameStats() is the read-only accessor for that case.
    FrameStats& MutableFrameStats() { return lastFrameStats_; }

public:
    // --- IDropTarget (COM) ------------------------------------------------
    // Registered with the OS by RegisterDragDrop in Create. Each event hit-tests
    // the element tree for the deepest element with a drop handler installed and
    // forwards to it; when no element accepts, the effect is None so the OS shows
    // the "no drop" cursor.
    //
    // The window's COM lifetime is NOT reference-counted in the usual way: it is
    // owned by the C++ object, which outlives the OLE registration (Close /
    // WM_DESTROY calls RevokeDragDrop first). AddRef/Release therefore return a
    // fixed count and never delete — OLE holds a borrowed pointer, which is the
    // documented pattern for a window that is its own drop target.
    STDMETHOD(QueryInterface)(REFIID riid, void** ppv) override;
    STDMETHOD_(ULONG, AddRef)() override { return 2; }
    STDMETHOD_(ULONG, Release)() override { return 1; }
    STDMETHOD(DragEnter)(IDataObject* data, DWORD keyState, POINTL pt,
                         DWORD* effect) override;
    STDMETHOD(DragOver)(DWORD keyState, POINTL pt, DWORD* effect) override;
    STDMETHOD(DragLeave)() override;
    STDMETHOD(Drop)(IDataObject* data, DWORD keyState, POINTL pt,
                    DWORD* effect) override;

private:
    friend class Application;
    // DialogWindow's legacy (no-Application) modal GetMessageW loop is the other
    // pump that knows when it is about to block; it calls NoteLoopIdle /
    // NoteLoopAwake on itself and its owner. Friend rather than protected so the
    // hooks do not become part of the subclassing API.
    friend class DialogWindow;
    static LRESULT CALLBACK WndProc(HWND, UINT, WPARAM, LPARAM);
    LRESULT HandleMessage(UINT msg, WPARAM wp, LPARAM lp);

    // Screen POINTL (physical px) -> window-client DIPs, for the drop routing.
    Point DropPointToClientDip(POINTL pt) const;
    // The deepest registered root's element that accepts a drop at these DIPs.
    UIElement* FindDropTarget(Point dip) const;

    // The element currently under an in-flight drag (null when none). Held so
    // DragLeave / a target change can send OnDragLeave to the right element —
    // the OS gives no element identity, only coordinates.
    UIElement* dragOverElement_ = nullptr;
    bool dragDropRegistered_ = false;


    UINT iconResourceId_ = 0;
    Microsoft::WRL::ComPtr<ID2D1Bitmap> titleIconBitmap_;
    // Per-DPI icon pair handed to WM_SETICON. Owned (loaded without LR_SHARED so
    // each DPI gets its own size; a shared icon is cached at the first size
    // loaded); freed when replaced or after the HWND is gone.
    HICON iconSmall_ = nullptr;
    HICON iconBig_ = nullptr;
    void ReleaseWindowIcons();
    // Title-bar icon inset from the window's left edge (DIP).
    static constexpr float kTitleIconLeftDip = 10.0f;

    HRESULT InitGraphics(UINT pixelW, UINT pixelH);
    // Rebuild the GPU device stack after a loss. Rebuilds d2d_ IN
    // PLACE (borrowers keep valid pointers), re-inits comp_ + the resource cache
    // factories, clears cached GPU resources, closes transient popups (they
    // recreate their own composition target from the fresh device on next open), notifies
    // the tree (OnDeviceLost/Restored), re-dirties, and requests a repaint.
    // Returns true on success. Logs the device-removed reason on entry.
    bool RecoverDevice();
    // Paint one content frame synchronously (BeginContentFrame -> draw -> Present
    // -> Commit). This is the real render; Render()/RequestFrame only *schedule*
    // it. Used directly only where a frame must be produced immediately and the
    // message loop is not running: the startup first frame (drawn before ShowWindow), and
    // WM_SIZE / WM_DPICHANGED which run inside the system's modal resize/DPI loop,
    // plus the animation timer tick.
    // `reason` is recorded into lastFrameStats_ for diagnostics; defaults to Paint
    // for the direct (synchronous) callers (startup / resize / DPI).
    void RenderNow(FrameReason reason = FrameReason::Paint);
    void ApplyMica();
    void ApplyDarkTitleBar();
    void RecreateControlBrushes();
    void OnSize(UINT pixelW, UINT pixelH);
    // Resize coalescing: WM_SIZE only records the latest pixel size and
    // requests a Resize frame; the actual ResizeBuffers + OnLayout happen once per
    // frame here, at the start of the paint (RunDueFrame / the modal-loop timer /
    // WM_EXITSIZEMOVE / OnDpiChanged). A drag that fires N WM_SIZE messages thus
    // does at most one ResizeBuffers+layout per painted frame instead of N. No-op
    // when nothing is pending, so it is safe to call unconditionally before a paint.
    void ApplyPendingResize();
    // Service one synchronous frame while a SYSTEM modal loop (drag
    // resize/move) owns the message pump — our main loop is not spinning, so
    // nothing else advances animation or paints. Advances animation (real QPC dt),
    // folds in any pending resize, and paints once, only if there is something to
    // show. Driven by BOTH the kAnimTimerId heartbeat (covers a motionless mouse)
    // AND WM_SIZE/WM_MOVING (covers active dragging, where the low-priority
    // WM_TIMER is starved by the input flood so the heartbeat rarely fires).
    //
    // `pace`: skip the frame when it is not yet due at the display refresh. Pass true
    // from the mouse-driven callers (their rate is the mouse's, not vsync's) and FALSE
    // from the heartbeat — that timer is already self-limiting and too coarse to pace
    // safely (see the comment at the check for the 60Hz failure mode).
    void ServiceModalFrame(bool pace);
    void OnDpiChanged(UINT newDpi, const RECT* suggested);
    void DrawCaptionButtons(ID2D1DeviceContext* dc, float dpiScale);
    void RecreateWindowIcon();
    void DrawWindowIcon(ID2D1DeviceContext* dc);
    void DrawStandardTitle(ID2D1DeviceContext* dc);

    // Apply a saved placement onto the centered startup rect (DPI rescale +
    // multi-monitor validation). No-op when `restore` is null/invalid.
    void ApplyRestoreState(RECT& rect, UINT dpi, const WindowState* restore);

    // Ask any active popup to dismiss before the window/elements are torn down.
    void CloseActivePopups();

    // Hit-test against the custom title bar for caption drag / system buttons.
    LRESULT HitTestNca(POINT ptScreen);
    int CaptionButtonAt(float dipX, float dipY) const;
    RectDip CaptionButtonRect(int hit) const;

    // Translate a client-pixel point to window DIPs and read live modifier keys —
    // the only preprocessing the window does before feeding the InputManager.
    Point ToDip(int px, int py) const;
    static ModifierKeys CurrentModifiers();

    // Parent-window light-dismiss: hand any active no-activate popup a click at
    // the given client-pixel point (converted to screen) and clear the slot if it
    // dismisses. Shared by left-click routing and right-click context menus so a
    // right-click on empty space closes an already-open menu before opening a new
    // one (they previously diverged — only left-click dismissed).
    void LightDismissAt(int px, int py);
    // Invoke the active popup's dismiss hook safely (copy out, generation-checked
    // clear). Every host call site goes through it; see the definition.
    bool DispatchPopupDismiss(PopupDismissReason reason, HWND other, int x, int y);

public:
    // The slot logic behind DispatchPopupDismiss, as a pure function so it is testable
    // without a window: run `slot` from a local copy; afterwards, if `gen` is unchanged
    // (nobody registered or unregistered during the call), clear the slot when the
    // hook handled the event (bumping gen so a stale Subscription cannot clear a later
    // hook) or put it back when it declined. If gen changed, leave the slot alone —
    // it holds whatever the hook's side effects registered (e.g. a reopened popup).
    template <class Fn, class... Args>
    static bool InvokeOwnedHook(Fn& slot, unsigned& gen, Args&&... args) {
        if (!slot) return false;
        const unsigned before = gen;
        Fn hook = std::move(slot);
        slot = nullptr;
        const bool handled = hook(std::forward<Args>(args)...);
        if (gen == before) {
            if (handled) ++gen;
            else slot = std::move(hook);
        }
        return handled;
    }

private:

    // Re-ask the element tree which cursor to show, and apply it now.
    //
    // Exists because WM_SETCURSOR is not sent while this window holds mouse capture
    // (Win32 assumes the capturing window manages its own cursor), and every drag
    // gesture here captures. A cursor that should change WITHOUT a move — the pan
    // arrow appearing on middle-press, the I-beam returning on release — therefore
    // has to be pushed from the press/release handlers. Asks the captured element
    // first (it owns the gesture even when the pointer has left its bounds), then
    // the element under the pointer; null from both means the arrow.
    void RefreshCursor();

    // Invalidate hook the InputManager calls after routing an event (input changes
    // visual state); forwards to Render (coalesced frame request).
    static void InvalidateThunk(void* ctx);

    // Move the IME candidate/composition window to the focused element's caret.
    void UpdateImePosition();

    // Start/stop the ~60fps animation timer based on whether any registered
    // element currently wants per-frame ticks (e.g. an in-flight smooth scroll).
    void UpdateAnimationTimer();

    // Start/stop the caret-blink timer when the focused element changes: it runs
    // only while a blink-wanting element (a text control) holds focus. Called from
    // the FocusManager's FocusChanged event.
    void UpdateBlinkTimer(UIElement* newFocus);

    // Tooltip: track the element under the pointer and (re)arm the hover timer.
    // ArmTooltip is called on pointer move; when the hover target changes we
    // hide any visible tooltip and restart the delay. HideTooltip cancels both
    // the timer and the popup (on click, wheel, leave, or move-away).
    //
    // The target is the element AND its tooltip text: a composite control that is
    // one hit target (NavRail, a chart) swaps its text per region with SetTooltip,
    // and keying on the element alone never re-armed after the first popup —
    // moving to the next region showed nothing until a click reset the state.
    //
    // Deliberately NOT dismissed by a keypress. The tooltip used to hide on any
    // WM_KEYDOWN, which made a hotkey-triggered screenshot of a visible tooltip
    // impossible: pressing the first key of Ctrl+1 / Alt+A killed the popup
    // before the capture ran. A modifier going down is not a completed action,
    // and the pointer has not moved — so the thing the user is pointing at is
    // still the thing they want explained. Hover state, not keyboard state, owns
    // the tooltip's lifetime; a key that actually moves focus (Tab, arrows) also
    // moves the pointer's relevance, and those paths already hide it via
    // ArmTooltip on the next mouse move.
    //
    // OnPopupOpened handles WM_FLUENT_POPUP_OPENED: hide now, and hold the tooltip
    // off while the popup is visible and for the element it opened from (rules in
    // TooltipArming). InteractivePopupOpen answers "is any announced popup still
    // visible", pruning the ones that have since been hidden or destroyed.
    void ArmTooltip(UIElement* hovered);
    void HideTooltip();
    void CancelTooltip();  // kill the hover timer and hide the card; no state change
    void OnPopupOpened(HWND popup);
    bool InteractivePopupOpen();

    HWND hwnd_ = nullptr;
    HINSTANCE hInst_ = nullptr;
    std::wstring title_;
    UINT dpi_ = 96;
    UINT wheelScrollLines_ = 3;  // SPI_GETWHEELSCROLLLINES, updated on WM_SETTINGCHANGE
    UINT pixelW_ = 0, pixelH_ = 0;
    // Resize coalescing: the latest size seen in WM_SIZE, applied to the
    // content surface + layout once per frame by ApplyPendingResize(). resizePending_ is
    // the "dirty" flag; pixelW_/pixelH_ stay at the committed (applied) size until
    // then, so ClientWidthDip()/rendering use a consistent size mid-drag.
    UINT pendingPixelW_ = 0, pendingPixelH_ = 0;
    bool resizePending_ = false;
    bool graphicsReady_ = false;
    // Minimum draggable client size in DIPs; 0 = no limit (system floor applies).
    float minClientWDip_ = 0.0f, minClientHDip_ = 0.0f;
    HBRUSH controlBgBrush_ = nullptr;

    // Graphics device stack (D2D, DWrite, DComp, composition backend, resource cache).
    GraphicsHost graphics_;
    // GPU device-loss state machine. Fed each frame's present/draw
    // HRESULT; when it flips to Lost the frame loop runs RecoverDevice().
    DeviceLostController deviceLost_;
    // Consecutive failed rebuilds, for DeviceRebuildRetryDelayMs. Reset on success.
    unsigned deviceRebuildFailures_ = 0;

    // The window's theme authority. Owns the one stable
    // ThemeSnapshot handed to the tree via context_.theme; its pointee is
    // overwritten in place on a theme change so the parked pointer stays valid.
    // The app drives it through SetDarkMode / FollowSystemTheme; controls read the
    // snapshot via UIElement::Theme(). Replaced the old global Theme::Instance().
    // Theme subsystem. ThemeHost encapsulates the token
    // manager (the snapshot controls read via UIElement::Theme(); overwritten in
    // place on a theme change so the parked UIContext pointer stays valid) and the
    // native appearance controller (resolves the requested BackdropKind against
    // the live environment — OS build / composition / transparency / high-contrast
    // / RDP — and applies it through DWM with a solid fallback).
    ThemeHost theme_;
    BackdropKind requestedBackdrop_ = BackdropKind::Auto;
    // Requested opacity of the window's own background layer. See
    // SetBackgroundOpacity — 1.0 (opaque) is both the default and, on a
    // material-less system, the reason the window is readable at all.
    float backgroundOpacity_ = 1.0f;
    float cornerRadiusDip_ = 12.0f;
    bool shadowEnabled_ = true;

    bool restoreMaximized_ = false;

    // Input routing. InputHost encapsulates InputManager, FocusManager,
    // ClickCounter, and drag-drop COM state. The window only translates raw Win32
    // messages and feeds them to the host.
    InputHost input_;
    Subscription focusChangedSub_;  // drives the caret-blink timer off focus changes
    int hotCaption_ = 0;
    int pressedCaption_ = 0;
    bool blinkTimerOn_ = false;   // caret-blink timer is running
    bool rendering_ = false;      // reentrancy guard for Render() (see .cpp)
    // Set once WM_DESTROY begins. An input handler can synchronously destroy the
    // window (an app Close/Exit button), which runs WM_DESTROY nested inside the
    // input message; the input message's trailing work (HitTest / OnClientClick /
    // UpdateAnimationTimer) must then bail rather than touch the torn-down window.
    bool destroying_ = false;

    // UI Automation entry point (automation/AutomationHost.h). One null pointer until
    // a UIA client sends WM_GETOBJECT; torn down in WM_DESTROY (and the destructor
    // safety net) while hwnd_ is still valid, as UiaReturnRawElementProvider needs it.
    automation::AutomationHost automation_;
    // automation::IAutomationWindow: the roots and focus the UIA tree exposes.
    size_t AutomationRootCount() const override { return elements_.size(); }
    UIElement* AutomationRootAt(size_t i) const override {
        return i < elements_.size() ? elements_[i] : nullptr;
    }
    UIElement* AutomationFocusedElement() const override { return FocusedElement(); }

    // Per-frame animation subsystem. FrameHost encapsulates the
    // frame scheduler (clock: running/dt state, arm/disarm logic) and animation
    // registry (active set: rebuilt from the tree, ticked in place, so per-frame
    // cost is O(animating) not a whole-tree scan). The clock is driven by the main
    // loop (PumpAnimations) at the display refresh rate; the OS anim timer only
    // arms as a fallback while inModalLoop_ (a system drag-resize loop blocks us).
    FrameHost frame_;

    // True only between WM_ENTERSIZEMOVE and WM_EXITSIZEMOVE (a system modal
    // move/resize loop). While set, our message loop — hence PumpAnimations and
    // FrameWaiter — is not running, so frames are driven by modalHeartbeat_ plus
    // whatever WM_SIZE / WM_MOVING the drag generates. Cleared on exit.
    bool inModalLoop_ = false;

    // Nesting count of system modal loops the hang watchdog must treat as idle
    // between messages: move/resize (WM_ENTERSIZEMOVE) and menu tracking
    // (WM_ENTERMENULOOP). Kept apart from inModalLoop_, which drives the frame
    // heartbeat and is about move/resize only. See WndProc.
    int foreignModalLoops_ = 0;

    // The message loop tells each window's hang watchdog when the UI thread is
    // about to block while THIS window has nothing scheduled (idle is not a hang,
    // even if another window is busy) and when it woke. Private: only the pump
    // knows which wait is the idle one (Application and DialogWindow's legacy
    // modal loop are friends). The beat after waking also covers wakes that
    // dispatch nothing to this window (a thread message, Application::Post work).
    void NoteLoopIdle() { diagnostics_.Hang().EnterIdle(); }
    void NoteLoopAwake() { diagnostics_.Hang().Beat(); }

    // Precise frame tick for the modal loop above. Replaces the old kAnimTimerId
    // WM_TIMER heartbeat, which capped a held-but-motionless resize border at
    // ~30-45fps: WM_TIMER is low-priority and its period is rounded up to the
    // system timer resolution, so a 16ms request measured ~28.6ms. See
    // ModalFrameHeartbeat.h. The kAnimTimerId timer is still armed alongside it as
    // a fallback for the case where the heartbeat cannot start (no waitable timer).
    ModalFrameHeartbeat modalHeartbeat_;

    // Observed interval between consecutive ServiceModalFrame opportunities, in ms
    // (exponentially smoothed). Inside a modal loop we cannot WAIT for a deadline,
    // only decide at each arriving message whether to paint — so the pacing rule
    // needs to know how long the next opportunity is likely to be away. See
    // FrameIsNearestOpportunity in FramePacing.h for why that turns the decision from a
    // hard deadline test into a nearest-opportunity one.
    //
    // 0 = no estimate yet (the first opportunity of a drag has nothing to measure
    // against). FrameIsNearestOpportunity then falls back to the plain due-test
    // rather than guessing a spacing — conservative, so the opening frame of a drag
    // can never overshoot the panel.
    double modalGapMs_ = 0.0;
    int64_t modalLastOpportunityQpc_ = 0;
    // Heartbeat ticks in the current drag that skipped their paint because input was
    // queued (ModalTickShouldYieldToInput). Reset on WM_ENTERSIZEMOVE, traced on exit.
    int modalTickYields_ = 0;
    int64_t borderHitQpc_ = 0;        // Last WM_NCHITTEST that returned a resize border
    int64_t lastSizeMsgQpc_ = 0;      // Last WM_SIZE arrival timestamp
    // Reset at WM_ENTERSIZEMOVE: the previous drag's estimate describes a mouse
    // rate and message mix that may no longer apply.
    void ResetModalGapEstimate() {
        modalGapMs_ = 0.0;
        modalLastOpportunityQpc_ = 0;
        lastSizeMsgQpc_ = 0;  // First WM_SIZE will show gap from ENTERSIZEMOVE
    }
    // Fold this opportunity into the estimate and return the current gap estimate.
    double SampleModalGap();

    // Service context injected into every element registered via AddElement.
    // Built once graphics are up (device / DWrite / anim set /
    // HWND / DPI), pushed on attach, and refreshed on WM_DPICHANGED. Replaces the
    // old per-control SetDWrite / Attach(WindowServices*) wiring.
    UIContext context_;

    // Metrics for the most recently serviced frame, filled by
    // RenderNow and exposed via LastFrameStats() for a Debug HUD / diagnostics.
    FrameStats lastFrameStats_;
    // Cost (ms) of the animation tick that ran in PumpAnimations, carried to the next
    // frame's stats. The tick happens OUTSIDE RenderNow, so cpuFrameMs cannot include
    // it — and it is where a control's compositor surface refill runs (the dominant
    // UI-thread cost of an overscan scroll). RenderNow consumes and clears it.
    double pendingAnimationMs_ = 0.0;
    // Cost (ms) of ApplyPendingResize, carried to the next frame's stats for the same
    // reason as pendingAnimationMs_: it runs before RenderNow's clock starts, so
    // cpuFrameMs cannot see it. For a resize drag over a text-heavy control this is
    // the single largest number in the frame, and it used to appear nowhere.
    double pendingResizeMs_ = 0.0;

    // Label for the per-resize trace line (the Gallery pushes the visible page's
    // name). Empty = no trace, which is what keeps the instrumentation silent for
    // any host that does not opt in. The cost split itself is unconditional and
    // already lands in FrameStats::resizeMs; this only names it in the DebugView
    // stream so a per-page comparison is possible. See ApplyPendingResize.
    std::wstring resizeTraceLabel_;

public:
    // Setting a label opts this window into resize diagnostics, which is why it also
    // arms LayoutCostProbe. Keeping the two on one switch is deliberate: the probe
    // was previously gated on FLUENTUI_TRACE_LAYOUT alone and had NO caller for
    // SetEnabled anywhere in the tree, so every per-control number it was written to
    // produce silently came back empty. A host that asks to be traced gets all of the
    // trace, from one call it already makes.
    void SetResizeTraceLabel(const std::wstring& label) {
        resizeTraceLabel_ = label;
        LayoutCostProbe::SetEnabled(!label.empty());
    }

private:

    // Diagnostic subsystem. DiagnosticsHost encapsulates the
    // SessionLog (in-memory operation capture + per-frame stats for timeline/
    // export) and two FrameRing<120> buffers (last 120 frame build costs and
    // intervals — the raw data behind FPS / P95 / P99 jank metrics).
    DiagnosticsHost diagnostics_;
    bool sessionLogEnabled_ = false;
    // Session-relative clock origin (QPC at EnableSessionLog) so event/frame
    // timestamps read as seconds-since-start.
    int64_t sessionStartQpc_ = 0;
    // Seconds since the session log started (0 if disabled).
    double SessionClockSec() const;
    // The phase to stamp the current frame with, from modal/anim/resize state.
    SessionPhase CurrentSessionPhase(FrameReason reason) const;

    // Refresh rate (Hz) of the monitor the window is ON, 0 until first queried.
    // Cached because pacing reads it every loop turn and EnumDisplaySettings is not
    // free. Re-queried when the window lands on another monitor (WM_MOVE compares
    // refreshMonitor_), on WM_DISPLAYCHANGE (mode switch) and on WM_DPICHANGED.
    int refreshHz_ = 0;
    HMONITOR refreshMonitor_ = nullptr;  // monitor refreshHz_ was read from
    // Pacing rate: the monitor's rate, capped at 144 only inside the system modal
    // move/size loop (see the definition for why). 60 if it cannot be determined.
    int RefreshHz();
    void InvalidateRefreshHz() { refreshHz_ = 0; refreshMonitor_ = nullptr; }
    // WM_MOVE / WM_DISPLAYCHANGE: drop the cache if the window changed monitor.
    void TrackRefreshMonitor();

    int64_t lastFrameQpc_ = 0;  // 0 = no previous frame yet (skip first interval)
    // Was there pending work when the previous frame ended — i.e. did the loop go
    // straight into the next frame rather than blocking on input? Only then is the
    // gap between the two frames a cadence sample. This replaces a "gap > 500ms means
    // idle" heuristic that discarded every genuinely slow frame, which is precisely
    // the set of frames the FPS/jank numbers exist to expose. See the interval-ring
    // guard in RenderNow for the full account.
    bool loopWasContinuous_ = false;

    // QPC at the START of the last painted frame. This — not lastFrameQpc_ — is what
    // frame pacing measures from, and the distinction is worth a paragraph because
    // getting it wrong costs exactly one frame's CPU time per frame.
    //
    // Pacing asks "when is the next frame due". The honest answer is one refresh
    // after the previous frame BEGAN: a frame that starts on the refresh boundary and
    // takes 0.8ms to build still lands inside its slot, so the slot is the unit to
    // measure. Measuring from the END instead makes every cycle
    //
    //     wait(interval - margin) + drainMessages + RenderNow
    //
    // which is longer than `interval` by the frame's own cost — a systematic slow
    // drift, not jitter. On a 120Hz panel with a 0.78ms frame that is
    // 8.33 - 0.30 + 0.78 + wake ≈ 9.1ms, i.e. a rock-steady 110fps instead of 120,
    // and it reproduced identically on every run because nothing about it is random.
    //
    // Pacing from the start makes the frame's cost absorbed BY the interval rather
    // than added TO it, so the cadence is the refresh for any frame that fits in its
    // slot. A frame that overruns its slot leaves the next deadline already past, and
    // the loop paints immediately — degrading to "as fast as it can", which is the
    // correct behaviour when the budget is genuinely blown.
    //
    // lastFrameQpc_ (end) is deliberately kept for the DIAGNOSTICS: the interval ring
    // and the HUD's FPS/jank measure end-to-end, which in steady state has the same
    // mean and is the number a user comparing against a frame budget expects to read.
    int64_t lastFrameStartQpc_ = 0;  // 0 = no previous frame yet (paint immediately)

    // On-screen performance HUD. Off by default; the demo flips it (F12). When on,
    // hud_ owns the HUD thread and RenderNow publishes one snapshot per painted
    // frame to it; nothing else on the UI thread changes. Null while off, so a
    // window that never opens the HUD pays nothing for it.
    std::unique_ptr<HudOverlay> hud_;
    bool hudVisible_ = false;
    uint64_t paintedFrames_ = 0;  // frames painted since creation, for the HUD
    void PublishHud(int64_t frameEndQpc);

    // Dirty-rect partial redraw state.
    // fullDrawRequired_: set after resize / DPI / theme / device-loss; forces a full
    //   clear for the next 2 frames so no partial redraw trusts content-surface
    //   pixels that those events left undefined.
    // prevHotCaption_ / prevPressedCaption_: caption-button hover/press state is
    //   host chrome (not an element), so a change is invisible to the element
    //   dirty walk — a diff here forces a full redraw so the caption repaints.
    uint32_t fullDrawRequired_ = 2u;  // warm-up frames on startup
    int prevHotCaption_ = 0;
    int prevPressedCaption_ = 0;

    std::function<bool(PopupDismissReason, HWND, int, int)> popupDismiss_;  // callback to dismiss active popup
    std::function<bool(UINT)> popupKey_;  // active popup consumes keys first
    std::function<bool(int, int, int)> popupWheel_;  // forward wheel to active popup (screen x/y, delta)
    std::vector<UIElement*> auxAnimRoots_;  // auxiliary animation roots (e.g. PopupHost content)
    // RenderNow's per-frame dirty-rect list, kept so its capacity survives frames.
    std::vector<RectDip> dirtyBoundsScratch_;
    // Generation counters for the three slots above. Register* bumps the counter
    // and captures the new value; the returned Subscription only clears the slot
    // if the counter still matches — so a stale subscription's destructor never
    // wipes a newer registration that replaced it.
    unsigned dismissGen_ = 0;
    unsigned keyGen_ = 0;
    unsigned wheelGen_ = 0;

    // Tooltip service (created lazily on first hover with a tooltip). tooltipArming_
    // holds the hover target the timer is counting down for and the popup
    // suppression; tooltipTimerOn_ mirrors whether kTooltipTimerId is set.
    std::unique_ptr<TooltipService> tooltip_;
    TooltipArming tooltipArming_;
    bool tooltipTimerOn_ = false;
    // Popups that announced themselves with WM_FLUENT_POPUP_OPENED. No close notice
    // exists: an entry is dropped once its HWND is no longer visible (Close hides,
    // a destroyed window fails IsWindow). Usually empty or one entry; a menu
    // cascade adds one per open level.
    std::vector<HWND> openPopups_;
    Event<NativeWindowHost, ThemeChangedArgs> themeChanged_;
    Application* application_ = nullptr;
    uint64_t applicationGeneration_ = 0;
};

} // namespace fluent
