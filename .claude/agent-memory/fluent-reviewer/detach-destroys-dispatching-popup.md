---
name: detach-destroys-dispatching-popup
description: Controls that reset popup_/content in OnDetachedFromTree can free the PopupHost and the content's Event mid-dispatch when a user handler detaches the control synchronously
metadata:
  type: project
---

Popup-owning controls (`FluentUI/controls/ComboBox.cpp`; `DatePicker.cpp` briefly did it and was reverted to keeping them) destroy the
PopupHost and its content on detach. That detach can be reached synchronously from
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
