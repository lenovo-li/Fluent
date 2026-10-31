// AutomationHost.h — one per top-level window: the window's UI Automation site.
//
// WHAT IT OWNS. The window-root provider (created on the first
// WM_GETOBJECT(UiaRootObjectId)) and the queues below. Until a UIA client
// (Narrator, Inspect.exe, a test tool) asks, nothing here is allocated and every
// entry point returns after a null check: the "idle costs zero" rule. Nothing is
// on the frame path.
//
// QUEUES AND THE ONE FLUSH MESSAGE. Work UIA must not do inline is queued and run
// from WM_FLUENT_UIA_FLUSH, posted once when the first item is queued:
//   * actions (Invoke/Toggle/Select/SetValue/SetFocus): app handlers may destroy
//     the element, which must not happen under UIA core's stack frame;
//   * disconnects of frozen element providers: a tree mutation can happen inside
//     an input handler or a sent message, and UiaDisconnectProvider makes outgoing
//     COM calls (illegal inside SendMessage, and re-entrant on an STA);
//   * events (focus, invoked, property re-checks): raised only if
//     UiaClientsAreListening(), coalesced per flush (last focus wins; one re-check
//     per element however many invalidations).
// Order inside a flush: actions, then disconnects (an action can detach elements),
// then events.
//
// TEARDOWN (WM_DESTROY, and the ~NativeWindowHost safety net while the HWND is
// valid): OnWindowDestroying() first stops serving WM_GETOBJECT and drops queued
// actions and events; the window then detaches its elements, which freezes and
// queues their providers; OnWindowDestroyed() then disconnects them and finally the
// root: UiaDisconnectProvider while the root still answers (detaching it first
// made the disconnect fail with UIA_E_ELEMENTNOTAVAILABLE, measured), then detach,
// then UiaReturnRawElementProvider(hwnd, 0, 0, nullptr).
#pragma once

#include "../fl_common.h"  // NOMINMAX / WIN32_LEAN_AND_MEAN before <windows.h>
#include <wrl/client.h>

#include <string>
#include <vector>

#include "AutomationSite.h"

namespace fluent {
class FocusManager;
}

namespace fluent::automation {

class IUiaCore;

// What the host needs from its window. NativeWindowHost implements it; tests fake it.
class IAutomationWindow {
public:
    virtual ~IAutomationWindow() = default;
    virtual size_t AutomationRootCount() const = 0;
    virtual UIElement* AutomationRootAt(size_t i) const = 0;
    virtual UIElement* AutomationFocusedElement() const = 0;
};

// Posted to the host window to run the queues (WM_APP numbering: services/PopupHost.h).
constexpr UINT WM_FLUENT_UIA_FLUSH = WM_APP + 23;

class AutomationHost final : public IAutomationSite {
public:
    AutomationHost();
    // Tests inject a recording core (UiaCore.h); the window uses the real one.
    explicit AutomationHost(IUiaCore& core) : core_(&core) {}
    ~AutomationHost() override;
    AutomationHost(const AutomationHost&) = delete;
    AutomationHost& operator=(const AutomationHost&) = delete;

    // The window's side; set once before any message can reach the host.
    void Bind(IAutomationWindow* window) { window_ = window; }

    // WM_GETOBJECT. True and `result` set when the request is ours (UiaRootObjectId,
    // not tearing down); false = pass to DefWindowProc (MSAA OBJID_* included).
    bool OnGetObject(HWND hwnd, WPARAM wp, LPARAM lp, bool destroying, LRESULT& result);
    // WM_FLUENT_UIA_FLUSH.
    void OnFlush();
    // The window's FocusChanged hook. A null check when no client ever came.
    void OnFocusChanged(UIElement* newFocus);

    // Teardown, in this order around the window's element detach (see top).
    void OnWindowDestroying();
    void OnWindowDestroyed(HWND hwnd);

    bool HasRootProvider() const { return root_ != nullptr; }
    size_t PendingDisconnects() const { return disconnects_.size(); }

    // IAutomationSite
    HWND Hwnd() const override { return hwnd_; }
    double Dpi() const override;
    POINT ClientOriginOnScreen() const override;
    bool HostHasOsFocus() const override;
    IUnknown* RootProvider() const override { return root_.Get(); }
    size_t RootElementCount() const override;
    UIElement* RootElementAt(size_t i) const override;
    UIElement* FocusedElement() const override;
    void QueueDisconnect(IUnknown* frozenProvider) override;
    void PostAction(IUnknown* provider, PeerAction action, const wchar_t* value) override;
    void QueueChanged(IUnknown* provider) override;
    void QueueInvoked(IUnknown* provider) override;
    void QueueSelected(IUnknown* provider) override;

private:
    using Ref = Microsoft::WRL::ComPtr<IUnknown>;
    struct Action {
        Ref provider;
        PeerAction action;
        std::wstring value;
    };
    void RequestFlush();
    void FlushDisconnects();
    void RaiseEvents();

    IUiaCore* core_;  // non-owning, never null
    IAutomationWindow* window_ = nullptr;
    HWND hwnd_ = nullptr;  // captured from the first WM_GETOBJECT
    Ref root_;
    bool torn_down_ = false;
    bool flushPosted_ = false;
    unsigned disconnected_ = 0, disconnectFailures_ = 0;  // traced at window teardown
    std::vector<Action> actions_;
    std::vector<Ref> disconnects_;
    std::vector<Ref> changed_, invoked_, selected_;
    Ref focus_;
};

}  // namespace fluent::automation
