// ScrollViewer.cpp

#include "ScrollViewer.h"
#include "../styling/ThemeTokens.h"
#include <algorithm>
#include <cmath>

namespace fluent {

namespace {
// Bar metrics (DIP). Thin rail when idle; wide pill on hover/drag.
constexpr float kThinW = 3.0f;
constexpr float kWideW = 7.0f;
constexpr float kRightPad = 3.0f;      // gap from the right edge
constexpr float kHoverStripW = 16.0f;  // pointer hit strip near the right edge
constexpr float kMinThumbH = 24.0f;
constexpr float kIdleHideSec = 1.2f;   // auto-hide after this idle time
constexpr float kFadeTau = 0.06f;      // fade/expand time constant (s)
} // namespace

void ScrollViewer::SetContentHeight(float heightDip)
{
    contentHeight_ = std::max(0.0f, heightDip);
    // Clamp the current offset to the new valid range, but DON'T cancel an in-flight
    // smooth scroll — Tick already clamps target_ on its own (line 145+). The old
    // SetOffset(offset_) call turned every height change into an animation cancel,
    // causing DisplayChange's rapid wheel input to feel sluggish: each notch restarted
    // from the current interpolated position (18.5) instead of accumulating momentum
    // against the pending target (114.5), losing ~90 DIP per notch.
    float newMax = MaxOffset();
    if (offset_ > newMax) {
        offset_ = newMax;
        if (animating_ && target_ > newMax)
            target_ = newMax;
        Invalidate();
        if (scrollChanged_.HasSubscribers()) {
            ScrollChangedArgs args;
            args.offsetOld = offset_;  // best-effort: we don't have the true old value here
            args.offsetNew = offset_;
            args.offsetXOld = offsetX_;
            args.offsetXNew = offsetX_;
            scrollChanged_.Raise(*this, args);
        }
    }
}

float ScrollViewer::MaxOffset() const
{
    return std::max(0.0f, contentHeight_ - bounds_.h);
}

void ScrollViewer::SetOffset(float offsetDip)
{
    float oldOffset = offset_;
    float oldOffsetX = offsetX_;
    float next = std::clamp(offsetDip, 0.0f, MaxOffset());
    // A direct set cancels any smooth scroll (drag, keyboard nav, EnsureVisible).
    target_ = next;
    animating_ = false;
    if (next == offset_) return;
    offset_ = next;
    Invalidate();
    // Fire ScrollChanged only if we have subscribers (avoid constructing args).
    if (scrollChanged_.HasSubscribers()) {
        ScrollChangedArgs args;
        args.offsetOld = oldOffset;
        args.offsetNew = offset_;
        args.offsetXOld = oldOffsetX;
        args.offsetXNew = offsetX_;
        scrollChanged_.Raise(*this, args);
    }
}

void ScrollViewer::ScrollBy(float deltaDip)
{
    SetOffset(offset_ + deltaDip);
}

// ---------------------------------------------------------------------------
// Horizontal axis. Deliberately no smooth-scroll counterpart: see the header.
// ---------------------------------------------------------------------------

void ScrollViewer::SetContentWidth(float widthDip)
{
    contentWidth_ = std::max(0.0f, widthDip);
    SetOffsetX(offsetX_);   // the new extent may have made the current offset illegal
}

float ScrollViewer::MaxOffsetX() const
{
    return std::max(0.0f, contentWidth_ - bounds_.w);
}

void ScrollViewer::SetOffsetX(float offsetDip)
{
    float oldOffset = offset_;
    float oldOffsetX = offsetX_;
    float next = std::clamp(offsetDip, 0.0f, MaxOffsetX());
    if (next == offsetX_) return;
    offsetX_ = next;
    Invalidate();
    if (scrollChanged_.HasSubscribers()) {
        ScrollChangedArgs args;
        args.offsetOld = oldOffset;
        args.offsetNew = offset_;
        args.offsetXOld = oldOffsetX;
        args.offsetXNew = offsetX_;
        scrollChanged_.Raise(*this, args);
    }
}

void ScrollViewer::SetHBarHover(bool over)
{
    if (over == barHoverX_) return;
    barHoverX_ = over;
    expandXTarget_ = over ? 1.0f : 0.0f;
    if (over) { idleSec_ = 0.0f; visTarget_ = 1.0f; }
    Invalidate();
}

void ScrollViewer::AnimateTo(float offsetDip)
{
    target_ = std::clamp(offsetDip, 0.0f, MaxOffset());
    animating_ = (target_ != offset_);
    if (animating_) Wake();
}

void ScrollViewer::Wake()
{
    idleSec_ = 0.0f;
    visTarget_ = 1.0f;
    Invalidate();
}

void ScrollViewer::SetBarHover(bool over)
{
    if (over == barHover_) return;
    barHover_ = over;
    expandTarget_ = over ? 1.0f : 0.0f;
    if (over) { idleSec_ = 0.0f; visTarget_ = 1.0f; }
    Invalidate();
}

bool ScrollViewer::NeedsTick() const
{
    if (animating_) return true;
    if (std::abs(visibility_ - visTarget_) > 0.01f) return true;
    if (std::abs(expand_ - expandTarget_) > 0.01f) return true;
    if (std::abs(expandX_ - expandXTarget_) > 0.01f) return true;
    // A visible bar with no pointer on EITHER rail and no drag on either is still
    // counting down to hide. Both axes have to be quiet: hovering the horizontal rail
    // must keep the shared fade alive, or the bar the pointer is sitting on vanishes.
    if (visibility_ > 0.01f && !barHover_ && !barHoverX_ && !dragging_ && !draggingX_ &&
        !autoscrolling_ && idleSec_ < idleHideDelaySec_)
        return true;
    return false;
}

bool ScrollViewer::Tick(float dtSec)
{
    // Smooth-scroll offset.
    bool wasAnimating = animating_;
    float oldOffset = offset_;
    if (animating_) {
        target_ = std::clamp(target_, 0.0f, MaxOffset());
        float diff = target_ - offset_;
        float t = 1.0f - std::exp(-dtSec / 0.045f);
        if (std::abs(diff) <= 0.5f) { offset_ = target_; animating_ = false; }
        else offset_ += diff * t;
    }

    // Idle countdown -> auto-hide (unless hovering, dragging, or autoscrolling).
    if (!barHover_ && !barHoverX_ && !dragging_ && !draggingX_ && !autoscrolling_) {
        idleSec_ += dtSec;
        if (idleSec_ >= idleHideDelaySec_) visTarget_ = 0.0f;
    }

    // Fade + expand easing (framerate-independent). One fade, two expands.
    float f = 1.0f - std::exp(-dtSec / fadeTau_);
    visibility_ += (visTarget_ - visibility_) * f;
    expand_ += (expandTarget_ - expand_) * f;
    expandX_ += (expandXTarget_ - expandX_) * f;
    if (std::abs(visTarget_ - visibility_) <= 0.01f) visibility_ = visTarget_;
    if (std::abs(expandTarget_ - expand_) <= 0.01f) expand_ = expandTarget_;
    if (std::abs(expandXTarget_ - expandX_) <= 0.01f) expandX_ = expandXTarget_;

    // Fire ScrollChanged only once when animation completes (not every frame).
    if (wasAnimating && !animating_ && scrollChanged_.HasSubscribers()) {
        ScrollChangedArgs args;
        args.offsetOld = oldOffset;
        args.offsetNew = offset_;
        args.offsetXOld = offsetX_;
        args.offsetXNew = offsetX_;
        scrollChanged_.Raise(*this, args);
    }

    Invalidate();
    return NeedsTick();
}

float ScrollViewer::BarWidth() const
{
    return kThinW + (kWideW - kThinW) * expand_;
}

RectDip ScrollViewer::ThumbRect() const
{
    if (contentHeight_ <= bounds_.h || bounds_.h <= 0.0f) return {};
    float w = BarWidth();
    float ratio = bounds_.h / contentHeight_;
    float thumbH = std::max(kMinThumbH, bounds_.h * ratio);
    float maxOffset = std::max(1.0f, MaxOffset());
    float thumbY = bounds_.y + (bounds_.h - thumbH) * (offset_ / maxOffset);
    return {bounds_.right() - w - kRightPad, thumbY, w, thumbH};
}

RectDip ScrollViewer::TrackRect() const
{
    float w = BarWidth();
    return {bounds_.right() - w - kRightPad, bounds_.y, w, bounds_.h};
}

bool ScrollViewer::HitThumb(float dipX, float dipY) const
{
    RectDip r = ThumbRect();
    return r.contains(dipX, dipY);
}

bool ScrollViewer::HitBarRegion(float dipX, float dipY) const
{
    if (contentHeight_ <= bounds_.h || bounds_.h <= 0.0f) return false;
    // A wider strip along the right edge than the visible rail, so the pointer
    // finds the bar even while it is thin/faded.
    RectDip strip = {bounds_.right() - kHoverStripW, bounds_.y,
                     kHoverStripW, bounds_.h};
    return strip.contains(dipX, dipY);
}

void ScrollViewer::BeginDrag(float dipY)
{
    dragging_ = true;
    dragStartY_ = dipY;
    dragStartOffset_ = offset_;
    idleSec_ = 0.0f;
    visTarget_ = 1.0f;
    expandTarget_ = 1.0f;   // stay wide while dragging
    Invalidate();
}

void ScrollViewer::DragTo(float dipY)
{
    if (!dragging_) return;
    RectDip thumb = ThumbRect();
    float trackRange = std::max(1.0f, bounds_.h - thumb.h);
    float contentRange = MaxOffset();
    SetOffset(dragStartOffset_ + (dipY - dragStartY_) * contentRange / trackRange);
}

// Browser-style autoscroll: click once to start, velocity scales with distance from
// the anchor, click again (or press another button) to stop. This is the middle-click
// behavior in Chrome/Edge/Firefox — the anchor stays fixed, the pointer roams free,
// and the view scrolls continuously until explicitly canceled.
//
// Dead zone prevents drift when the pointer sits near the anchor. The gain constant
// (pixels-per-second per DIP of distance) and max speed are tuned to feel responsive
// without being twitchy; adjust if needed after testing on real content.
void ScrollViewer::BeginAutoscroll(float anchorX, float anchorY)
{
    autoscrolling_ = true;
    autoscrollAnchorX_ = anchorX;
    autoscrollAnchorY_ = anchorY;
    autoscrollVelocityX_ = 0.0f;
    autoscrollVelocityY_ = 0.0f;
    Wake();  // keep scrollbar visible while autoscrolling
}

void ScrollViewer::UpdateAutoscrollVelocity(float pointerX, float pointerY)
{
    if (!autoscrolling_) return;
    constexpr float kDeadZone = 10.0f;      // DIP: no scroll within this radius
    constexpr float kGain = 15.0f;          // DIP/sec per DIP of distance
    constexpr float kMaxSpeed = 2000.0f;    // DIP/sec cap

    const float dx = pointerX - autoscrollAnchorX_;
    const float dy = pointerY - autoscrollAnchorY_;
    const float dist = std::sqrt(dx * dx + dy * dy);

    if (dist <= kDeadZone) {
        autoscrollVelocityX_ = autoscrollVelocityY_ = 0.0f;
        return;
    }

    const float speed = std::min((dist - kDeadZone) * kGain, kMaxSpeed);
    autoscrollVelocityX_ = (dx / dist) * speed;
    autoscrollVelocityY_ = (dy / dist) * speed;
}

bool ScrollViewer::TickAutoscroll(float dtSec)
{
    if (!autoscrolling_) return false;
    // Apply velocity. SetOffset clamps to [0, MaxOffset], so hitting a boundary stops
    // that axis naturally without explicit checks.
    if (std::abs(autoscrollVelocityX_) > 0.1f || std::abs(autoscrollVelocityY_) > 0.1f) {
        SetOffset(offset_ + autoscrollVelocityY_ * dtSec);
        SetOffsetX(offsetX_ + autoscrollVelocityX_ * dtSec);
        Wake();  // reset idle countdown on every scroll frame
    }
    return true;  // still active, keep ticking
}

HCURSOR ScrollViewer::AutoscrollCursor(float pointerX, float pointerY) const
{
    if (!autoscrolling_) return nullptr;

    constexpr float kDeadZone = 10.0f;
    const float dx = pointerX - autoscrollAnchorX_;
    const float dy = pointerY - autoscrollAnchorY_;
    const float dist = std::sqrt(dx * dx + dy * dy);

    // In the dead zone: show the four-way anchor cursor.
    if (dist <= kDeadZone) {
        static HCURSOR sizeAll = LoadCursor(nullptr, IDC_SIZEALL);
        return sizeAll;
    }

    // Outside dead zone: show direction-indicating cursor based on the dominant direction.
    // Use 8-way compass rose: N, NE, E, SE, S, SW, W, NW.
    // Angle = atan2(dy, dx) in radians, then map to 8 directions.
    const float angle = std::atan2(dy, dx);  // -π to π
    const float deg = angle * 180.0f / 3.14159f;  // convert to degrees

    // Map to 8 sectors: each 45° wide, centered on cardinal/diagonal directions.
    // Right (E): -22.5° to 22.5°
    // Down-Right (SE): 22.5° to 67.5°
    // Down (S): 67.5° to 112.5°
    // Down-Left (SW): 112.5° to 157.5°
    // Left (W): 157.5° to 180° and -180° to -157.5°
    // Up-Left (NW): -157.5° to -112.5°
    // Up (N): -112.5° to -67.5°
    // Up-Right (NE): -67.5° to -22.5°

    static HCURSOR sizeWE = LoadCursor(nullptr, IDC_SIZEWE);      // horizontal ↔
    static HCURSOR sizeNS = LoadCursor(nullptr, IDC_SIZENS);      // vertical ↕
    static HCURSOR sizeNWSE = LoadCursor(nullptr, IDC_SIZENWSE);  // diagonal ↖↘
    static HCURSOR sizeNESW = LoadCursor(nullptr, IDC_SIZENESW);  // diagonal ↗↙

    if (deg >= -22.5f && deg < 22.5f) {
        return sizeWE;  // Right (E)
    } else if (deg >= 22.5f && deg < 67.5f) {
        return sizeNWSE;  // Down-Right (SE) → ↘
    } else if (deg >= 67.5f && deg < 112.5f) {
        return sizeNS;  // Down (S)
    } else if (deg >= 112.5f && deg < 157.5f) {
        return sizeNESW;  // Down-Left (SW) → ↙
    } else if (deg >= 157.5f || deg < -157.5f) {
        return sizeWE;  // Left (W)
    } else if (deg >= -157.5f && deg < -112.5f) {
        return sizeNWSE;  // Up-Left (NW) → ↖
    } else if (deg >= -112.5f && deg < -67.5f) {
        return sizeNS;  // Up (N)
    } else {  // deg >= -67.5f && deg < -22.5f
        return sizeNESW;  // Up-Right (NE) → ↗
    }
}

float ScrollViewer::HBarThickness() const
{
    return kThinW + (kWideW - kThinW) * expandX_;
}

float ScrollViewer::HTrackLength() const
{
    // Leave the corner to the vertical rail when both are live. Reserve the rail's
    // WIDE width, not its current one, so the track length does not change while the
    // vertical bar expands under the pointer — a track that breathes would slide the
    // horizontal thumb sideways for no reason the user can see.
    const bool vertical = contentHeight_ > bounds_.h && bounds_.h > 0.0f;
    const float reserve = vertical ? kWideW + kRightPad * 2.0f : 0.0f;
    return std::max(1.0f, bounds_.w - reserve);
}

RectDip ScrollViewer::HTrackRect() const
{
    const float trackLen = HTrackLength();
    const float th = HBarThickness();
    return {bounds_.x, bounds_.bottom() - th - kRightPad, trackLen, th};
}

RectDip ScrollViewer::HThumbRect() const
{
    if (contentWidth_ <= bounds_.w || bounds_.w <= 0.0f) return {};
    const float th = HBarThickness();
    const float trackLen = HTrackLength();
    const float ratio = bounds_.w / contentWidth_;
    const float thumbW = std::max(kMinThumbH, trackLen * ratio);
    const float maxOffset = std::max(1.0f, MaxOffsetX());
    const float thumbX = bounds_.x + (trackLen - thumbW) * (offsetX_ / maxOffset);
    return {thumbX, bounds_.bottom() - th - kRightPad, thumbW, th};
}

bool ScrollViewer::HitHThumb(float dipX, float dipY) const
{
    RectDip r = HThumbRect();
    return r.contains(dipX, dipY);
}

bool ScrollViewer::HitHBarRegion(float dipX, float dipY) const
{
    if (contentWidth_ <= bounds_.w || bounds_.w <= 0.0f) return false;
    RectDip strip = {bounds_.x, bounds_.bottom() - kHoverStripW,
                     HTrackLength(), kHoverStripW};
    return strip.contains(dipX, dipY);
}

void ScrollViewer::BeginHDrag(float dipX)
{
    draggingX_ = true;
    dragStartX_ = dipX;
    dragStartOffsetX_ = offsetX_;
    idleSec_ = 0.0f;
    visTarget_ = 1.0f;
    expandXTarget_ = 1.0f;   // stay wide while dragging
    Invalidate();
}

void ScrollViewer::HDragTo(float dipX)
{
    if (!draggingX_) return;
    RectDip thumb = HThumbRect();
    float trackRange = std::max(1.0f, HTrackLength() - thumb.w);
    float contentRange = MaxOffsetX();
    SetOffsetX(dragStartOffsetX_ + (dipX - dragStartX_) * contentRange / trackRange);
}

void ScrollViewer::Render(const DrawingContext& dc)
{
    // Either axis can be scrollable on its own (a NoWrap text view with long lines
    // that all fit vertically is horizontal-only), so each rail is gated separately
    // rather than by one combined early return.
    const bool vertical = contentHeight_ > bounds_.h && bounds_.h > 0.0f;
    const bool horizontal = contentWidth_ > bounds_.w && bounds_.w > 0.0f;
    if (!vertical && !horizontal) return;

    // When keepVisibleWhenOverflow_ is set and content overflows, the bar never
    // fully hides — it settles at a minimum visibility so the user can always see
    // that scrolling is available. Otherwise it fades to zero after the idle
    // countdown (classic overlay behavior).
    const float minVis = keepVisibleWhenOverflow_ ? 0.35f : 0.0f;
    const float vis = std::max(std::clamp(visibility_, 0.0f, 1.0f), minVis);
    if (vis <= 0.01f) return;  // fully faded out: nothing to draw

    const ThemeSnapshot& th = Theme();
    const ColorTokens& pal = th.colors;

    // When no custom scrollbar color is set, use the theme's textPrimary (near-black
    // in light themes, near-white in dark). When a custom color is set, use that
    // instead — needed for controls that override their background to conflict with
    // the theme (LogViewer's black surface in a light theme would make textPrimary
    // ink invisible).
    D2D1_COLOR_F ink = hasCustomScrollbarColor_ ? scrollbarColor_
        : D2D1::ColorF(pal.textPrimary.r, pal.textPrimary.g, pal.textPrimary.b);

    const float trackBase = th.dark ? 0.12f : 0.08f;
    const float idleBase = th.dark ? 0.40f : 0.32f;

    if (vertical) {
        // Track groove: a faint rounded channel behind the thumb, fading in as the
        // bar expands toward its pill form (invisible for the thin idle rail).
        if (expand_ > 0.01f) {
            RectDip tr = TrackRect();
            float trackAlpha = trackBase * expand_ * vis;
            float r = tr.w * 0.5f;
            dc.FillRoundedRect(
                D2D1::RoundedRect(D2D1::RectF(tr.x, tr.y, tr.right(), tr.bottom()), r, r),
                D2D1::ColorF(ink.r, ink.g, ink.b, trackAlpha));
        }

        // Thumb: a pill (corner = half width), opacity driven by fade + drag/hover.
        RectDip thumb = ThumbRect();
        float base = dragging_ ? 0.65f : (barHover_ ? 0.55f : idleBase);
        float rad = thumb.w * 0.5f;
        dc.FillRoundedRect(
            D2D1::RoundedRect(D2D1::RectF(thumb.x, thumb.y, thumb.right(), thumb.bottom()),
                              rad, rad),
            D2D1::ColorF(ink.r, ink.g, ink.b, base * vis));
    }

    if (horizontal) {
        if (expandX_ > 0.01f) {
            const float t = HBarThickness();
            RectDip tr = {bounds_.x, bounds_.bottom() - t - kRightPad, HTrackLength(), t};
            float trackAlpha = trackBase * expandX_ * vis;
            float r = tr.h * 0.5f;
            dc.FillRoundedRect(
                D2D1::RoundedRect(D2D1::RectF(tr.x, tr.y, tr.right(), tr.bottom()), r, r),
                D2D1::ColorF(ink.r, ink.g, ink.b, trackAlpha));
        }

        RectDip thumb = HThumbRect();
        float base = draggingX_ ? 0.65f : (barHoverX_ ? 0.55f : idleBase);
        float rad = thumb.h * 0.5f;
        dc.FillRoundedRect(
            D2D1::RoundedRect(D2D1::RectF(thumb.x, thumb.y, thumb.right(), thumb.bottom()),
                              rad, rad),
            D2D1::ColorF(ink.r, ink.g, ink.b, base * vis));
    }
}

} // namespace fluent
