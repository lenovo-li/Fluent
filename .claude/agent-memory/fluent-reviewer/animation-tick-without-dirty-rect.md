---
name: animation-tick-without-dirty-rect
description: An OnAnimationTick that returns without marking a dirty rect while WantsAnimationTick stays true makes every remaining frame a FULL-window redraw, because PumpAnimations requests the frame regardless
metadata:
  type: project
---

`NativeWindowHost::PumpAnimations` calls `RequestFrame(Animation)` whenever the
active set is still animating, independent of whether any element invalidated.
`RenderNow` has no early-out for an empty dirty set and `PlanRedraw` treats empty
as full. So "this tick marks nothing" does NOT mean "this tick costs nothing": it
means "this frame repaints the whole window".

Seen in: Slider/ProgressBar ticks without `Invalidate()` (fixed), and the
ScrollPanel rail-only path `InvalidateRails()` in `FluentUI/layout/ScrollPanel.h`,
whose comment claims returning early when nothing overflows "requests nothing";
the pump still requests the frame while `scroll_.NeedsTick()` is true.

**Why:** fixers reason about the invalidate call alone and miss that the pump is a
second frame source.

**How to apply:** when a diff makes a tick skip invalidation under some condition,
check whether `WantsAnimationTick()` can still be true under that condition. If
yes, the tick must either mark a (small) dirty rect or make the animation stop.
Related: [[untested-nativewindowhost-behavior]].
