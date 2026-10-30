---
paths:
  - "FluentUI/controls/**"
  - "FluentUI/layout/**"
  - "FluentUI/base/**"
  - "extensions/**"
---

## Non-negotiables for every new control

This framework exists to be small, fast, and smooth. Those are not aspirations to
trade away when a control is inconvenient to write — they are acceptance criteria.
A control that works but stutters is not done.

**Cost must scale with what's on screen, not with what's in the model.** Any
control that can hold an unbounded number of items — a list, a tree, a grid, a log
view, a text document — has to draw and hit-test O(visible), not O(total). Build
the virtualized path *first*; retrofitting it means rewriting the control, because
the non-virtual version leaks "I have a layout object per item" assumptions into
its scrolling, selection, and measurement. `text/LineIndex.h` +
`text/LineLayoutCache.h` + `text/VisibleLines.h` is the worked example: an index
that answers "which lines are visible" in O(log n), a cache holding only the
visible rows' layouts, and the visible-range arithmetic extracted as a pure
function so it's testable headless.

Know which of your dimensions is exactly virtualizable before promising it.
Exact virtualization needs an O(1) item count, a constant item height, and
resize-invariant item boundaries — TextArea's NoWrap mode has all three. Soft wrap
breaks all three, so TextArea's Wrap mode uses measure-on-demand plus an estimated
extent instead (see "Text virtualization" in `.claude/rules/text.md` and `controls/TextArea.h`). A new control
whose items are variable-height or reflow on resize should do the same, rather
than quietly falling back to "lay out everything".

**Animation should not depend on the UI thread being free when smooth motion
matters.** Property animations go through `ICompositionBackend` so the compositor
thread holds frames even when the UI thread is busy. ProgressBar's indeterminate
sweep is the current example. Scrolling runs on the UI thread (TextArea, TreeView,
ScrollViewer all animate via `AnimatedValue` and `OnAnimationTick`). The rule:
`Composition()` can return null (no-composition host, device loss), so every
compositor caller needs a working UI-thread fallback.

**Nothing on the input path may block, and nothing may paint synchronously.**
Input handlers record dirty flags and return. No file I/O, no network, no `Sleep`,
no synchronous device creation inside a message handler. Work that genuinely takes
time belongs off-thread with the result marshalled back as a normal invalidation.

**Idle costs zero.** No polling timers, no "just repaint every frame to keep it
simple", no animation tick that stays registered after the animation ends
(`WantsAnimationTick()` must go false). The message loop blocks on `INFINITE` when
nothing is pending, and a single always-on ticker destroys that property for the
whole application.

**Invalidate the smallest correct region.** Pick the lowest `DirtyFlags` level
that's actually needed and declare `VisualOverflowDip()` honestly — over-declaring
either one turns every interaction into a full-window repaint, which is how a
framework ends up "fast in isolation, sluggish in a real window".

**Keep the surface area small.** No new third-party dependency without asking. No
new abstraction layer for a single caller. Prefer a free function over a class, a
value over a heap allocation, and an existing token over a new one. Binary size
and compile time are part of the product.

Verify before claiming any of this. Frame cost is measurable here — the F12 HUD
(read it per `.claude/rules/frame-pipeline.md`: trust CPU Max, not P50) and
`FluentUIBench` are the instruments. Smoothness itself is not verifiable from a
headless session; when a change touches scrolling or animation, ask the user to
check on real hardware and tell them where to look.
