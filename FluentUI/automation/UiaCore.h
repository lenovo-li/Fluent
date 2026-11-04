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
// Events go through it too, so the "no client listening -> raise nothing" gate and
// the coalescing are testable, as does the posted flush message (tests run the
// flush by hand instead of pumping a window).
#pragma once

#include "../fl_common.h"  // NOMINMAX / WIN32_LEAN_AND_MEAN before <windows.h>
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

    // UiaClientsAreListening(): false means no client anywhere wants events.
    virtual bool ClientsAreListening() = 0;
    // UiaRaiseAutomationEvent(provider, eventId).
    virtual HRESULT RaiseEvent(IUnknown* provider, int eventId) = 0;
    // UiaRaiseAutomationPropertyChangedEvent for a VT_BOOL / VT_I4 / VT_BSTR value.
    virtual HRESULT RaiseBoolChanged(IUnknown* provider, int propertyId, bool oldV, bool newV) = 0;
    virtual HRESULT RaiseIntChanged(IUnknown* provider, int propertyId, int oldV, int newV) = 0;
    virtual HRESULT RaiseDoubleChanged(IUnknown* provider, int propertyId, double oldV,
                                       double newV) = 0;
    virtual HRESULT RaiseStringChanged(IUnknown* provider, int propertyId, const wchar_t* oldV,
                                       const wchar_t* newV) = 0;
    // UiaRaiseStructureChangedEvent(provider, StructureChangeType_ChildrenInvalidated).
    virtual HRESULT RaiseChildrenInvalidated(IUnknown* provider) = 0;
    // PostMessageW(hwnd, msg, 0, 0) for the host's coalesced flush.
    virtual void PostToHost(HWND hwnd, UINT msg) = 0;
};

// The process-wide real implementation. Stateless; never null.
IUiaCore& RealUiaCore();

}  // namespace fluent::automation
