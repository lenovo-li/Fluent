---
name: demo-check
description: Pre-flight check in the real FluentUIDemo window before asking the user to verify on hardware — launch the built demo through cua-driver, open the gallery page of each changed control, take a screenshot and read its UIA tree. Use after /verify passes for a change with visible or accessibility impact.
argument-hint: "[ControlName ...]  (default: controls changed in the working tree)"
disable-model-invocation: true
---

Controls to check: $ARGUMENTS

This runs in the main conversation, because the screenshots have to be looked at
by whoever reports to the user. It does not build: run `/verify` first. If the
demo binary is older than the newest changed source file, stop and say so rather
than checking a stale build.

## What this can and cannot show

It proves one real frame through the real D2D/DComp stack at the current DPI:
layout, clipping, theme colors, text, and whether the control's UIA peer exposes
the right role, name and patterns. It does **not** prove ghosting, flicker,
animation smoothness or partial-redraw correctness across frames; those still go
to the user on real hardware, with the exact place to look. Never report a
demo-check as "visually verified" beyond the frames actually captured.

## Steps

1. **Pick the pages.** With no argument, take the changed controls from
   `git status --short` under `FluentUI/controls/`, `FluentUI/layout/` and
   `extensions/`. The gallery labels live in `GalleryApp::BuildNavigationTree()`
   in `FluentUIDemo/GalleryMain.cpp` (e.g. `ListBox`, `NavRail + PageHost`,
   framework pages such as `透明度`). A control with no gallery row: say so and
   skip it, do not invent a page.

2. **Launch in the background.** `launch_app` with
   `path: D:\project\Fluent\x64\Debug\FluentUIDemo.exe` and a `session` label
   (repeat it on every call). Keep the returned `pid` and `window_id`. Stay in
   `delivery_mode: "background"`; the user may be working in another window.

3. **Navigate through UIA, not pixels.** The left navigation is a TreeView whose
   peer exposes each row as `TreeItem "<label>"` with `select`. Find it with
   `get_window_state` and `query: "<label>"`, then `click` its `element_token`
   (this performs `SelectionItem.Select`). Rows below the fold may be missing
   from the tree because the TreeView is virtualized; scroll the `Tree` element
   and query again.

4. **Capture.** `get_window_state` with `max_image_dimension: 1200` for the
   screenshot, then a second read with `query` set to the control's role or name
   (e.g. `List`, `ComboBox`) and a larger `max_elements` for its subtree. The
   default budget of 60–250 rows is used up by the navigation tree alone.

5. **Exercise only what the change touched.** For an interaction fix, drive the
   one interaction through `run_steps` with `observe: {}` (open the ComboBox,
   scroll the LogViewer, select a ListBox row), then capture again. Do not tour
   the whole page.

6. **Optional DPI and theme pass**, only when the change is DPI- or
   theme-sensitive: the `🌙` button in the header toggles the theme; for DPI
   use `set_window_frame` to move the window to another monitor if one exists,
   otherwise say that the second DPI was not checked.

7. **Close.** `kill_app` on the pid. The demo writes its `SessionLog` only on a
   normal close, so if timing data is wanted, invoke the `关闭` caption button
   through UIA instead.

## What to look for

- Screenshot: clipped or overlapping content, text cut off, wrong theme colors,
  controls drawn outside their area, scrollbars missing or doubled.
- UIA tree: the control appears with the expected role and a non-empty name;
  the patterns match its behavior (`invoke`, `toggle`, `expand`, `select`,
  `set_value`, `scroll`); items of virtualized controls appear for the visible
  rows only. A missing or wrongly-roled peer is a real finding against
  `FluentUI/automation/`.

## Report

Per control: the page opened, what the screenshot shows (one or two sentences,
concrete), the UIA findings, and anything that still needs the user's eyes on
hardware with the precise spot to watch. List skipped controls with the reason.
