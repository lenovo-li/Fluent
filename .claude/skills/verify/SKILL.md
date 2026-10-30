---
name: verify
description: Full FluentUI verification in an isolated builder agent — check_all.py, MSBuild Debug + Release, FluentUITests.exe — returning only failures. Use before reporting any code change complete.
argument-hint: "[test-filter]"
context: fork
agent: fluent-builder
background: false
---

Run the full verification for the FluentUI working tree, in this order, and
stop at the first failing stage:

1. `python scripts/check_all.py`
2. MSBuild Debug (`Fluent.slnx`, x64)
3. MSBuild Release (`Fluent.slnx`, x64)
4. `./x64/Debug/FluentUITests.exe $ARGUMENTS`
   (with no argument this runs the whole suite)

Report in your standard format: one line per stage, deduplicated
warnings/errors as `file(line): code: message`, the runner's `N run, M passed`
line, and each failing test with its assertion message. Separate known
environment failures (Device Guard block, `DataGrid.CopyEnabledAllowsCtrlC`
clipboard denial) from real regressions.
