// AnimationRegistry.h — the active set of currently-animating elements, so the
// animation TICK never scans the whole tree.
//
// The host rebuilds this set at trigger points — input events, and the end of each
// painted frame (which is where program-driven state changes surface) — by calling
// Collect() over its element roots. Between rebuilds the clock ticks only the
// collected set, so the tick costs O(animating elements) rather than O(whole tree).
// The rebuild itself is a tree walk; see Collect for why it is per painted frame.
//
// Depends on UIElement (to call CollectAnimations / OnAnimationTick /
// WantsAnimationTick) but not on any window or COM type, so it is unit-testable
// against a plain UIElement subtree.
#pragma once

#include <vector>

namespace fluent {

class UIElement;

class AnimationRegistry {
public:
    // Rebuild the active set from the given roots. Each root's CollectAnimations
    // appends the elements that currently want a tick (leaves add themselves,
    // panels recurse). It is a whole-tree walk, so call it at trigger points, not
    // from the tick. NativeWindowHost calls it on input events and once at the end
    // of each painted frame (a programmatic change can start an animation with no
    // input at all, and the frame boundary is the one point every change reaches);
    // that keeps the walk proportional to painted frames, and an idle window pays
    // nothing.
    void Collect(const std::vector<UIElement*>& roots);

    // Advance every element in the active set by dt seconds, then drop the ones
    // that no longer want a tick. Returns true while the set is still non-empty
    // (i.e. the host should keep the timer running). Does not render.
    bool Tick(float dtSec);

    // Drop an element from the active set immediately (e.g. it is being detached
    // from the tree / destroyed). No-op if it was not animating. Keeps a torn-down
    // element from being ticked between the detach and the next Collect.
    void Remove(UIElement* e);

    bool Empty() const { return active_.empty(); }
    size_t Count() const { return active_.size(); }

private:
    std::vector<UIElement*> active_;
};

} // namespace fluent
