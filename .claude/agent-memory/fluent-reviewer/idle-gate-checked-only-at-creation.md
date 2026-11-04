---
name: idle-gate-checked-only-at-creation
description: "Zero cost without a client" gates are checked when an object is created, not on the per-invalidation path it then hooks; costs persist after the client leaves
metadata:
  type: project
---

Idle-cost gates (e.g. `UiaClientsAreListening`, "no root provider yet") tend to be
checked once, where a peer/observer is created. After that the object hooks a hot
path (`Visual::InvalidateDirty` -> `OnObservedInvalidation`) that queues and posts
work unconditionally, so the cost stays after the client disconnects. Example:
`FluentUI/automation/AutomationPeer.cpp` `OnOwnerInvalidated` -> `AutomationHost::QueueChanged`
posts a flush per frame for an animating element and `RaiseEvents` re-reads
(copies) TextBox text even when nobody listens. The test only covered "no client
ever connected".

**Why:** the design rule is "idle costs zero"; tests usually pin the before-first-
client state, not the after-client-left state.

**How to apply:** for any lazily created observer, trace what each subsequent hot-
path call costs once it exists, with the client gone. Ask for a test that sets
`listening=false` after peers exist and asserts zero posts/reads.
