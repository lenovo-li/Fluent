// AutomationHost.cpp — see AutomationHost.h for the ownership and teardown contract.
#include "AutomationHost.h"

#include "UiaProvider.h"
#include "../fl_common.h"

namespace fluent::automation {

AutomationHost::AutomationHost() : core_(&RealUiaCore()) {}

AutomationHost::~AutomationHost() {
    // The window always calls OnWindowDestroying while its HWND is alive. If it did
    // not (a host object that never created a window), there is no HWND to pass
    // UIA, so just make the provider inert before dropping our reference.
    if (root_) DetachProvider(root_.Get());
}

bool AutomationHost::OnGetObject(HWND hwnd, WPARAM wp, LPARAM lp, bool destroying,
                                 LRESULT& result) {
    if (torn_down_ || !ShouldServeUiaRoot(lp, destroying)) return false;
    if (!root_) {
        // First client contact: the only allocation UIA ever causes for a window.
        // The window's thread is the thread this message is dispatched on.
        const HRESULT hr = CreateWindowRootProvider(hwnd, GetCurrentThreadId(),
                                                    root_.ReleaseAndGetAddressOf());
        if (FAILED(hr)) {
            Trace("Uia", "CreateWindowRootProvider FAILED", hr);
            return false;
        }
    }
    result = core_->ReturnProvider(hwnd, wp, lp, root_.Get());
    return true;
}

void AutomationHost::OnWindowDestroying(HWND hwnd) {
    if (torn_down_) return;
    torn_down_ = true;
    if (!root_) return;  // no client ever asked: nothing was handed out
    // torn_down_ is already set, so a re-entrant WM_GETOBJECT during the disconnect
    // goes to DefWindowProc and never re-hands-out this provider (Learn).
    // ReleaseWindowProvider disconnects first and detaches after (see its comment).
    if (hwnd) ReleaseWindowProvider(*core_, hwnd, root_.Get());
    else DetachProvider(root_.Get());
    root_.Reset();
}

}  // namespace fluent::automation
