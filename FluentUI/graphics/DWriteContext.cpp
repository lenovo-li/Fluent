// DWriteContext.cpp

#include "DWriteContext.h"

#pragma comment(lib, "dwrite.lib")

namespace fluent {

namespace {
const char* kTag = "DWriteContext";

// True if the given font family is installed in the system font collection.
bool FamilyExists(IDWriteFactory* factory, const wchar_t* name) {
    ComPtr<IDWriteFontCollection> coll;
    if (FAILED(factory->GetSystemFontCollection(coll.GetAddressOf(), FALSE)))
        return false;
    UINT32 index = 0;
    BOOL exists = FALSE;
    if (FAILED(coll->FindFamilyName(name, &index, &exists)))
        return false;
    return exists != FALSE;
}
} // namespace

HRESULT DWriteContext::Initialize() {
    FL_RETURN_IF_FAILED(
        kTag, DWriteCreateFactory(
                  DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory),
                  reinterpret_cast<IUnknown**>(factory_.ReleaseAndGetAddressOf())));

    if (FamilyExists(factory_.Get(), L"Segoe UI Variable Text"))
        family_ = L"Segoe UI Variable Text";
    else if (FamilyExists(factory_.Get(), L"Segoe UI Variable"))
        family_ = L"Segoe UI Variable";
    else
        family_ = L"Segoe UI";

    TraceMsg(kTag, "initialized");
    return S_OK;
}

unsigned long long DWriteContext::PackKey(const FormatKey& k) {
    // Size quantized to 1/4 DIP (0..~16k) in the low 32 bits; the four enum
    // attributes packed into the high bits. Each enum fits comfortably in a byte.
    unsigned long long size = static_cast<unsigned long long>(k.sizeDip * 4.0f);
    return (size & 0xFFFFFFFFull) |
           ((static_cast<unsigned long long>(k.weight) & 0xFFFF) << 32) |
           ((static_cast<unsigned long long>(k.textAlign) & 0xF) << 48) |
           ((static_cast<unsigned long long>(k.paraAlign) & 0xF) << 52) |
           ((static_cast<unsigned long long>(k.wrapping) & 0xF) << 56);
}

IDWriteTextFormat* DWriteContext::Format(float sizeDip, DWRITE_FONT_WEIGHT weight,
                                         DWRITE_TEXT_ALIGNMENT textAlign,
                                         DWRITE_PARAGRAPH_ALIGNMENT paraAlign,
                                         DWRITE_WORD_WRAPPING wrapping) {
    FormatKey fk{sizeDip, weight, textAlign, paraAlign, wrapping};
    unsigned long long key = PackKey(fk);

    std::lock_guard<std::mutex> lock(formatCacheMutex_);

    auto it = cache_.find(key);
    if (it != cache_.end())
        return it->second.Get();

    ComPtr<IDWriteTextFormat> fmt;
    HRESULT hr = factory_->CreateTextFormat(
        family_.c_str(), nullptr, weight, DWRITE_FONT_STYLE_NORMAL,
        DWRITE_FONT_STRETCH_NORMAL, sizeDip, L"", fmt.GetAddressOf());
    if (FAILED(hr)) {
        Trace(kTag, "CreateTextFormat failed", hr);
        return nullptr;
    }
    // Bake the full layout key into the object; it is logically immutable after
    // this, so callers never mutate a shared format again.
    fmt->SetTextAlignment(textAlign);
    fmt->SetParagraphAlignment(paraAlign);
    fmt->SetWordWrapping(wrapping);

    auto* raw = fmt.Get();
    cache_.emplace(key, std::move(fmt));
    return raw;
}

bool DWriteContext::PrimaryLineMetrics(float sizeDip, DWRITE_FONT_WEIGHT weight,
                                       float* height, float* baseline) {
    // Alignment and wrapping do not change a line's metrics, so key on size + weight
    // only (fixed values for the rest of the format key).
    const unsigned long long key = PackKey(FormatKey{
        sizeDip, weight, DWRITE_TEXT_ALIGNMENT_LEADING, DWRITE_PARAGRAPH_ALIGNMENT_NEAR,
        DWRITE_WORD_WRAPPING_NO_WRAP});
    {
        std::lock_guard<std::mutex> lock(lineMetricsMutex_);
        auto it = lineMetrics_.find(key);
        if (it != lineMetrics_.end()) {
            *height = it->second.height;
            *baseline = it->second.baseline;
            return true;
        }
    }
    // Built outside the lock: Format() takes its own lock, and two threads racing
    // here compute the same value, so the second emplace is a harmless no-op.
    IDWriteTextFormat* fmt = Format(sizeDip, weight, DWRITE_TEXT_ALIGNMENT_LEADING,
                                    DWRITE_PARAGRAPH_ALIGNMENT_NEAR,
                                    DWRITE_WORD_WRAPPING_NO_WRAP);
    if (!fmt) return false;
    ComPtr<IDWriteTextLayout> probe;
    if (FAILED(factory_->CreateTextLayout(L"Ag中", 3, fmt, 1e5f, 1e5f,
                                          probe.GetAddressOf())))
        return false;
    DWRITE_LINE_METRICS lm{};
    UINT32 count = 0;
    if (FAILED(probe->GetLineMetrics(&lm, 1, &count)) || count == 0 || !(lm.height > 0.0f))
        return false;
    {
        std::lock_guard<std::mutex> lock(lineMetricsMutex_);
        lineMetrics_.emplace(key, LineMetrics{lm.height, lm.baseline});
    }
    *height = lm.height;
    *baseline = lm.baseline;
    return true;
}

} // namespace fluent
