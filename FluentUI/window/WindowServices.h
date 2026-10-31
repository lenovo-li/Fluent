// WindowServices.h — narrow interface the controls depend on instead of the
// concrete NativeWindowHost.
//
// This interface decouples controls from the full window class. A
// control that needs a floating layer (ComboBox dropdown, MenuBar / MenuFlyout)
// or the shared device stack asks for these capabilities through this interface,
// not by including NativeWindowHost.h. NativeWindowHost implements it; a test double
// or a future alternative host can implement the same small surface.
//
// This is not a cross-platform abstraction — it only narrows the compile-time
// and lifetime coupling between a control and its host window.
#pragma once

#include "../fl_common.h"
#include "../core/Subscription.h"
#include <functional>
#include <string>

namespace fluent {

class D2DContext;
class DWriteContext;
class ICompositionBackend;
class AnimationRegistry;
class UIElement;

// Why the host asked an active popup to dismiss. Free (namespace-level) so a
// control's callback signature does not have to name a concrete window class.
// The window raises Move/Resize/Deactivate itself; Click carries the screen-
// pixel point of the mouse-down so a popup can test click-outside.
enum class PopupDismissReason { Click, Deactivate, Move, Resize };

// The capabilities a control needs from its host window. Kept intentionally
// small: device access for a popup's own composition target, the compositor backend
// (so even an embedded member like a TreeView's scroll model can animate off the UI
// thread), the owner HWND / HINSTANCE / DPI for positioning, and the two active-popup
// registries the window routes deactivation and keyboard input through.
class WindowServices {
public:
    virtual HINSTANCE Instance() const = 0;
    virtual HWND Hwnd() const = 0;
    virtual float DpiScale() const = 0;
    virtual D2DContext& D2D() = 0;
    virtual DWriteContext& DWrite() = 0;

    // Composition layer: a control that runs an animation on the
    // DirectComposition compositor thread (e.g. an indeterminate ProgressBar's
    // sweeping segment, the one user today) reaches the
    // compositor through this backend — create visuals, parent them above the
    // window content, request a commit — without naming any DComp type itself.
    // Returns null on a host without composition or mid device-loss; a control
    // MUST null-check and fall back to its UI-thread path. The backend is owned by
    // the host and outlives the attached tree; a control holds it only while
    // attached (via Window()->Composition()), never past its own lifetime.
    virtual ICompositionBackend* Composition() = 0;

    // Record a diagnostic event into the host's session log, if one is active
    // (diagnostics/SessionLog.h). Default no-op so a control can trace geometry /
    // state changes into the on-close report without every host or test double
    // having to implement logging. `category` is a short tag; `detail` is free text.
    virtual void LogSessionEvent(const char* category, const std::string& detail) {
        UNREFERENCED_PARAMETER(category);
        UNREFERENCED_PARAMETER(detail);
    }

    // System mouse wheel scroll-lines setting (read from SPI_GETWHEELSCROLLLINES).
    // One wheel notch (WHEEL_DELTA = 120) scrolls this many lines. Default 3 if
    // the query fails. Updated on WM_SETTINGCHANGE so the control's wheel handler
    // stays in sync with the user's system preference.
    virtual UINT WheelScrollLines() const { return 3; }

    // Install the callback and return a Subscription that clears *this specific*
    // registration when destroyed. The host stamps each slot with a generation,
    // so a stale Subscription (whose slot was later overwritten by another popup)
    // is a no-op on destruction — it never clears the newer owner's callback. A
    // control stores the Subscription as a member, so its destructor unregisters
    // automatically (no dangling `[this]`).
    virtual Subscription RegisterActivePopupDismiss(
        std::function<bool(PopupDismissReason, HWND otherHwnd, int screenX,
                           int screenY)> cb) = 0;
    virtual Subscription RegisterActivePopupKeyHandler(
        std::function<bool(UINT)> cb) = 0;
    
    // Register a handler for WM_MOUSEWHEEL events that should be forwarded to an
    // active popup. Returns a Subscription that clears the registration. The
    // callback receives screen-pixel coordinates and wheel delta; it should return
    // true if the wheel event was handled (consumed) by the popup.
    //
    // WHY THIS EXISTS: WS_EX_NOACTIVATE popups (ComboBox dropdown, MenuFlyout) never
    // receive focus, so WM_MOUSEWHEEL goes to the parent window instead. Without
    // forwarding, the dropdown is unscrollable via mouse wheel — a functional gap.
    virtual Subscription RegisterActivePopupWheelHandler(
        std::function<bool(int screenX, int screenY, int delta)> cb) = 0;

    // Register an auxiliary animation root (e.g. PopupHost content) that should be
    // collected when UpdateAnimationTimer gathers animating elements. Returns a
    // Subscription that unregisters on destruction. The host ticks these roots
    // alongside elements_ so popup content can use smooth scrolling, scrollbar
    // fade-in/out, and other UIElement animations without needing their own timer.
    //
    // WHY THIS EXISTS: PopupHost is a lightweight WS_EX_NOACTIVATE window with no
    // FrameHost of its own. Without this, popup content (ComboListView scrollbar)
    // sets WantsAnimationTick but never receives OnAnimationTick, so AnimateBy and
    // scrollbar expand/fade remain frozen. Hooking into the parent window's clock
    // reuses one message loop, one scheduler, zero idle overhead when not animating.
    //
    // Default: no-op (empty Subscription) for hosts without an animation clock
    // (test doubles); popup content then simply never ticks.
    virtual Subscription RegisterAuxAnimationRoot(UIElement* root) {
        UNREFERENCED_PARAMETER(root);
        return Subscription();
    }

    // The host's AnimationRegistry, injected into popup content's UIContext so
    // UIElement::DetachFromContext can drop it from the active set. Must be the
    // same registry RegisterAuxAnimationRoot collects into. Default: nullptr.
    virtual AnimationRegistry* Anims() { return nullptr; }

    // Ask the host to re-collect its active-animation set now. A popup calls this
    // after routing its own input (hover / press / leave arrive on the popup's
    // WndProc, never on the host's), so a hover-started scrollbar expand actually
    // gets ticked. Default: no-op.
    virtual void RequestAnimationUpdate() {}

protected:
    // Non-virtual protected dtor: the host owns its own lifetime; controls hold
    // a non-owning WindowServices* and never delete through it.
    ~WindowServices() = default;
};

} // namespace fluent
