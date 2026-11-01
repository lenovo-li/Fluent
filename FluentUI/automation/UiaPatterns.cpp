// UiaPatterns.cpp — ElementProvider's RangeValue, ExpandCollapse, Scroll, Selection
// and ScrollItem patterns. Same rules as the patterns in UiaElementProvider.cpp:
// getters read the peer now; actions are posted (AutomationSite.h) and the method
// returns S_OK once queued; a frozen provider returns UIA_E_ELEMENTNOTAVAILABLE.
#include "UiaElementProvider.h"

#include "AutomationPeer.h"
#include "ItemPeers.h"

#include <algorithm>
#include <vector>

namespace fluent::automation {

// --- RangeValue ----------------------------------------------------------------

HRESULT ElementProvider::SetValue(double val) {
    CheckThread("RangeValue.SetValue");
    IRangeValuePeer* r = LiveSite() ? peer_->AsRangeValue() : nullptr;
    if (!r) return UIA_E_ELEMENTNOTAVAILABLE;
    if (r->IsReadOnly()) return UIA_E_ELEMENTNOTENABLED;
    // UIA: out-of-range is an argument error, not a silent clamp.
    if (val < r->GetMinimum() || val > r->GetMaximum()) return E_INVALIDARG;
    return Post(PeerAction::SetRangeValue, nullptr, val);
}

#define FL_RANGE_GETTER(Method, Call)                                        \
    HRESULT ElementProvider::Method(double* ret) {                           \
        CheckThread(#Method);                                                \
        if (!ret) return E_POINTER;                                          \
        IRangeValuePeer* r = LiveSite() ? peer_->AsRangeValue() : nullptr;   \
        if (!r) return UIA_E_ELEMENTNOTAVAILABLE;                            \
        *ret = r->Call();                                                    \
        return S_OK;                                                         \
    }
FL_RANGE_GETTER(get_Value, GetValue)
FL_RANGE_GETTER(get_Maximum, GetMaximum)
FL_RANGE_GETTER(get_Minimum, GetMinimum)
FL_RANGE_GETTER(get_LargeChange, GetLargeChange)
FL_RANGE_GETTER(get_SmallChange, GetSmallChange)
#undef FL_RANGE_GETTER

// --- ExpandCollapse ------------------------------------------------------------

HRESULT ElementProvider::Expand() {
    CheckThread("Expand");
    IExpandCollapsePeer* x = LiveSite() ? peer_->AsExpandCollapse() : nullptr;
    if (!x) return UIA_E_ELEMENTNOTAVAILABLE;
    if (x->GetExpandState() == ExpandState::LeafNode) return UIA_E_INVALIDOPERATION;
    return Post(PeerAction::Expand);
}
HRESULT ElementProvider::Collapse() {
    CheckThread("Collapse");
    IExpandCollapsePeer* x = LiveSite() ? peer_->AsExpandCollapse() : nullptr;
    if (!x) return UIA_E_ELEMENTNOTAVAILABLE;
    if (x->GetExpandState() == ExpandState::LeafNode) return UIA_E_INVALIDOPERATION;
    return Post(PeerAction::Collapse);
}
HRESULT ElementProvider::get_ExpandCollapseState(ExpandCollapseState* ret) {
    CheckThread("get_ExpandCollapseState");
    if (!ret) return E_POINTER;
    IExpandCollapsePeer* x = LiveSite() ? peer_->AsExpandCollapse() : nullptr;
    if (!x) return UIA_E_ELEMENTNOTAVAILABLE;
    *ret = static_cast<ExpandCollapseState>(x->GetExpandState());
    return S_OK;
}

// --- Scroll --------------------------------------------------------------------

HRESULT ElementProvider::Scroll(ScrollAmount h, ScrollAmount v) {
    CheckThread("Scroll");
    IScrollPeer* sc = LiveSite() ? peer_->AsScroll() : nullptr;
    if (!sc) return UIA_E_ELEMENTNOTAVAILABLE;
    // A request on an axis that cannot scroll is an error per the Scroll pattern.
    const double ch = sc->HorizontalPercent(), cv = sc->VerticalPercent();
    if ((h != ScrollAmount_NoAmount && ch < 0) || (v != ScrollAmount_NoAmount && cv < 0))
        return UIA_E_INVALIDOPERATION;
    // Amounts -> percent steps. Percent is of the scrollable range (extent - view),
    // view size is a percent of the extent, so one viewport is
    // view / (100 - view) * 100 percent of the range. A line is a tenth of a page.
    auto step = [](ScrollAmount a, double cur, double view) -> double {
        if (cur < 0) return -1;  // axis not scrollable: leave it
        const double page = (view > 0 && view < 100) ? std::min(100.0, view / (100.0 - view) * 100.0)
                                                     : 10.0;
        const double line = page / 10.0;
        switch (a) {
            case ScrollAmount_LargeDecrement: return std::max(0.0, cur - page);
            case ScrollAmount_SmallDecrement: return std::max(0.0, cur - line);
            case ScrollAmount_LargeIncrement: return std::min(100.0, cur + page);
            case ScrollAmount_SmallIncrement: return std::min(100.0, cur + line);
            default: return -1;
        }
    };
    const double nh = step(h, ch, sc->HorizontalViewSize());
    const double nv = step(v, cv, sc->VerticalViewSize());
    return Post(PeerAction::SetScroll, nullptr, nh, nv);
}
HRESULT ElementProvider::SetScrollPercent(double h, double v) {
    CheckThread("SetScrollPercent");
    IScrollPeer* sc = LiveSite() ? peer_->AsScroll() : nullptr;
    if (!sc) return UIA_E_ELEMENTNOTAVAILABLE;
    // UIA_ScrollPatternNoScroll (-1) means "leave that axis".
    const bool badH = h != UIA_ScrollPatternNoScroll && (h < 0 || h > 100);
    const bool badV = v != UIA_ScrollPatternNoScroll && (v < 0 || v > 100);
    if (badH || badV) return E_INVALIDARG;
    if ((h != UIA_ScrollPatternNoScroll && sc->HorizontalPercent() < 0) ||
        (v != UIA_ScrollPatternNoScroll && sc->VerticalPercent() < 0))
        return UIA_E_INVALIDOPERATION;
    return Post(PeerAction::SetScroll, nullptr, h, v);
}
#define FL_SCROLL_GETTER(Method, Call)                                       \
    HRESULT ElementProvider::Method(double* ret) {                           \
        CheckThread(#Method);                                                \
        if (!ret) return E_POINTER;                                          \
        IScrollPeer* sc = LiveSite() ? peer_->AsScroll() : nullptr;          \
        if (!sc) return UIA_E_ELEMENTNOTAVAILABLE;                           \
        *ret = sc->Call();                                                   \
        return S_OK;                                                         \
    }
FL_SCROLL_GETTER(get_HorizontalScrollPercent, HorizontalPercent)
FL_SCROLL_GETTER(get_VerticalScrollPercent, VerticalPercent)
FL_SCROLL_GETTER(get_HorizontalViewSize, HorizontalViewSize)
FL_SCROLL_GETTER(get_VerticalViewSize, VerticalViewSize)
#undef FL_SCROLL_GETTER
HRESULT ElementProvider::get_HorizontallyScrollable(BOOL* ret) {
    CheckThread("get_HorizontallyScrollable");
    if (!ret) return E_POINTER;
    IScrollPeer* sc = LiveSite() ? peer_->AsScroll() : nullptr;
    if (!sc) return UIA_E_ELEMENTNOTAVAILABLE;
    *ret = sc->HorizontalPercent() >= 0 ? TRUE : FALSE;
    return S_OK;
}
HRESULT ElementProvider::get_VerticallyScrollable(BOOL* ret) {
    CheckThread("get_VerticallyScrollable");
    if (!ret) return E_POINTER;
    IScrollPeer* sc = LiveSite() ? peer_->AsScroll() : nullptr;
    if (!sc) return UIA_E_ELEMENTNOTAVAILABLE;
    *ret = sc->VerticalPercent() >= 0 ? TRUE : FALSE;
    return S_OK;
}


// --- Selection (container) -------------------------------------------------------

HRESULT ElementProvider::GetSelection(SAFEARRAY** ret) {
    CheckThread("GetSelection");
    if (!ret) return E_POINTER;
    *ret = nullptr;
    // Shared by ISelectionProvider and ITextProvider (identical signatures).
    if (LiveSite() && !peer_->AsSelection() && peer_->AsText()) return TextSelection(ret);
    ISelectionPeer* sel = LiveSite() ? peer_->AsSelection() : nullptr;
    IItemsPeer* items = peer_ ? peer_->AsItems() : nullptr;
    if (!sel || !items) return UIA_E_ELEMENTNOTAVAILABLE;
    // Index-scan with a cap: a 1M-row multi-selection reports its first
    // kMaxReported items rather than creating a provider per selected row.
    std::vector<ComPtr<IUnknown>> out;
    const size_t n = items->ItemCount();
    if (!sel->CanSelectMultiple()) {
        const size_t i = sel->SelectedIndex();
        if (i < n) {
            if (AutomationPeer* it = items->ItemAt(i)) out.emplace_back(it->Provider());
        }
    } else {
        for (size_t i = 0; i < n && out.size() < ISelectionPeer::kMaxReported; ++i)
            if (sel->IsIndexSelected(i))
                if (AutomationPeer* it = items->ItemAt(i)) out.emplace_back(it->Provider());
    }
    *ret = SafeArrayCreateVector(VT_UNKNOWN, 0, static_cast<ULONG>(out.size()));
    if (!*ret) return E_OUTOFMEMORY;
    for (LONG k = 0; k < static_cast<LONG>(out.size()); ++k)
        SafeArrayPutElement(*ret, &k, AsSimple(out[k].Get()));  // AddRefs
    return S_OK;
}
HRESULT ElementProvider::get_CanSelectMultiple(BOOL* ret) {
    CheckThread("get_CanSelectMultiple");
    if (!ret) return E_POINTER;
    ISelectionPeer* sel = LiveSite() ? peer_->AsSelection() : nullptr;
    if (!sel) return UIA_E_ELEMENTNOTAVAILABLE;
    *ret = sel->CanSelectMultiple() ? TRUE : FALSE;
    return S_OK;
}
HRESULT ElementProvider::get_IsSelectionRequired(BOOL* ret) {
    CheckThread("get_IsSelectionRequired");
    if (!ret) return E_POINTER;
    ISelectionPeer* sel = LiveSite() ? peer_->AsSelection() : nullptr;
    if (!sel) return UIA_E_ELEMENTNOTAVAILABLE;
    *ret = sel->IsSelectionRequired() ? TRUE : FALSE;
    return S_OK;
}

// --- ScrollItem ------------------------------------------------------------------

// --- ItemContainer / VirtualizedItem -----------------------------------------------

HRESULT ElementProvider::FindItemByProperty(IRawElementProviderSimple* startAfter, PROPERTYID id,
                                            VARIANT value, IRawElementProviderSimple** ret) {
    CheckThread("FindItemByProperty");
    if (!ret) return E_POINTER;
    *ret = nullptr;
    IItemsPeer* items = LiveSite() ? peer_->AsItems() : nullptr;
    if (!items) return UIA_E_ELEMENTNOTAVAILABLE;
    // Where to start: after an item of THIS container, or from the beginning.
    size_t after = SIZE_MAX;
    if (startAfter) {
        // A client may pass any provider; only an element provider may be cast as one
        // (a caption button or the window root would read garbage).
        ComPtr<IRawElementProviderFragmentRoot> asRoot;
        if (IsCaptionProvider(startAfter) || SUCCEEDED(startAfter->QueryInterface(IID_PPV_ARGS(&asRoot))))
            return E_INVALIDARG;
        AutomationPeer* sp = PeerFromProvider(startAfter);
        if (!sp || sp->ItemContainer() != peer_) return E_INVALIDARG;
        after = sp->IndexInContainer();
    }
    // Supported properties: none (= the next item), Name, SelectionItem.IsSelected.
    std::wstring name;
    const std::wstring* namePtr = nullptr;
    int selected = -1;
    if (id == 0) {
    } else if (id == UIA_NamePropertyId) {
        if (value.vt != VT_BSTR) return E_INVALIDARG;
        name.assign(value.bstrVal ? value.bstrVal : L"", value.bstrVal ? SysStringLen(value.bstrVal) : 0);
        namePtr = &name;
    } else if (id == UIA_SelectionItemIsSelectedPropertyId) {
        if (value.vt != VT_BOOL) return E_INVALIDARG;
        selected = value.boolVal ? 1 : 0;
    } else {
        return E_INVALIDARG;
    }
    const size_t found = items->FindItem(after, namePtr, selected);
    if (found == SIZE_MAX) return S_OK;  // no match: null, not an error
    AutomationPeer* item = items->ItemAt(found);
    IUnknown* prov = item ? item->Provider() : nullptr;
    return prov ? prov->QueryInterface(IID_PPV_ARGS(ret)) : S_OK;
}

HRESULT ElementProvider::Realize() {
    CheckThread("Realize");
    if (!LiveSite() || !peer_->CanScrollIntoView()) return UIA_E_ELEMENTNOTAVAILABLE;
    if (!peer_->IsEnabled()) return UIA_E_ELEMENTNOTENABLED;  // a gone item: nothing to realize
    return Post(PeerAction::ScrollIntoView);
}

HRESULT ElementProvider::ScrollIntoView() {
    CheckThread("ScrollIntoView");
    if (!LiveSite() || !peer_->CanScrollIntoView()) return UIA_E_ELEMENTNOTAVAILABLE;
    return Post(PeerAction::ScrollIntoView);
}

}  // namespace fluent::automation
