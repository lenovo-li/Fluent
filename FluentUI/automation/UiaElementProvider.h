// UiaElementProvider.h — the COM provider for one AutomationPeer. INTERNAL: only
// automation/*.cpp include it (it pulls in <UIAutomation.h> via UiaInternal.h).
//
// The provider owns no semantics: every answer comes from the peer, converted to
// UIA types. Pattern interfaces are implemented on this one object and handed out
// only when the peer supports them (GetPatternProvider), so UIA sees the same
// identity for the element and its patterns.
//
// FROZEN STATE. When the element detaches or dies, the peer freezes the provider
// (peer_ = null) and queues a UIA disconnect. A frozen provider must still answer
// what UIA core asks during that disconnect: ProviderOptions, GetRuntimeId and
// get_FragmentRoot are served from values captured at creation / freeze; every
// other method returns UIA_E_ELEMENTNOTAVAILABLE. (The window root showed the
// failure mode when it answered nothing: UiaDisconnectProvider returned
// UIA_E_ELEMENTNOTAVAILABLE and the disconnect did not complete.)
#pragma once

#include "UiaInternal.h"

namespace fluent::automation {

class ElementProvider final : public IRawElementProviderSimple,
                              public IRawElementProviderFragment,
                              public IInvokeProvider,
                              public IToggleProvider,
                              public IValueProvider,
                              public ISelectionItemProvider {
public:
    explicit ElementProvider(AutomationPeer& peer);

    void Freeze();
    AutomationPeer* Peer() const { return peer_; }
    void SetSiblingHint(size_t i) { siblingHint_ = i; }

    // Last values raised as property-changed events (see RaiseChangedProperties).
    struct Reported {
        bool valid = false;
        bool enabled = false;
        int toggle = -1;  // -1 = no Toggle pattern
        std::wstring name, value;
    } reported;

    // IUnknown
    IFACEMETHODIMP_(ULONG) AddRef() override { return ++refs_; }
    IFACEMETHODIMP_(ULONG) Release() override;
    IFACEMETHODIMP QueryInterface(REFIID riid, void** out) override;

    // IRawElementProviderSimple
    IFACEMETHODIMP get_ProviderOptions(ProviderOptions* ret) override;
    IFACEMETHODIMP GetPatternProvider(PATTERNID id, IUnknown** ret) override;
    IFACEMETHODIMP GetPropertyValue(PROPERTYID id, VARIANT* ret) override;
    IFACEMETHODIMP get_HostRawElementProvider(IRawElementProviderSimple** ret) override;

    // IRawElementProviderFragment
    IFACEMETHODIMP Navigate(NavigateDirection dir, IRawElementProviderFragment** ret) override;
    IFACEMETHODIMP GetRuntimeId(SAFEARRAY** ret) override;
    IFACEMETHODIMP get_BoundingRectangle(UiaRect* ret) override;
    IFACEMETHODIMP GetEmbeddedFragmentRoots(SAFEARRAY** ret) override;
    IFACEMETHODIMP SetFocus() override;
    IFACEMETHODIMP get_FragmentRoot(IRawElementProviderFragmentRoot** ret) override;

    // Patterns. Actions are POSTED (AutomationSite.h); getters read the peer now.
    IFACEMETHODIMP Invoke() override;
    IFACEMETHODIMP Toggle() override;
    IFACEMETHODIMP get_ToggleState(::ToggleState* ret) override;
    IFACEMETHODIMP SetValue(LPCWSTR val) override;
    IFACEMETHODIMP get_Value(BSTR* ret) override;
    IFACEMETHODIMP get_IsReadOnly(BOOL* ret) override;
    IFACEMETHODIMP Select() override;
    IFACEMETHODIMP AddToSelection() override;
    IFACEMETHODIMP RemoveFromSelection() override;
    IFACEMETHODIMP get_IsSelected(BOOL* ret) override;
    IFACEMETHODIMP get_SelectionContainer(IRawElementProviderSimple** ret) override;

private:
    ~ElementProvider() = default;
    void CheckThread(const char* m) const { CheckProviderThread(uiThreadId_, m); }
    // Live = not frozen and the element still attached to a site.
    IAutomationSite* LiveSite() const;
    HRESULT Post(PeerAction action, const wchar_t* value = nullptr);

    std::atomic<ULONG> refs_{1};
    AutomationPeer* peer_;  // null once frozen
    const uint32_t serial_;
    const DWORD uiThreadId_;
    // The window root, captured at freeze so a frozen provider can still answer
    // get_FragmentRoot without reaching the (possibly gone) site.
    ComPtr<IRawElementProviderFragmentRoot> frozenRoot_;
    size_t siblingHint_ = 0;
    // Pattern support, fixed at creation: COM requires an object's interface set to
    // stay constant, so QueryInterface answers from these even after freeze (the
    // pattern methods themselves then return UIA_E_ELEMENTNOTAVAILABLE).
    const bool hasInvoke_, hasToggle_, hasValue_, hasSelectionItem_;
};

}  // namespace fluent::automation
