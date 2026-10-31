// Visual.cpp — see Visual.h.

#include "Visual.h"

namespace fluent {

void Visual::InvalidateDirty(DirtyFlags flags) {
    // Accumulate the closure of the requested flags on this visual so a change
    // records exactly what it dirtied (Measure -> Arrange -> Render).
    dirty_ |= ExpandDirty(flags);

    // A Measure invalidation means the desired size may have changed, so any
    // subclass measure cache must be dropped. Visual keeps no
    // measure state itself, so it delegates to the hook UIElement overrides —
    // keeping the layer boundary clean.
    if (Has(flags, DirtyFlags::Measure)) OnMeasureInvalidated();
    if (observeInvalidation_) OnObservedInvalidation();

    // Record the change in the subtree mark of this node and of every ancestor,
    // so the host's per-frame walks can skip every subtree nobody touched (see
    // Visual::SubtreeDirty). Render is always included, even for flags == None:
    // a None invalidation is a request to be VISITED this frame (ScrollPanel's
    // rail-only repaint reports pixels without setting its own dirty_), and
    // Render is the bit that drives the CollectDirtyBounds / ClearDirtySubtree
    // visit without implying any layout.
    //
    // The mark walk goes all the way to the top of the Parent() chain, past a
    // node that carries its own invalidate callback. Before pruning, the frame
    // request stopped at the first callback and that was all; but an outer tree
    // that also walks this node (a popup root that is still parented, say) must
    // see the mark too, or the pruned outer walk would skip it.
    const DirtyFlags mark = ExpandDirty(flags) | DirtyFlags::Render;
    subtreeDirty_ |= mark;
    MarkAncestorsSubtreeDirty(mark);

    // Request a frame: prefer this visual's own callback; otherwise the nearest
    // ancestor's. Setting the callback on the tree root is enough for the whole
    // tree — nested elements need no invalidate hook of their own. Iterative
    // rather than recursing through parent_->InvalidateDirty(None) as this used
    // to: that recursion would re-run the mark walk once per level (O(depth^2)),
    // and the parent has nothing of its own to record.
    for (Visual* v = this; v; v = v->parent_) {
        if (v->invalidateCb_) {
            v->invalidateCb_(v->invalidateCtx_);
            return;
        }
    }
}

} // namespace fluent
