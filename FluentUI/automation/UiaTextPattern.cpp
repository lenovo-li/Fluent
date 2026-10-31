// UiaTextPattern.cpp — ITextProvider and ITextRangeProvider over an ITextPeer
// (TextBox, TextArea). What a screen reader needs to read and navigate an edit:
// the document, the selection, ranges moved by character / word / line /
// paragraph / document, text of a range, select, scroll into view.
//
// SCOPE. Ranges are plain offset pairs over the control's current text, clamped on
// every use so a range a client holds across an edit never indexes out of bounds
// (UIA allows a range to be imprecise after the text changed). Formatting
// attributes are not reported (GetAttributeValue answers "not supported" via
// UiaGetReservedNotSupportedValue). GetBoundingRectangles returns the control's
// rect for a non-empty range: per-line rectangles need layout geometry the text
// controls keep private; recorded in the design doc as the next refinement.
//
// COST. Nothing runs without a client. A range holds a reference to the element's
// provider and two integers; GetText copies only the requested span (capped by
// maxLength), never the whole document unless asked.
#include "UiaElementProvider.h"

#include <algorithm>

#include "AutomationGeometry.h"
#include "AutomationPeer.h"
#include "../text/WordBoundary.h"

namespace fluent::automation {

// Posts a text selection through the element's provider (defined at the end).
HRESULT PostTextSelect(IUnknown* element, size_t start, size_t length);

namespace {

// Text of the peer behind a live element provider, or null (frozen / not text).
ITextPeer* TextOf(IUnknown* elementProvider) {
    AutomationPeer* p = PeerFromProvider(elementProvider);
    return p ? p->AsText() : nullptr;
}

bool IsLineBreak(wchar_t c) { return c == L'\n' || c == L'\r'; }

// Unit boundaries. `pos` in [0, n]. Next = smallest boundary > pos (or n);
// Prev = largest boundary < pos (or 0); StartOf = largest boundary <= pos.
size_t NextBoundary(const std::wstring& t, size_t pos, TextUnit unit) {
    const size_t n = t.size();
    if (pos >= n) return n;
    switch (unit) {
        case TextUnit_Character: return pos + 1;
        case TextUnit_Format:
        case TextUnit_Word: return std::max(pos + 1, NextWordBoundary(t, pos));
        case TextUnit_Line:
        case TextUnit_Paragraph: {
            size_t i = pos;
            while (i < n && !IsLineBreak(t[i])) ++i;
            if (i < n && t[i] == L'\r' && i + 1 < n && t[i + 1] == L'\n') ++i;
            return std::min(n, i + 1);
        }
        default: return n;  // Page, Document
    }
}
size_t PrevBoundary(const std::wstring& t, size_t pos, TextUnit unit) {
    if (pos == 0) return 0;
    switch (unit) {
        case TextUnit_Character: return pos - 1;
        case TextUnit_Format:
        case TextUnit_Word: return std::min(pos - 1, PrevWordBoundary(t, pos));
        case TextUnit_Line:
        case TextUnit_Paragraph: {
            size_t i = pos - 1;  // the char before pos
            while (i > 0 && !IsLineBreak(t[i - 1])) --i;
            return i;
        }
        default: return 0;
    }
}
size_t StartOfUnit(const std::wstring& t, size_t pos, TextUnit unit) {
    if (pos >= t.size()) pos = t.size();
    if (pos == 0) return 0;
    const size_t prev = PrevBoundary(t, pos, unit);
    return NextBoundary(t, prev, unit) == pos ? pos : prev;
}

class TextRange final : public ITextRangeProvider {
public:
    TextRange(IUnknown* element, size_t start, size_t end)
        : element_(element), start_(start), end_(std::max(start, end)) {}

    IFACEMETHODIMP_(ULONG) AddRef() override { return ++refs_; }
    IFACEMETHODIMP_(ULONG) Release() override {
        const ULONG r = --refs_;
        if (!r) delete this;
        return r;
    }
    IFACEMETHODIMP QueryInterface(REFIID riid, void** out) override {
        if (!out) return E_POINTER;
        *out = nullptr;
        if (riid != __uuidof(IUnknown) && riid != __uuidof(ITextRangeProvider)) return E_NOINTERFACE;
        *out = static_cast<ITextRangeProvider*>(this);
        AddRef();
        return S_OK;
    }

    IFACEMETHODIMP Clone(ITextRangeProvider** ret) override;
    IFACEMETHODIMP Compare(ITextRangeProvider* other, BOOL* ret) override;
    IFACEMETHODIMP CompareEndpoints(TextPatternRangeEndpoint ep, ITextRangeProvider* other,
                                    TextPatternRangeEndpoint otherEp, int* ret) override;
    IFACEMETHODIMP ExpandToEnclosingUnit(TextUnit unit) override;
    IFACEMETHODIMP FindAttribute(TEXTATTRIBUTEID, VARIANT, BOOL, ITextRangeProvider** ret) override {
        if (!ret) return E_POINTER;
        *ret = nullptr;  // no attributes are reported
        return Live() ? S_OK : UIA_E_ELEMENTNOTAVAILABLE;
    }
    IFACEMETHODIMP FindText(BSTR text, BOOL backward, BOOL ignoreCase, ITextRangeProvider** ret) override;
    IFACEMETHODIMP GetAttributeValue(TEXTATTRIBUTEID id, VARIANT* ret) override;
    IFACEMETHODIMP GetBoundingRectangles(SAFEARRAY** ret) override;
    IFACEMETHODIMP GetEnclosingElement(IRawElementProviderSimple** ret) override;
    IFACEMETHODIMP GetText(int maxLength, BSTR* ret) override;
    IFACEMETHODIMP Move(TextUnit unit, int count, int* ret) override;
    IFACEMETHODIMP MoveEndpointByUnit(TextPatternRangeEndpoint ep, TextUnit unit, int count,
                                      int* ret) override;
    IFACEMETHODIMP MoveEndpointByRange(TextPatternRangeEndpoint ep, ITextRangeProvider* other,
                                       TextPatternRangeEndpoint otherEp) override;
    IFACEMETHODIMP Select() override;
    IFACEMETHODIMP AddToSelection() override { return UIA_E_INVALIDOPERATION; }      // single selection
    IFACEMETHODIMP RemoveFromSelection() override { return UIA_E_INVALIDOPERATION; }
    IFACEMETHODIMP ScrollIntoView(BOOL alignToTop) override;
    IFACEMETHODIMP GetChildren(SAFEARRAY** ret) override {
        if (!ret) return E_POINTER;
        *ret = SafeArrayCreateVector(VT_UNKNOWN, 0, 0);  // no embedded objects
        return *ret ? S_OK : E_OUTOFMEMORY;
    }

private:
    ~TextRange() = default;
    ITextPeer* Live() const { return TextOf(element_.Get()); }
    // Clamp to the CURRENT text before every use (the text may have changed).
    void Clamp(const std::wstring& t) {
        start_ = std::min(start_, t.size());
        end_ = std::min(std::max(end_, start_), t.size());
    }
    static TextRange* From(ITextRangeProvider* p) { return static_cast<TextRange*>(p); }
    size_t& Ep(TextPatternRangeEndpoint e) { return e == TextPatternRangeEndpoint_Start ? start_ : end_; }

    std::atomic<ULONG> refs_{1};
    ComPtr<IUnknown> element_;
    size_t start_, end_;
    friend HRESULT MakeRange(IUnknown*, size_t, size_t, ITextRangeProvider**);
};

HRESULT MakeRange(IUnknown* element, size_t s, size_t e, ITextRangeProvider** out) {
    *out = new (std::nothrow) TextRange(element, s, e);
    return *out ? S_OK : E_OUTOFMEMORY;
}


HRESULT TextRange::Clone(ITextRangeProvider** ret) {
    if (!ret) return E_POINTER;
    return MakeRange(element_.Get(), start_, end_, ret);
}
HRESULT TextRange::Compare(ITextRangeProvider* other, BOOL* ret) {
    if (!ret || !other) return E_INVALIDARG;
    TextRange* o = From(other);
    *ret = (o->element_.Get() == element_.Get() && o->start_ == start_ && o->end_ == end_) ? TRUE : FALSE;
    return S_OK;
}
HRESULT TextRange::CompareEndpoints(TextPatternRangeEndpoint ep, ITextRangeProvider* other,
                                    TextPatternRangeEndpoint otherEp, int* ret) {
    if (!ret || !other) return E_INVALIDARG;
    TextRange* o = From(other);
    if (o->element_.Get() != element_.Get()) return E_INVALIDARG;  // ranges of different controls
    const size_t a = Ep(ep), b = o->Ep(otherEp);
    *ret = a < b ? -1 : (a > b ? 1 : 0);
    return S_OK;
}
HRESULT TextRange::ExpandToEnclosingUnit(TextUnit unit) {
    ITextPeer* t = Live();
    if (!t) return UIA_E_ELEMENTNOTAVAILABLE;
    const std::wstring& s = t->Text();
    Clamp(s);
    if (unit == TextUnit_Document || unit == TextUnit_Page) {
        start_ = 0;
        end_ = s.size();
        return S_OK;
    }
    start_ = StartOfUnit(s, start_, unit);
    end_ = NextBoundary(s, start_, unit);
    return S_OK;
}
HRESULT TextRange::FindText(BSTR text, BOOL backward, BOOL ignoreCase, ITextRangeProvider** ret) {
    if (!ret) return E_POINTER;
    *ret = nullptr;
    ITextPeer* t = Live();
    if (!t) return UIA_E_ELEMENTNOTAVAILABLE;
    const std::wstring& s = t->Text();
    Clamp(s);
    std::wstring needle = text ? text : L"";
    if (needle.empty()) return S_OK;
    std::wstring hay = s.substr(start_, end_ - start_);
    if (ignoreCase) {
        CharLowerBuffW(hay.data(), static_cast<DWORD>(hay.size()));
        CharLowerBuffW(needle.data(), static_cast<DWORD>(needle.size()));
    }
    const size_t at = backward ? hay.rfind(needle) : hay.find(needle);
    if (at == std::wstring::npos) return S_OK;
    return MakeRange(element_.Get(), start_ + at, start_ + at + needle.size(), ret);
}
HRESULT TextRange::GetAttributeValue(TEXTATTRIBUTEID id, VARIANT* ret) {
    if (!ret) return E_POINTER;
    VariantInit(ret);
    ITextPeer* t = Live();
    if (!t) return UIA_E_ELEMENTNOTAVAILABLE;
    if (id == UIA_IsReadOnlyAttributeId) {
        ret->vt = VT_BOOL;
        ret->boolVal = t->IsTextReadOnly() ? VARIANT_TRUE : VARIANT_FALSE;
        return S_OK;
    }
    ret->vt = VT_UNKNOWN;
    return UiaGetReservedNotSupportedValue(&ret->punkVal);
}
HRESULT TextRange::GetBoundingRectangles(SAFEARRAY** ret) {
    if (!ret) return E_POINTER;
    *ret = nullptr;
    ITextPeer* t = Live();
    if (!t) return UIA_E_ELEMENTNOTAVAILABLE;
    Clamp(t->Text());
    // A non-empty range reports the control's rect (see the file comment).
    UiaRect r{};
    ComPtr<IRawElementProviderFragment> frag;
    if (start_ < end_ && SUCCEEDED(element_.As(&frag))) frag->get_BoundingRectangle(&r);
    const bool any = r.width > 0 && r.height > 0;
    *ret = SafeArrayCreateVector(VT_R8, 0, any ? 4 : 0);
    if (!*ret) return E_OUTOFMEMORY;
    if (any) {
        double v[4] = {r.left, r.top, r.width, r.height};
        for (LONG i = 0; i < 4; ++i) SafeArrayPutElement(*ret, &i, &v[i]);
    }
    return S_OK;
}
HRESULT TextRange::GetEnclosingElement(IRawElementProviderSimple** ret) {
    if (!ret) return E_POINTER;
    *ret = nullptr;
    if (!Live()) return UIA_E_ELEMENTNOTAVAILABLE;
    return element_->QueryInterface(IID_PPV_ARGS(ret));
}
HRESULT TextRange::GetText(int maxLength, BSTR* ret) {
    if (!ret) return E_POINTER;
    *ret = nullptr;
    ITextPeer* t = Live();
    if (!t) return UIA_E_ELEMENTNOTAVAILABLE;
    const std::wstring& s = t->Text();
    Clamp(s);
    size_t len = end_ - start_;
    if (maxLength >= 0) len = std::min(len, static_cast<size_t>(maxLength));
    *ret = SysAllocStringLen(s.data() + start_, static_cast<UINT>(len));
    return *ret ? S_OK : E_OUTOFMEMORY;
}
HRESULT TextRange::Move(TextUnit unit, int count, int* ret) {
    if (!ret) return E_POINTER;
    *ret = 0;
    ITextPeer* t = Live();
    if (!t) return UIA_E_ELEMENTNOTAVAILABLE;
    const std::wstring& s = t->Text();
    Clamp(s);
    // UIA: a degenerate range moves its insertion point; a non-degenerate range is
    // first normalized to the start of its unit, then moved by whole units.
    const bool degenerate = start_ == end_;
    size_t pos = StartOfUnit(s, start_, unit);
    int moved = 0;
    if (unit == TextUnit_Document || unit == TextUnit_Page) {
        if (count > 0 && pos < s.size()) { pos = s.size(); moved = 1; }
        if (count < 0 && pos > 0) { pos = 0; moved = -1; }
    } else {
        for (; moved < count; ++moved) {
            const size_t nx = NextBoundary(s, pos, unit);
            if (nx == pos || nx >= s.size()) break;
            pos = nx;
        }
        for (; moved > count; --moved) {
            if (pos == 0) break;
            pos = PrevBoundary(s, pos, unit);
        }
    }
    start_ = pos;
    end_ = degenerate ? pos : NextBoundary(s, pos, unit);
    *ret = moved;
    return S_OK;
}
HRESULT TextRange::MoveEndpointByUnit(TextPatternRangeEndpoint ep, TextUnit unit, int count, int* ret) {
    if (!ret) return E_POINTER;
    *ret = 0;
    ITextPeer* t = Live();
    if (!t) return UIA_E_ELEMENTNOTAVAILABLE;
    const std::wstring& s = t->Text();
    Clamp(s);
    size_t& e = Ep(ep);
    const bool doc = unit == TextUnit_Document || unit == TextUnit_Page;
    int moved = 0;
    for (; moved < count && e < s.size(); ++moved) e = doc ? s.size() : NextBoundary(s, e, unit);
    for (; moved > count && e > 0; --moved) e = doc ? 0 : PrevBoundary(s, e, unit);
    // Crossing endpoints collapse the range onto the moved one (UIA rule).
    if (start_ > end_) {
        if (ep == TextPatternRangeEndpoint_Start) end_ = start_;
        else start_ = end_;
    }
    *ret = moved;
    return S_OK;
}
HRESULT TextRange::MoveEndpointByRange(TextPatternRangeEndpoint ep, ITextRangeProvider* other,
                                       TextPatternRangeEndpoint otherEp) {
    if (!other) return E_INVALIDARG;
    TextRange* o = From(other);
    if (o->element_.Get() != element_.Get()) return E_INVALIDARG;
    Ep(ep) = o->Ep(otherEp);
    if (start_ > end_) {
        if (ep == TextPatternRangeEndpoint_Start) end_ = start_;
        else start_ = end_;
    }
    return S_OK;
}
HRESULT TextRange::Select() {
    ITextPeer* t = Live();
    if (!t) return UIA_E_ELEMENTNOTAVAILABLE;
    Clamp(t->Text());
    return PostTextSelect(element_.Get(), start_, end_ - start_);
}
HRESULT TextRange::ScrollIntoView(BOOL) {
    ITextPeer* t = Live();
    if (!t) return UIA_E_ELEMENTNOTAVAILABLE;
    // Placing the caret at the range start scrolls the control to it (its own
    // EnsureCaretVisible), the same effect a user's click there has.
    Clamp(t->Text());
    return PostTextSelect(element_.Get(), start_, 0);
}

}  // namespace

// --- ElementProvider: ITextProvider ----------------------------------------------

HRESULT ElementProvider::GetVisibleRanges(SAFEARRAY** ret) {
    CheckThread("GetVisibleRanges");
    if (!ret) return E_POINTER;
    *ret = nullptr;
    ITextPeer* t = LiveSite() ? peer_->AsText() : nullptr;
    if (!t) return UIA_E_ELEMENTNOTAVAILABLE;
    // The whole document: the controls do not expose their visible line span.
    ComPtr<ITextRangeProvider> r;
    HRESULT hr = MakeRange(static_cast<IRawElementProviderSimple*>(this), 0, t->Text().size(), r.GetAddressOf());
    if (FAILED(hr)) return hr;
    *ret = SafeArrayCreateVector(VT_UNKNOWN, 0, 1);
    if (!*ret) return E_OUTOFMEMORY;
    LONG i = 0;
    return SafeArrayPutElement(*ret, &i, r.Get());
}
HRESULT ElementProvider::TextSelection(SAFEARRAY** ret) {
    ITextPeer* t = peer_->AsText();
    const size_t n = t->Text().size();
    const size_t s = std::min(t->SelectionStart(), n);
    const size_t len = std::min(t->SelectionLength(), n - s);
    ComPtr<ITextRangeProvider> r;
    HRESULT hr = MakeRange(static_cast<IRawElementProviderSimple*>(this), s, s + len, r.GetAddressOf());
    if (FAILED(hr)) return hr;
    *ret = SafeArrayCreateVector(VT_UNKNOWN, 0, 1);
    if (!*ret) return E_OUTOFMEMORY;
    LONG i = 0;
    return SafeArrayPutElement(*ret, &i, r.Get());
}
HRESULT ElementProvider::RangeFromChild(IRawElementProviderSimple*, ITextRangeProvider** ret) {
    if (!ret) return E_POINTER;
    *ret = nullptr;
    return UIA_E_INVALIDOPERATION;  // no embedded child elements
}
HRESULT ElementProvider::RangeFromPoint(UiaPoint pt, ITextRangeProvider** ret) {
    CheckThread("RangeFromPoint");
    if (!ret) return E_POINTER;
    *ret = nullptr;
    IAutomationSite* site = LiveSite();
    ITextPeer* t = site ? peer_->AsText() : nullptr;
    if (!t) return UIA_E_ELEMENTNOTAVAILABLE;
    const POINT o = site->ClientOriginOnScreen();
    const DipPoint d = HostDipFromScreen(pt.x, pt.y, o.x, o.y, site->Dpi());
    const size_t at = t->OffsetAtPoint(d.x, d.y);
    return MakeRange(static_cast<IRawElementProviderSimple*>(this), at, at, ret);
}
HRESULT ElementProvider::get_DocumentRange(ITextRangeProvider** ret) {
    CheckThread("get_DocumentRange");
    if (!ret) return E_POINTER;
    *ret = nullptr;
    ITextPeer* t = LiveSite() ? peer_->AsText() : nullptr;
    if (!t) return UIA_E_ELEMENTNOTAVAILABLE;
    return MakeRange(static_cast<IRawElementProviderSimple*>(this), 0, t->Text().size(), ret);
}
HRESULT ElementProvider::get_SupportedTextSelection(SupportedTextSelection* ret) {
    if (!ret) return E_POINTER;
    *ret = SupportedTextSelection_Single;
    return LiveSite() ? S_OK : UIA_E_ELEMENTNOTAVAILABLE;
}

HRESULT PostTextSelect(IUnknown* element, size_t start, size_t length) {
    auto* p = static_cast<ElementProvider*>(AsSimple(element));
    return p->PostSelectText(start, length);
}

}  // namespace fluent::automation
