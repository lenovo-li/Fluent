# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Agent usage policy

For complex or multi-step tasks in this repository, you are authorized to use the
Agent tool to spawn specialized sub-agents (architect, implementer, test-runner,
reviewer, visual-reviewer) as defined in `.claude/agents/`. Judge task complexity
yourself: simple bug fixes and small changes can be done directly; architectural
changes, multi-file refactors, or tasks requiring specialized analysis should use
agents. Ultracode/workflows are also permitted when appropriate for the task scale.

## What this is

FluentUI — a from-scratch retained-mode UI framework for Windows desktop, built on
Win32 + Direct2D + DirectWrite + DirectComposition. No WinUI, no XAML, no WinRT.
The element model, layout, input routing, theming, and compositor integration are
all hand-written C++20. The API surface deliberately mirrors WPF/WinUI concepts
(Measure/Arrange, DirtyFlags, routed events, dependency-free "tokens" theming) so
those mental models transfer.

## Build and test

The solution is `Fluent.slnx` at the repo root. Toolset v145, C++20, x64,
static-lib core + console/GUI apps. Build commands are in the global
`~/.claude/CLAUDE.md`; the short form (from the repo root):

```bash
MSBUILD="C:/Program Files/Microsoft Visual Studio/18/Community/MSBuild/Current/Bin/MSBuild.exe"
"$MSBUILD" Fluent.slnx -p:Configuration=Debug   -p:Platform=x64 -verbosity:minimal
"$MSBUILD" Fluent.slnx -p:Configuration=Release -p:Platform=x64 -verbosity:minimal
```

Both configurations must build clean before reporting a change complete.

Tests are a zero-dependency custom runner (`tests/framework/Test.h`), not gtest:

```bash
./x64/Debug/FluentUITests.exe             # all tests; the run prints its own count
./x64/Debug/FluentUITests.exe Opacity     # substring filter on "Suite.Name"
./x64/Debug/FluentUIBench.exe             # perf harness; GPU scenes print stubs
```

Three gotchas:

- **Two test binaries exist.** Building the whole solution produces
  `x64/Debug/FluentUITests.exe` (repo root) — that's the one to run. Building only
  the tests project outputs to `tests/x64/Debug/`, which goes stale.
- **New test files must be registered in `tests/FluentUITests.vcxproj`** (and the
  matching `.filters`). Sources are listed one-by-one; there is no glob. Same for
  new library sources in `FluentUI/FluentUI.vcxproj`.
- **This applies to every project, including third-party headers.** Any source
  file on disk under a project's directory (`.h`/`.hpp`/`.cpp`, including
  vendored third-party headers like `FluentSettings/third_party/nlohmann/json.hpp`)
  must appear in that project's `.vcxproj`. Do not skip third-party files just
  because they are vendored — an unregistered header is invisible to Solution
  Explorer search and to "Go To" navigation.
- **Solution Explorer must mirror the folder layout on disk.** A file's
  `<Filter>` in the `.filters` file is exactly its directory relative to the
  project, with `\` separators — `controls/primitives/ButtonBase.h` gets
  `<Filter>controls\primitives</Filter>`, and a file at the project root
  (`fl_common.h`) gets no `<Filter>` element at all. Every nested directory also
  needs its own `<Filter Include="...">` declaration with a fresh
  `UniqueIdentifier` GUID; MSBuild does not create intermediate filters
  implicitly. This holds with zero exceptions across all 281 registered entries
  today, so keep it that way: Solution Explorer is how this codebase is browsed,
  and a filter that disagrees with the path makes `graphics/DCompHost.h` show up
  somewhere other than `graphics`. When a file moves on disk, update its
  `Include=` path *and* its `<Filter>` in the same edit.

Tests are headless by design — no window, no GPU. Anything touching a real device
(`ServiceModalFrame`, actual pixel output, visual smoothness) is not covered and
needs the user to verify on real hardware. Say so rather than implying otherwise.

## Control Inheritance (2026-09-27)

**How users extend FluentUI**: by inheriting controls in a **separate library**, not by modifying FluentUI source.

### Phase 1-3 Status

| Phase | Controls | Status | Notes |
|-------|----------|--------|-------|
| **Phase 1** | Calendar, Button, Slider | ✅ Implemented | Added `protected` APIs (read-only accessors + geometry helpers + split rendering) |
| **Phase 2** | ComboBox, TextBox, TextArea | ✅ Evaluated, skipped | ComboBox public API sufficient; TextEditBase already fully inheritable |
| **Phase 3** | CheckBox, ToggleSwitch, ProgressBar, Rating, DataGrid, TreeView | ✅ Evaluated, skipped | Base classes (ToggleButton, RangeBase) already expose `protected` members |

### Architecture: Separate-Library Pattern

**Don't** put custom controls in FluentUI source tree. Instead:

```
FluentUI.lib (framework, unchanged)
    ↓ links
examples/CustomControls/ (user extension library: LunarCalendar, BigButton, LogSlider)
    ↓ links
FluentUIDemo.exe (links both)
```

**Why:**
- ✅ Clear boundary: FluentUI = stable core, CustomControls = user code
- ✅ Demonstrates real integration path (users fork CustomControls, not FluentUI)
- ✅ FluentUI.lib doesn't bloat with examples
- ❌ Old pattern (`examples/InheritanceExamples.h` in source tree) kept for tests only

### Documentation

- **User guide**: `docs/controls/inheritance-guide.md` (when to inherit, what's exposed, extension points)
- **Phase 1-3 decisions** (what was exposed, and why Phase 2-3 needed no code changes): the "Phase 1" / "Phase 2" / "Phase 3" sections of `docs/controls/inheritance-guide.md`
- **Separate library template**: `examples/CustomControls/README.md` (how to build extension library)

### Implementation Files

- **Separate library** (recommended for users): `examples/CustomControls/*.{h,cpp}` — LunarCalendar, BigButton, LogSlider
- **Header-only test** (kept): `examples/InheritanceExamples.h` — compile-checked by `tests/unit/InheritanceExamplesTest.cpp`

## Projects

| Project | Kind | Role |
|---|---|---|
| `FluentUI` | static lib | the framework; everything below describes this |
| `FluentUIDemo` | GUI app | acceptance demo + the reference message loop |
| `FluentUITests` | console | headless unit/component tests; count grows, so read it from a run rather than from here |
| `FluentUIBench` | console | perf scenes; CPU-measurable ones run, GPU ones stub out |
| `FluentSettings` | static lib | JSON config persistence (`nlohmann/json` vendored) |
| `examples/CustomControls` | (template) | demonstrates user-library pattern: LunarCalendar, BigButton, LogSlider link FluentUI.lib |

## Architecture

### Element hierarchy

Four layers, each adding exactly one concern. The split is enforced — don't
collapse it:

```
Visual           visibility, opacity, Render(), dirty flags, invalidation plumbing
  UIElement      bounds (DIPs), hit-test, Measure/Arrange protocol, input, focus,
                 attach/detach lifecycle, per-frame animation ticks
    FrameworkElement   Width/Height/Margin/HAlign/VAlign; concrete leaf layout
      Control          Background/Padding/theme-aware chrome
      Panel            owns children (unique_ptr), recurses render/hit-test/attach
```

`Visual` knows nothing about size or input. `UIElement` knows nothing about
`Width`/`Margin`. Layout is two-pass WPF-style: `Measure(avail)` fills
`desired_`, `Arrange(rect)` sets absolute bounds. Panels call
`MeasureCached()` on children, not `Measure()` — it short-circuits when the
element isn't Measure-dirty and the constraint matches.

### Services via UIContext, not constructor wiring

A control never takes a window pointer or a DWrite pointer in its constructor.
`UIContext` (`core/UIContext.h`) is a flat aggregate of non-owning service
pointers — window, dwrite, animations, input, focus, resourceCache, theme, hwnd,
dpiScale — injected by the framework when the element joins a live tree
(`AttachToContext`) and cleared on detach.

Rules that follow from this:

- Read services through the protected `Context()` / `Dwrite()` / `Window()` /
  `Theme()` accessors, only while attached. Never cache a service pointer past
  the attach period.
- Acquire host registrations in `OnAttachedToTree()` and wrap them in
  `AddContextSubscription()` so they auto-cancel on detach. This is what prevents
  dangling `[this]` callbacks.
- Every context member can be null (headless tests wire only what they exercise).
  `Theme()` is the exception — it falls back to a static default snapshot and
  never returns null.

Controls depend on `WindowServices` (`window/WindowServices.h`), a small
interface, rather than the concrete `NativeWindowHost`. That's what makes controls
testable against a mock host.

### The frame pipeline

Nothing paints synchronously from the input path. The flow is:

1. Something changes → `SetProperty(field, value, DirtyFlags::X)` records dirty
   flags and calls `InvalidateDirty`, which walks `Parent()` to the root's
   invalidate callback → `RequestFrame(reason)`.
2. `FrameScheduler` (`window/FrameScheduler.h`) coalesces: all requests between
   two frames OR into one pending frame with a merged `FrameReason` bitmask.
3. The app's message loop (see `FluentUIDemo/main.cpp`) does:
   `PumpAnimations()` → wait (`MsgWaitForMultipleObjectsEx`) → drain *all* queued
   messages → `RunDueFrame()` at most once.
4. `RenderNow` applies any pending resize, runs layout if Measure/Arrange is
   dirty anywhere, plans the redraw region, paints, and commits.

`DirtyFlags` expand to their closure: Measure ⊃ Arrange ⊃ Render
(`core/Invalidation.h`). Pick the highest-level flag the change actually needs —
tagging a render-only change as Measure collapses the frame into a full-window
repaint.

Idle means no pending frame and no active animation, and it must cost zero CPU:
the loop blocks on input with `INFINITE`. When a frame *is* pending it waits only
until the frame is due (`FrameWaitTimeoutMs()` / `window/FramePacing.h`). This
throttle is load-bearing — `Commit()` doesn't block on vsync the way the old swap
chain's `Present1` did, so without it the loop free-runs (measured ~1927 fps on a
120 Hz panel, invisible on screen but burning a core and corrupting every
diagnostic number).

**Reading the HUD (F12) when frames are slow.** Frame times under real load are
often *bimodal*: a few catastrophic frames interleaved with a burst of trivial
ones. Three things exist because of that, and the reasoning is worth keeping:

- **Never gate a diagnostic on a magnitude threshold that "must mean idle".** The
  interval ring used `gap > 500ms → discard as an idle resume`, which cannot tell
  a 3-second idle from a 3-second *frame*. On a resize drag over a large document
  it discarded every real sample and reported four-digit FPS. It now asks the
  scheduler whether the loop was continuously busy
  (`FrameIntervalIsCadenceSample`, fed by `loopWasContinuous_`).
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

### Rendering: DComp surface, not a swap chain

Window content is an `IDCompositionVirtualSurface`, deliberately not a swap
chain. Reason: content updates via `Present1` (DXGI queue) and child-visual
transforms via `Commit()` (composition tree) can land on different vsyncs, so
during a resize the background and an independently composited child desync and
visibly shake. Drawing content into a DComp surface means both publish in the
*same* `Commit()` and can't disagree. `graphics/DCompHost.h` owns this.

Consequences worth knowing before touching this layer:

- The surface is **persistent** — pixels outside the update rect keep last
  frame's content. That's what partial redraw relies on.
- `FrameLatencyWaitable()` is permanently `nullptr`; there's no swap chain to
  pace against.
- `BeginContentFrame` returns an atlas-tile offset that must be folded into the
  transform (`Scale(dpi) * Translation(net)`).

Controls draw through `DrawingContext` (`graphics/DrawingContext.h`), a thin
wrapper over the frame's `ID2D1DeviceContext` plus one shared solid brush. The
brush is private and `Dc()` has no callers, so the typed methods
(`FillRoundedRect(rr, color)`, `DrawTextLayout(...)`) are the only path — which is
exactly why folding opacity into `Faded()` covers every control, present and
future, with no per-control change.

### Partial redraw and the dirty-bounds contract

`window/DirtyRegion.h` is a pure function: given this frame's dirty-rect union it
decides full vs partial redraw and returns a pixel-aligned update rect. Two
contracts feed it, and violating either produces on-screen ghosting:

- **`CollectDirtyBounds` must cover every pixel the element paints.** Nothing
  widens the reported region afterwards. An element that paints outside
  `bounds_` (focus ring, a stroke centered on the bounds edge, an oversized drag
  thumb) must override `VisualOverflowDip()`; `VisualBounds()` then inflates
  automatically. Current declarations: Button 3.75, CheckBox/RadioButton/
  ToggleSwitch 4.75, ComboBox 3.75, `TextEditBase` 1.75 (inherited by TextBox and
  TextArea), Slider ≈14.2.
- **Overflow must be declared unconditionally**, not only while focused — the
  frame where focus leaves still needs those pixels cleared.

`VisualOverflowDip()` is deliberately one virtual feeding *both* the dirty-rect
report and `Panel::Render`'s cull, so the "what gets cleared" and "what gets
redrawn" judgments cannot drift apart. They did once, and that was a ghosting
bug class.

The redraw region is snapped to whole pixels inside `PlanRedraw` because
`ClipGuard` uses antialiased clipping — a fractional clip edge gives boundary
pixels partial coverage, leaving a faint 1px seam tracing the dirty rect.

### Compositor offload

Animations that must survive a busy UI thread run on the DirectComposition
compositor thread. Controls reach it through `ICompositionBackend`
(`composition/ICompositionBackend.h`) — obtained from `WindowServices::Composition()`,
never by naming a DComp type. Two payoffs: an embedded member that isn't a tree
node (a TreeView's scroll model) can still animate, and composition behavior is
testable headless against `tests/framework/FakeCompositionBackend.h`.

`Composition()` returns null on a host without composition or mid device-loss.
**Every caller must null-check and fall back to its UI-thread path** — that
fallback is also the revert path if a migration regresses.

**Compositor-driven scrolling (ScrollContentHost) was removed on 2026-08-31.**
The engine drew viewport-plus-margin into one DComp surface and let the
compositor translate it, making short scrolls frame-independent. It was removed
after sustained white-screen bugs that could not be fixed: scroll tweens
occasionally left surfaces blank, root cause undiagnosed despite repro attempts.
TextArea and TreeView now scroll on the UI thread only. If the feature is ever
re-introduced, one failure mode is already understood: a tween bakes the surface
origin in at submission time, so re-basing the origin while a tween runs must be
split into two commits (freeze in the old coordinates, then re-base and start the
new tween), or the content jumps by the origin delta. That fix alone did not
cure the blank surfaces. ProgressBar still uses ICompositionBackend for
indeterminate-mode sweeps.

**When drawing into a DComp surface, always `GetTransform` and pre-multiply
(`mine * base`) — never `SetTransform` outright.** `BeginDraw` hands back a tile
in a shared atlas and the tile origin changes between draws; overwriting the
transform puts drawing outside the tile, which flickers as tiles get reassigned.
Use `SurfaceTransformFromWindowDip()` for window-DIP→tile mapping. The fake
backend passes a null dc and zero offsets, so unit tests cannot catch this class
of bug on their own.

### Theming

`ThemeSnapshot` (`styling/ThemeTokens.h`) is an immutable value holding typed
token groups plus a `generation` stamp. The host's `ThemeManager` owns one stable
snapshot and **overwrites it in place** on a theme change, bumping the cache
epoch — so the pointer parked on every `UIContext` stays valid and only the
pointee changes. A bumped generation is the signal for theme-derived caches
(text layouts, themed geometry) to rebuild.

Controls read tokens through `Theme()`. Never hard-code a color, corner radius,
font size, or spacing value in a control.

Semi-transparent tokens (`cardFill` is alpha 0.70, meant to sit over Mica) need
someone to supply an opaque base when no material exists. `ResolveBaseFill` does
this for the window; popups pre-flatten via `FlattenOver`. A new surface that
renders themed tokens without a DWM material behind it needs the same treatment.

### Opacity

Three levels, and picking the wrong one is a real bug:

- `DrawingContext::WithOpacity` / `Faded()` — per-primitive alpha multiply. The
  single choke point; covers all controls automatically.
- `Visual::SetOpacity` + `RenderWithOpacity` — what containers call on children
  (`Panel::Render` uses `RenderWithOpacity`, not `Render`). Opacity 1.0 takes the
  original path at zero cost; 0.0 skips `Render` entirely.
- `EffectiveOpacity()` (multiplies the ancestor chain) — **only** for composited
  controls. TreeView/TextArea in composited mode `return` from `Render()`, so
  host-context opacity never reaches their pixels; they must recompute and push
  to `ScrollContentHost::SetOpacity` (which lands on `viewport_`, so the
  scrollbar and focus ring fade too).

Opacity is appearance-only: layout and hit-testing are unaffected (use
`SetVisible(false)` to remove from layout). It carries a Render-only dirty flag —
never Measure. Alpha is straight, not premultiplied; D2D premultiplies
internally, and doing it by hand makes every faded control go dark.

### Undo/Redo

`text/UndoStack.h` implements a standard two-stack undo/redo with grouped edits.
`TextEditBase` holds an `UndoStack` and records text operations (`RecordSnapshot`
before mutating `text_`). Three rules:

- **Record before you mutate.** `SetText` / `DeleteSelection` / `InsertText` all
  call `RecordSnapshot(GetText())` before touching `text_`. Recording after makes
  the undo restore the *new* state instead of the old one.
- **Group typed characters.** `RecordSnapshot` takes a `groupWithPrevious` flag —
  ordinary keystrokes set it true so "abc" becomes one undo instead of three.
  Paste, delete selection, or any programmatic `SetText` breaks the group.
- **Clear on SetText from outside.** When text is set by the host (not via
  typing), call `ClearUndoStack()` — the old buffer is gone and an undo would
  restore garbage. The exception: restoring a *snapshot* (like "load autosave")
  can leave undo intact if the snapshot is one of the states already in the stack.

TextBox and TextArea expose `Undo()` / `Redo()` / `CanUndo()` / `CanRedo()` and
bind Ctrl+Z/Ctrl+Y in their `OnKeyDown`. When adding a new text-input control,
the same pattern applies: hold an `UndoStack`, record before mutating, and clear
on external `SetText`.

### Drag and drop

`input/DragDrop.h` defines `IFluentDropTarget` — the interface a control
implements to accept drops — and `GetDroppedFiles` / `GetDroppedText`, which
extract payload from an `IDataObject`. `NativeWindowHost` implements the COM
`IDropTarget` and routes `DragEnter` / `DragOver` / `Drop` to the element tree via
`HitTestDropTarget` (which walks the tree like `HitTest`, looking for the deepest
element that has a drop handler *and* contains the point).

A control opts into drops by calling `SetDropTarget(handler)`, where `handler` is
an `IFluentDropTarget` lambda or object. The handler receives `OnDragEnter` /
`OnDragOver` / `OnDragLeave` / `OnDrop` with a `DragEventArgs` holding the mouse
position, the requested effect, and the `IDataObject`. Return the effect you
accept (`DROPEFFECT_COPY` / `LINK` / `MOVE` / `NONE`); the window reports it back
to the drag source.

Three rules:

- **Hit-testing respects visibility and enabled state.** A disabled or invisible
  element is never a drop target, even if it has a handler. The check happens in
  `UIElement::HitTestDropTarget`, not in the control, so you cannot override it.
- **Child handlers win over panel handlers.** `Panel::HitTestDropTarget` recurses
  into children first (front to back) and only returns itself if no child
  accepted. A `StackPanel` with a drop handler that contains a `TextArea` with its
  own handler will route to the `TextArea` when the drop is inside it — no
  explicit Z-order check needed.
- **Cache the `IDataObject` carefully.** `DragEnter` supplies it; `DragOver` does
  not (COM rule — the object is expensive to marshal). `NativeWindowHost` caches it
  in `dragData_` and passes the same pointer to every `OnDragOver` in that drag
  session. If a control wants payload during hover (to show a preview), it must
  either extract on `OnDragEnter` or be prepared to fail gracefully in
  `OnDragOver`. `OnDrop` gets a fresh `IDataObject` and is the authoritative
  source.

The one platform quirk: `RegisterDragDrop` requires a message loop pumping; it
cannot work before the window is shown. `NativeWindowHost` registers in
`WM_CREATE` and revokes in `WM_DESTROY`. A control can assume drop routing works
once `OnAttachedToTree` has fired.

### Text capacity: both modes virtualized, by different means

**There is no document-wide `IDWriteTextLayout` anywhere any more.** `EnsureLayout`
is gone. Both `TextWrapMode`s draw and hit-test O(visible), and the only
O(document) work left on either path is the newline scan in `EnsureLineIndex`.

**NoWrap is virtualized exactly.** Line count is the newline count, line height is
constant, and a resize changes no line breaks — those three facts are what make
"which lines are visible" arithmetic instead of a layout query. `text/LineIndex.h`
maps offset↔line in O(log n), `text/LineLayoutCache.h` holds layouts for only the
visible rows, `text/VisibleLines.h` is the visible-range arithmetic as a pure
function, and `DrawLinesToSurface` draws just the band that intersects the
viewport. Caret and hit-testing go through `NoWrapCaretMetrics` / `NoWrapHitIndex`.
This is the mode that reaches tens of MB, and the one a log view wants.

**Wrap is virtualized by estimation.** Soft wrap breaks all three premises above:
a paragraph's visual line count depends on where glyphs break at this exact width,
so the document height is unknowable without wrapping all of it, and every height
changes on resize. `text/WrapExtentMap.h` resolves that by *estimating* the height
of paragraphs nobody has looked at and replacing each estimate with a real
measurement when the paragraph is drawn. Layouts are per paragraph, through the
same `LineLayoutCache` (its layout box is the wrap width, and changing it flushes —
a layout built at 400 DIP has the wrong line breaks at 300).

The consequence, which was accepted deliberately and is not a defect to fix: **the
scrollbar adjusts slightly while scrolling through never-visited regions**, then
settles. The alternatives are worse — laying out everything up front is a
multi-second stall on open (3.7 s on 28 MB, measured), and laying out in the
background burns CPU *and* stalls on contact with an unmeasured region (Scintilla's
idle wrapping; two upstream bugs are cited in the plan). VS Code and Sublime make
the same trade.

Both of Wrap's old hard ceilings are gone with the whole-document layout: content
past `maxHeight = 100000` DIP is no longer clipped away invisibly, and a resize is
now O(paragraphs) of integer arithmetic (`WrapExtentMap::Reset`) instead of
re-wrapping the entire buffer every frame. If a change here seems to need a
document-wide layout again, it is reintroducing both.

Things to know before touching either path:

- **Drawing is measuring, under Wrap.** `DrawParagraphsToSurface` measures the band
  before it paints, which means a draw path mutates layout state. That is why the
  extent map is `mutable` and why `MeasureParagraphsInBand` exists as a public
  method: headless, the draw callback gets a null DC and returns before its own
  logic, so the walk had to be reachable some other way. Deleting the `SetMeasured`
  call inside it left all 806 tests green — estimates are plausible heights, and
  nothing else could tell.
- **The measuring walk cannot trust its own up-front span.** A paragraph estimated
  at 2 visual lines that measures 9 pushes everything below it down; one estimated
  too tall means the band needs *more* paragraphs than the span named. Both the
  loop and `MeasureParagraphsInBand` re-read each paragraph's top as they advance.
- **An estimate must never overwrite a measurement.** That invariant is what makes
  the extent monotonically more accurate instead of oscillating;
  `ReEstimateUnmeasured` skips measured paragraphs for exactly this reason.

Two guards keep the small-input path cheap: `TextArea::LayoutWidthKnown()` defers
everything until Arrange supplies a real width (wrapping at `ContentWidth()`'s 1 DIP
clamp floor puts one visual line per character — the most expensive wrap possible),
and `DisplayText(scratch)` returns a **reference**, so the common no-IME case copies
nothing. Prefer `DisplayLength()` / `DisplayEmpty()` when only the size or emptiness
is needed.

### High-throughput log append

`TextArea::AppendText` is the log path, and it exists because `SetText` is
O(document): at log rates (the target is 10 k lines/s) anything proportional to the
buffer is a permanent stall. It rebuilds nothing — `LineIndex::Append` scans only the
new characters and leaves every existing line's *number* intact, which is what lets a
layout cache keyed by line number survive an append untouched. The one entry that must
go is the old last line's (appending to `"a\nb"` makes line 1 `"bc"`), and
`LineLayoutCache::Erase` is the narrowest correct invalidation for it.

Four things it deliberately does not do, each of which would put the O(document) cost
back: no index rebuild, no cache flush, no caret move (a reader scrolled back through
history must not be yanked to the end), and no horizontal high-water-mark reset (an
append can only lengthen lines, so resetting would shrink the scroll range and jerk the
thumb backwards every batch). `TextChanged` fires only when something is subscribed —
its payload is the whole buffer by value.

`SetMaxLines(n)` caps the buffer as a ring, and the trim is the asymmetric case: it
renumbers every remaining line, so the layout cache *must* be flushed. Appends are
O(1)-ish and frequent; trims are O(visible) and happen once per `n` lines. Under NoWrap
the scroll offset is compensated exactly (`k` lines dropped == `k * lineHeight` gone) so
a reader sees the text hold still; under Wrap no exact compensation exists and the view
can jump — NoWrap is the log mode.

`text/LogSink.h` is the producer side: a mutex-guarded staging buffer for data arriving
off-thread (UI is single-threaded here, so a producer must never touch `text_`). Its two
non-obvious properties are **at most one unconsumed wakeup** — same reasoning as
`ModalFrameHeartbeat`, since posting per line queues one message per line and the
backlog outlives the burst — and **the wakeup is injected**, not a `PostMessage`, which
is what makes it testable with no window (the app passes `Application::Post`). `Drain`
re-arms the wakeup *inside* the lock, atomically with taking the data; doing it after
unlocking strands whatever a producer appended in between, and the symptom is a log that
stops updating until some unrelated later write arrives.

Tail following (`SetAutoScrollToTail`) has one rule worth stating because getting it
backwards is silent: **`AppendText` reads the follow flag, it never recomputes it.** An
append grows the extent, so recomputing "is the view at the bottom" there always
concludes the user scrolled away and switches following off on the first batch. Only
user-driven scrolls (wheel, thumb drag, keyboard, tick) recompute it — and the wheel
judges the *target* offset, not the current one, since a tween has not moved yet on the
frame the notch arrives.

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

Know which of your dimensions is actually virtualizable before promising it. The
reason `TextArea` virtualizes NoWrap but not Wrap is spelled out in
`controls/TextArea.h`: virtualization needs an O(1) item count, a constant item
height, and resize-invariant item boundaries. Soft wrap breaks all three. If a new
control's items are variable-height or reflow on resize, say so and design for it
(measure-on-demand plus an estimated extent) rather than quietly falling back to
"lay out everything".

**Scrolling and animation must not depend on the UI thread being free.** Anything
that scrolls or animates goes through the compositor: `ScrollContentHost` for
scrolling, `ICompositionBackend` for property animations. The point is that a busy
UI thread — a relayout, a large paint, a blocking call — cannot make a scroll or a
fade hitch, because the compositor thread is already holding the frames. Two rules
come with it, both already stated above but worth repeating because they're where
this goes wrong: `Composition()` can return null (no-composition host, device
loss), so every caller needs a working UI-thread fallback; and hit-testing during
a composited scroll must read `EffectiveOffset()`, not `TargetOffset()`.

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
(read it per "The frame pipeline" above: trust CPU Max, not P50) and
`FluentUIBench` are the instruments. Smoothness itself is not verifiable from a
headless session; when a change touches scrolling or animation, ask the user to
check on real hardware and tell them where to look.

## Conventions

- **DIPs everywhere.** All geometry is `RectDip` / `SizeDip` at a 96-DPI basis.
  Physical pixels = DIP × dpi/96. Composition visuals are the exception — they're
  pixel-space, and the owning control converts. Mouse coordinates arrive in
  physical pixels and are converted at the window boundary.
- **Per-window DPI.** `GetDpiForWindow` per window; never cache a global DPI.
- **`OutputDebugStringA` on device paths.** Use the `Trace` / `TraceMsg` /
  `FL_RETURN_IF_FAILED` helpers in `fl_common.h`; tag format is
  `[FluentUI.Module]`. Every critical D3D/D2D/DComp/surface call prints its
  HRESULT so a user's screenshot pinpoints the failure.
- **Property setters go through `SetProperty(storage, value, flags)`.** It
  no-ops when unchanged (so a redundant set never schedules a frame), stores,
  and records the expanded dirty closure.
- **Events use `Subscription`** (`core/Subscription.h`) — RAII; holding one as a
  member means teardown unregisters automatically.
- **Pure functions get extracted for testability.** `PlanRedraw`,
  `EnsureVisibleOffset`, `ResolveBaseFill`, `FramePacing`,
  `MenuBar::VisibleTitleRect`, `SurfaceTransformFromWindowDip` all exist as free
  or static functions specifically so they're unit-testable headless. Follow that
  pattern when adding logic that would otherwise need a window.
- **Comments explain *why*, at length.** Headers in this repo carry design
  rationale, history of what was tried and rejected, and explicit contracts.
  Match that — a terse one-liner is out of place here.

### COM lifetime rules

The framework uses COM for WIC (Image decoding), IFileDialog (file picker), and
the ID2D1Bitmap / IDWriteTextLayout / IDCompositionVisual interfaces throughout
the rendering stack. `Application` owns the COM apartment (`CoInitializeEx` /
`CoUninitialize`) and guarantees it outlives every COM interface acquired
through the framework.

**Two lifetime violations and their fixes:**

1. **Static or global `ComPtr<T>` that destructs during CRT teardown** (after
   `main()` returns) calls `Release()` into a dead apartment. **Fix:** Use a
   raw pointer and deliberately leak it. The OS reclaims the memory on exit,
   and we sidestep the teardown-order hazard. See `Image.cpp:22` for the WIC
   factory singleton.

2. **Stack variable in `wWinMain` that destructs after `CoUninitialize`** —
   same symptom, different cause. The old pattern of calling `CoUninitialize()`
   just before `return` looks safe but is not: `return exitCode` triggers stack
   unwinding, which destructs the window *after* COM has shut down. **Fix:**
   RAII guard declared as the first local in `wWinMain`, so it destructs last.
   `Application` owns this guard (`Application::ComApartment`), and its header
   documents the declaration-order contract.

**DWrite / D2D / DComp don't use `CoCreateInstance`** — they're created via
exported functions (`DWriteCreateFactory`, `D2D1CreateFactory`,
`DCompositionCreateDevice`) and theoretically don't need the COM apartment. But
`ID2D1Bitmap` created via `CreateBitmapFromWicBitmap` straddles both worlds:
the D2D object's release path touches WIC's internal state, so `Application`'s
COM lifetime protection covers it too.

When adding a new COM component (e.g., Media Foundation for video), follow the
rule: **any interface obtained via `CoCreateInstance` must be owned by an
object whose lifetime is strictly shorter than `Application`'s.** That means no
static/global `ComPtr`, and the `Application` instance must be declared first
in `wWinMain`.

## Persistent task state and context compaction

For every long-running or multi-step task, maintain
`.claude/TASK_STATE.md` as the persistent checkpoint for the current task.

The checkpoint must contain enough concrete information for a new session to
resume the task without relying on previous conversation history. Do not wait
until context compaction is imminent. Automatic compaction may occur before
there is another opportunity to update the file.

Create the checkpoint at the start of a long-running task. Update it in place
after a meaningful state change, including:

- completing, starting, blocking, or changing a plan step;
- modifying a related group of files;
- making or revising an important technical decision;
- completing a Debug build, Release build, test run, or benchmark;
- encountering a failure, blocker, or abandoned approach;
- before an intentional `/compact`, `/clear`, or session exit;
- before reading very large files or running commands expected to produce
  substantial output.

The checkpoint must record:

- the current objective, acceptance criteria, and user constraints;
- plan steps marked as not started, in progress, completed, or blocked;
- completed work and remaining work;
- modified, created, moved, or deleted files, including relevant symbols and
  reasons for the changes;
- important technical decisions and their rationale;
- failed approaches, relevant error excerpts, and why they should not be
  repeated without new evidence;
- the most recent Debug, Release, test, benchmark, and hardware-verification
  status, but only when actually executed;
- current blockers;
- exact ordered next actions, with the first action executable as written.

Keep the checkpoint concise. Do not copy complete files, large diffs, verbose
build logs, or conversation transcripts into it. Refer to repository paths,
symbols, commands, commits, and concise error excerpts instead.

After automatic or manual context compaction, or when resuming a task:

1. Read `.claude/TASK_STATE.md`.
2. Run `git status --short`.
3. Inspect the relevant diff.
4. Verify that the checkpoint matches the working tree.
5. Correct stale checkpoint information before continuing.
6. Continue from the first incomplete item under `Exact Next Actions`.
7. Do not repeat completed work or a recorded failed approach without new
   evidence.
8. Update the checkpoint after the next meaningful state change.

The working tree and actual command results are authoritative. Never claim
that a build, test, benchmark, or hardware check passed unless it was actually
executed successfully.

`project_bundle.md` is a generated export for external review and duplicates
source files already available in the working tree. Do not read it during
normal repository work unless the user explicitly requests analysis of that
bundle. Inspect the original repository files instead.

## Working on this codebase

Headers reference a "roadmap §N" throughout. That document is no longer in the
repo; treat the section numbers as historical labels, not something to look up.

What actually exists under `docs/`:

- `docs/api-surface.md` — public API conventions.
- `docs/BUILD_OPTIMIZATION.md` — build performance notes.
- `docs/controls/` — per-control reference pages off `TEMPLATE.md`.
- `docs/design/` — design and task documents for in-flight features. **Transient**:
  deleted once the work lands.
- `docs/dual-repo-workflow.md` — workflow for syncing between internal and public repos.
- `docs/learn/` — Chinese technical notes (compositor clipping, borderless-window
  resize, DirectWrite font fallback).

**Documentation lifetime rule.** Only knowledge, learning notes, and architecture
docs are permanent (`CLAUDE.md`, `docs/api-surface.md`, `docs/BUILD_OPTIMIZATION.md`,
`docs/controls/`, `docs/learn/`, library READMEs). Design and task documents are
deleted when finished, so:

- Permanent docs must not link to `docs/design/` or other task documents. When a
  task finishes, move whatever is still true (the decision, the rationale, the
  pitfall) into a permanent doc, then delete the task doc.
- No doc may reference git history: no commit SHAs, no `git show`/`git log`
  recipes, no "the last working version is commit X". State the knowledge itself.
- A code comment follows the same rule: describe the behavior, not the plan
  section it came from.

Long-running task state lives in `.claude/TASK_STATE.md` (regimen at the end of this
file). That path is **gitignored**, so it is local scratch rather than a repo
document — a fresh clone will not have it, and nothing in the repo may depend on it.

Verification convention for any change: MSBuild Debug + Release clean, then
`x64/Debug/FluentUITests.exe` fully green. Add tests for new behavior — and
prefer verifying them by mutation (break the fix deliberately, confirm a test
catches it). Several bugs here shipped past large green suites because the test
exercised a path the fake backend stubbed out. When a mutation *isn't* caught,
first ask whether the mutation is genuinely equivalent before adding assertions.

Visual correctness — ghosting, flicker, smoothness, pixel alignment — cannot be
verified from here. Session logs (`SessionLog`, written by the demo on close)
prove timing regressions, not pixels. Ask the user to check on real hardware, and
point them at the specific place to look (e.g. "around the control you tabbed
away from") rather than asking whether it generally looks right.

Commit only when explicitly asked; push only after asking, separately.
