---
name: untested-nativewindowhost-behavior
description: NativeWindowHost RenderNow/SetX behavior changes (forceFull inputs, RequestFrame calls) usually have no headless test; new tests cover only extracted pure helpers
metadata:
  type: project
---

Changes inside `NativeWindowHost::RenderNow` or its setters (what feeds `forceFull`,
whether a toggle calls `RequestFrame`) are typically not covered by any test. The
accompanying tests exercise only the pure helper extracted alongside
(example: the off-thread HUD change tested `diagnostics/HudFeed.h` only; removing
`hudVisible_` from `forceFull` and the per-frame `RequestFrame` had no test, and
`SetHudVisible` has zero callers in `tests/`).

**Why:** NativeWindowHost needs an HWND and device, so tests stay headless around it.
The repo's CLAUDE.md notes bugs shipped past green suites for this reason.

**How to apply:** When a diff changes a RenderNow decision, grep `tests/` for the
member/setter. If absent, report the gap and suggest extracting the decision into a
pure function (PlanRedraw pattern) so a test can pin it.
