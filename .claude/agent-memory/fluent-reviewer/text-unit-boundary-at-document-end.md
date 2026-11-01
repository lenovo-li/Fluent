---
name: text-unit-boundary-at-document-end
description: TextPattern unit helpers treat pos == text.size() as its own boundary, so ExpandToEnclosingUnit/Move at the document end return empty or refuse to move
metadata:
  type: project
---

`FluentUI/automation/UiaTextPattern.cpp` computes units with `StartOfUnit`,
`NextBoundary`, `PrevBoundary`. At `pos == n` (caret after the last character, the
normal state after typing), `StartOfUnit` returns `n`, so `ExpandToEnclosingUnit(Line)`
gives an empty range instead of the last line. `Move` stops when `nx >= s.size()`,
so a degenerate range can never move onto the end of the document. CRLF: the
position between `\r` and `\n` counts as a line start in `PrevBoundary`.

**Why:** the tests walk from offset 0 forward over `\n` text and never start at the
end or use CRLF.

**How to apply:** for any text-range or caret-unit code, check pos = 0, pos = n on a
last line with no trailing newline, an empty last line, and `\r\n`. Ask for a test
that expands at the end of the text.
