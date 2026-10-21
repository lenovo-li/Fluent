# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Agent usage policy

For complex or multi-step tasks you may use the Agent tool to spawn sub-agents
(general-purpose, or forks). Simple bug fixes and small changes are done directly;
architectural changes, multi-file refactors, or tasks needing independent analysis
may use agents. Ultracode/workflows are permitted when the task scale warrants it.

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

**Alternative build path**: The repo maintains CMake (`CMakeLists.txt`) as a
backup for CLion / VS Code CMake Tools / pure command-line environments:

```bash
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build build
```

CMake produces the static libraries (`FluentUI.lib`, `FluentSettings.lib`) with
install/`find_package` support; `examples/Minimal` is the minimal consumer example
(built with `-DFLUENTUI_BUILD_EXAMPLES=ON`). Tests and demos are MSBuild-only.

**Dual source lists**: New `.cpp` files must be registered in **both** build systems:
- `FluentUI/FluentUI.vcxproj` (`<ClCompile>` entries)
- `CMakeLists.txt` (`FLUENTUI_SOURCES` list)

Run `python scripts/check_all.py` to verify source list sync and Solution Explorer
tree consistency. This is fast, read-only, and does not require compilation.

Tests are a zero-dependency custom runner (`tests/framework/Test.h`), not gtest:

```bash
./x64/Debug/FluentUITests.exe             # all tests; the run prints its own count
./x64/Debug/FluentUITests.exe Opacity     # substring filter on "Suite.Name"
./x64/Debug/FluentUIBench.exe             # perf harness; GPU scenes print stubs
```

Four gotchas:

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
  implicitly. This holds with zero exceptions across every registered entry,
  so keep it that way: Solution Explorer is how this codebase is browsed,
  and a filter that disagrees with the path makes `graphics/DCompHost.h` show up
  somewhere other than `graphics`. When a file moves on disk, update its
  `Include=` path *and* its `<Filter>` in the same edit.

Tests are headless by design — no window, no GPU. Anything touching a real device
(`ServiceModalFrame`, actual pixel output, visual smoothness) is not covered and
needs the user to verify on real hardware. Say so rather than implying otherwise.

## Extending FluentUI: inherit in a separate library

Custom controls are built by inheriting FluentUI controls in a **separate
library** that links `FluentUI.lib` — never by adding them to the FluentUI source
tree. `examples/CustomControls/` (LunarCalendar, BigButton, LogSlider) and
`examples/InstallerControls/` are the working templates; the demo links them.
This keeps the core stable and shows users the real integration path. What each
control exposes as `protected`, and why, is in
`docs/controls/inheritance-guide.md`. `examples/InheritanceExamples.h` is a
header-only variant kept only so `tests/unit/InheritanceExamplesTest.cpp` can
compile-check the extension points.

When a control's internals are made `protected` for subclassing, they become API:
renaming or changing them breaks downstream libraries.

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
compositor thread. Controls reach it through `ICompositionBackend` (obtained from
`WindowServices::Composition()`), never by naming a DComp type. `Composition()`
returns null on a host without composition or mid device-loss — **every caller
must null-check and fall back to its UI-thread path**. ProgressBar's
indeterminate sweep is the current example. See `composition/ICompositionBackend.h`
for usage rules (coordinate space, transform premultiply, surface drawing) and
`docs/learn/` for compositor integration patterns and ScrollContentHost removal
history.

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
- `EffectiveOpacity()` (multiplies the ancestor chain) — **only** for pixels drawn
  through a composition visual instead of the parent's `DrawingContext`. Ancestor
  opacity folded into that context never reaches such a visual, so the owner must
  push `EffectiveOpacity()` to `ICompositionVisual::SetOpacity` itself. No control
  does this today (ProgressBar's sweep visual ignores ancestor opacity); a new
  composition-visual user should.

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

### Text virtualization

TextArea virtualizes both NoWrap (exact, via arithmetic) and Wrap (estimated, via
measure-on-demand). See `controls/TextArea.h` (both modes), `text/WrapExtentMap.h`
(estimation strategy), `text/LineLayoutCache.h` (per-line layouts),
`text/LogSink.h` (off-thread append), and `docs/controls/TextArea.md` for usage.

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
extent instead (see "Text virtualization" above and `controls/TextArea.h`). A new control
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
