---
name: programmatic-path-skips-user-events
description: Programmatic/automation actions reuse the external setter (SetText) that skips the user-path event (TextChanged) and filters (MaxLength); check every new "act like the user" entry point
metadata:
  type: project
---

When a diff adds a programmatic "do what the user would do" path (UIA SetValue,
PerformActivate, scripted input), check which setter it calls. External setters
here are deliberately "new document" operations: `TextEditBase::SetText` clears undo
and does NOT raise `TextChanged` or apply `MaxLength` (those live in `Changed()` /
`InsertText`). Example: `FluentUI/automation/ControlPeers.cpp` `TextEditPeer::SetValue`
called `SetText`, so a UIA client filling a form left the app's bound model stale.
`ButtonBase::PerformActivate -> OnActivate` is the good counter-example (same path
as a click).

It recurred across several UIA control peers at once, because many controls
document "programmatic setter does NOT raise X": `NavRail::SetSelectedIndex` (silent,
also skips `Selectable()`; the user path is private `UserSelect`), `NavRail::SetExpanded`
and `Expander::SetExpanded` (no `ExpandedChanged`; user path is private
`ToggleFromUser`), `ComboBox::SetText` (no `TextChanged`). See
`extensions/NavigationControls/NavRailPeer.cpp`, `FluentUI/automation/SelectorPeers.cpp`.
Grep the setter's header comment for "NOT raised" / "silent" / "programmatic".

A subtler variant: the event is raised but a click-path side step is skipped.
`DataGridPeer::SelectItem` (`FluentUI/automation/DataPeers.cpp`) calls `SetSelectedRow`,
while the click path first runs `CommitEdit()` when the clicked row differs from the
editing row. Compare the whole click handler body, not only the event it ends in.

**Why:** the user-path and programmatic-path setters diverge on purpose, and a
headless test that reads `Text()` back passes either way.

**How to apply:** for each new automation/programmatic action, compare against the
keyboard/mouse path: events raised, validation, undo. Ask for a test that subscribes
to the app-visible event, not one that reads the stored value.
