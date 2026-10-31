// UiaCaptionButtons.cpp — the window's minimize / maximize / close buttons as UIA
// Buttons. They are drawn by NativeWindowHost, not elements, so neither the peer
// tree nor the system HWND proxy can describe them correctly (the proxy reports a
// title bar it did not draw). One small provider per button, children of the
// window root, listed first.
//
// Names come from the window's system menu (SC_MINIMIZE / SC_MAXIMIZE / SC_RESTORE
// / SC_CLOSE), so they are in the user's UI language with no strings of ours.
#include "UiaInternal.h"

#include "AutomationGeometry.h"
#include "AutomationHost.h"
#include "AutomationTree.h"
#include "../core/UIElement.h"

namespace fluent::automation {
namespace {

int HitOfSlot(int slot) { return slot == 0 ? HTMINBUTTON : slot == 1 ? HTMAXBUTTON : HTCLOSE; }

std::wstring SystemMenuName(HWND hwnd, int hit) {
    UINT cmd = hit == HTMINBUTTON ? SC_MINIMIZE : hit == HTCLOSE ? SC_CLOSE
               : (IsZoomed(hwnd) ? SC_RESTORE : SC_MAXIMIZE);
    wchar_t buf[128] = {};
    HMENU menu = hwnd ? GetSystemMenu(hwnd, FALSE) : nullptr;
    if (menu && GetMenuStringW(menu, cmd, buf, 127, MF_BYCOMMAND) > 0) {
        std::wstring s;
        for (const wchar_t* p = buf; *p && *p != L'\t'; ++p)  // drop "&" and "\tAlt+F4"
            if (*p != L'&') s.push_back(*p);
        if (!s.empty()) return s;
    }
    return hit == HTMINBUTTON ? L"Minimize" : hit == HTCLOSE ? L"Close"
           : (IsZoomed(hwnd) ? L"Restore" : L"Maximize");
}

class CaptionButtonProvider final : public IRawElementProviderSimple,
                                    public IRawElementProviderFragment,
                                    public IInvokeProvider {
public:
    CaptionButtonProvider(IAutomationSite* site, int slot)
        : site_(site), slot_(slot), serial_(0x7FFF0000u + slot), ui_(GetCurrentThreadId()) {}
    void Detach() { site_ = nullptr; }

    IFACEMETHODIMP_(ULONG) AddRef() override { return ++refs_; }
    IFACEMETHODIMP_(ULONG) Release() override {
        const ULONG r = --refs_;
        if (!r) delete this;
        return r;
    }
    IFACEMETHODIMP QueryInterface(REFIID riid, void** out) override {
        if (!out) return E_POINTER;
        *out = nullptr;
        if (riid == __uuidof(IUnknown) || riid == __uuidof(IRawElementProviderSimple))
            *out = static_cast<IRawElementProviderSimple*>(this);
        else if (riid == __uuidof(IRawElementProviderFragment))
            *out = static_cast<IRawElementProviderFragment*>(this);
        else if (riid == __uuidof(IInvokeProvider))
            *out = static_cast<IInvokeProvider*>(this);
        else
            return E_NOINTERFACE;
        AddRef();
        return S_OK;
    }

    IFACEMETHODIMP get_ProviderOptions(ProviderOptions* ret) override {
        if (!ret) return E_POINTER;
        *ret = static_cast<ProviderOptions>(ProviderOptions_ServerSideProvider |
                                            ProviderOptions_UseComThreading);
        return S_OK;
    }
    IFACEMETHODIMP GetPatternProvider(PATTERNID id, IUnknown** ret) override {
        CheckProviderThread(ui_, "Caption.GetPatternProvider");
        if (!ret) return E_POINTER;
        *ret = nullptr;
        if (!site_) return UIA_E_ELEMENTNOTAVAILABLE;
        if (id == UIA_InvokePatternId) {
            *ret = static_cast<IInvokeProvider*>(this);
            AddRef();
        }
        return S_OK;
    }
    IFACEMETHODIMP GetPropertyValue(PROPERTYID id, VARIANT* ret) override {
        CheckProviderThread(ui_, "Caption.GetPropertyValue");
        if (!ret) return E_POINTER;
        VariantInit(ret);
        if (!site_) return UIA_E_ELEMENTNOTAVAILABLE;
        switch (id) {
            case UIA_ControlTypePropertyId:
                ret->vt = VT_I4;
                ret->lVal = UIA_ButtonControlTypeId;
                return S_OK;
            case UIA_NamePropertyId: {
                ret->vt = VT_BSTR;
                ret->bstrVal = SysAllocString(SystemMenuName(site_->Hwnd(), HitOfSlot(slot_)).c_str());
                return S_OK;
            }
            case UIA_AutomationIdPropertyId: {
                static const wchar_t* ids[3] = {L"Minimize", L"Maximize", L"Close"};
                ret->vt = VT_BSTR;
                ret->bstrVal = SysAllocString(ids[slot_]);
                return S_OK;
            }
            case UIA_FrameworkIdPropertyId:
                ret->vt = VT_BSTR;
                ret->bstrVal = SysAllocString(L"FluentUI");
                return S_OK;
            case UIA_IsEnabledPropertyId:
            case UIA_IsControlElementPropertyId:
            case UIA_IsContentElementPropertyId:
                ret->vt = VT_BOOL;
                ret->boolVal = VARIANT_TRUE;
                return S_OK;
            case UIA_IsKeyboardFocusablePropertyId:  // caption buttons take no focus
                ret->vt = VT_BOOL;
                ret->boolVal = VARIANT_FALSE;
                return S_OK;
            default: return S_OK;
        }
    }
    IFACEMETHODIMP get_HostRawElementProvider(IRawElementProviderSimple** ret) override {
        if (!ret) return E_POINTER;
        *ret = nullptr;
        return site_ ? S_OK : UIA_E_ELEMENTNOTAVAILABLE;
    }

    IFACEMETHODIMP Navigate(NavigateDirection dir, IRawElementProviderFragment** ret) override;
    IFACEMETHODIMP GetRuntimeId(SAFEARRAY** ret) override {
        if (!ret) return E_POINTER;
        const int ids[2] = {UiaAppendRuntimeId, static_cast<int>(serial_)};
        *ret = SafeArrayCreateVector(VT_I4, 0, 2);
        if (!*ret) return E_OUTOFMEMORY;
        for (LONG i = 0; i < 2; ++i) SafeArrayPutElement(*ret, &i, const_cast<int*>(&ids[i]));
        return S_OK;
    }
    IFACEMETHODIMP get_BoundingRectangle(UiaRect* ret) override {
        if (!ret) return E_POINTER;
        *ret = UiaRect{};
        if (!site_) return UIA_E_ELEMENTNOTAVAILABLE;
        const POINT o = site_->ClientOriginOnScreen();
        const ScreenRect r = ScreenRectFromHostDip(site_->CaptionRect(HitOfSlot(slot_)), o.x, o.y, site_->Dpi());
        *ret = UiaRect{r.left, r.top, r.width, r.height};
        return S_OK;
    }
    IFACEMETHODIMP GetEmbeddedFragmentRoots(SAFEARRAY** ret) override {
        if (!ret) return E_POINTER;
        *ret = nullptr;
        return S_OK;
    }
    IFACEMETHODIMP SetFocus() override { return S_OK; }
    IFACEMETHODIMP get_FragmentRoot(IRawElementProviderFragmentRoot** ret) override {
        if (!ret) return E_POINTER;
        *ret = nullptr;
        IUnknown* root = site_ ? site_->RootProvider() : nullptr;
        return root ? root->QueryInterface(IID_PPV_ARGS(ret)) : UIA_E_ELEMENTNOTAVAILABLE;
    }
    IFACEMETHODIMP Invoke() override {
        CheckProviderThread(ui_, "Caption.Invoke");
        if (!site_) return UIA_E_ELEMENTNOTAVAILABLE;
        site_->PostCaptionInvoke(HitOfSlot(slot_));  // closing the window inside a COM call: no
        return S_OK;
    }

private:
    ~CaptionButtonProvider() = default;
    std::atomic<ULONG> refs_{1};
    IAutomationSite* site_;
    const int slot_;
    const uint32_t serial_;
    const DWORD ui_;
};

HRESULT CaptionButtonProvider::Navigate(NavigateDirection dir, IRawElementProviderFragment** ret) {
    CheckProviderThread(ui_, "Caption.Navigate");
    if (!ret) return E_POINTER;
    *ret = nullptr;
    if (!site_) return UIA_E_ELEMENTNOTAVAILABLE;
    auto slotFrag = [&](int s) -> HRESULT {
        for (; s >= 0 && s < 3; s += (dir == NavigateDirection_NextSibling ? 1 : -1)) {
            if (IUnknown* p = site_->CaptionProvider(s)) return p->QueryInterface(IID_PPV_ARGS(ret));
        }
        return S_OK;
    };
    switch (dir) {
        case NavigateDirection_Parent: {
            IUnknown* root = site_->RootProvider();
            return root ? root->QueryInterface(IID_PPV_ARGS(ret)) : S_OK;
        }
        case NavigateDirection_NextSibling: {
            const HRESULT hr = slotFrag(slot_ + 1);
            if (*ret) return hr;
            return FragmentForElement(FirstExposedRoot(*site_), ret);  // after the buttons: elements
        }
        case NavigateDirection_PreviousSibling: return slot_ > 0 ? slotFrag(slot_ - 1) : S_OK;
        default: return S_OK;  // no children
    }
}

}  // namespace

// --- AutomationHost's caption-button side ----------------------------------------

RectDip AutomationHost::CaptionRect(int hit) const {
    return window_ ? window_->AutomationCaptionRect(hit) : RectDip{};
}

void AutomationHost::PostCaptionInvoke(int hit) {
    if (torn_down_) return;
    captionInvokes_.push_back(hit);
    RequestFlush();
}

IUnknown* AutomationHost::CaptionProvider(int slot) {
    if (slot < 0 || slot > 2 || torn_down_ || CaptionRect(HitOfSlot(slot)).isEmpty()) return nullptr;
    if (!captions_[slot]) {
        auto* p = new (std::nothrow) CaptionButtonProvider(this, slot);
        if (!p) return nullptr;
        captions_[slot].Attach(static_cast<IRawElementProviderSimple*>(p));
    }
    return captions_[slot].Get();
}

// Detach + disconnect the caption providers at teardown (called by the host).
void ReleaseCaptionProviders(IUiaCore& core, Microsoft::WRL::ComPtr<IUnknown> (&caps)[3]) {
    for (auto& c : caps) {
        if (!c) continue;
        if (!core.InSendMessage()) core.DisconnectProvider(c.Get());
        static_cast<CaptionButtonProvider*>(AsSimple(c.Get()))->Detach();
        c.Reset();
    }
}

}  // namespace fluent::automation
