// UiaCore.h — the UIA core entry points the provider side calls, behind an interface.
//
// WHY. The teardown contract (disconnect while the provider still answers, then
// detach, then release the event map; skip the disconnect inside SendMessage) was
// first established by measuring under Inspect.exe, and a later reorder would fail
// no headless test if these calls went straight to UIAutomationCore. Routing them
// through this interface lets tests inject a recorder and assert the order and the
// provider's state at each call, without a window, COM runtime or UIA client.
//
// Providers are passed as IUnknown so this header stays free of <UIAutomation.h>;
// the real implementation (RealUiaCore, in UiaProvider.cpp) converts them.
//
// Event raising and UiaClientsAreListening join this interface when providers start
// raising events, so the "no client listening -> raise nothing" gate is testable too.
#pragma once

#include <windows.h>
#include <unknwn.h>

namespace fluent::automation {

class IUiaCore {
public:
    virtual ~IUiaCore() = default;
    // UiaReturnRawElementProvider(hwnd, wp, lp, provider) for a WM_GETOBJECT answer.
    virtual LRESULT ReturnProvider(HWND hwnd, WPARAM wp, LPARAM lp, IUnknown* provider) = 0;
    // UiaDisconnectProvider(provider).
    virtual HRESULT DisconnectProvider(IUnknown* provider) = 0;
    // UiaReturnRawElementProvider(hwnd, 0, 0, nullptr): drop UIA's event-map refs.
    virtual void ReleaseEventMap(HWND hwnd) = 0;
    // InSendMessageEx(nullptr) != ISMEX_NOSEND: UiaDisconnectProvider would fail
    // with RPC_E_CANTCALLOUT_ININPUTSYNCCALL here.
    virtual bool InSendMessage() = 0;
};

// The process-wide real implementation. Stateless; never null.
IUiaCore& RealUiaCore();

}  // namespace fluent::automation
