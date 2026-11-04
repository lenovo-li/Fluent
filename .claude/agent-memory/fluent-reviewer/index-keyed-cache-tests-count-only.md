---
name: index-keyed-cache-tests-count-only
description: Tests for caches keyed by line/row index often assert only "how many layouts were built", which passes even when the cache is renumbered wrong
metadata:
  type: project
---

Caches keyed by a line or row index (LogViewer's `layoutRows_`/`layoutFirst_`,
TextArea's `LineLayoutCache`) have to be renumbered whenever the model drops lines
from the front. Their tests usually assert a build counter (`SpanLayoutsBuilt`,
`LineCacheClearCount`) and use identical text on every line. With that setup, an
off-by-N renumbering (the window not shifted on trim) builds *fewer* layouts and
draws the neighbouring line's text, but every assertion still passes.
Example: `tests/unit/LogViewerTests.cpp` `TrimRenumbersTheLayoutWindowWithTheLines`.

**Why:** "Cost stayed O(1)" is easy to count headless; "the right layout is attached
to the right line" needs distinct line contents plus a width or text read through the
cache, so it tends to be skipped.

**How to apply:** When a diff renumbers an index-keyed cache, check that at least one
test uses distinct per-line text and reads something through the cache (layout
width, hit-test column) after the renumbering. Report the gap if not. Related:
[[untested-nativewindowhost-behavior]].
