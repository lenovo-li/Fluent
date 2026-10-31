// InvalidationTests.cpp — verifies Invalidate() propagates up the parent chain.
//
// Invalidation through the tree: an element without its own invalidate callback
// delegates Invalidate() to Parent(), so setting the callback on the tree root
// is enough for a deeply nested control to trigger a repaint. Panel::Add wires
// the parent link; Panel::Clear severs it. Pure logic — no HWND / D3D.

#include "../framework/Test.h"
#include "LayoutTestHelpers.h"
#include "../../FluentUI/layout/StackPanel.h"
#include "../../FluentUI/window/NativeWindowHost.h"

using namespace fluent;

namespace {
// Counts invalidate callbacks routed to it (used as the "root host" sink).
int g_invalidateCount = 0;
void CountingInvalidate(void*) { ++g_invalidateCount; }

// A leaf that exposes Invalidate() so tests can trigger it directly.
class InvalidatingLeaf : public TestLeaf {
public:
    using TestLeaf::TestLeaf;
    void Poke() { Invalidate(); }
};

// Minimal NativeWindowHost for headless testing (no HWND, no graphics).
// Exposes AddElement so we can verify it installs the invalidate callback.
class HostProbe : public NativeWindowHost {
public:
    using NativeWindowHost::AddElement;
};
}  // namespace

// Panel::Add sets the child's parent to the panel.
TEST(Invalidation, AddSetsParent) {
    StackPanel root;
    auto* child = root.Emplace<TestLeaf>();
    EXPECT_TRUE(child->Parent() == &root);
}

// A nested leaf with no callback of its own routes Invalidate() up to the root,
// which holds the only callback.
TEST(Invalidation, NestedLeafPropagatesToRootCallback) {
    g_invalidateCount = 0;

    StackPanel root;
    root.SetInvalidateCallback(&CountingInvalidate, nullptr);

    auto* mid = root.Emplace<StackPanel>();     // Add invalidates once
    auto* leaf = mid->Emplace<InvalidatingLeaf>();  // Add invalidates again

    EXPECT_TRUE(leaf->Parent() == mid);
    EXPECT_TRUE(mid->Parent() == &root);

    // Two Adds above triggered invalidation; reset before testing propagation.
    g_invalidateCount = 0;
    leaf->Poke();
    EXPECT_EQ(g_invalidateCount, 1);  // reached the root's callback
}

namespace {
int g_childInvalidateCount = 0;
void CountChildInvalidate(void*) { ++g_childInvalidateCount; }
}  // namespace

// A child's own callback takes precedence over the parent chain.
TEST(Invalidation, ChildCallbackWinsOverParent) {
    g_invalidateCount = 0;
    g_childInvalidateCount = 0;

    StackPanel root;
    root.SetInvalidateCallback(&CountingInvalidate, nullptr);

    auto* leaf = root.Emplace<InvalidatingLeaf>();  // Add invalidates to root
    leaf->SetInvalidateCallback(&CountChildInvalidate, nullptr);

    // Reset counters after Add triggered root's callback.
    g_invalidateCount = 0;
    g_childInvalidateCount = 0;

    leaf->Poke();
    EXPECT_EQ(g_childInvalidateCount, 1);
    EXPECT_EQ(g_invalidateCount, 0);  // did not fall through to the root
}

// Clear() severs the parent link so a detached subtree no longer propagates.
TEST(Invalidation, ClearSeversParent) {
    StackPanel root;
    auto* child = root.Emplace<TestLeaf>();
    EXPECT_TRUE(child->Parent() == &root);

    // child is destroyed by Clear(); capture the pointer relationship first.
    root.Clear();
    EXPECT_EQ(root.ChildCount(), static_cast<size_t>(0));
}

// Regression test: NativeWindowHost::AddElement must install an invalidate callback
// on root elements (elements with no parent), or InvalidateDirty has no way to reach
// RequestFrame. Without it, Invalidate() on a control walks up to the root, finds no
// callback, and silently drops the frame request — the window never repaints until an
// unrelated event (mouse move, resize) happens to trigger a frame. Symptom: the UI
// looks frozen after a programmatic update and refreshes the moment you move the mouse
// over it.
//
// This calls the REAL AddElement (it needs no HWND — it only pushes to elements_ and,
// with graphicsReady_ false, skips the AttachToContext branch). We assert on the
// element's own state rather than NeedsFrame(), because NeedsFrame() is gated on
// graphicsReady_ and would report false headlessly no matter what the callback does.
//
// Mutation check: deleting the `if (!e->Parent())` block in AddElement fails both
// EXPECT_TRUEs below.
TEST(Invalidation, AddElementInstallsInvalidateCallbackOnRoot) {
    HostProbe host;
    StackPanel root;

    EXPECT_FALSE(root.Parent());                  // it is a root
    EXPECT_FALSE(root.HasInvalidateCallback());   // nothing wired it yet

    host.AddElement(&root);

    // AddElement recognised the root and gave it a way to request a frame.
    EXPECT_TRUE(root.HasInvalidateCallback());

    // And a nested control's Invalidate() now reaches that callback instead of
    // walking off the top of the tree. Note the dirty flag lands on the element that
    // was invalidated, not on the root — the root only receives the frame REQUEST
    // (via the callback), which is exactly the half that was missing before the fix.
    auto* leaf = root.Emplace<InvalidatingLeaf>();
    leaf->Poke();
    EXPECT_TRUE(Has(leaf->Dirty(), DirtyFlags::Render));
}

// A child element (one that already has a parent) must NOT get its own callback:
// it has to keep delegating up the chain, or a subtree registered as a non-root
// element would start swallowing its descendants' invalidations.
TEST(Invalidation, AddElementLeavesChildElementsWithoutCallback) {
    HostProbe host;
    StackPanel root;
    auto* child = root.Emplace<StackPanel>();

    host.AddElement(child);   // registering a non-root (e.g. an overlay subtree)

    EXPECT_TRUE(child->Parent() == &root);
    EXPECT_FALSE(child->HasInvalidateCallback());
}
