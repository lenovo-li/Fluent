---
name: hidden-child-dirty-bit-lost
description: Containers that skip hidden children in MeasureOverride lose Measure dirty bits to the frame-end ClearDirtySubtree; check every page/tab host for the hidden-but-dirty measure
metadata:
  type: project
---

Recurring defect: a container that measures only its visible/current child
(page hosts, wizards, tab-like controls) drops a hidden child's Measure dirty bit.
`Panel::ClearDirtySubtree` ignores visibility, `Visual::InvalidateDirty` only
propagates `DirtyFlags::None` upward, and `Panel::NeedsRemeasure` depends on the
dirty bit, so intermediate panels return stale `MeasureCached` sizes when the page
is shown again at the same width.

Seen in `extensions/NavigationControls/PageHost.cpp` (fixed first) and
`extensions/InstallerControls/WizardControl.cpp` (same bug, fixed later).
Correct shape: in MeasureOverride, also `MeasureCached` hidden children whose
`NeedsRemeasure()` is true, without counting them in desired size.

**Why:** the same bug shipped twice because the second container copied the
"hidden page costs no layout" idea without the PageHost fix.
**How to apply:** when reviewing any new container that hides children (or a
change to one), grep its MeasureOverride for the hidden-but-dirty loop. A proper
test is page > group > leaf with ClearDirtySubtree between frames; without the
middle panel the bug cannot be seen. Related: [[animation-tests-skip-when-effects-off]].
