---
paths:
  - "FluentUI/text/**"
  - "FluentUI/controls/Text*"
  - "FluentUI/controls/PasswordBox*"
  - "FluentUI/controls/RichText*"
  - "FluentUI/controls/LogViewer*"
  - "FluentUI/controls/NumericUpDown*"
  - "tests/**/*Text*"
---

### Undo/Redo

`text/UndoStack.h` implements a standard two-stack undo/redo with grouped edits.
`TextEditBase` holds an `UndoStack` and records text operations (`RecordSnapshot`
before mutating `text_`). Three rules:

- **Record before you mutate.** `SetText` / `DeleteSelection` / `InsertText` all
  call `RecordSnapshot(GetText())` before touching `text_`. Recording after makes
  the undo restore the *new* state instead of the old one.
- **Group typed characters.** `RecordSnapshot` takes a `groupWithPrevious` flag —
  ordinary keystrokes set it true so "abc" becomes one undo instead of three.
  Paste, delete selection, or any programmatic `SetText` breaks the group.
- **Clear on SetText from outside.** When text is set by the host (not via
  typing), call `ClearUndoStack()` — the old buffer is gone and an undo would
  restore garbage. The exception: restoring a *snapshot* (like "load autosave")
  can leave undo intact if the snapshot is one of the states already in the stack.

TextBox and TextArea expose `Undo()` / `Redo()` / `CanUndo()` / `CanRedo()` and
bind Ctrl+Z/Ctrl+Y in their `OnKeyDown`. When adding a new text-input control,
the same pattern applies: hold an `UndoStack`, record before mutating, and clear
on external `SetText`.

### Text virtualization

TextArea virtualizes both NoWrap (exact, via arithmetic) and Wrap (estimated, via
measure-on-demand). See `controls/TextArea.h` (both modes), `text/WrapExtentMap.h`
(estimation strategy), `text/LineLayoutCache.h` (per-line layouts),
`text/LogSink.h` (off-thread append), and `docs/controls/TextArea.md` for usage.
