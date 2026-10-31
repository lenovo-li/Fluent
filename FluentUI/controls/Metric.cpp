// Metric.cpp — see Metric.h.

#include "Metric.h"
#include "../graphics/DrawingContext.h"
#include "../graphics/DWriteContext.h"
#include "../styling/ThemeTokens.h"
#include "../graphics/ResourceCache.h"
#include "../core/UIContext.h"

#include <algorithm>

namespace fluent {

namespace {
constexpr float kLabelGap = 2.0f;   // label -> value
constexpr float kDeltaGap = 2.0f;   // value -> delta
// The value is the visual anchor, so it is set in the subtitle size rather than body.
// Read from typography tokens at draw time; this is only the fallback ratio used when
// DWrite is unavailable and heights must still be estimated.
constexpr float kLineRatio = 1.35f;

// Every Metric line is one NO_WRAP line, whose metrics do not depend on the box
// width; Measure and Render therefore share this one width so they share one cache
// entry (Render used its bounds width and would have keyed a second layout).
constexpr float kLineBoxW = 100000.0f;

// Metrics of `text` laid out LEADING / NEAR at `maxW` x unbounded height. Measure and
// Render both call this with the same key, so through the shared, epoch-versioned
// ResourceCache the layout is built once and every later Measure (each frame of a
// resize drag) and every Render (each repaint, including hover repaints) is a hash
// lookup. Render used to build a fresh IDWriteTextLayout per text block per paint
// only to read its height and advance y. Without a cache (headless tests that wire
// only DWrite) it falls back to a one-off layout. Zeroed metrics on any failure.
DWRITE_TEXT_METRICS LayoutMetrics(ResourceCache* cache, DWriteContext* dw,
                                  const std::wstring& text, float size,
                                  DWRITE_FONT_WEIGHT weight, DWRITE_WORD_WRAPPING wrap,
                                  float maxW) {
    DWRITE_TEXT_METRICS m{};
    if (text.empty() || !dw || !dw->Valid()) return m;
    ComPtr<IDWriteTextLayout> layout;
    if (cache) {
        TextLayoutKey key;
        key.text = text;
        key.fontSize = size;
        key.weight = weight;
        key.textAlign = DWRITE_TEXT_ALIGNMENT_LEADING;
        key.paraAlign = DWRITE_PARAGRAPH_ALIGNMENT_NEAR;
        key.wrapping = wrap;
        key.maxWidth = maxW;
        key.maxHeight = 100000.0f;
        layout = cache->GetTextLayout(std::move(key));
    } else if (IDWriteTextFormat* fmt = dw->Format(size, weight,
                                                   DWRITE_TEXT_ALIGNMENT_LEADING,
                                                   DWRITE_PARAGRAPH_ALIGNMENT_NEAR, wrap)) {
        dw->Factory()->CreateTextLayout(text.c_str(), static_cast<UINT32>(text.size()),
                                        fmt, maxW, 100000.0f, layout.GetAddressOf());
    }
    if (!layout || FAILED(layout->GetMetrics(&m))) m = DWRITE_TEXT_METRICS{};
    return m;
}
}  // namespace

Metric::Metric() {
    SetFocusable(false);   // read-only display; nothing to activate
    // Stretch, because a Metric is a dashboard tile: it fills its column so a row of them
    // aligns. Without this the tiles huddle at their text width and the strip looks ragged.
    SetHAlign(HAlign::Stretch);
}

void Metric::SetLabel(std::wstring text) {
    if (label_ == text) return;
    label_ = std::move(text);
    InvalidateDirty(DirtyFlags::Measure);
}

void Metric::SetValue(std::wstring text) {
    if (value_ == text) return;
    value_ = std::move(text);
    InvalidateDirty(DirtyFlags::Measure);
}

void Metric::SetDelta(std::wstring text, Trend trend) {
    // Both together: they are one fact, and setting them separately would allow a frame
    // where the text says "+1.2%" and the trend still says Down from the previous value.
    const bool sameText = (delta_ == text);
    if (sameText && trend_ == trend) return;
    delta_ = std::move(text);
    // A trend change alone is Render-level (same box, different colour), but a text change
    // is Measure-level, and so is switching to/from None because that adds or removes the
    // whole delta line. Pick the higher level only when it is really needed.
    const bool lineAppearedOrVanished =
        (trend_ == Trend::None) != (trend == Trend::None);
    trend_ = trend;
    if (!sameText || lineAppearedOrVanished) InvalidateDirty(DirtyFlags::Measure);
    else Invalidate();
}

void Metric::SetInverted(bool inverted) {
    // Render-level: inversion only swaps which colour a trend resolves to.
    if (inverted_ == inverted) return;
    inverted_ = inverted;
    Invalidate();
}

D2D1_COLOR_F Metric::TrendColor(Trend t) const {
    const ColorTokens& c = Theme().colors;
    switch (t) {
        case Trend::Up:
            return inverted_ ? c.dataNegative : c.dataPositive;
        case Trend::Down:
            return inverted_ ? c.dataPositive : c.dataNegative;
        case Trend::Flat:
            return c.dataNeutral;
        case Trend::None:
        default:
            // Never drawn, but returning a real colour keeps callers (and tests) from
            // having to special-case the value.
            return c.textSecondary;
    }
}

void Metric::Measure(float availW, float availH) {
    UNREFERENCED_PARAMETER(availH);

    const TypographyTokens& t = Theme().typography;
    // Containers measure with an INFINITE available width to ask "what do you need?"
    // (UniformGrid does exactly that before dividing its width up). Treating infinity as
    // an offer to fill would report an infinite desired width, so an unbounded measure
    // must fall back to the content width instead.
    const bool bounded = availW > 0.0f && std::isfinite(availW);
    const float offered = bounded ? availW : 160.0f;

    float w = 0.0f;
    float h = 0.0f;

    DWriteContext* dw = Dwrite();
    auto measure = [&](const std::wstring& s, float size, DWRITE_FONT_WEIGHT weight) {
        if (s.empty()) return;
        if (!dw || !dw->Valid()) {
            // Estimate: enough to keep layout sane headlessly. Width uses a 0.6em average
            // advance, which is close for Latin and an under-estimate for CJK — acceptable
            // because the real path measures properly and this only feeds tests.
            w = std::max(w, static_cast<float>(s.size()) * size * 0.6f);
            h += size * kLineRatio;
            return;
        }
        const DWRITE_TEXT_METRICS m = LayoutMetrics(Context().resourceCache, dw, s, size,
                                                    weight, DWRITE_WORD_WRAPPING_NO_WRAP,
                                                    kLineBoxW);
        w = std::max(w, m.widthIncludingTrailingWhitespace);
        h += m.height;
    };

    measure(label_, t.captionSize, DWRITE_FONT_WEIGHT_NORMAL);
    if (!label_.empty() && !value_.empty()) h += kLabelGap;
    measure(value_, t.subtitleSize, DWRITE_FONT_WEIGHT_SEMI_BOLD);
    if (trend_ != Trend::None && !delta_.empty()) {
        if (!value_.empty()) h += kDeltaGap;
        measure(delta_, t.captionSize, DWRITE_FONT_WEIGHT_NORMAL);
    }

    // Take the whole offered width, not the measured text width.
    //
    // A Metric is a TILE in a dashboard row (the st.columns shape): its neighbours are
    // other tiles, and reporting only the text width made a four-up strip in a 800 DIP
    // UniformGrid claim 63 DIP per cell, so labels and deltas were clipped even though the
    // cell had 200 DIP to give. `w` is still computed because it is the MINIMUM the content
    // needs — it is what a caller measuring for auto-fit would want — and it is used when
    // nothing is offered at all.
    // Bounded measure: fill the offer (tile behaviour). Unbounded: report what the text
    // needs, so the container has a real number to divide up.
    const float desiredW = bounded ? offered : w;
    SetDesired({IsAuto(width_) ? desiredW : width_,
                IsAuto(height_) ? h : height_});
}

void Metric::Render(const DrawingContext& dc) {
    if (!dc.Dc()) return;
    DWriteContext* dw = Dwrite();
    if (!dw || !dw->Valid()) return;

    const ColorTokens& c = Theme().colors;
    const TypographyTokens& t = Theme().typography;
    float y = bounds_.y;

    auto draw = [&](const std::wstring& s, float size, DWRITE_FONT_WEIGHT weight,
                    const D2D1_COLOR_F& color) {
        if (s.empty()) return;
        IDWriteTextFormat* fmt = dw->Format(size, weight, DWRITE_TEXT_ALIGNMENT_LEADING,
                                            DWRITE_PARAGRAPH_ALIGNMENT_NEAR,
                                            DWRITE_WORD_WRAPPING_NO_WRAP);
        if (!fmt) return;
        // Bottom is bounds_.bottom(), not y + one line: a box exactly one em tall shears
        // descenders (the Expander header bug). PARAGRAPH_ALIGNMENT_NEAR keeps the line at
        // the top of whatever box it is given, so a taller box does not move the text.
        dc.DrawText(s.c_str(), static_cast<UINT32>(s.size()), fmt,
                    D2D1::RectF(bounds_.x, y, bounds_.right(), bounds_.bottom()),
                    color, D2D1_DRAW_TEXT_OPTIONS_CLIP);
        y += LayoutMetrics(Context().resourceCache, dw, s, size, weight,
                           DWRITE_WORD_WRAPPING_NO_WRAP, kLineBoxW).height;
    };

    draw(label_, t.captionSize, DWRITE_FONT_WEIGHT_NORMAL, c.textSecondary);
    if (!label_.empty() && !value_.empty()) y += kLabelGap;
    draw(value_, t.subtitleSize, DWRITE_FONT_WEIGHT_SEMI_BOLD, c.textPrimary);
    if (trend_ != Trend::None && !delta_.empty()) {
        if (!value_.empty()) y += kDeltaGap;
        draw(delta_, t.captionSize, DWRITE_FONT_WEIGHT_NORMAL, TrendColor(trend_));
    }
}

}  // namespace fluent
