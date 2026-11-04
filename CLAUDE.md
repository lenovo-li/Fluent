# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Agent usage policy

Simple bug fixes and small changes are done directly in the main conversation.
Delegate when a task produces output the main context doesn't need (builds, test
runs, wide searches), needs a reviewer that didn't write the code, or splits into
independent read-only pieces. Ultracode/workflows are permitted when the task
scale warrants it.

Project subagents live in `.claude/agents/`:

| Agent | Tools | Use it for |
|---|---|---|
| `fluent-explorer` | read-only | Before writing or changing a control: read the nearest existing controls, docs and `inheritance-guide.md`, return the real API |
| `fluent-builder` | Bash, read | `check_all.py`, Debug + Release builds, test runs; returns only errors, warnings and failing tests |
| `fluent-reviewer` | read-only + git | Review a diff against the non-negotiables, the dirty-bounds contract, COM lifetime and `protected` API stability |
| `fluent-doc-auditor` | read-only | Doc record for every new/optimized control, doc lifetime rule, source-list registration |

Writes stay serial: only one agent edits files or runs MSBuild at a time, because
`.vcxproj`/`.filters` and the `x64/` output tree are shared. Read-only review can
run in parallel. Skills in `.claude/skills/`: `/verify` (full verification via
`fluent-builder`), `/new-control` (explore → implement → verify → review), and
`task-state` (the checkpoint regimen below).

## What this is

FluentUI — a from-scratch retained-mode UI framework for Windows desktop, built on
Win32 + Direct2D + DirectWrite + DirectComposition. No WinUI, no XAML, no WinRT.
The element model, layout, input routing, theming, and compositor integration are
all hand-written C++20. The API surface deliberately mirrors WPF/WinUI concepts
(Measure/Arrange, DirtyFlags, routed events, dependency-free "tokens" theming) so
those mental models transfer.

## Build and test

The solution is `Fluent.slnx` at the repo root. Toolset v145, C++20, x64,
static-lib core + console/GUI apps. Build from the repo root through
`scripts/build.ps1`:

```bash
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/build.ps1 -Configuration Debug
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/build.ps1 -Configuration Release
```

The script finds MSBuild with `vswhere.exe` instead of a hard-coded path, because
this repo is copied between machines whose Visual Studio install paths differ.
The v145 toolset needs Visual Studio 18 (VS 2022 ships v143 and fails with
MSB8020). Keep `build.ps1` ASCII-only: Windows PowerShell 5.1 reads a BOM-less
script in the system code page, and a Chinese comment corrupts the next line.

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

Gotchas:

- **Two test binaries exist.** Building the whole solution produces
  `x64/Debug/FluentUITests.exe` (repo root) — that's the one to run. Building only
  the tests project outputs to `tests/x64/Debug/`, which goes stale.
- **Every source file on disk must be registered in its project's `.vcxproj`**
  (and `.filters`, and for `FluentUI` also `CMakeLists.txt`), including vendored
  third-party headers; there is no glob. The `.filters` path must mirror the
  directory on disk. Full rules: `.claude/rules/project-files.md`, which loads
  when a project file is opened. A PostToolUse hook runs `check_all.py` after any
  edit to a `.vcxproj`, `.filters` or `CMakeLists.txt`.

Tests are headless by design — no window, no GPU. Anything touching a real device
(`ServiceModalFrame`, actual pixel output, visual smoothness) is not covered and
needs the user to verify on real hardware. Say so rather than implying otherwise.

## Extending FluentUI: inherit in a separate library

Custom controls are built by inheriting FluentUI controls in a **separate
library** that links `FluentUI.lib` — never by adding them to the FluentUI source
tree. Those libraries live under `extensions/`: `extensions/CustomControls/`
(LunarCalendar, BigButton, LogSlider, SpinnerControl), `extensions/InstallerControls/`
and `extensions/NavigationControls/` are the working templates. The demo links
CustomControls and InstallerControls; NavigationControls is linked only by the
tests (the tests also link the other two).
`examples/` is kept for something different — minimal consumer programs
(`Minimal/`, `PackageConsumer/`) that show how to *use* FluentUI, not how to
extend it. A new reusable control library goes in `extensions/`, a new "how do I
link this" sample goes in `examples/`.
This keeps the core stable and shows users the real integration path. What each
control exposes as `protected`, and why, is in
`docs/controls/inheritance-guide.md`. `tests/unit/InheritanceExamples.h` is a
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
| `extensions/CustomControls` | static lib (template) | demonstrates user-library pattern: LunarCalendar, BigButton, LogSlider, SpinnerControl link FluentUI.lib |
| `extensions/InstallerControls` | static lib | WizardControl + FolderPicker; linked by the demo and tests |
| `extensions/NavigationControls` | static lib | NavRail, PageHost, OverlayHost; linked by the tests, not the demo |
| `demos/TrayAppDemo` | GUI app | close-to-tray sample (window tray icon, `ShutdownMode::OnExplicitShutdown`) |
| `examples/Minimal`, `examples/PackageConsumer` | GUI app | minimal consumers (C++14/17/20 floor checks, CMake `find_package`) |

## Architecture

The architecture notes live in `.claude/rules/`, each scoped with `paths:` so it
loads only when matching files are read or edited. Read the file directly when a
task needs it before touching code (planning, review, a question):

| Rule file | Covers | Loads for |
|---|---|---|
| `element-model.md` | Visual → UIElement → FrameworkElement → Control/Panel; services via `UIContext` | `FluentUI/**`, `extensions/**`, `tests/**` |
| `frame-pipeline.md` | Dirty → `FrameScheduler` → `RunDueFrame`; frame pacing; reading the F12 HUD | `window/`, `animation/`, `diagnostics/`, demo, bench |
| `rendering.md` | DComp surface (no swap chain), partial redraw + `VisualOverflowDip`, compositor offload, opacity levels | `graphics/`, `composition/`, `window/`, `base/`, `core/`, `controls/`, `layout/`, `extensions/` |
| `theming.md` | `ThemeSnapshot`, generation stamp, semi-transparent tokens | `styling/`, `controls/`, `dialogs/`, `window/`, `extensions/` |
| `text.md` | Undo/Redo rules, text virtualization | `text/`, text-input controls |
| `drag-drop.md` | `IFluentDropTarget`, drop hit-testing, `IDataObject` caching | `input/`, `window/`, `base/` |
| `control-non-negotiables.md` | Full acceptance criteria for controls | `controls/`, `layout/`, `base/`, `extensions/` |
| `com-lifetime.md` | COM apartment ownership, static `ComPtr` and `wWinMain` hazards | `Application.*`, `Image.*`, `dialogs/`, `services/`, demo, examples, InstallerControls |
| `project-files.md` | Dual source lists, `.vcxproj`/`.filters` registration | project files, `CMakeLists.txt` |

When editing a rule file, keep its `paths:` list in step with this table.

## Non-negotiables for every new control

Summary; the full text with rationale is `.claude/rules/control-non-negotiables.md`.
A control that works but stutters is not done.

- Cost scales with what's on screen: draw and hit-test O(visible), build the
  virtualized path first.
- Smooth animation goes through `ICompositionBackend`; `Composition()` can be null,
  so every caller keeps a UI-thread fallback.
- Nothing on the input path blocks or paints synchronously.
- Idle costs zero: no polling timers; `WantsAnimationTick()` goes false when done.
- Invalidate the smallest correct region: lowest `DirtyFlags`, honest
  `VisualOverflowDip()`.
- Keep the surface area small: no new dependency without asking, no single-caller
  abstraction, reuse existing tokens.
- Verify with the F12 HUD (trust CPU Max) and `FluentUIBench`; smoothness needs the
  user on real hardware.

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

## Persistent task state and context compaction

For every long-running or multi-step task, maintain `.claude/TASK_STATE.md` as the
persistent checkpoint. Create it at the start and update it after every meaningful
state change, not only before compaction. After compaction or when resuming: read
it, run `git status --short`, check the diff matches, then continue from the first
item under `Exact Next Actions`. The full regimen (what to record, when to update,
the resume steps) is the `task-state` skill in `.claude/skills/task-state/`; a
SessionStart hook reminds you after compaction or resume.

The working tree and actual command results are authoritative. Never claim
that a build, test, benchmark, or hardware check passed unless it was actually
executed successfully.

`project_bundle.md` is a generated export for external review and duplicates
source files already available in the working tree. Do not read it during
normal repository work unless the user explicitly requests analysis of that
bundle. Inspect the original repository files instead.

## Working on this codebase

Do not label comments or docs with plan sections or work packages ("roadmap §6.2",
"§S4", "WP-07", "Plan B"). The planning documents those labels pointed to were
deleted; name the mechanism instead. Some older comments, mostly in tests, still
carry such labels (`P1-12`, `Phase 2`); replace one with the mechanism when you
touch that code rather than copying the style.

What actually exists under `docs/`:

- `docs/api-surface.md` — public API conventions.
- `docs/BUILD_OPTIMIZATION.md` — build performance notes.
- `docs/controls/` — per-control reference pages off `TEMPLATE.md`.
- `docs/design/` — design and task documents for in-flight features. **Transient**:
  deleted once the work lands.
- `docs/dual-repo-workflow.md` — workflow for syncing between internal and public repos.
- `docs/learn/` — Chinese technical notes (compositor clipping, borderless-window
  resize, DirectWrite font fallback, UI Automation server-side provider).

**Documentation lifetime rule.** Only knowledge, learning notes, and architecture
docs are permanent (`CLAUDE.md`, `.claude/rules/`, `.claude/skills/`, `.claude/agents/`, `docs/api-surface.md`, `docs/BUILD_OPTIMIZATION.md`,
`docs/controls/`, `docs/learn/`, library READMEs). Design and task documents are
deleted when finished, so:

- Permanent docs must not link to `docs/design/` or other task documents. When a
  task finishes, move whatever is still true (the decision, the rationale, the
  pitfall) into a permanent doc, then delete the task doc.
- No doc may reference git history: no commit SHAs, no `git show`/`git log`
  recipes, no "the last working version is commit X". State the knowledge itself.
- A code comment follows the same rule: describe the behavior, not the plan
  section it came from.

Long-running task state lives in `.claude/TASK_STATE.md` (regimen in the `task-state`
skill). That path is **gitignored**, so it is local scratch rather than a repo
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
