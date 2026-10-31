// PopupState.h — explicit popup lifecycle state + pure transition rules.
//
// PopupHost tracks its lifecycle as a four-state machine rather than a plain
// `bool open_`, so the intermediate states (Opening / Closing) are representable
// and re-entrancy during the close side effects is guarded.
//
// Closing covers only the popup's OWN side effects (unregistering its animation
// root, hiding the HWND). The owner's onClose_ callback runs after the state has
// already become Closed, as the very last thing Close() does. Two consequences the
// owners (ComboBox, DatePicker) rely on, because they raise their public
// DropDownClosed / CalendarClosed events from that callback:
//   * a handler that reopens the popup works: Open() sees Closed, not Closing;
//   * Close() itself touches no member after the callback returns.
// That second point does NOT make it safe for a handler to destroy the owner
// synchronously. The public event is raised by the owner's Event::Raise, which reads
// its own slot list after each handler; and when the close came from a click inside
// the popup, the popup's InputManager and the clicked element are still on the stack.
// A handler that wants the control gone must defer it (post a message, or remove it
// on the next frame).
//
// The transition *decisions* live here as pure functions so they can be unit
// tested without a HWND / DComp device. PopupHost holds a PopupState and calls
// these to decide whether an Open/Close request should proceed.
#pragma once

namespace fluent {

enum class PopupState {
    Closed,   // not shown; the default and post-close state
    Opening,  // building graphics / drawing the first frame (transient)
    Open,     // fully shown and interactive
    Closing,  // hiding the HWND, before onClose_ runs; rejects re-open (transient)
};

// Should an Open() request proceed given the current state? Only a Closed popup
// may open. Opening/Open are already (becoming) open; Closing must finish first
// (re-open during close is rejected in v1 rather than queued).
inline bool CanOpen(PopupState s) {
    return s == PopupState::Closed;
}

// Should a Close() request proceed? Only a fully Open popup runs the close
// sequence. Closed/Closing are no-ops (idempotent); Opening is treated as
// not-yet-open and also a no-op (the Open path will settle it).
inline bool CanClose(PopupState s) {
    return s == PopupState::Open;
}

// IsOpen for external callers: true only in the fully-Open state.
inline bool IsOpenState(PopupState s) {
    return s == PopupState::Open;
}

} // namespace fluent
