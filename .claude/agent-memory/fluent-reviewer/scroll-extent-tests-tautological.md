---
name: scroll-extent-tests-tautological
description: "last child visible at MaxOffset" via Bounds() is an identity in ScrollPanel; it cannot catch stale wrapped-text desired heights
metadata:
  type: feedback
---

A test that scrolls a ScrollPanel to MaxVerticalOffset() and asserts the last child's
Bounds().bottom() <= viewport bottom cannot fail from a wrong extent. ArrangeOverride
stacks children by the same Desired().h values that MeasureChildren summed into
contentHeight_, and MaxOffset = contentHeight_ + padding - bounds.h, so the last child
always lands at bounds.bottom - padding.bottom. Example:
`tests/unit/ScrollPanelDynamicCardTest.cpp` (2026-11-17).

**Why:** the real clipping bug is a child's Desired().h computed at a wider width than
it is arranged at (wrapped text gains a line). Bounds() inherits that stale height, so
the stale value is invisible to a bounds check.

**How to apply:** for scroll-extent / clipping tests, demand an independent height
oracle: re-measure a fresh identical child at its arranged Bounds().w and compare to
Bounds().h, or compare a TextBlock's Desired().h with a fresh measure at its arranged
width. Mutation to verify: delete ScrollPanel's Arrange re-measure plus the narrowW
re-measure in MeasureOverride. Related: [[mutation-test-needs-the-right-signal]].
