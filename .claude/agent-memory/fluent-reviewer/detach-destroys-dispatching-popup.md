---
name: detach-destroys-dispatching-popup
description: Controls that reset popup_/content in OnDetachedFromTree can free the PopupHost and the content's Event mid-dispatch when a user handler detaches the control synchronously
metadata:
  type: project
---

Popup-owning controls used to destroy the PopupHost and its content on detach
(`FluentUI/controls/ComboBox.cpp` did until the collection-controls audit; it and `DatePicker.cpp`
now keep them and reuse on re-attach). Watch for new controls repeating it. That detach can be reached synchronously from
inside the popup's own input dispatch: content raises SelectionChanged -> control
raises its public event -> app handler switches TabControl page / removes the
control -> DetachFromContext -> reset. On return, `Event::Raise` touches the freed
content's `slots_`/`raising_`, and `InputManager::PointerPressed` calls
`RequestInvalidate()` on the freed PopupHost member.

**Why:** Event::Raise and TabControl::SetSelectedIndex are both synchronous, and
nothing defers teardown. Headless tests never drive the popup WndProc path.

**How to apply:** When a diff adds teardown of popup/content in OnDetachedFromTree,
trace whether a user-visible event raised from that content can reach it. Suggest
re-subscribing in OnAttachedToTree instead, or deferring destruction (PostMessage /
next frame). See also [[untested-nativewindowhost-behavior]].

The same hazard applies to any synchronous subtree destruction, beyond popups. Example:
`Window::SetRoot` (`FluentUI/window/Window.cpp`) on a live window frees the old root
right away. If a Click handler inside that root calls it, `Event::Raise` returns into
the freed Button's `slots_`.
