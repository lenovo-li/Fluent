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

Two models exist, one per editing base.

**`TextEditBase` (TextBox, TextArea, PasswordBox): operation log.**
`text/UndoStack.h` records each edit as an `UndoAction` (Insert / Delete /
Replace, with the affected text and the caret before and after) and replays its
inverse. The rules the wiring follows:

- **Record after the mutation, through the helpers.** `InsertText` calls
  `RecordInsert` (or `RecordReplace` when it types over a selection, so the
  overwrite undoes in one step); `EraseRange` calls `RecordDelete`;
  `DeleteSelectionRecorded` is `EraseRange` over the selection. Every user-facing
  edit (typing, paste, IME commit, Backspace, Delete, Ctrl+X, the context menu's
  Cut) must go through one of these. The bare `DeleteSelection()` does **not**
  record: it is the raw buffer operation `InsertText` uses inside its own
  `RecordReplace`. Calling it from an edit path leaves the stack describing a
  buffer that no longer exists, and the next Ctrl+Z replays a stale entry against
  shifted offsets (`ApplyUndoResult` clamps, so it deletes the wrong text instead
  of crashing).
- **Typing merges by itself.** The single-character `RecordInsert(start, wchar_t, ...)`
  merges into the previous entry when that entry is a mergeable insert ending
  exactly at `start`, so a typed word is one undo. The string overload (paste,
  IME commit) is never mergeable, and any delete or replace ends the run.
- **Replay does not record.** `Undo()` / `Redo()` take an `UndoResult` from the
  stack and apply it with `ApplyUndoResult`, which writes `text_` directly and
  bypasses casing, filters and `MaxLength` so the restored text is exact.
  Recording there would push the inverse back and make Ctrl+Z a toggle.
- **`SetText` clears the history** (`undo_.Clear()`) instead of recording: a
  replaced document is a new document, and undoing across it would show text
  the caller never displayed. `ClearUndoHistory()` is the public way to do the
  same thing without replacing the text. `TextArea::AppendText` mutates `text_`
  without recording; appending at the end moves no recorded offset, so the
  history stays valid. A `SetMaxLines` trim (from `SetMaxLines` itself or from an
  append that overflows the cap) erases the FRONT of `text_`, which shifts every
  recorded offset, so `TrimToMaxLines` clears the undo/redo history, the same
  rule as `SetText`. Anything else that removes text other than through a
  recorded edit must do the same.
- The stack is bounded (100 entries by default; the oldest falls off) and any new
  record clears redo.

Public surface: `Undo()`, `Redo()`, `CanUndo()`, `CanRedo()`,
`ClearUndoHistory()`. `OnKeyDownRouted` binds Ctrl+Z, Ctrl+Y and Ctrl+Shift+Z;
all are no-ops while read-only.

**`RichTextBox`: whole-document snapshots.** It does not use `UndoStack`. A
`Snapshot` is the full paragraph vector plus caret and anchor, so paragraph
formatting (alignment, spacing, indents) undoes along with the text. The rule is
the opposite of `TextEditBase`'s: call `PushUndo()` **before** mutating, and only
from a top-level edit entry point (typing, delete, paste, cut, a style change),
never from a shared helper, or one user action records two snapshots. Each
`PushUndo()` also clears redo, so an entry point that turns out to change nothing
(Cut with no selection) must return before calling it. Typing is not merged:
each keystroke is its own snapshot, capped at 100.

A new text-input control should derive from `TextEditBase` and get the
operation log for free. If it cannot, pick one model and keep every mutation of
its buffer going through that model's record step.

### Text virtualization

TextArea virtualizes both NoWrap (exact, via arithmetic) and Wrap (estimated, via
measure-on-demand). See `controls/TextArea.h` (both modes), `text/WrapExtentMap.h`
(estimation strategy), `text/LineLayoutCache.h` (per-line layouts),
`text/LogSink.h` (off-thread append), and `docs/controls/TextArea.md` for usage.
