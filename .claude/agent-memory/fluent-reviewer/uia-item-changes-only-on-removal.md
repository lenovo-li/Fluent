---
name: uia-item-changes-only-on-removal
description: UIA item containers detect child changes by count delta or a released cached key; same-count content shifts (capped log at its cap, re-sort) and hit-test points below the last row slip through
metadata:
  type: project
---

`ItemsContainerPeer` (`FluentUI/automation/ItemPeers.cpp`) now raises ChildrenChanged
when the item count changed or a cached key was released, and re-checks up to 64
cached visible items per flush (`QueueItemRechecks`). Still missed: a change that
keeps the count AND releases no cached item. Examples: `LogViewer` at its
`SetMaxLines` cap (each append drops one line and adds one; if the client cached
no trimmed line, nothing is raised), DataGrid re-sort (index keys, same count; only
visible cached rows get renamed). `OnOwnerInvalidated` also queues the trim only on
a count change, so these cases depend on the listener-gated QueueChanged path.

Hit-testing: `ItemAtPoint` must map host DIPs back through Viewbox (`HostToOwner`),
and the control helper must return "none" past the last item. `LogViewer::LineIndexAt`
uses `LineAtContentY`, which clamps to the last line, so a point in the empty area
below a short log reports the last line.

**Why:** tests use a fake core and check events on the paths the author thought of
(count changes, trims of cached rows), not the steady state of a capped/sorted view.

**How to apply:** for each item container in a diff, ask what a client gets when
content changes at constant count, and what ItemAtPoint returns for a point inside
the control but past the content. See also [[idle-gate-checked-only-at-creation]]
and [[index-keyed-cache-tests-count-only]].
