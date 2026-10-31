// DrawingContext.h — the thin Windows-specific drawing layer handed to
// Element::Render. It is deliberately NOT a cross-platform
// virtual renderer: it wraps the frame's Direct2D device context plus the one
// reusable solid-color brush the host creates per frame, and exposes typed,
// color-taking draw calls so a control never touches the raw brush or repeats
// the SetColor + draw dance.
//
// Per-frame diagnostics and partial-redraw support:
//   * `drawOps` counter: optional pointer incremented on each draw call,
//     filling FrameStats.drawOps to track GPU work per frame.
//   * `clipHint`: optional dirty-region rect in DIPs. Panel::Render skips
//     children whose bounds don't intersect it, reducing CPU traversal for
//     partial redraws. Default is effectively unbounded (all children visited).
#pragma once

#include "../fl_common.h"
#include <d2d1_1.h>
#include <dwrite.h>
#include <cstdint>

namespace fluent {

// RAII clip: PushClip pushes an axis-aligned clip rect on construction and pops
// it on destruction, so a control can scope a clip to a block without manually
// balancing PushAxisAlignedClip / PopAxisAlignedClip (which is easy to leak on an
// early return). Move-only; a moved-from guard does nothing on destruction.
class ClipGuard {
public:
    ClipGuard() = default;
    ClipGuard(ID2D1DeviceContext* dc, const D2D1_RECT_F& rect) : dc_(dc) {
        if (dc_)
            dc_->PushAxisAlignedClip(rect, D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
    }
    ~ClipGuard() { if (dc_) dc_->PopAxisAlignedClip(); }

    ClipGuard(ClipGuard&& o) noexcept : dc_(o.dc_) { o.dc_ = nullptr; }
    ClipGuard& operator=(ClipGuard&& o) noexcept {
        if (this != &o) {
            if (dc_) dc_->PopAxisAlignedClip();
            dc_ = o.dc_;
            o.dc_ = nullptr;
        }
        return *this;
    }
    ClipGuard(const ClipGuard&) = delete;
    ClipGuard& operator=(const ClipGuard&) = delete;

private:
    ID2D1DeviceContext* dc_ = nullptr;  // non-owning; null = inactive guard
};

// RAII transform: PushTransform pre-multiplies `m` onto the context's current
// transform (so a translate positions a cached unit-space glyph geometry while
// the host's DPI-scale transform stays in effect) and restores the previous
// transform on destruction. Move-only; a moved-from guard is inert.
class TransformGuard {
public:
    TransformGuard() = default;
    TransformGuard(ID2D1DeviceContext* dc, const D2D1_MATRIX_3X2_F& m) : dc_(dc) {
        if (dc_) {
            dc_->GetTransform(&saved_);
            D2D1_MATRIX_3X2_F combined =
                D2D1::Matrix3x2F(m.m11, m.m12, m.m21, m.m22, m.dx, m.dy) *
                D2D1::Matrix3x2F(saved_.m11, saved_.m12, saved_.m21, saved_.m22,
                                 saved_.dx, saved_.dy);
            dc_->SetTransform(combined);
        }
    }
    ~TransformGuard() { if (dc_) dc_->SetTransform(saved_); }

    TransformGuard(TransformGuard&& o) noexcept : dc_(o.dc_), saved_(o.saved_) {
        o.dc_ = nullptr;
    }
    TransformGuard& operator=(TransformGuard&& o) noexcept {
        if (this != &o) {
            if (dc_) dc_->SetTransform(saved_);
            dc_ = o.dc_;
            saved_ = o.saved_;
            o.dc_ = nullptr;
        }
        return *this;
    }
    TransformGuard(const TransformGuard&) = delete;
    TransformGuard& operator=(const TransformGuard&) = delete;

private:
    ID2D1DeviceContext* dc_ = nullptr;  // non-owning; null = inactive guard
    D2D1_MATRIX_3X2_F saved_ = D2D1::Matrix3x2F::Identity();
};

class DrawingContext {
public:
    // The host builds one per frame from the frame device context, the single
    // shared solid brush it created on that context, and the DPI scale.
    // `drawOps` (optional): pointer to a counter incremented on each draw call,
    //   filling FrameStats.drawOps.
    // `clipHint` (optional): the dirty region in DIPs; Panel::Render skips
    //   children outside it. For a full frame omit / pass nullptr (default =
    //   effectively unbounded so every element is visited).
    // `opacity` (optional): multiplies the alpha of EVERY color drawn through
    //   this context (see Opacity() below). 1.0 = unchanged.
    DrawingContext(ID2D1DeviceContext* dc, ID2D1SolidColorBrush* brush,
                   float dpiScale,
                   uint32_t* drawOps = nullptr,
                   const RectDip* clipHint = nullptr,
                   float opacity = 1.0f)
        : dc_(dc), brush_(brush), dpiScale_(dpiScale), drawOps_(drawOps),
          opacity_(Clamp01(opacity))
    {
        clipHint_ = clipHint ? *clipHint : RectDip{-1e9f, -1e9f, 2e9f, 2e9f};
    }

    // --- Opacity ----------------------------------------------------------
    // The alpha multiplier applied to every color this context draws. This is the
    // choke point for element opacity: the shared brush is private, so every typed
    // draw method below applies it, and a control that only uses those methods is
    // covered with no per-control code. The exception is Dc(), the raw device
    // context: anything drawn through it (a bitmap, a brush the control creates
    // itself) must apply Opacity() / Faded() by hand. Image, TreeView's custom
    // icons and RichTextBox's run colours do; the other Dc() callers only
    // null-check it or push a transform/clip, which carries no colour.
    //
    // Composition: a parent renders a child through a context obtained from
    // WithOpacity(child->Opacity()), so nested opacities multiply the way they do
    // in WPF/WinUI.
    //
    // Deliberate simplification: true opacity semantics would render the subtree
    // to an intermediate layer and composite that ONCE, so overlapping strokes
    // inside one element don't show through each other. This multiplies per
    // primitive instead. For flat Fluent controls (a fill plus a border plus text,
    // which barely overlap) the result is visually identical at a fraction of the
    // cost — no D2D layer, no extra surface, no fill-rate hit. Where exact
    // subtree-composite semantics matter, put the element on its own composition
    // visual and use ICompositionVisual::SetOpacity (no control needs that today).
    float Opacity() const { return opacity_; }

    // A copy of this context whose opacity is multiplied by `factor`. Cheap (the
    // context is a handful of scalars + borrowed pointers).
    [[nodiscard]] DrawingContext WithOpacity(float factor) const {
        DrawingContext out = *this;
        out.opacity_ = Clamp01(opacity_ * Clamp01(factor));
        return out;
    }

    // A copy whose clip hint is narrowed to its intersection with `r` (window DIPs).
    // For a container that clips its children to a viewport: without it a panel
    // nested inside a scroller sees the frame's hint (unbounded on a full frame) and
    // its own bounds, which span the whole scrolled content, so nothing gets culled
    // and every row of a 10000-row StackPanel is drawn to be clipped away. The hint
    // only ever narrows, so a partial-redraw frame keeps its dirty-rect culling.
    [[nodiscard]] DrawingContext WithClipHint(const RectDip& r) const {
        DrawingContext out = *this;
        const float x1 = (clipHint_.x > r.x) ? clipHint_.x : r.x;
        const float y1 = (clipHint_.y > r.y) ? clipHint_.y : r.y;
        const float x2 = (clipHint_.right() < r.right()) ? clipHint_.right() : r.right();
        const float y2 = (clipHint_.bottom() < r.bottom()) ? clipHint_.bottom() : r.bottom();
        // Disjoint -> a rect far outside everything, so intersects() is false for any
        // child (a zero-size rect placed at x1/y1 could still touch one).
        out.clipHint_ = (x2 > x1 && y2 > y1) ? RectDip{x1, y1, x2 - x1, y2 - y1}
                                              : RectDip{-1e9f, -1e9f, 0.0f, 0.0f};
        return out;
    }

    // A copy whose clip hint is REPLACED by `r` — for a container that draws its
    // content under a transform (Viewbox), where the frame's window-space hint must
    // be mapped into the content's own space rather than intersected with it.
    [[nodiscard]] DrawingContext WithMappedClipHint(const RectDip& r) const {
        DrawingContext out = *this;
        out.clipHint_ = r;
        return out;
    }

    // `color` with this context's opacity folded into its alpha. Exposed so a
    // control that must reach past the typed helpers (a composited surface
    // building its own context) can stay consistent.
    D2D1_COLOR_F Faded(const D2D1_COLOR_F& color) const {
        if (opacity_ >= 1.0f) return color;
        return D2D1_COLOR_F{color.r, color.g, color.b, color.a * opacity_};
    }

    // --- Fills ------------------------------------------------------------
    void FillRect(const D2D1_RECT_F& r, const D2D1_COLOR_F& color) const {
        if (drawOps_) ++(*drawOps_);
        brush_->SetColor(Faded(color));
        dc_->FillRectangle(r, brush_);
    }
    void FillRoundedRect(const D2D1_ROUNDED_RECT& rr, const D2D1_COLOR_F& color) const {
        if (drawOps_) ++(*drawOps_);
        brush_->SetColor(Faded(color));
        dc_->FillRoundedRectangle(rr, brush_);
    }
    void FillEllipse(const D2D1_ELLIPSE& e, const D2D1_COLOR_F& color) const {
        if (drawOps_) ++(*drawOps_);
        brush_->SetColor(Faded(color));
        dc_->FillEllipse(e, brush_);
    }
    void FillGeometry(ID2D1Geometry* geo, const D2D1_COLOR_F& color) const {
        if (drawOps_) ++(*drawOps_);
        brush_->SetColor(Faded(color));
        dc_->FillGeometry(geo, brush_);
    }

    // --- Strokes ----------------------------------------------------------
    void DrawRect(const D2D1_RECT_F& r, const D2D1_COLOR_F& color,
                  float strokeWidth = 1.0f) const {
        if (drawOps_) ++(*drawOps_);
        brush_->SetColor(Faded(color));
        dc_->DrawRectangle(r, brush_, strokeWidth);
    }
    void DrawRoundedRect(const D2D1_ROUNDED_RECT& rr, const D2D1_COLOR_F& color,
                         float strokeWidth = 1.0f) const {
        if (drawOps_) ++(*drawOps_);
        brush_->SetColor(Faded(color));
        dc_->DrawRoundedRectangle(rr, brush_, strokeWidth);
    }
    void DrawEllipse(const D2D1_ELLIPSE& e, const D2D1_COLOR_F& color,
                     float strokeWidth = 1.0f) const {
        if (drawOps_) ++(*drawOps_);
        brush_->SetColor(Faded(color));
        dc_->DrawEllipse(e, brush_, strokeWidth);
    }
    void DrawLine(D2D1_POINT_2F p0, D2D1_POINT_2F p1, const D2D1_COLOR_F& color,
                  float strokeWidth = 1.0f) const {
        if (drawOps_) ++(*drawOps_);
        brush_->SetColor(Faded(color));
        dc_->DrawLine(p0, p1, brush_, strokeWidth);
    }
    void DrawGeometry(ID2D1Geometry* geo, const D2D1_COLOR_F& color,
                      float strokeWidth = 1.0f) const {
        if (drawOps_) ++(*drawOps_);
        brush_->SetColor(Faded(color));
        dc_->DrawGeometry(geo, brush_, strokeWidth);
    }

    // --- Text -------------------------------------------------------------
    void DrawText(const wchar_t* text, UINT32 len, IDWriteTextFormat* fmt,
                  const D2D1_RECT_F& layoutRect, const D2D1_COLOR_F& color,
                  D2D1_DRAW_TEXT_OPTIONS opts = D2D1_DRAW_TEXT_OPTIONS_NONE) const {
        if (drawOps_) ++(*drawOps_);
        brush_->SetColor(Faded(color));
        dc_->DrawText(text, len, fmt, layoutRect, brush_, opts);
    }
    void DrawTextLayout(D2D1_POINT_2F origin, IDWriteTextLayout* layout,
                        const D2D1_COLOR_F& color,
                        D2D1_DRAW_TEXT_OPTIONS opts = D2D1_DRAW_TEXT_OPTIONS_NONE) const {
        if (drawOps_) ++(*drawOps_);
        brush_->SetColor(Faded(color));
        dc_->DrawTextLayout(origin, layout, brush_, opts);
    }

    // --- Clip / transform -------------------------------------------------
    // Scope an axis-aligned clip to the returned guard's lifetime (always
    // PER_PRIMITIVE antialiasing, the only mode any control uses).
    [[nodiscard]] ClipGuard PushClip(const D2D1_RECT_F& r) const {
        return ClipGuard(dc_, r);
    }
    // Scope a transform (pre-multiplied onto the current one) to the guard's
    // lifetime — used to position a cached unit-space glyph geometry.
    [[nodiscard]] TransformGuard PushTransform(const D2D1_MATRIX_3X2_F& m) const {
        return TransformGuard(dc_, m);
    }

    // --- Accessors --------------------------------------------------------
    float DpiScale() const { return dpiScale_; }

    // The dirty region hint in DIPs. Panel::Render skips children
    // whose bounds don't intersect this rect. Defaults to effectively unbounded
    // (all children visited) for a full-frame draw; the host narrows it on a
    // partial-redraw frame to the dirty rect union.
    const RectDip& ClipHint() const { return clipHint_; }

    // Escape hatch to the raw context, for host frame chrome and for the few
    // controls that need something the typed methods do not offer (bitmaps,
    // per-range text brushes, transforms, clips). Prefer the typed methods: a
    // control that draws colour or a bitmap through this must apply Opacity() /
    // Faded() itself, because nothing between it and D2D will.
    ID2D1DeviceContext* Dc() const { return dc_; }

private:
    static float Clamp01(float v) {
        return (v < 0.0f) ? 0.0f : (v > 1.0f ? 1.0f : v);
    }

    ID2D1DeviceContext* dc_ = nullptr;       // frame device context (host-owned)
    ID2D1SolidColorBrush* brush_ = nullptr;  // shared brush; set via the methods
    float dpiScale_ = 1.0f;                  // dpi / 96
    uint32_t* drawOps_ = nullptr;            // per-frame draw-call counter
    RectDip clipHint_;                       // dirty region for Panel culling
    float opacity_ = 1.0f;                   // alpha multiplier for every color
};

} // namespace fluent
