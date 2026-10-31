---
paths:
  - "FluentUI/window/**"
  - "FluentUI/animation/**"
  - "FluentUI/diagnostics/**"
  - "FluentUI/Application.*"
  - "FluentUIDemo/**"
  - "FluentUIBench/**"
---

### The frame pipeline

Nothing paints synchronously from the input path. The flow is:

1. Something changes → `SetProperty(field, value, DirtyFlags::X)` records dirty
   flags and calls `InvalidateDirty`, which walks `Parent()` to the root's
   invalidate callback → `RequestFrame(reason)`.
2. `FrameScheduler` (`window/FrameScheduler.h`) coalesces: all requests between
   two frames OR into one pending frame with a merged `FrameReason` bitmask.
3. The message loop is `Application::PumpOneTurn` (`Application.cpp`; the demo
   just calls `app.Run()`). Per turn: keep a frame pending for any running
   animation clock → wait on `FrameWaiter` with the soonest
   `FrameWaitRemaining()` (`MsgWaitForMultipleObjectsEx` plus a high-resolution
   waitable timer) → drain *all* queued messages → for each window whose frame is
   due (`FrameIsDue`), `PumpAnimations()` then `RunDueFrame()`, at most once. The
   tick sits next to the paint, not at the top of the turn: a turn ends on any
   input, so a 1000 Hz mouse would otherwise tick ~8x per painted frame at 120 Hz.
4. `RenderNow` applies any pending resize, runs layout if Measure/Arrange is
   dirty anywhere, plans the redraw region, paints, and commits.

`DirtyFlags` expand to their closure: Measure ⊃ Arrange ⊃ Render
(`core/Invalidation.h`). Pick the highest-level flag the change actually needs —
tagging a render-only change as Measure collapses the frame into a full-window
repaint.

Idle means no pending frame and no active animation, and it must cost zero CPU:
the loop blocks on input with `INFINITE`. When a frame *is* pending it waits only
until the frame is due (`FrameWaitRemaining()` / `window/FramePacing.h` /
`window/FrameWaiter.h`; the whole-millisecond `FrameWaitTimeoutMs()` is only for
the legacy self-frame timer, since integer waits capped 120 Hz at ~68 fps). This
throttle is load-bearing — `Commit()` doesn't block on vsync the way the old swap
chain's `Present1` did, so without it the loop free-runs (measured ~1927 fps on a
120 Hz panel, invisible on screen but burning a core and corrupting every
diagnostic number).

**The HUD runs off the UI thread.** `window/HudOverlay.h` draws it on its own
thread, device and *topmost* DComp target (the content target is the non-topmost
one), ~10 Hz. The UI thread only publishes a snapshot at the end of each painted
frame (`diagnostics/HudFeed.h`). Two consequences when reading it:

- **Opening it changes nothing it measures.** It does not request frames and does
  not force full redraws (it used to do both, so its numbers described its own
  continuous full-window repaint). An idle window therefore stays idle: the frame
  lines freeze at the last active stretch and the bottom line says `idle 2.5s`. To
  read FPS, make the window work — scroll, drag, animate.
- **The bottom line is liveness, not frame data.** The HUD thread posts a ping
  (`WM_FLUENT_HUD_PING`, one in flight at most) and shows the round trip. An
  unanswered ping past 250 ms shows `UI STALLED N ms` while the stall is still
  happening, which the frame rings cannot do — they are written by the thread that
  is stuck. Frame age is not used for this because an idle window paints nothing.

**Reading the HUD (F12) when frames are slow.** Frame times under real load are
often *bimodal*: a few catastrophic frames interleaved with a burst of trivial
ones. Three things exist because of that, and the reasoning is worth keeping:

- **Never gate a diagnostic on a magnitude threshold that "must mean idle".** The
  interval ring used `gap > 500ms → discard as an idle resume`, which cannot tell
  a 3-second idle from a 3-second *frame*. On a resize drag over a large document
  it discarded every real sample and reported four-digit FPS. It now asks the
  scheduler whether the loop was continuously busy
  (`FrameIntervalIsCadenceSample`, fed by `loopWasContinuous_`). Inside the modal
  resize loop that includes a queued mouse move: the next `WM_SIZE` is generated
  only after the frame returns, so `resizePending_` alone read every drag interval
  as idle (HUD: FPS 66 / Max 18ms during 327ms resize frames).
- **Cost that runs outside `RenderNow` needs its own carry-over field.**
  `cpuFrameMs` starts its clock inside `RenderNow`, so `PumpAnimations` (→
  `animationMs`) and `ApplyPendingResize` (→ `resizeMs`) are invisible to it. The
  second one is the *dominant* cost of a resize drag for a text control. Anything
  new that runs before the frame clock needs the same treatment.
- **A median can't see the expensive mode.** The HUD shows CPU P50/P99/Max next to
  the FPS line for exactly this reason. Low interval P50 beside a huge CPU Max
  means the FPS number is describing the cheap mode only — trust the Max.

Related: `ModalFrameHeartbeat` bounds itself to **one unconsumed tick** in the
queue. Posting unconditionally while the UI thread is stalled queues a tick per
refresh for the whole stall, and the backlog then paints back-to-back — pure
waste, and a burst of sub-millisecond intervals that pollutes exactly the numbers
above.
