// ScrollViewer.h — Minimal Fluent scroll container state + scrollbar renderer.
#pragma once

#include "../core/Control.h"
#include "../base/Event.h"
#include "../input/RoutedEvent.h"

namespace fluent {

struct ScrollChangedArgs {
    float offsetOld = 0.0f;    // 垂直：滚动前
    float offsetNew = 0.0f;    // 垂直：滚动后
    float offsetXOld = 0.0f;   // 水平：滚动前
    float offsetXNew = 0.0f;   // 水平：滚动后
};

class ScrollViewer : public Control {
public:
    void SetContentHeight(float heightDip);
    // Set the offset immediately (no animation). Cancels any in-flight smooth
    // scroll and snaps the animation target to the new offset. Use for drag,
    // keyboard navigation, and EnsureVisible where the move should be instant.
    void SetOffset(float offsetDip);
    void ScrollBy(float deltaDip);
    float Offset() const { return offset_; }
    float MaxOffset() const;
    RectDip ThumbRect() const;
    RectDip TrackRect() const;
    bool HitThumb(float dipX, float dipY) const;
    void BeginDrag(float dipY);
    void DragTo(float dipY);
    // End a drag; resume hover-driven expand and the idle fade countdown.
    void EndDrag() { dragging_ = false; expandTarget_ = barHover_ ? 1.0f : 0.0f; idleSec_ = 0.0f; }
    bool IsDragging() const { return dragging_; }

    // --- Horizontal axis (NoWrap text; list controls leave it at zero) -------
    // A second, independent axis rather than a second ScrollViewer instance, because
    // the two bars must share the fade state: an editor that scrolls both ways would
    // otherwise show one rail fading while the other stays lit, and the shared idle
    // countdown is what makes them appear and disappear together. Only the EXPAND
    // (thin rail -> pill) is per-axis, since that follows which rail the pointer is
    // actually near.
    //
    // Everything here is inert until SetContentWidth reports content wider than the
    // bounds, so TreeView and a wrapping TextArea are unaffected: MaxOffsetX() is 0,
    // HThumbRect() is empty, and Render draws nothing horizontal.
    void SetContentWidth(float widthDip);
    void SetOffsetX(float offsetDip);
    float OffsetX() const { return offsetX_; }
    float MaxOffsetX() const;
    bool HorizontallyScrollable() const { return MaxOffsetX() > 0.0f; }
    RectDip HThumbRect() const;
    // Length of the horizontal track. Shortened by the vertical rail's footprint when
    // BOTH bars are live, so the two do not overlap in the corner. Public because
    // controls that implement click-to-position need the track extent, not just the thumb.
    float HTrackLength() const;
    // Full horizontal track rect, in the SAME coordinate space as HThumbRect. Prefer this
    // over HTrackLength when converting a click position into an offset: the track does not
    // start at the owning control's left edge whenever the ScrollViewer was arranged into a
    // sub-rect of it (DataGrid does exactly that to exclude frozen columns), and using the
    // control's bounds instead skews the mapping by that inset.
    RectDip HTrackRect() const;
    bool HitHThumb(float dipX, float dipY) const;
    bool HitHBarRegion(float dipX, float dipY) const;
    void SetHBarHover(bool over);
    void BeginHDrag(float dipX);
    void HDragTo(float dipX);
    void EndHDrag() { draggingX_ = false; expandXTarget_ = barHoverX_ ? 1.0f : 0.0f; idleSec_ = 0.0f; }
    bool IsHDragging() const { return draggingX_; }
    // Horizontal expand factor, for a composited overlay's change signature.
    float HExpandFactor() const { return expandX_; }

    // --- Middle-button autoscroll (browser-style: click to toggle, speed by distance) --
    // Begin autoscroll mode: record the anchor point where the user clicked. Does NOT
    // capture the pointer — the user can move the mouse freely, and the scroll speed
    // depends on how far the pointer is from the anchor.
    void BeginAutoscroll(float anchorX, float anchorY);
    // Update velocity based on current pointer position relative to the anchor. Called
    // from OnPointerMoved while autoscrolling. The farther the pointer from the anchor,
    // the faster the scroll.
    void UpdateAutoscrollVelocity(float pointerX, float pointerY);
    // Apply the current velocity to the scroll offsets. Called from OnAnimationTick.
    // Returns true if still autoscrolling (velocity non-zero or just started).
    bool TickAutoscroll(float dtSec);
    // End autoscroll mode. The scroll stops immediately.
    void EndAutoscroll() { autoscrolling_ = false; autoscrollVelocityX_ = autoscrollVelocityY_ = 0.0f; }
    bool IsAutoscrolling() const { return autoscrolling_; }
    // The anchor point in window DIPs, for rendering an indicator (if desired).
    float AutoscrollAnchorX() const { return autoscrollAnchorX_; }
    float AutoscrollAnchorY() const { return autoscrollAnchorY_; }
    // Get the appropriate cursor for the current autoscroll state based on pointer position.
    // Returns the direction-indicating cursor (up/down/left/right/diagonal arrows) when
    // outside the dead zone, or the four-way anchor cursor when in the dead zone.
    HCURSOR AutoscrollCursor(float pointerX, float pointerY) const;

    // --- Smooth (animated) scrolling --------------------------------------
    // Animate toward an absolute offset / by a delta. Successive AnimateBy calls
    // accumulate against the pending target, so spinning the wheel keeps building
    // momentum instead of restarting from the current position. The owning
    // element drives Tick() each frame while NeedsTick() is true.
    void AnimateTo(float offsetDip);
    void AnimateBy(float deltaDip) { AnimateTo(target_ + deltaDip); }
    bool IsAnimating() const { return animating_; }
    // The smooth scroll's destination; == Offset() when settled. Needed by any
    // decision about where a scroll is HEADED rather than where it is: on the frame a
    // wheel notch arrives, Offset() has not moved yet, so judging "did the user
    // scroll away from the bottom" against it always answers "no".
    float TargetOffset() const { return animating_ ? target_ : offset_; }
    // Advance offset + fade/expand animations by dt seconds. Returns true while
    // anything is still in motion (or the idle fade countdown is running).
    bool Tick(float dtSec);
    // True while the bar needs per-frame ticks: an in-flight scroll, an unfinished
    // fade/expand, or a visible bar still counting down to auto-hide.
    bool NeedsTick() const;

    // --- Auto-fade / hover-expand (WinUI-11 style) ------------------------
    // Mark scroll activity: fade the bar in and restart the idle countdown.
    void Wake();
    // Pointer over the bar's hover strip near the right edge: fade in + expand to
    // a pill. Leaving shrinks back to a thin rail and starts the idle fade.
    void SetBarHover(bool over);
    // Wider hit strip near the right edge (for discoverability when thin).
    bool HitBarRegion(float dipX, float dipY) const;
    // Pointer currently on either rail's hover strip (SetBarHover / SetHBarHover).
    // While true the idle countdown is held, so a stale true keeps the bar shown.
    bool IsBarHovered() const { return barHover_ || barHoverX_; }
    // End the fade/expand cycle immediately: bars hidden, thin, not hovered, no
    // countdown, so NeedsTick() is false unless a scroll is still in flight. For a
    // host whose content stopped overflowing while the bar was awake: there is no
    // rail left to draw (Render returns early without overflow), so easing the
    // remaining ~1.5 s of fade towards nothing only spent frames. The next Wake on
    // renewed overflow starts the bar from hidden, as if it had faded out.
    void HideBarsNow();

    void Render(const DrawingContext& dc) override;

    // Current scrollbar visual state (0..1), for a compositor host that caches the
    // overlay surface and only re-rasterizes when these actually change (avoids
    // redrawing a static bar every frame during the idle-hide countdown).
    float Visibility() const { return visibility_; }
    float ExpandFactor() const { return expand_; }

    // When true and content overflows the viewport, the scrollbar never fully
    // fades out — it settles at a minimum visibility so the user can always see
    // that scrolling is available. When false (default), the bar hides completely
    // after the idle countdown, matching the classic overlay-scrollbar behavior.
    void SetKeepVisibleWhenOverflow(bool on) { keepVisibleWhenOverflow_ = on; }

    // Tune fade timing: how long the bar stays visible after the last activity
    // (seconds, default 1.2) and how fast it fades in/out (time constant in
    // seconds, default 0.06 — smaller is faster). Use longer idle timeouts for
    // controls where the bar is primarily navigational (a log view), shorter for
    // controls where it is incidental (a settings panel).
    void SetIdleHideDelay(float seconds) { idleHideDelaySec_ = seconds; }
    void SetFadeSpeed(float tauSeconds) { fadeTau_ = tauSeconds; }
    // Override the scrollbar color (default: theme's textPrimary). Use when the
    // control's background differs from the theme default (e.g. black LogViewer
    // in a light theme) and the automatic color would be invisible.
    void SetScrollbarColor(D2D1::ColorF color) { scrollbarColor_ = color; hasCustomScrollbarColor_ = true; }

    // Fired when the scroll offset changes (SetOffset, ScrollBy, DragTo, HDragTo,
    // or animated scroll completes). Args carry the old and new offsets for both
    // axes. Use for "scroll to bottom triggers load more", syncing two scrollers,
    // or updating a virtualizing list's visible range.
    Event<ScrollViewer, ScrollChangedArgs>& ScrollChanged() { return scrollChanged_; }

protected:
    // Re-clamp BOTH axes: a resize changes each viewport extent, so an offset that
    // was legal a moment ago can now point past the end of its content.
    void OnBoundsChanged() override {
        LayoutCostProbe::Scope probe(LayoutCostKey::ScrollViewerBoundsChanged);
        SetOffset(offset_);
        SetOffsetX(offsetX_);
    }

private:
    float BarWidth() const;   // current width (DIP), lerped thin<->wide
    float HBarThickness() const;  // horizontal rail thickness (DIP)

    float contentHeight_ = 0.0f;
    float offset_ = 0.0f;
    float target_ = 0.0f;      // smooth-scroll destination
    bool animating_ = false;
    bool dragging_ = false;
    float dragStartY_ = 0.0f;
    float dragStartOffset_ = 0.0f;

    // Horizontal axis. No smooth-scroll target: horizontal movement comes from
    // Shift+wheel, a thumb drag and caret-following, none of which want inertia —
    // and a tween would force hit-testing to ask "where is the horizontal scroll
    // RIGHT NOW" mid-flight, which is the one bug class the vertical axis had to
    // solve with EffectiveOffset(). Not having the tween removes the question.
    float contentWidth_ = 0.0f;
    float offsetX_ = 0.0f;
    bool draggingX_ = false;
    float dragStartX_ = 0.0f;
    float dragStartOffsetX_ = 0.0f;
    float expandX_ = 0.0f;
    float expandXTarget_ = 0.0f;
    bool barHoverX_ = false;

    // Middle-button autoscroll state (browser-style: click to toggle, speed by distance).
    bool autoscrolling_ = false;
    float autoscrollAnchorX_ = 0.0f;
    float autoscrollAnchorY_ = 0.0f;
    float autoscrollVelocityX_ = 0.0f;  // DIP/sec
    float autoscrollVelocityY_ = 0.0f;  // DIP/sec

    // Fade + expand state (0..1). visibility_ = opacity; expand_ = thin->pill.
    float visibility_ = 0.0f;
    float visTarget_ = 0.0f;
    float expand_ = 0.0f;
    float expandTarget_ = 0.0f;
    float idleSec_ = 0.0f;     // seconds since the last activity
    bool barHover_ = false;
    bool keepVisibleWhenOverflow_ = false;  // never fully hide when content overflows
    float idleHideDelaySec_ = 1.2f;  // auto-hide after this idle time
    float fadeTau_ = 0.06f;          // fade/expand time constant (smaller = faster)

    // Scrollbar ink. Defaults to the theme's textPrimary, which is correct whenever
    // the scrolled surface follows the theme. A control that paints its own
    // background regardless of theme (LogViewer's terminal black in a light theme)
    // breaks that assumption — near-black ink at 35% alpha on black is invisible —
    // and sets this instead.
    D2D1_COLOR_F scrollbarColor_ = D2D1::ColorF(D2D1::ColorF::White);
    bool hasCustomScrollbarColor_ = false;

    Event<ScrollViewer, ScrollChangedArgs> scrollChanged_;
};

} // namespace fluent
