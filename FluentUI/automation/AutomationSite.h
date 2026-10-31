// AutomationSite.h — what a peer/provider may ask of the window that hosts it.
//
// One site per top-level window (AutomationHost implements it). Reached through
// UIContext::automation, so it exists exactly while an element is attached to that
// window, and a peer only exists while its element is attached (peers are released
// in DetachFromContext). That is the lifetime rule the provider relies on: while a
// provider is not frozen, its element's context().automation is valid.
//
// Every method runs on the UI thread (the provider uses UseComThreading on an STA).
#pragma once

#include "../fl_common.h"  // NOMINMAX / WIN32_LEAN_AND_MEAN before <windows.h>
#include <unknwn.h>
#include <cstddef>

namespace fluent {
class UIElement;
}

namespace fluent::automation {

class AutomationPeer;

// The deferred operations a provider queues instead of running inline. A pattern
// method (Invoke/Toggle/Select/SetValue/SetFocus) runs app code (Click handlers),
// which may destroy the element mid-call; running it from a posted message, after
// the COM call has returned, keeps that off UIA core's stack.
enum class PeerAction { Invoke, Toggle, Select, SetValue, SetFocus };

class IAutomationSite {
public:
    virtual ~IAutomationSite() = default;

    virtual HWND Hwnd() const = 0;
    // Host DPI and client origin in screen pixels, read now (never cached).
    virtual double Dpi() const = 0;
    virtual POINT ClientOriginOnScreen() const = 0;
    // True when the host HWND has OS keyboard focus (HasKeyboardFocus needs both).
    virtual bool HostHasOsFocus() const = 0;

    // The window root's provider (IRawElementProviderSimple as IUnknown), not AddRef'd.
    virtual IUnknown* RootProvider() const = 0;
    // Top-level elements registered with the window, in z-order (last = topmost).
    virtual size_t RootElementCount() const = 0;
    virtual UIElement* RootElementAt(size_t i) const = 0;
    virtual UIElement* FocusedElement() const = 0;

    // Takes one reference to an already-frozen provider and disconnects it from UIA
    // later, from a posted message (never synchronously inside a tree mutation).
    virtual void QueueDisconnect(IUnknown* frozenProvider) = 0;
    // Runs `action` on the peer's element from a posted message, if the provider is
    // still attached by then. `value` is used by SetValue only.
    virtual void PostAction(IUnknown* provider, PeerAction action, const wchar_t* value) = 0;
    // Queues a "property/state may have changed" re-check for this provider, raised
    // in the next flush only if a client is listening.
    virtual void QueueChanged(IUnknown* provider) = 0;
    virtual void QueueInvoked(IUnknown* provider) = 0;
    virtual void QueueSelected(IUnknown* provider) = 0;
    // The exposed children of `provider` changed (added, removed, hidden, shown);
    // null = the window root's children. Raised as ChildrenInvalidated, once per
    // parent per flush, only while a client listens.
    virtual void QueueChildrenChanged(IUnknown* parentProviderOrNull) = 0;
};

}  // namespace fluent::automation
