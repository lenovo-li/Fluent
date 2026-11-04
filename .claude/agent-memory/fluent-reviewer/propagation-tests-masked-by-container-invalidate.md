---
name: propagation-tests-masked-by-container-invalidate
description: Tests for "dirty child joins tree" pass even with the propagation fix reverted, because Panel::Add/SetChild invalidate the container themselves
metadata:
  type: feedback
---

When a change claims to fix "element dirtied while detached, then attached" (e.g. Visual::SetParent carrying the subtree mark up), tests that attach through Panel::Add / Border::SetChild do not prove it: those adopters call InvalidateDirty(Measure) on the container, which marks the path anyway. Only a bare SetParent (DataGrid's editOverlay_ in FluentUI/controls/DataGrid.cpp) depends on the fix.

**Why:** the subtree-dirty pruning review (tests/unit/SubtreeDirtyTests.cpp, SubtreeDirtiedOffTreeThenAddedIsCollected / ReparentedDirtyChildIsCollectedUnderNewParent) had several "attach" tests, but only ElementDirtiedWhileDetachedIsMarkedOnSetParent fails when SetParent's propagation is removed.

**How to apply:** for any invalidation-propagation fix, ask which test dies if the fix is reverted. If the attach goes through a container API that invalidates itself, the test is not covering the fix. Related: [[mutation-test-needs-the-right-signal]], [[hidden-child-dirty-bit-lost]].
