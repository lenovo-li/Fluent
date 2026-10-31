// DWriteContext.h — DirectWrite factory + cached text formats.
//
// Prefers "Segoe UI Variable" (Win11) and falls back to "Segoe UI". Font sizes
// are in DIPs, so DPI changes do not require recreating formats — only the
// render target DPI changes.
#pragma once

#include "../fl_common.h"
#include <dwrite_3.h>
#include <string>
#include <unordered_map>
#include <mutex>

namespace fluent {

class DWriteContext {
public:
    HRESULT Initialize();

    IDWriteFactory* Factory() const { return factory_.Get(); }

    // Return a cached text format for the FULL layout key (size, weight, text +
    // paragraph alignment, wrapping). The cache is keyed by the whole key, so
    // two callers asking for different alignments get different objects and can
    // never pollute each other's format state.
    //
    // The returned object is logically immutable: callers must NOT call
    // SetTextAlignment / SetParagraphAlignment / SetWordWrapping on it — pass the
    // desired values here instead. Defaults are centered + no wrap, so most call
    // sites just pass size (+ weight).
    //
    // THREAD-SAFE. This is the entry point every control's Measure reaches DWrite
    // through, and AsyncLayout calls Measure from a worker thread — so two threads
    // can ask for a size that is not yet cached, both miss, and both insert. An
    // unsynchronized unordered_map cannot survive that (at best a leaked format, at
    // worst a corrupted bucket list), so the whole lookup-then-insert is under
    // formatCacheMutex_. See DWriteFormatTests.ConcurrentFormatCacheInsert*.
    //
    // The RETURNED POINTER stays valid without the lock: cache entries are never
    // erased or overwritten for the context's lifetime, so the raw pointer cannot
    // dangle even though the caller reads it after the lock is released. Do not add
    // eviction to this cache without revisiting that.
    IDWriteTextFormat* Format(
        float sizeDip,
        DWRITE_FONT_WEIGHT weight = DWRITE_FONT_WEIGHT_NORMAL,
        DWRITE_TEXT_ALIGNMENT textAlign = DWRITE_TEXT_ALIGNMENT_CENTER,
        DWRITE_PARAGRAPH_ALIGNMENT paraAlign = DWRITE_PARAGRAPH_ALIGNMENT_CENTER,
        DWRITE_WORD_WRAPPING wrapping = DWRITE_WORD_WRAPPING_NO_WRAP);

    bool Valid() const { return factory_ != nullptr; }

    // Line height and baseline (DIP) of the PRIMARY font at this size/weight, for
    // callers that need every line to have the same metrics regardless of script.
    //
    // WHY. With DWrite's default line spacing a line's height is that of whichever
    // physical font its glyphs came from. Pure-CJK text falls back to Microsoft YaHei
    // (12pt: 15.24 DIP); a line with any Latin glyph uses Segoe UI Variable (16.00
    // DIP). So "结果：非模态已打开（主窗口仍可交互）" and "结果：非模态关闭 Cancel
    // (取消)" are different heights, and swapping one for the other moves every
    // element below it — see docs/learn/技术笔记-DirectWrite字体回退与行高一致性.md.
    // Measured from a mixed probe ("Ag中") so the primary font wins, exactly as
    // TextArea::LineHeight does; passing these to SetLineSpacing(UNIFORM) leaves
    // Latin and mixed lines where they already were and lifts pure-CJK lines to match.
    //
    // Thread-safe and memoized per (size, weight), like Format(). Returns false if
    // no layout could be built (caller keeps DWrite's default spacing).
    bool PrimaryLineMetrics(float sizeDip, DWRITE_FONT_WEIGHT weight,
                            float* height, float* baseline);

private:
    // Full immutable cache key: size (quantized to 1/4 DIP) + weight + the three
    // layout attributes. Packed into a single 64-bit value for the map.
    struct FormatKey {
        float sizeDip;
        DWRITE_FONT_WEIGHT weight;
        DWRITE_TEXT_ALIGNMENT textAlign;
        DWRITE_PARAGRAPH_ALIGNMENT paraAlign;
        DWRITE_WORD_WRAPPING wrapping;
    };
    static unsigned long long PackKey(const FormatKey& k);

    ComPtr<IDWriteFactory> factory_;
    std::wstring family_;  // resolved family name
    std::unordered_map<unsigned long long, ComPtr<IDWriteTextFormat>> cache_;
    // Guards cache_ only. Initialize() is NOT under it: the context must be fully
    // initialized on the UI thread before any worker touches it, and taking a lock
    // there would imply otherwise. factory_ and family_ are read-only afterwards.
    mutable std::mutex formatCacheMutex_;

    struct LineMetrics { float height; float baseline; };
    std::unordered_map<unsigned long long, LineMetrics> lineMetrics_;  // under lineMetricsMutex_
    std::mutex lineMetricsMutex_;
};

} // namespace fluent
