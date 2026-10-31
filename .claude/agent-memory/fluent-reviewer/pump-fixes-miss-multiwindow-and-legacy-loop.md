---
name: pump-fixes-miss-multiwindow-and-legacy-loop
description: Fixes wired into Application::PumpOneTurn often use a loop-global decision where per-window state matters, and skip the legacy no-Application GetMessage loops
metadata:
  type: project
---

Per-window behavior wired through `Application::PumpOneTurn` (`FluentUI/Application.cpp`)
tends to key on a loop-wide fact. Example: the hang watchdog's idle mark keyed on
`remaining == kWaitForever` (no window needs a frame). That fact is global, but each
window has its own HangDetector. When window B animates without end (a fallback
ProgressBar sweep), window A is never marked idle and never beaten, so A false-alarms
after the threshold.

The same fixes also skip the second pump: the legacy `GetMessageW` loop in
`DialogWindow::ShowDialog` (used without an Application) and the self-frame-timer
hosts never call the new hook.

**Why:** headless tests cover only the pure helper, and the demo runs one main window
through Application, so neither path is exercised.

**How to apply:** When a diff adds a hook to PumpOneTurn, ask two questions. Is the
condition per window or loop-wide? Does every other pump (the DialogWindow legacy
loop, the modal heartbeat, the self-frame WM_TIMER) need the same hook? See
[[untested-nativewindowhost-behavior]].
