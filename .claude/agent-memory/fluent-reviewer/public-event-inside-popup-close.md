---
name: public-event-inside-popup-close
description: Public Closed events raised from PopupHost's SetOnClose callback; ordering now fixed (Closed before onClose_), but reopen from a dismiss-hook path loses the new hook and destroy-in-handler is still UAF via Event::Raise
metadata:
  type: project
---

History: `PopupHost::Close()` used to call `onClose_()` while `state_ == Closing`, so a
DropDownClosed / CalendarClosed handler (`FluentUI/controls/ComboBox.cpp` `OnPopupClosed`,
`DatePicker.cpp`) could not reopen, and destroying the control UAF'd on the later
`state_ = Closed` write. The fix moved `state_ = Closed` before a local-copy call of
`onClose_`. Two hazards survive that fix and are easy to miss:

1. **Reopen from a dismiss-hook path loses the new hook.** `NativeWindowHost`'s single-slot
   hooks are invoked as `if (popupDismiss_(...)) popupDismiss_ = nullptr;`
   (`LightDismissAt`, WM_MOVE, WM_ENTERSIZEMOVE, WM_ACTIVATE). If the callback closes, the
   Closed handler reopens, and OpenPopup re-registers, the host's trailing `= nullptr` wipes
   the NEW registration (it ignores `dismissGen_`). The reopened popup has no light dismiss.
   The callback also resets its own `std::function` mid-call (dismissSub_.reset()).
2. **Destroy-in-handler is still UAF one frame up.** `Event::Raise` (`FluentUI/base/Event.h`)
   reads `slots_.size()` and `--raising_` after each handler, so a handler that deletes the
   sender frees the event being raised. A list click also unwinds through the freed
   ComboListView / PopupHost::input_. Comments claiming "a handler may destroy the owner"
   are overclaims.

**Why:** headless tests cannot open a PopupHost, and the generation counter on the
Subscription side makes the hook look safe while the host's direct `= nullptr` bypasses it.

**How to apply:** When a fix enables re-entry from a callback (reopen, re-register), trace
what the caller does AFTER the callback returns: any unconditional reset of a slot the
callback could have refilled is a bug. Treat "safe to destroy the owner in the handler"
claims as false unless Event::Raise and every frame up to the WndProc are checked.
Related: [[detach-destroys-dispatching-popup]], [[untested-nativewindowhost-behavior]].
