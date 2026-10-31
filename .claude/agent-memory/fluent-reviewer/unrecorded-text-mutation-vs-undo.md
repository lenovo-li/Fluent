---
name: unrecorded-text-mutation-vs-undo
description: Any TextEditBase path that changes text_ without recording or clearing undo (menu Cut, TextArea trim) makes the next Ctrl+Z delete the wrong text
metadata:
  type: project
---

`TextEditBase` undo is an offset-based operation log (`text/UndoStack.h`), and
`ApplyUndoResult` clamps offsets instead of failing. Any mutation of `text_` that
neither records an entry nor calls `undo_.Clear()` leaves the stack describing a
buffer that no longer exists. The next Ctrl+Z then silently deletes or restores the
wrong characters. Seen so far: the context-menu Cut calling bare `DeleteSelection()`
(fixed), and `TextArea::TrimToMaxLines` (`FluentUI/controls/TextArea.cpp`, reached by
`SetMaxLines`/`AppendText`), which erases from the front with no undo handling.

**Why:** The clamp turns a desync into wrong text instead of a crash, so no test
fails unless it undoes after the mutation.

**How to apply:** For any diff touching `text_` in `TextEditBase` subclasses, check
that every new write goes through `Record*`/`EraseRange`/`DeleteSelectionRecorded`,
or clears history. Front-shifting writes (trims) need extra care. Docs that list
such a path as "does not record" without saying the stack goes stale are a finding.
