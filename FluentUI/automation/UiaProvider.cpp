// UiaProvider.cpp — the only translation unit that includes <UIAutomation.h>.
//
// The window root is currently the whole provider: a fragment root with no
// children. It proves the plumbing end to end (WM_GETOBJECT ->
// UiaReturnRawElementProvider -> UIA core merges us with the system HWND proxy) and
// carries the threading contract described in UiaProvider.h that the peer layer
// relies on. The window's Name, BoundingRectangle and Window/Transform patterns
// come from the HWND host provider (get_HostRawElementProvider), so this class
// deliberately reports nothing it does not own.
//
// COST. Nothing in this file runs until a UIA client sends the first WM_GETOBJECT.
// Every method is O(1) and reads only members of this object.
#include "UiaProvider.h"

#include "UiaInternal.h"

#include "AutomationGeometry.h"
#include "AutomationTree.h"
#include "../core/UIElement.h"

#pragma comment(lib, "uiautomationcore.lib")

namespace fluent::automation {

std::atomic<uint32_t> g_threadViolations{0};
std::atomic<bool> g_threadAssertEnabled{true};
std::atomic<bool> g_optionsOffThreadTraced{false};
namespace {
std::atomic<bool> g_firstCallTraced{false};
}  // namespace

void CheckProviderThread(DWORD uiThreadId, const char* method) {
    const DWORD tid = GetCurrentThreadId();
    if (tid == uiThreadId) {
        if (!g_firstCallTraced.exchange(true))
            TraceFmt("Uia", "first provider call on UI thread (%s) tid=%lu", method, tid);
        return;
    }
    const uint32_t n = ++g_threadViolations;
    if (n <= 16)  // a confused client can call thousands of times; keep the log readable
        TraceFmt("Uia", "WRONG THREAD %s tid=%lu ui=%lu (#%u)", method, tid, uiThreadId, n);
    _ASSERTE(!g_threadAssertEnabled.load() &&
             "UIA provider called off the UI thread; see automation/UiaProvider.h");
}

namespace {

class WindowRootProvider final : public IRawElementProviderSimple,
                                 public IRawElementProviderFragment,
                                 public IRawElementProviderFragmentRoot {
public:
    WindowRootProvider(HWND hwnd, DWORD uiThreadId, IAutomationSite* site)
        : hwnd_(hwnd), uiThreadId_(uiThreadId), site_(site) {}

    void Detach() { hwnd_ = nullptr; site_ = nullptr; attached_ = false; }
    bool Attached() const { return attached_; }

    // IUnknown. The count is atomic because UIA core may Release from its own
    // threads during client disconnect; every other member is UI-thread only.
    IFACEMETHODIMP_(ULONG) AddRef() override { return ++refs_; }
    IFACEMETHODIMP_(ULONG) Release() override {
        const ULONG left = --refs_;
        if (left == 0) delete this;  // touches no framework object (see com-lifetime rule)
        return left;
    }
    IFACEMETHODIMP QueryInterface(REFIID riid, void** out) override {
        if (!out) return E_POINTER;
        *out = nullptr;
        if (riid == __uuidof(IUnknown) || riid == __uuidof(IRawElementProviderSimple))
            *out = static_cast<IRawElementProviderSimple*>(this);
        else if (riid == __uuidof(IRawElementProviderFragment))
            *out = static_cast<IRawElementProviderFragment*>(this);
        else if (riid == __uuidof(IRawElementProviderFragmentRoot))
            *out = static_cast<IRawElementProviderFragmentRoot*>(this);
        else
            return E_NOINTERFACE;
        AddRef();
        return S_OK;
    }

    // IRawElementProviderSimple
    IFACEMETHODIMP get_ProviderOptions(ProviderOptions* ret) override {
        // NOT thread-checked. Observed under Inspect.exe: UIA core calls this from
        // its own worker thread (stack: UIAutomationCore.dll -> get_ProviderOptions)
        // right after WM_GETOBJECT, before it can know that we want COM routing (the
        // answer is what tells it). It reads no mutable member and returns a
        // constant, so any thread is safe. Off-thread calls are traced once.
        if (GetCurrentThreadId() != uiThreadId_ && !g_optionsOffThreadTraced.exchange(true))
            TraceFmt("Uia", "get_ProviderOptions off UI thread tid=%lu ui=%lu (expected)",
                     GetCurrentThreadId(), uiThreadId_);
        if (!ret) return E_POINTER;
        // Valid even when detached: UIA core asks this while tearing down.
        // UseComThreading: without it UIA core called get_FragmentRoot on its own
        // worker thread (measured). With it, COM delivers every call on our STA.
        *ret = static_cast<ProviderOptions>(ProviderOptions_ServerSideProvider |
                                            ProviderOptions_UseComThreading);
        return S_OK;
    }
    IFACEMETHODIMP GetPatternProvider(PATTERNID, IUnknown** ret) override {
        CheckThread("GetPatternProvider");
        if (!ret) return E_POINTER;
        *ret = nullptr;
        return attached_ ? S_OK : UIA_E_ELEMENTNOTAVAILABLE;
    }
    IFACEMETHODIMP GetPropertyValue(PROPERTYID id, VARIANT* ret) override {
        CheckThread("GetPropertyValue");
        if (!ret) return E_POINTER;
        VariantInit(ret);
        if (!attached_) return UIA_E_ELEMENTNOTAVAILABLE;
        // FrameworkId is the one property only we can supply; it is also what makes
        // the provider visible in Inspect.exe ("FrameworkId: FluentUI"), the simplest
        // proof that WM_GETOBJECT is answered at all.
        if (id == UIA_FrameworkIdPropertyId) {
            ret->vt = VT_BSTR;
            ret->bstrVal = SysAllocString(L"FluentUI");
            return ret->bstrVal ? S_OK : E_OUTOFMEMORY;
        }
        return S_OK;  // VT_EMPTY: let the HWND host provider answer
    }
    IFACEMETHODIMP get_HostRawElementProvider(IRawElementProviderSimple** ret) override {
        CheckThread("get_HostRawElementProvider");
        if (!ret) return E_POINTER;
        *ret = nullptr;
        if (!attached_) return UIA_E_ELEMENTNOTAVAILABLE;
        const HRESULT hr = UiaHostProviderFromHwnd(hwnd_, ret);
        if (FAILED(hr)) Trace("Uia", "UiaHostProviderFromHwnd FAILED", hr);
        return hr;
    }

    // IRawElementProviderFragment
    IFACEMETHODIMP Navigate(NavigateDirection dir, IRawElementProviderFragment** ret) override {
        CheckThread("Navigate");
        if (!ret) return E_POINTER;
        *ret = nullptr;
        if (!attached_) return UIA_E_ELEMENTNOTAVAILABLE;
        // A fragment root has no parent or siblings in its fragment; UIA asks the
        // HWND host for those. Children are the window's exposed root elements.
        if (!site_) return S_OK;
        if (dir == NavigateDirection_FirstChild) return FragmentForElement(FirstExposedRoot(*site_), ret);
        if (dir == NavigateDirection_LastChild) return FragmentForElement(LastExposedRoot(*site_), ret);
        return S_OK;
    }
    IFACEMETHODIMP GetRuntimeId(SAFEARRAY** ret) override {
        CheckThread("GetRuntimeId");
        if (!ret) return E_POINTER;
        *ret = nullptr;  // an HWND-hosted root takes its runtime id from the host
        return attached_ ? S_OK : UIA_E_ELEMENTNOTAVAILABLE;
    }
    IFACEMETHODIMP get_BoundingRectangle(UiaRect* ret) override {
        CheckThread("get_BoundingRectangle");
        if (!ret) return E_POINTER;
        *ret = UiaRect{};  // empty: the host provider supplies the window rect
        return attached_ ? S_OK : UIA_E_ELEMENTNOTAVAILABLE;
    }
    IFACEMETHODIMP GetEmbeddedFragmentRoots(SAFEARRAY** ret) override {
        CheckThread("GetEmbeddedFragmentRoots");
        if (!ret) return E_POINTER;
        *ret = nullptr;
        return attached_ ? S_OK : UIA_E_ELEMENTNOTAVAILABLE;
    }
    IFACEMETHODIMP SetFocus() override {
        CheckThread("SetFocus");
        return attached_ ? S_OK : UIA_E_ELEMENTNOTAVAILABLE;
    }
    IFACEMETHODIMP get_FragmentRoot(IRawElementProviderFragmentRoot** ret) override {
        CheckThread("get_FragmentRoot");
        if (!ret) return E_POINTER;
        *ret = nullptr;
        if (!attached_) return UIA_E_ELEMENTNOTAVAILABLE;
        *ret = this;
        AddRef();
        return S_OK;
    }

    // IRawElementProviderFragmentRoot
    IFACEMETHODIMP ElementProviderFromPoint(double x, double y,
                                            IRawElementProviderFragment** ret) override {
        CheckThread("ElementProviderFromPoint");
        if (!ret) return E_POINTER;
        *ret = nullptr;  // null = the root itself
        if (!attached_) return UIA_E_ELEMENTNOTAVAILABLE;
        if (!site_) return S_OK;
        const POINT o = site_->ClientOriginOnScreen();
        const DipPoint pt = HostDipFromScreen(x, y, o.x, o.y, site_->Dpi());
        return FragmentForElement(ExposedElementAt(*site_, pt.x, pt.y), ret);
    }
    IFACEMETHODIMP GetFocus(IRawElementProviderFragment** ret) override {
        CheckThread("GetFocus");
        if (!ret) return E_POINTER;
        *ret = nullptr;  // null = focus is on the root (no focused element)
        if (!attached_) return UIA_E_ELEMENTNOTAVAILABLE;
        if (!site_) return S_OK;
        UIElement* f = site_->FocusedElement();
        return f ? FragmentForElement(NearestListed(*site_, f), ret) : S_OK;
    }

private:
    ~WindowRootProvider() = default;

    void CheckThread(const char* method) const { CheckProviderThread(uiThreadId_, method); }

    std::atomic<ULONG> refs_{1};
    HWND hwnd_;
    const DWORD uiThreadId_;
    IAutomationSite* site_;  // non-owning; nulled by Detach (the host dies first)
    bool attached_ = true;
};

WindowRootProvider* FromUnknown(IUnknown* unk) {
    // Every IUnknown handed out by this file is the IRawElementProviderSimple base.
    return unk ? static_cast<WindowRootProvider*>(static_cast<IRawElementProviderSimple*>(unk))
               : nullptr;
}

}  // namespace

HRESULT CreateWindowRootProvider(HWND hwnd, DWORD uiThreadId, IAutomationSite* site,
                                 IUnknown** out) {
    if (!out) return E_POINTER;
    auto* p = new (std::nothrow) WindowRootProvider(hwnd, uiThreadId, site);
    *out = p ? static_cast<IRawElementProviderSimple*>(p) : nullptr;
    // UseComThreading only routes calls back to us if this thread is an STA. Trace
    // the apartment so a log from a host without Application's ComApartment (or one
    // that chose MTA) explains a later WRONG THREAD line.
    APTTYPE apt{};
    APTTYPEQUALIFIER qual{};
    const HRESULT aptHr = CoGetApartmentType(&apt, &qual);
    const bool sta = SUCCEEDED(aptHr) && (apt == APTTYPE_STA || apt == APTTYPE_MAINSTA);
    TraceFmt("Uia", "root provider created hwnd=%p ui=%lu %s apt=%d hr=0x%08X%s",
             static_cast<void*>(hwnd), uiThreadId, p ? "ok" : "OOM",
             SUCCEEDED(aptHr) ? static_cast<int>(apt) : -1, static_cast<unsigned>(aptHr),
             sta ? "" : " NOT STA: calls cannot be routed to the UI thread");
    return p ? S_OK : E_OUTOFMEMORY;
}

void DetachProvider(IUnknown* provider) {
    if (auto* p = FromUnknown(provider)) p->Detach();
}

bool IsProviderAttached(IUnknown* provider) {
    auto* p = FromUnknown(provider);
    return p && p->Attached();
}

namespace {

// Every IUnknown that reaches the core is an IRawElementProviderSimple base handed
// out by this file (FromUnknown's invariant), so the cast below is exact.
class RealCore final : public IUiaCore {
public:
    // Both provider kinds hand out their IRawElementProviderSimple base as the
    // IUnknown (UiaInternal.h), so AsSimple is exact for either.
    LRESULT ReturnProvider(HWND hwnd, WPARAM wp, LPARAM lp, IUnknown* provider) override {
        return UiaReturnRawElementProvider(hwnd, wp, lp, AsSimple(provider));
    }
    HRESULT DisconnectProvider(IUnknown* provider) override {
        return UiaDisconnectProvider(AsSimple(provider));
    }
    void ReleaseEventMap(HWND hwnd) override { UiaReturnRawElementProvider(hwnd, 0, 0, nullptr); }
    bool InSendMessage() override { return InSendMessageEx(nullptr) != ISMEX_NOSEND; }
    bool ClientsAreListening() override { return UiaClientsAreListening() != FALSE; }
    HRESULT RaiseEvent(IUnknown* provider, int eventId) override {
        return UiaRaiseAutomationEvent(AsSimple(provider), eventId);
    }
    HRESULT RaiseBoolChanged(IUnknown* provider, int id, bool oldV, bool newV) override {
        VARIANT o, n;
        o.vt = n.vt = VT_BOOL;
        o.boolVal = oldV ? VARIANT_TRUE : VARIANT_FALSE;
        n.boolVal = newV ? VARIANT_TRUE : VARIANT_FALSE;
        return UiaRaiseAutomationPropertyChangedEvent(AsSimple(provider), id, o, n);
    }
    HRESULT RaiseIntChanged(IUnknown* provider, int id, int oldV, int newV) override {
        VARIANT o, n;
        o.vt = n.vt = VT_I4;
        o.lVal = oldV;
        n.lVal = newV;
        return UiaRaiseAutomationPropertyChangedEvent(AsSimple(provider), id, o, n);
    }
    HRESULT RaiseStringChanged(IUnknown* provider, int id, const wchar_t* oldV,
                               const wchar_t* newV) override {
        VARIANT o, n;
        o.vt = n.vt = VT_BSTR;
        o.bstrVal = SysAllocString(oldV ? oldV : L"");
        n.bstrVal = SysAllocString(newV ? newV : L"");
        const HRESULT hr = UiaRaiseAutomationPropertyChangedEvent(AsSimple(provider), id, o, n);
        SysFreeString(o.bstrVal);
        SysFreeString(n.bstrVal);
        return hr;
    }
    void PostToHost(HWND hwnd, UINT msg) override {
        if (!PostMessageW(hwnd, msg, 0, 0))
            Trace("Uia", "PostMessage(flush) FAILED", HRESULT_FROM_WIN32(GetLastError()));
    }
};

}  // namespace

IUiaCore& RealUiaCore() {
    static RealCore core;  // stateless: no COM pointer, so no static-ComPtr hazard
    return core;
}

void ReleaseWindowProvider(IUiaCore& core, HWND hwnd, IUnknown* provider) {
    auto* p = FromUnknown(provider);
    if (p) {
        // ORDER: disconnect while the provider still answers, detach afterwards.
        // Measured under Inspect.exe: detaching first made UiaDisconnectProvider
        // return 0x80040201 (UIA_E_ELEMENTNOTAVAILABLE) -- UIA core calls back into
        // the provider while disconnecting and got our "not available" answer, so the
        // disconnect did not complete. The window is still alive here (WM_DESTROY /
        // destructor safety net, before hwnd_ is cleared), so answering is safe.
        //
        // Learn: UiaDisconnectProvider cannot be called while responding to
        // SendMessage (RPC_E_CANTCALLOUT_ININPUTSYNCCALL). Skipping it only delays
        // UIA core's Release until UiaDisconnectAllProviders / process exit; the
        // provider is detached below either way.
        if (!core.InSendMessage()) {
            const HRESULT hr = core.DisconnectProvider(provider);
            Trace("Uia", "UiaDisconnectProvider(root)", hr);
        } else {
            TraceMsg("Uia", "UiaDisconnectProvider(root) skipped: inside SendMessage");
        }
        p->Detach();
    }
    // Learn: call from WM_DESTROY of a window that returned a provider, to release
    // the references UIA kept for events. Harmless if no event was ever raised.
    core.ReleaseEventMap(hwnd);
    TraceMsg("Uia", "UiaReturnRawElementProvider(hwnd,0,0,null) on destroy");
}

bool ShouldServeUiaRoot(LPARAM lp, bool destroying) {
    return !destroying && static_cast<LONG>(lp) == UiaRootObjectId;
}

uint32_t ThreadViolations() { return g_threadViolations.load(); }
void SetThreadAssertEnabled(bool enabled) { g_threadAssertEnabled.store(enabled); }

}  // namespace fluent::automation
