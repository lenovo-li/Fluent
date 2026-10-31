// AutomationHost.h — one per top-level window: the window's UI Automation entry point.
//
// WHAT IT OWNS. The window-root provider, created lazily on the first
// WM_GETOBJECT(UiaRootObjectId). Until a UIA client (Narrator, Inspect.exe,
// Accessibility Insights, a test tool) asks, this object is one null pointer: no
// COM object, no subscription, no timer. That is the "idle costs zero" rule, and it
// is also why a window with no client pays nothing per frame — nothing here is on
// the frame path at all.
//
// WHY THE ROOT IS HELD HERE AND NOT BY UIA. UIA identifies elements by provider
// object, so every WM_GETOBJECT must return the SAME provider; holding one
// reference here guarantees it. UIA core holds its own references and may Release
// after the window is gone, which is why the provider never touches framework
// objects once detached (UiaProvider.h).
//
// TEARDOWN ORDER (OnWindowDestroying, from WM_DESTROY and the ~NativeWindowHost
// safety net, while the HWND is still valid):
//   1. stop serving: a re-entrant WM_GETOBJECT during teardown goes to
//      DefWindowProc, never returns the provider being disconnected (Learn);
//   2. UiaDisconnectProvider unless inside SendMessage, while the provider still
//      answers (UIA core calls back into it; detaching first made the disconnect
//      fail with UIA_E_ELEMENTNOTAVAILABLE, measured under Inspect.exe);
//   3. detach the provider (inert from now on), then
//      UiaReturnRawElementProvider(hwnd, 0, 0, nullptr);
//   4. drop our reference.
//
// The peer tree, the event/action queues and WM_SETFOCUS handling belong here too;
// the window calls only these entry points.
#pragma once

#include <windows.h>
#include <wrl/client.h>

namespace fluent::automation {

class IUiaCore;

class AutomationHost {
public:
    AutomationHost();
    // Tests inject a recording core (UiaCore.h); the window uses the real one.
    explicit AutomationHost(IUiaCore& core) : core_(&core) {}
    ~AutomationHost();
    AutomationHost(const AutomationHost&) = delete;
    AutomationHost& operator=(const AutomationHost&) = delete;

    // WM_GETOBJECT. Returns true and sets `result` when the request is ours
    // (UiaRootObjectId and the window is not being torn down); false means the
    // caller passes the message to DefWindowProc (MSAA OBJID_* requests included).
    bool OnGetObject(HWND hwnd, WPARAM wp, LPARAM lp, bool destroying, LRESULT& result);

    // WM_DESTROY / destructor safety net. Idempotent; no-op if no client ever came.
    void OnWindowDestroying(HWND hwnd);

    bool HasRootProvider() const { return root_ != nullptr; }

private:
    IUiaCore* core_;  // non-owning, never null
    Microsoft::WRL::ComPtr<IUnknown> root_;
    bool torn_down_ = false;
};

}  // namespace fluent::automation
