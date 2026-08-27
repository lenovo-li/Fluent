# Async layout: what it was, why it was removed

**Status**: removed 2026-08-17. Do not reintroduce without reading this first.

`UIElement::MeasureCached` and `Window::OnLayout` both carry comments pointing here.

## The idea

Resizing the Gallery window stalled the UI thread for what was believed to be ~7 ms
per `WM_SIZE`. The plan (`AsyncLayoutWorker`) was to run `Measure` on a background
thread so the drag stayed responsive.

`Measure` *writes* (`desired_`, plus four measure-cache fields), so worker and UI
thread were both writers — no reader/writer lock helps. The chosen resolution was
field separation: a `thread_local` flag (`IsAsyncMeasure()`) made every `Measure`
write an `async*` mirror instead, and the UI thread published mirror→real after the
worker finished. That machinery reached 18 controls, 9 panels, `FrameworkElement`,
and three dedicated test files.

## Why it was removed

Per-phase instrumentation of a real `Off`-mode resize drag. Button page (no
`TextArea` at all), typical frame:

```
spawn=0.866  wait=12.103  publish=0.155  arrange=3.938  total=17.062ms
```

TextArea page, worst frame observed:

```
spawn=0.865  wait=17.247  publish=0.239  arrange=11.885  total=30.236ms
```

- `spawn` — creating the single-shot worker thread, **every** `WM_SIZE`: ~0.8 ms of
  pure overhead that synchronous layout does not pay.
- `wait` — **the UI thread blocked here for the entire measure**, 7–17 ms. The cost
  never went anywhere; it was accounted to another thread while the UI thread waited
  exactly as long.
- `publish` — ~0.1 ms. The mirror-publish walk was never the problem.
- `arrange` — 4–12 ms, on the UI thread, which async *measure* cannot touch by
  construction. This alone caps what any async-measure design can win.

Zero timeouts fired across the whole session, so the 50 ms fallback was dead code.

Net: async layout was **slower** than synchronous layout by roughly the spawn cost,
and it added a thread_local mode flag, a mirror field set, a participation guard, and
two recursion hooks to every container in the framework.

## Hypotheses the same data killed

Worth recording, because all three were plausible and all three were wrong:

- **"`TextBlock::Measure` bypasses its layout cache, so text measurement dominates."**
  It does bypass `cachedLayout_` (it builds a throwaway `IDWriteTextLayout`), but
  measured cost is `n=13 total=0.29 ms` — 0.02 ms each. Not worth changing.
- **"`TextArea`'s Wrap cache flush on every width change dominates."**
  `TextArea::WrapExtent` measured `n=11 total=0.02 ms`. The comment in
  `EnsureWrapExtent` claiming `Reset` is integer-only with no DWrite is accurate.
- **"Hidden Gallery pages are being measured."** They are not. Every panel's
  `MeasureOverride`/`ArrangeOverride` already skips `!IsVisible()` children, so the
  44 inactive pages cost nothing.

The real cost is spread across the whole visible tree — no single control owns it.
Button page (no TextArea) still cost 12.8 ms mean, versus 17.8 ms for the TextArea
page, so ~13 ms is control-type-independent baseline.

## What to do instead

**Skip the work, don't move it.** `ResizeMode::Scale` and `ResizeMode::Anchored`
(`NativeWindowHost`) already do exactly that: during a modal resize drag they do not
lay out at all (`layout=0.00ms DEFERRED` in the resize trace), and converge with one
precise layout at `WM_EXITSIZEMOVE`. This is the same strategy Chromium uses — it
scales/crops the last composited frame during a drag rather than reflowing per pixel.

The remaining unexploited optimization is **viewport culling in Measure/Arrange**.
`Panel::Render` culls to the dirty/clip hint, but `ScrollPanel::MeasureOverride`
measures *every* child at unbounded height and `ArrangeOverride` arranges all of
them — a page with 20 cards pays for 20 even when 3 are on screen. This is not a
cleanup: `ScrollPanel` needs total content height for its scroll range, and total
height is unknowable without measuring everything, so it requires the
estimate-then-measure-on-demand approach that `text/WrapExtentMap.h` already
implements for wrapped text. Architectural, and not yet scoped.

## If you reintroduce something like this

The bar is: show that `arrange` is small *and* that the measure can be made
genuinely non-blocking (cancellable mid-flight, not join-on-timeout — the old
`Cancel()` was a join, so a timeout measured the tree twice). Absent both, the
numbers above say it loses to plain synchronous layout.
