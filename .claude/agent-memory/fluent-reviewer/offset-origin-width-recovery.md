---
name: offset-origin-width-recovery
description: Pen x starts at origin - OffsetX; deriving a logical width from the final pen must add OffsetX back. Fixes on rarely reached paths get this backwards.
metadata:
  type: project
---

When a draw loop starts its pen at `bounds_.x + kPadding - scroll_.OffsetX()` and
advances by span widths, the logical line width is
`x - (bounds_.x + kPadding - OffsetX)`. Subtracting only `bounds_.x + kPadding`
yields `width - OffsetX`, which under-reports by the current scroll.

Seen in `FluentUI/controls/LogViewer.cpp` RenderLine (late-measure width feed into
`maxLineWidth_`), where a "fix" removed the correct `+ OffsetX`, while
`CachedLineWidthForTest` in `LogViewer.h` kept it. The path had no test because it
only runs when LayoutVisibleLines hits its pass cap.

**Why:** horizontal scroll retrofits touch several x-origin sites at once; the
author reasons in "screen vs DIP" terms and drops the offset term.

**How to apply:** for every site that turns a pen position back into a width or
column after an origin shift, do the algebra explicitly; distrust "unreachable,
but theoretically correct" claims and check the arithmetic. Related:
[[scroll-extent-tests-tautological]].
