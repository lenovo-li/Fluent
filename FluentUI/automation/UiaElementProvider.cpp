// UiaElementProvider.cpp — see UiaElementProvider.h.
#include "UiaElementProvider.h"

#include "AutomationGeometry.h"
#include "AutomationPeer.h"
#include "AutomationTree.h"
#include "ItemPeers.h"
#include "../core/UIElement.h"

namespace fluent::automation {
namespace {

// Longest Value compared for ValueChanged (see RaiseChangedProperties).
constexpr size_t kMaxComparedValue = 4096;

HRESULT ReturnString(const std::wstring& s, VARIANT* ret) {
    ret->vt = VT_BSTR;
    ret->bstrVal = SysAllocString(s.c_str());
    return ret->bstrVal ? S_OK : E_OUTOFMEMORY;
}
void ReturnBool(bool b, VARIANT* ret) {
    ret->vt = VT_BOOL;
    ret->boolVal = b ? VARIANT_TRUE : VARIANT_FALSE;
}

}  // namespace

ElementProvider::ElementProvider(AutomationPeer& peer)
    : peer_(&peer),
      serial_(peer.Serial()),
      uiThreadId_(GetCurrentThreadId()),
      hasInvoke_(peer.AsInvoke() != nullptr),
      hasToggle_(peer.AsToggle() != nullptr),
      hasValue_(peer.AsValue() != nullptr),
      hasSelectionItem_(peer.AsSelectionItem() != nullptr),
      hasRange_(peer.AsRangeValue() != nullptr),
      hasExpand_(peer.AsExpandCollapse() != nullptr),
      hasScroll_(peer.AsScroll() != nullptr),
      hasSelection_(peer.AsSelection() != nullptr),
      hasScrollItem_(peer.CanScrollIntoView()),
      hasText_(peer.AsText() != nullptr),
      hasItemContainer_(peer.AsItems() && !peer.ItemContainer() && peer.AsItems()->VirtualizesItems()),
      hasVirtualized_(peer.ItemContainer() && peer.ItemContainer()->AsItems() &&
                      peer.ItemContainer()->AsItems()->VirtualizesItems() && peer.CanScrollIntoView()) {}

ULONG ElementProvider::Release() {
    const ULONG left = --refs_;
    if (left == 0) delete this;  // touches no framework object (may run on a UIA thread)
    return left;
}

HRESULT ElementProvider::QueryInterface(REFIID riid, void** out) {
    if (!out) return E_POINTER;
    *out = nullptr;
    if (riid == __uuidof(IUnknown) || riid == __uuidof(IRawElementProviderSimple))
        *out = static_cast<IRawElementProviderSimple*>(this);
    else if (riid == __uuidof(IRawElementProviderFragment))
        *out = static_cast<IRawElementProviderFragment*>(this);
    else if (riid == __uuidof(IInvokeProvider) && hasInvoke_)
        *out = static_cast<IInvokeProvider*>(this);
    else if (riid == __uuidof(IToggleProvider) && hasToggle_)
        *out = static_cast<IToggleProvider*>(this);
    else if (riid == __uuidof(IValueProvider) && hasValue_)
        *out = static_cast<IValueProvider*>(this);
    else if (riid == __uuidof(ISelectionItemProvider) && hasSelectionItem_)
        *out = static_cast<ISelectionItemProvider*>(this);
    else if (riid == __uuidof(IRangeValueProvider) && hasRange_)
        *out = static_cast<IRangeValueProvider*>(this);
    else if (riid == __uuidof(IExpandCollapseProvider) && hasExpand_)
        *out = static_cast<IExpandCollapseProvider*>(this);
    else if (riid == __uuidof(IScrollProvider) && hasScroll_)
        *out = static_cast<IScrollProvider*>(this);
    else if (riid == __uuidof(ISelectionProvider) && hasSelection_)
        *out = static_cast<ISelectionProvider*>(this);
    else if (riid == __uuidof(IScrollItemProvider) && hasScrollItem_)
        *out = static_cast<IScrollItemProvider*>(this);
    else if (riid == __uuidof(ITextProvider) && hasText_)
        *out = static_cast<ITextProvider*>(this);
    else if (riid == __uuidof(IItemContainerProvider) && hasItemContainer_)
        *out = static_cast<IItemContainerProvider*>(this);
    else if (riid == __uuidof(IVirtualizedItemProvider) && hasVirtualized_)
        *out = static_cast<IVirtualizedItemProvider*>(this);
    else
        return E_NOINTERFACE;
    AddRef();
    return S_OK;
}

void ElementProvider::Freeze() {
    if (!peer_) return;
    if (IAutomationSite* site = LiveSite()) {
        if (IUnknown* root = site->RootProvider())
            root->QueryInterface(IID_PPV_ARGS(frozenRoot_.ReleaseAndGetAddressOf()));
    }
    peer_ = nullptr;
}

IAutomationSite* ElementProvider::LiveSite() const {
    return peer_ ? peer_->Owner().AutomationSite() : nullptr;
}

HRESULT ElementProvider::Post(PeerAction action, const wchar_t* value, double a, double b) {
    IAutomationSite* site = LiveSite();
    if (!site) return UIA_E_ELEMENTNOTAVAILABLE;
    if (!peer_->IsEnabled()) return UIA_E_ELEMENTNOTENABLED;
    site->PostAction(static_cast<IRawElementProviderSimple*>(this), action, value, a, b);
    return S_OK;
}

// --- IRawElementProviderSimple ------------------------------------------------

HRESULT ElementProvider::get_ProviderOptions(ProviderOptions* ret) {
    // Not thread-checked, for the reason given on the window root's version.
    if (!ret) return E_POINTER;
    *ret = static_cast<ProviderOptions>(ProviderOptions_ServerSideProvider |
                                        ProviderOptions_UseComThreading);
    return S_OK;
}

HRESULT ElementProvider::GetPatternProvider(PATTERNID id, IUnknown** ret) {
    CheckThread("GetPatternProvider");
    if (!ret) return E_POINTER;
    *ret = nullptr;
    if (!LiveSite()) return UIA_E_ELEMENTNOTAVAILABLE;
    void* p = nullptr;
    if (id == UIA_InvokePatternId) QueryInterface(__uuidof(IInvokeProvider), &p);
    else if (id == UIA_TogglePatternId) QueryInterface(__uuidof(IToggleProvider), &p);
    else if (id == UIA_ValuePatternId) QueryInterface(__uuidof(IValueProvider), &p);
    else if (id == UIA_SelectionItemPatternId) QueryInterface(__uuidof(ISelectionItemProvider), &p);
    else if (id == UIA_RangeValuePatternId) QueryInterface(__uuidof(IRangeValueProvider), &p);
    else if (id == UIA_ExpandCollapsePatternId) QueryInterface(__uuidof(IExpandCollapseProvider), &p);
    else if (id == UIA_ScrollPatternId) QueryInterface(__uuidof(IScrollProvider), &p);
    else if (id == UIA_SelectionPatternId) QueryInterface(__uuidof(ISelectionProvider), &p);
    else if (id == UIA_ScrollItemPatternId) QueryInterface(__uuidof(IScrollItemProvider), &p);
    else if (id == UIA_TextPatternId) QueryInterface(__uuidof(ITextProvider), &p);
    else if (id == UIA_ItemContainerPatternId) QueryInterface(__uuidof(IItemContainerProvider), &p);
    else if (id == UIA_VirtualizedItemPatternId) QueryInterface(__uuidof(IVirtualizedItemProvider), &p);
    *ret = static_cast<IUnknown*>(p);  // null (S_OK) = pattern not supported
    return S_OK;
}


HRESULT ElementProvider::GetPropertyValue(PROPERTYID id, VARIANT* ret) {
    CheckThread("GetPropertyValue");
    if (!ret) return E_POINTER;
    VariantInit(ret);
    IAutomationSite* site = LiveSite();
    if (!site) return UIA_E_ELEMENTNOTAVAILABLE;
    AutomationPeer& peer = *peer_;
    switch (id) {
        case UIA_ControlTypePropertyId:
            ret->vt = VT_I4;
            ret->lVal = static_cast<LONG>(peer.GetControlType());
            return S_OK;
        case UIA_NamePropertyId: return ReturnString(peer.GetName(), ret);
        case UIA_AutomationIdPropertyId: {
            const std::wstring id = peer.GetAutomationId();
            return id.empty() ? S_OK : ReturnString(id, ret);
        }
        case UIA_FrameworkIdPropertyId: return ReturnString(L"FluentUI", ret);
        case UIA_HelpTextPropertyId: {
            const std::wstring h = peer.GetHelpText();
            return h.empty() ? S_OK : ReturnString(h, ret);
        }
        case UIA_IsEnabledPropertyId: ReturnBool(peer.IsEnabled(), ret); return S_OK;
        case UIA_IsKeyboardFocusablePropertyId: ReturnBool(peer.IsKeyboardFocusable(), ret); return S_OK;
        case UIA_HasKeyboardFocusPropertyId:
            // The focus manager keeps its element while the window is inactive (so it
            // can restore it); UIA must only report focus the user actually has.
            ReturnBool(peer.HasFocusWithin() && site->HostHasOsFocus(), ret);
            return S_OK;
        case UIA_IsControlElementPropertyId: ReturnBool(peer.IsControlElement(), ret); return S_OK;
        case UIA_IsContentElementPropertyId: ReturnBool(peer.IsContentElement(), ret); return S_OK;
        case UIA_IsPasswordPropertyId: ReturnBool(peer.IsPassword(), ret); return S_OK;
        case UIA_IsOffscreenPropertyId:
            ReturnBool(peer.IsOffscreen(), ret);
            return S_OK;
        case UIA_IsDialogPropertyId:
            if (!peer.IsDialog()) return S_OK;  // UIA default: false
            ReturnBool(true, ret);
            return S_OK;
        case UIA_LiveSettingPropertyId:
            if (peer.GetLiveSetting() == LiveSetting::Off) return S_OK;  // UIA default: Off
            ret->vt = VT_I4;
            ret->lVal = static_cast<LONG>(peer.GetLiveSetting());
            return S_OK;
        default: return S_OK;  // VT_EMPTY: UIA supplies its default
    }
}

HRESULT ElementProvider::get_HostRawElementProvider(IRawElementProviderSimple** ret) {
    CheckThread("get_HostRawElementProvider");
    if (!ret) return E_POINTER;
    *ret = nullptr;  // not an HWND root: no host provider
    return LiveSite() ? S_OK : UIA_E_ELEMENTNOTAVAILABLE;
}

// --- IRawElementProviderFragment ----------------------------------------------

HRESULT ElementProvider::Navigate(NavigateDirection dir, IRawElementProviderFragment** ret) {
    CheckThread("Navigate");
    if (!ret) return E_POINTER;
    *ret = nullptr;
    IAutomationSite* site = LiveSite();
    if (!site) return UIA_E_ELEMENTNOTAVAILABLE;
    AutomationPeer& peer = *peer_;
    UIElement& e = peer.Owner();
    AutomationPeer* container = peer.ItemContainer();  // non-null: this is an item
    IItemsPeer* items = peer.AsItems();                 // non-null: this lists items
    switch (dir) {
        case NavigateDirection_Parent:
            if (container) return FragmentForPeer(container, ret);
            if (UIElement* p = ExposedParent(e)) return FragmentForElement(p, ret);
            if (IUnknown* root = site->RootProvider())
                return root->QueryInterface(IID_PPV_ARGS(ret));
            return S_OK;
        case NavigateDirection_FirstChild:
            // An item's children are only its own items (a grid row's cells); the
            // owner element's children belong to the container that lists the item.
            if (items && items->ExposedEnd() > items->ExposedBegin())
                return FragmentForPeer(items->ItemAt(items->ExposedBegin()), ret);
            if (container) return S_OK;
            return FragmentForElement(FirstExposedChild(e), ret);
        case NavigateDirection_LastChild:
            if (container) {
                return items && items->ExposedEnd() > items->ExposedBegin()
                           ? FragmentForPeer(items->ItemAt(items->ExposedEnd() - 1), ret)
                           : S_OK;
            }
            if (UIElement* last = LastExposedChild(e)) return FragmentForElement(last, ret);
            if (items && items->ExposedEnd() > items->ExposedBegin())
                return FragmentForPeer(items->ItemAt(items->ExposedEnd() - 1), ret);
            return S_OK;
        case NavigateDirection_NextSibling:
        case NavigateDirection_PreviousSibling: {
            const bool next = dir == NavigateDirection_NextSibling;
            if (container) {
                // Within the container's items, then on into its element children.
                IItemsPeer* list = container->AsItems();
                const size_t i = peer.IndexInContainer();
                if (next) {
                    if (i + 1 < list->ExposedEnd()) return FragmentForPeer(list->ItemAt(i + 1), ret);
                    // Element children follow a top-level container's items; a nested
                    // item list (a row's cells) has none after it.
                    if (container->ItemContainer()) return S_OK;
                    return FragmentForElement(FirstExposedChild(container->Owner()), ret);
                }
                return (i > list->ExposedBegin() && i < list->ItemCount())
                           ? FragmentForPeer(list->ItemAt(i - 1), ret)
                           : S_OK;
            }
            size_t at = 0;
            UIElement* s = ExposedSibling(*site, e, next ? +1 : -1, siblingHint_, &at);
            if (!s && !next && !ExposedParent(e)) {
                // The first window root's previous siblings are the caption buttons.
                for (int c = 2; c >= 0; --c)
                    if (IUnknown* cp = site->CaptionProvider(c)) return cp->QueryInterface(IID_PPV_ARGS(ret));
                return S_OK;
            }
            if (!s && !next) {
                // First element child of a container that also lists items: the
                // previous sibling is its last item.
                UIElement* p = ExposedParent(e);
                AutomationPeer* pp = p ? p->AutomationPeerIfAny() : nullptr;
                IItemsPeer* pl = pp ? pp->AsItems() : nullptr;
                if (pl && pl->ExposedEnd() > pl->ExposedBegin())
                    return FragmentForPeer(pl->ItemAt(pl->ExposedEnd() - 1), ret);
            }
            const HRESULT hr = FragmentForElement(s, ret);
            // Seed the sibling's own hint so the walk's next step starts there.
            if (*ret) {
                // s has a peer now (FragmentForElement created it); its provider is ours.
                IUnknown* sp = s->AutomationPeerIfAny()->Provider();
                static_cast<ElementProvider*>(AsSimple(sp))->SetSiblingHint(at);
            }
            return hr;
        }
        default: return S_OK;
    }
}

HRESULT ElementProvider::GetRuntimeId(SAFEARRAY** ret) {
    CheckThread("GetRuntimeId");
    if (!ret) return E_POINTER;
    // Answered even when frozen: UIA core identifies the provider by it while
    // disconnecting. The serial is never reused, unlike the element address.
    const int ids[2] = {UiaAppendRuntimeId, static_cast<int>(serial_)};
    *ret = SafeArrayCreateVector(VT_I4, 0, 2);
    if (!*ret) return E_OUTOFMEMORY;
    for (LONG i = 0; i < 2; ++i) SafeArrayPutElement(*ret, &i, const_cast<int*>(&ids[i]));
    return S_OK;
}

HRESULT ElementProvider::get_BoundingRectangle(UiaRect* ret) {
    CheckThread("get_BoundingRectangle");
    if (!ret) return E_POINTER;
    *ret = UiaRect{};
    IAutomationSite* site = LiveSite();
    if (!site) return UIA_E_ELEMENTNOTAVAILABLE;
    const POINT o = site->ClientOriginOnScreen();
    const ScreenRect r =
        ScreenRectFromHostDip(peer_->BoundsInHostDip(), o.x, o.y, site->Dpi());
    *ret = UiaRect{r.left, r.top, r.width, r.height};  // not clipped (UIA convention)
    return S_OK;
}

HRESULT ElementProvider::GetEmbeddedFragmentRoots(SAFEARRAY** ret) {
    CheckThread("GetEmbeddedFragmentRoots");
    if (!ret) return E_POINTER;
    *ret = nullptr;
    return LiveSite() ? S_OK : UIA_E_ELEMENTNOTAVAILABLE;
}

HRESULT ElementProvider::SetFocus() {
    CheckThread("SetFocus");
    return Post(PeerAction::SetFocus);
}

HRESULT ElementProvider::get_FragmentRoot(IRawElementProviderFragmentRoot** ret) {
    CheckThread("get_FragmentRoot");
    if (!ret) return E_POINTER;
    *ret = nullptr;
    if (IAutomationSite* site = LiveSite()) {
        IUnknown* root = site->RootProvider();
        return root ? root->QueryInterface(IID_PPV_ARGS(ret)) : S_OK;
    }
    // Frozen: answer from the root captured at freeze (see the header).
    if (frozenRoot_) {
        *ret = frozenRoot_.Get();
        (*ret)->AddRef();
        return S_OK;
    }
    return UIA_E_ELEMENTNOTAVAILABLE;
}


// --- Patterns -----------------------------------------------------------------

HRESULT ElementProvider::Invoke() { CheckThread("Invoke"); return Post(PeerAction::Invoke); }
HRESULT ElementProvider::Toggle() { CheckThread("Toggle"); return Post(PeerAction::Toggle); }
HRESULT ElementProvider::get_ToggleState(::ToggleState* ret) {
    CheckThread("get_ToggleState");
    if (!ret) return E_POINTER;
    ITogglePeer* t = LiveSite() ? peer_->AsToggle() : nullptr;
    if (!t) return UIA_E_ELEMENTNOTAVAILABLE;
    *ret = static_cast<::ToggleState>(t->GetToggleState());
    return S_OK;
}
HRESULT ElementProvider::SetValue(LPCWSTR val) {
    CheckThread("SetValue");
    IValuePeer* v = LiveSite() ? peer_->AsValue() : nullptr;
    if (!v) return UIA_E_ELEMENTNOTAVAILABLE;
    if (v->IsReadOnly()) return UIA_E_ELEMENTNOTENABLED;
    return Post(PeerAction::SetValue, val ? val : L"");
}
HRESULT ElementProvider::get_Value(BSTR* ret) {
    CheckThread("get_Value");
    if (!ret) return E_POINTER;
    IValuePeer* v = LiveSite() ? peer_->AsValue() : nullptr;
    if (!v) return UIA_E_ELEMENTNOTAVAILABLE;
    *ret = SysAllocString(v->GetValue().c_str());
    return *ret ? S_OK : E_OUTOFMEMORY;
}
HRESULT ElementProvider::get_IsReadOnly(BOOL* ret) {
    // One method serves IValueProvider and IRangeValueProvider (identical
    // signatures). A peer with both reports read-only if either is.
    CheckThread("get_IsReadOnly");
    if (!ret) return E_POINTER;
    if (!LiveSite()) return UIA_E_ELEMENTNOTAVAILABLE;
    IValuePeer* v = peer_->AsValue();
    IRangeValuePeer* r = peer_->AsRangeValue();
    if (!v && !r) return UIA_E_ELEMENTNOTAVAILABLE;
    *ret = ((v && v->IsReadOnly()) || (r && r->IsReadOnly())) ? TRUE : FALSE;
    return S_OK;
}
HRESULT ElementProvider::Select() { CheckThread("Select"); return Post(PeerAction::Select); }
// Single-select items: adding is selecting, removing is refused. Multi-select
// containers' items (CanAddRemove) post the real add/remove.
HRESULT ElementProvider::AddToSelection() {
    CheckThread("AddToSelection");
    ISelectionItemPeer* si = LiveSite() ? peer_->AsSelectionItem() : nullptr;
    if (!si) return UIA_E_ELEMENTNOTAVAILABLE;
    return Post(si->CanAddRemove() ? PeerAction::AddToSelection : PeerAction::Select);
}
HRESULT ElementProvider::RemoveFromSelection() {
    CheckThread("RemoveFromSelection");
    ISelectionItemPeer* si = LiveSite() ? peer_->AsSelectionItem() : nullptr;
    if (!si) return UIA_E_ELEMENTNOTAVAILABLE;
    return si->CanAddRemove() ? Post(PeerAction::RemoveFromSelection) : UIA_E_INVALIDOPERATION;
}
HRESULT ElementProvider::get_IsSelected(BOOL* ret) {
    CheckThread("get_IsSelected");
    if (!ret) return E_POINTER;
    ISelectionItemPeer* s = LiveSite() ? peer_->AsSelectionItem() : nullptr;
    if (!s) return UIA_E_ELEMENTNOTAVAILABLE;
    *ret = s->IsSelected() ? TRUE : FALSE;
    return S_OK;
}
HRESULT ElementProvider::get_SelectionContainer(IRawElementProviderSimple** ret) {
    CheckThread("get_SelectionContainer");
    if (!ret) return E_POINTER;
    *ret = nullptr;  // null for a RadioButton: its group is an int*, not an element
    ISelectionItemPeer* si = LiveSite() ? peer_->AsSelectionItem() : nullptr;
    if (!si) return UIA_E_ELEMENTNOTAVAILABLE;
    AutomationPeer* c = si->SelectionContainer();
    IUnknown* prov = c ? c->Provider() : nullptr;
    return prov ? prov->QueryInterface(IID_PPV_ARGS(ret)) : S_OK;
}

// --- Free functions (UiaProvider.h / UiaInternal.h) ---------------------------

namespace {
// Every element-side IUnknown is an ElementProvider's IRawElementProviderSimple
// base: the host only ever receives providers from AutomationPeer::Provider().
ElementProvider* ElementFrom(IUnknown* p) {
    return p ? static_cast<ElementProvider*>(AsSimple(p)) : nullptr;
}
}  // namespace

HRESULT CreateElementProvider(AutomationPeer& peer, IUnknown** out) {
    if (!out) return E_POINTER;
    auto* p = new (std::nothrow) ElementProvider(peer);
    *out = p ? static_cast<IRawElementProviderSimple*>(p) : nullptr;
    // Baseline for property-changed events: the first change after creation is
    // raised against the state the client could have seen.
    if (p) RaiseChangedProperties(nullptr, *out);
    return p ? S_OK : E_OUTOFMEMORY;
}

void FreezeProvider(IUnknown* provider) {
    if (ElementProvider* p = ElementFrom(provider)) p->Freeze();
}

AutomationPeer* PeerFromProvider(IUnknown* provider) {
    ElementProvider* p = ElementFrom(provider);
    return p ? p->Peer() : nullptr;
}

HRESULT FragmentForPeer(AutomationPeer* peer, IRawElementProviderFragment** out) {
    *out = nullptr;
    IUnknown* prov = peer ? peer->Provider() : nullptr;
    return prov ? prov->QueryInterface(IID_PPV_ARGS(out)) : S_OK;
}

HRESULT FragmentForElement(UIElement* e, IRawElementProviderFragment** out) {
    *out = nullptr;
    if (!e) return S_OK;
    AutomationPeer* peer = e->GetOrCreateAutomationPeer();
    IUnknown* prov = peer ? peer->Provider() : nullptr;
    return prov ? prov->QueryInterface(IID_PPV_ARGS(out)) : S_OK;
}


void RunPeerAction(IUnknown* provider, PeerAction action, const std::wstring& value,
                   double a, double b) {
    AutomationPeer* peer = PeerFromProvider(provider);
    // Re-checked here, after the COM call returned: the element may have been
    // disabled, detached or destroyed in between (then the provider is frozen).
    if (!peer || !peer->IsEnabled()) return;
    switch (action) {
        case PeerAction::Invoke:
            if (IInvokePeer* i = peer->AsInvoke()) i->Invoke();
            break;
        case PeerAction::Toggle:
            if (ITogglePeer* t = peer->AsToggle()) t->Toggle();
            break;
        case PeerAction::Select:
            if (ISelectionItemPeer* s = peer->AsSelectionItem()) s->Select();
            break;
        case PeerAction::SetValue:
            if (IValuePeer* v = peer->AsValue(); v && !v->IsReadOnly()) v->SetValue(value);
            break;
        case PeerAction::SetFocus: peer->SetFocusFromAutomation(); break;
        case PeerAction::SetRangeValue:
            if (IRangeValuePeer* r = peer->AsRangeValue(); r && !r->IsReadOnly()) r->SetValue(a);
            break;
        case PeerAction::Expand:
            if (IExpandCollapsePeer* x = peer->AsExpandCollapse()) x->Expand();
            break;
        case PeerAction::Collapse:
            if (IExpandCollapsePeer* x = peer->AsExpandCollapse()) x->Collapse();
            break;
        case PeerAction::SetScroll:
            if (IScrollPeer* sc = peer->AsScroll()) sc->SetScrollPercent(a, b);
            break;
        case PeerAction::AddToSelection:
            if (ISelectionItemPeer* si = peer->AsSelectionItem()) si->AddToSelection();
            break;
        case PeerAction::RemoveFromSelection:
            if (ISelectionItemPeer* si = peer->AsSelectionItem()) si->RemoveFromSelection();
            break;
        case PeerAction::ScrollIntoView:
            if (peer->CanScrollIntoView()) peer->ScrollIntoView();
            break;
        case PeerAction::SelectText:
            if (ITextPeer* t = peer->AsText())
                t->SelectText(static_cast<size_t>(a), static_cast<size_t>(b));
            break;
    }
}

void RaiseChangedProperties(IUiaCore* core, IUnknown* provider) {
    ElementProvider* p = ElementFrom(provider);
    AutomationPeer* peer = p ? p->Peer() : nullptr;
    if (!peer) return;
    ElementProvider::Reported& was = p->reported;
    ElementProvider::Reported now;
    now.valid = true;
    now.enabled = peer->IsEnabled();
    now.name = peer->GetName();
    if (ITogglePeer* t = peer->AsToggle()) now.toggle = static_cast<int>(t->GetToggleState());
    if (IExpandCollapsePeer* x = peer->AsExpandCollapse()) now.expand = static_cast<int>(x->GetExpandState());
    if (IRangeValuePeer* r = peer->AsRangeValue()) {
        now.hasRange = true;
        now.range = r->GetValue();
    }
    if (ISelectionPeer* sel = peer->AsSelection()) {
        now.selected = sel->SelectedIndex();
        now.selectedCount = sel->SelectedCount();
    }
    if (ISelectionItemPeer* si = peer->AsSelectionItem()) now.isSelected = si->IsSelected() ? 1 : 0;
    // A password's value is never reported, so it can never be raised either. A
    // multi-line document does not raise Value changes at all (that is TextPattern's
    // job), and a long single-line value is not compared: either would copy the
    // whole text on every keystroke and caret blink while a client listens.
    if (IValuePeer* v = peer->AsValue(); v && !peer->IsPassword() && v->ReportsValueChanges()) {
        std::wstring value = v->GetValue();
        if (value.size() <= kMaxComparedValue) now.value = std::move(value);
        else now.value = was.value;  // too long to diff: not raised
    }
    if (was.valid && core) {
        if (was.enabled != now.enabled)
            core->RaiseBoolChanged(provider, UIA_IsEnabledPropertyId, was.enabled, now.enabled);
        if (was.name != now.name)
            core->RaiseStringChanged(provider, UIA_NamePropertyId, was.name.c_str(), now.name.c_str());
        if (was.toggle != now.toggle && now.toggle >= 0)
            core->RaiseIntChanged(provider, UIA_ToggleToggleStatePropertyId, was.toggle, now.toggle);
        if (was.value != now.value)
            core->RaiseStringChanged(provider, UIA_ValueValuePropertyId, was.value.c_str(),
                                    now.value.c_str());
        if (was.expand != now.expand && now.expand >= 0)
            core->RaiseIntChanged(provider, UIA_ExpandCollapseExpandCollapseStatePropertyId,
                                  was.expand, now.expand);
        if (was.isSelected != now.isSelected && now.isSelected >= 0)
            core->RaiseBoolChanged(provider, UIA_SelectionItemIsSelectedPropertyId,
                                   was.isSelected == 1, now.isSelected == 1);
        if (now.hasRange && was.range != now.range)
            core->RaiseDoubleChanged(provider, UIA_RangeValueValuePropertyId, was.range, now.range);
        if (was.selected != now.selected || was.selectedCount != now.selectedCount) {
            // One newly selected item: ElementSelected on it (what Narrator speaks).
            // Anything bigger (a range, a clear): Selection_Invalidated on the
            // container, so a 1M-row Shift+End never raises 1M events.
            IItemsPeer* items = peer->AsItems();
            AutomationPeer* item = (items && now.selectedCount == 1 && now.selected < items->ItemCount())
                                       ? items->ItemAt(now.selected) : nullptr;
            if (item) core->RaiseEvent(item->Provider(), UIA_SelectionItem_ElementSelectedEventId);
            else core->RaiseEvent(provider, UIA_Selection_InvalidatedEventId);
        }
    }
    was = std::move(now);
}

}  // namespace fluent::automation
