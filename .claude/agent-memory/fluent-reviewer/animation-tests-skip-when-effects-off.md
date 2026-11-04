---
name: animation-tests-skip-when-effects-off
description: Transition tests (e.g. PageHostTests) early-return when the Windows "Animation effects" setting is off, so they pass without asserting on such machines
metadata:
  type: project
---

Transition tests guard with `if (!host.IsTransitioning()) return;` because
transitions respect the system "Animation effects" setting. Example:
`tests/unit/PageHostTests.cpp` (BothPagesAreMeasuredDuringATransition,
FadeTickDirtiesRenderNotArrange, FadeSlideTickStillArranges).

**Why:** on a machine (or CI session) with animations disabled these tests run zero
assertions and still report green, so a green run does not prove the fix.
**How to apply:** when a fix is "verified" only by such a test, ask whether the run
happened with animation effects on (the fixer's mutation run is the evidence), and
mention the vacuous-pass risk instead of treating green as proof.
