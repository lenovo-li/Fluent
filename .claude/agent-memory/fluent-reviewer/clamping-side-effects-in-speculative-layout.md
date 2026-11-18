---
name: clamping-side-effects-in-speculative-layout
description: Scroll extent commits (SetContentHeight clamps offset_) done mid-layout lose the offset when a later pass grows the extent; tests set the offset only after layout so never see it
metadata:
  type: project
---

`ScrollViewer::SetContentHeight` clamps `offset_` immediately and irreversibly. Any
commit made from a provisional layout value (a Measure pass, or the start of an
Arrange before a re-measure) loses scroll position if a later pass makes content
taller. Example: `FluentUI/layout/ScrollPanel.cpp`. Grid measures each child twice
(full width, then column width, `Grid.cpp` MeasureOverride), so a ScrollPanel in a
Grid used to clamp against the shorter full-width height. Grid also measures with
`availH` = full grid height but arranges at the row height, so the ScrollPanel's
Measure-time rail decision can differ from Arrange's and Arrange re-measures.

**Why:** fixes that move the commit (Measure -> Arrange) tend to keep an early
commit (start of ArrangeOverride, OnBoundsChanged) that still clamps before the
re-measure.

**How to apply:** for any scroll-extent change, find every `SetContentHeight`
call that runs before the final measured value is known. Check that the test sets
the offset to max BEFORE the re-layout and asserts the offset survives it.
Static-path tests (layout, then scroll, then Arrange only) cannot fail on this bug.
See [[scroll-extent-tests-tautological]].
