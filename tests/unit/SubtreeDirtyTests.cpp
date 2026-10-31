// SubtreeDirtyTests.cpp — verifies subtree dirty queries and that size-affecting
// setters mark Measure dirty (draw-time auto relayout).
//
// The host calls AnyDirtyInSubtree(Measure) before a frame and re-runs layout if
// any descendant needs re-measuring; ClearDirtySubtree() resets the whole tree
// afterwards. These tests lock that Panel recurses correctly and that changing a
// control's text (which changes its desired size) sets the Measure bit so the
// host will relayout. Pure logic — no HWND / D3D.

#include "../framework/Test.h"
#include "LayoutTestHelpers.h"
#include "../../FluentUI/core/Invalidation.h"
#include "../../FluentUI/layout/StackPanel.h"
#include "../../FluentUI/controls/CheckBox.h"
#include "../../FluentUI/controls/Button.h"
#include "../../FluentUI/layout/Border.h"
#include "../../FluentUI/layout/GroupBox.h"
#include "../../FluentUI/layout/ScrollPanel.h"
#include "../../FluentUI/layout/Viewbox.h"
#include <memory>
#include <vector>

using namespace fluent;

// A clean tree reports no dirty anywhere.
TEST(SubtreeDirty, CleanTreeHasNoDirty) {
    StackPanel root;
    auto* mid = root.Emplace<StackPanel>();
    mid->Emplace<TestLeaf>();
    root.ClearDirtySubtree();
    EXPECT_FALSE(root.AnyDirtyInSubtree(DirtyFlags::Measure));
    EXPECT_FALSE(root.AnyDirtyInSubtree(DirtyFlags::Render));
}

// A deeply nested leaf's Measure dirty is visible from the root's subtree query.
TEST(SubtreeDirty, NestedMeasureDirtyVisibleFromRoot) {
    StackPanel root;
    auto* mid = root.Emplace<StackPanel>();
    auto* leaf = mid->Emplace<CheckBox>();
    root.ClearDirtySubtree();

    EXPECT_FALSE(root.AnyDirtyInSubtree(DirtyFlags::Measure));
    leaf->SetText(L"changed");  // size-affecting: marks Measure dirty
    EXPECT_TRUE(root.AnyDirtyInSubtree(DirtyFlags::Measure));
}

// ClearDirtySubtree() clears the whole tree, not just the root node.
TEST(SubtreeDirty, ClearResetsEntireTree) {
    StackPanel root;
    auto* leaf = root.Emplace<CheckBox>();
    leaf->SetText(L"dirty");
    EXPECT_TRUE(root.AnyDirtyInSubtree(DirtyFlags::Measure));

    root.ClearDirtySubtree();
    EXPECT_FALSE(root.AnyDirtyInSubtree(DirtyFlags::Measure));
    EXPECT_FALSE(leaf->AnyDirtyInSubtree(DirtyFlags::Measure));
}

// A Render-only change (not size-affecting) does NOT set Measure, so the host
// repaints without a needless relayout.
TEST(SubtreeDirty, CheckedChangeIsRenderNotMeasure) {
    StackPanel root;
    auto* leaf = root.Emplace<CheckBox>();
    root.ClearDirtySubtree();

    leaf->SetChecked(true);  // visual toggle only
    EXPECT_TRUE(root.AnyDirtyInSubtree(DirtyFlags::Render));
    EXPECT_FALSE(root.AnyDirtyInSubtree(DirtyFlags::Measure));
}

// ===========================================================================
// Subtree-mark pruning of the per-frame dirty walks.
//
// The host runs AnyDirtyInSubtree(Measure), AnyDirtyInSubtree(Arrange),
// CollectDirtyBounds and ClearDirtySubtree on every painted frame. Each now skips
// a subtree whose Visual::SubtreeDirty() mark is clear. The tests below pin two
// things: that the pruning actually happens (a clean sibling is not visited), and
// the dirty-bounds contract it must not break (every changed pixel is still
// reported, every flag is still cleared). Frame() reproduces the host's sequence.
// ===========================================================================

namespace {

// A fixed-size leaf the test can invalidate from outside, the way a caret blink or
// a hover change invalidates from inside a real control.
class PokeLeaf : public FrameworkElement {
public:
    PokeLeaf(float w = 40.0f, float h = 10.0f) { SetWidth(w); SetHeight(h); }
    void Render(const DrawingContext&) override {}
    void Poke() { Invalidate(); }
    void PokeMeasure() { InvalidateMeasure(); }
};

// Counts how often the frame walks reach its overridable bodies. A pruned walk
// never calls them for a clean leaf; an unpruned one calls them every frame.
class CountingLeaf : public PokeLeaf {
public:
    int collectVisits = 0;
    int clearVisits = 0;
protected:
    void CollectDirtyBoundsOverride(std::vector<RectDip>& out) override {
        ++collectVisits;
        PokeLeaf::CollectDirtyBoundsOverride(out);
    }
    void ClearDirtySubtreeOverride() override {
        ++clearVisits;
        PokeLeaf::ClearDirtySubtreeOverride();
    }
};

// A leaf that invalidates itself again from inside the end-of-frame clear, once.
// Stands in for "an invalidate that lands while the host is still finishing the
// frame": the fresh request must survive into the next frame.
class ReinvalidatingLeaf : public PokeLeaf {
public:
    bool armed = false;
protected:
    void ClearDirtySubtreeOverride() override {
        PokeLeaf::ClearDirtySubtreeOverride();
        if (armed) { armed = false; Invalidate(); }
    }
};

struct FrameResult {
    bool measure = false;
    bool arrange = false;
    std::vector<RectDip> rects;
};

// The host's per-frame dirty analysis + clear, in RenderNow's order (paint omitted).
FrameResult Frame(UIElement& root) {
    FrameResult r;
    r.measure = root.AnyDirtyInSubtree(DirtyFlags::Measure);
    r.arrange = root.AnyDirtyInSubtree(DirtyFlags::Arrange);
    root.CollectDirtyBounds(r.rects);
    root.ClearDirtySubtree();
    return r;
}

bool Covers(const std::vector<RectDip>& rects, const RectDip& want) {
    for (const RectDip& r : rects)
        if (r.x <= want.x && r.y <= want.y && r.right() >= want.right() &&
            r.bottom() >= want.bottom())
            return true;
    return false;
}

// root -> 3 sections -> 3 leaves each, laid out and settled.
struct Tree {
    StackPanel root;
    StackPanel* sections[3]{};
    CountingLeaf* leaves[3][3]{};
    Tree() {
        for (int s = 0; s < 3; ++s) {
            sections[s] = root.Emplace<StackPanel>();
            for (int l = 0; l < 3; ++l) leaves[s][l] = sections[s]->Emplace<CountingLeaf>();
        }
        root.UpdateLayout({0, 0, 200, 400});
        Frame(root);  // settle: the first frame consumes the construction dirt
        ResetCounts();
    }
    void ResetCounts() {
        for (auto& row : leaves)
            for (auto* l : row) l->collectVisits = l->clearVisits = 0;
    }
    int TotalVisits() const {
        int n = 0;
        for (auto& row : leaves)
            for (auto* l : row) n += l->collectVisits + l->clearVisits;
        return n;
    }
};

}  // namespace

// A dirty grandchild under clean ancestors is still reported, and only it. Its
// ancestors were clean before the poke; only the mark InvalidateDirty left on the
// path lets the pruned walk reach it.
TEST(SubtreeDirtyPruning, DirtyGrandchildUnderCleanAncestorsIsCollected) {
    Tree t;
    CountingLeaf* leaf = t.leaves[1][2];
    EXPECT_FALSE(Any(t.root.SubtreeDirty()));          // premise: settled tree is unmarked
    leaf->Poke();
    EXPECT_TRUE(Has(t.root.SubtreeDirty(), DirtyFlags::Render));
    EXPECT_TRUE(Has(t.sections[1]->SubtreeDirty(), DirtyFlags::Render));
    EXPECT_FALSE(Any(t.sections[0]->SubtreeDirty()));  // untouched sibling stays unmarked

    const FrameResult f = Frame(t.root);
    EXPECT_FALSE(f.measure);                            // Render-only: no layout
    EXPECT_FALSE(f.arrange);
    EXPECT_EQ(f.rects.size(), static_cast<size_t>(1));
    EXPECT_TRUE(Covers(f.rects, leaf->VisualBounds()));
}

// The pruning itself: on the dirty frame only the dirty leaf's bodies run; on the
// next frame (nothing changed) no leaf body runs at all and nothing is reported.
TEST(SubtreeDirtyPruning, CleanSiblingsAreNotVisitedAndSecondFrameWalksNothing) {
    Tree t;
    CountingLeaf* leaf = t.leaves[2][0];
    leaf->Poke();
    Frame(t.root);
    EXPECT_EQ(leaf->collectVisits, 1);
    EXPECT_EQ(leaf->clearVisits, 1);
    EXPECT_EQ(t.TotalVisits(), 2);                      // nobody else was walked

    EXPECT_FALSE(Any(t.root.SubtreeDirty()));           // the mark was consumed
    EXPECT_FALSE(Any(leaf->Dirty()));
    t.ResetCounts();
    const FrameResult f = Frame(t.root);
    EXPECT_TRUE(f.rects.empty());
    EXPECT_EQ(t.TotalVisits(), 0);
}

// A Measure change deep in the tree is visible to the root's layout query, and a
// clean sibling section does not claim to need a re-measure.
TEST(SubtreeDirtyPruning, LayoutQueriesSeeDeepMeasureThroughCleanAncestors) {
    Tree t;
    t.leaves[0][1]->PokeMeasure();
    EXPECT_TRUE(t.root.AnyDirtyInSubtree(DirtyFlags::Measure));
    EXPECT_TRUE(t.root.AnyDirtyInSubtree(DirtyFlags::Arrange));
    EXPECT_TRUE(t.root.NeedsRemeasure());
    EXPECT_FALSE(t.sections[2]->NeedsRemeasure());
    Frame(t.root);
    EXPECT_FALSE(t.root.AnyDirtyInSubtree(DirtyFlags::Measure));
}

// Hiding a child: its old pixels must be cleared. Collapsing is a Measure change,
// so the frame forces a full redraw (measure == true), and the hidden child's flags
// are cleared even though CollectDirtyBounds skips invisible children; leaving
// them set would re-report a stale rect on the frame it is shown again.
TEST(SubtreeDirtyPruning, HiddenChildForcesLayoutAndIsStillCleared) {
    Tree t;
    CountingLeaf* leaf = t.leaves[1][1];
    leaf->SetVisible(false);
    const FrameResult f = Frame(t.root);
    EXPECT_TRUE(f.measure);                             // -> host forces a full redraw
    EXPECT_FALSE(Any(leaf->Dirty()));
    EXPECT_FALSE(Any(leaf->SubtreeDirty()));
    EXPECT_FALSE(Any(t.root.SubtreeDirty()));
}

// Removal paths: RemoveAt, Clear and Border::SetChild replacement all dirty the
// container at Measure level, so the frame relayouts and redraws in full, which
// is what clears the departed child's pixels (nothing paints them any more).
TEST(SubtreeDirtyPruning, RemovingChildrenForcesLayoutFromCleanTree) {
    Tree t;
    t.sections[0]->RemoveAt(1);
    EXPECT_TRUE(Frame(t.root).measure);

    t.sections[2]->Clear();
    EXPECT_TRUE(Frame(t.root).measure);

    StackPanel root;
    auto* border = root.Emplace<Border>();
    border->SetChild(std::make_unique<PokeLeaf>());
    root.UpdateLayout({0, 0, 200, 200});
    Frame(root);
    EXPECT_FALSE(Frame(root).measure);                  // premise: settled
    border->SetChild(std::make_unique<PokeLeaf>(10.0f, 10.0f));  // replacement
    EXPECT_TRUE(Frame(root).measure);
}

// A child added to a clean, settled parent is reached and painted.
TEST(SubtreeDirtyPruning, ChildAddedToCleanParentIsCollected) {
    Tree t;
    auto* added = t.sections[1]->Emplace<PokeLeaf>();
    EXPECT_TRUE(t.root.AnyDirtyInSubtree(DirtyFlags::Measure));
    t.root.UpdateLayout({0, 0, 200, 400});              // what the host's OnLayout does
    std::vector<RectDip> rects;
    t.root.CollectDirtyBounds(rects);
    t.root.ClearDirtySubtree();
    EXPECT_TRUE(Covers(rects, added->VisualBounds()));
}

// An element dirtied while it had no parent, then parented by a container that does
// NOT invalidate itself (DataGrid wires its edit overlay with a bare SetParent).
// SetParent must carry the pending mark up the new chain or the pruned walk never
// reaches the element: its pixels go unreported and its flags are never cleared.
TEST(SubtreeDirtyPruning, ElementDirtiedWhileDetachedIsMarkedOnSetParent) {
    Tree t;
    PokeLeaf orphan;
    orphan.Poke();                                      // dirty, no parent: marks only itself
    EXPECT_FALSE(Any(t.root.SubtreeDirty()));
    orphan.SetParent(t.sections[0]);
    EXPECT_TRUE(Has(t.sections[0]->SubtreeDirty(), DirtyFlags::Render));
    EXPECT_TRUE(Has(t.root.SubtreeDirty(), DirtyFlags::Render));
    orphan.SetParent(nullptr);                          // it is not really a child; unhook
}

// The same path through a REAL frame. A container that attaches a child with a bare
// SetParent and no invalidation of its own (DataGrid does this for its edit overlay)
// relies entirely on SetParent carrying the child's existing dirt up the new chain.
// Panel::Add would mark the path itself, so it cannot test this; the container below
// pushes the child the way the overlay is attached. Without the propagation the
// clean ancestors prune the walk and the dirty child's pixels are never reported.
namespace {
// Owns one child outside any Panel child list and forwards the three walks to it,
// the way DataGrid owns and attaches its edit overlay.
class BareAttachHost : public FrameworkElement {
public:
    void Render(const DrawingContext&) override {}
    PokeLeaf* AttachBare(std::unique_ptr<PokeLeaf> c, const RectDip& r) {
        child_ = std::move(c);
        child_->SetBounds(r);
        child_->SetParent(this);
        return child_.get();
    }
protected:
    bool AnyDirtyInSubtreeOverride(DirtyFlags f) const override {
        return FrameworkElement::AnyDirtyInSubtreeOverride(f) ||
               (child_ && child_->AnyDirtyInSubtree(f));
    }
    void CollectDirtyBoundsOverride(std::vector<RectDip>& out) override {
        FrameworkElement::CollectDirtyBoundsOverride(out);
        if (child_) child_->CollectDirtyBounds(out);
    }
    void ClearDirtySubtreeOverride() override {
        FrameworkElement::ClearDirtySubtreeOverride();
        if (child_) child_->ClearDirtySubtree();
    }
private:
    std::unique_ptr<PokeLeaf> child_;
};
}  // namespace

TEST(SubtreeDirtyPruning, BareSetParentOfADirtyChildIsCollectedInARealFrame) {
    StackPanel root;
    auto* host = root.Emplace<BareAttachHost>();
    root.UpdateLayout({0, 0, 200, 400});
    Frame(root);                                        // everything clean now
    auto child = std::make_unique<PokeLeaf>();
    child->Poke();                                      // dirty before it has a parent
    PokeLeaf* raw = host->AttachBare(std::move(child), {0, 0, 40, 10});
    std::vector<RectDip> rects;
    root.CollectDirtyBounds(rects);
    EXPECT_TRUE(Covers(rects, raw->VisualBounds()));
}

// A whole subtree built and dirtied off-tree, then added: the dirty node in it is
// reported once laid out.
TEST(SubtreeDirtyPruning, SubtreeDirtiedOffTreeThenAddedIsCollected) {
    Tree t;
    auto sub = std::make_unique<StackPanel>();
    auto* inner = sub->Emplace<StackPanel>();
    auto* leaf = inner->Emplace<PokeLeaf>();
    sub->UpdateLayout({0, 0, 100, 100});
    sub->ClearDirtySubtree();
    leaf->Poke();                                       // dirty while outside the window tree
    t.sections[2]->Add(std::move(sub));
    EXPECT_TRUE(t.root.AnyDirtyInSubtree(DirtyFlags::Measure));
    t.root.UpdateLayout({0, 0, 200, 400});
    std::vector<RectDip> rects;
    t.root.CollectDirtyBounds(rects);
    EXPECT_TRUE(Covers(rects, leaf->VisualBounds()));
}

// Reparenting: an element removed from one panel, dirtied, and added to another is
// collected through its NEW parent and is clean afterwards.
TEST(SubtreeDirtyPruning, ReparentedDirtyChildIsCollectedUnderNewParent) {
    StackPanel root;
    auto* a = root.Emplace<StackPanel>();
    auto* b = root.Emplace<StackPanel>();
    a->Emplace<PokeLeaf>();
    a->Emplace<PokeLeaf>();
    root.UpdateLayout({0, 0, 200, 200});
    Frame(root);

    // Panel exposes no "take child back", so model the move as remove-from-a plus
    // add-to-b of an element that was dirtied in between.
    a->RemoveAt(0);
    auto moved = std::make_unique<PokeLeaf>();
    PokeLeaf* raw = moved.get();
    raw->Poke();
    b->Add(std::move(moved));
    root.UpdateLayout({0, 0, 200, 200});
    std::vector<RectDip> rects;
    root.CollectDirtyBounds(rects);
    root.ClearDirtySubtree();
    EXPECT_TRUE(Covers(rects, raw->VisualBounds()));
    EXPECT_FALSE(Any(raw->Dirty()));
    EXPECT_FALSE(Any(root.SubtreeDirty()));
}

// Single-child containers prune too: a dirty leaf under Border -> GroupBox is
// reported and cleared with every ancestor clean beforehand, and not revisited on
// the next frame.
TEST(SubtreeDirtyPruning, ContentContainersForwardThroughTheMark) {
    StackPanel root;
    auto* border = root.Emplace<Border>();
    auto* group = border->SetChild(std::make_unique<GroupBox>());
    auto* leaf = group->SetChild(std::make_unique<CountingLeaf>());
    root.UpdateLayout({0, 0, 300, 300});
    Frame(root);
    leaf->collectVisits = leaf->clearVisits = 0;

    leaf->Poke();
    const FrameResult f = Frame(root);
    EXPECT_TRUE(Covers(f.rects, leaf->VisualBounds()));
    EXPECT_EQ(leaf->clearVisits, 1);
    Frame(root);
    EXPECT_EQ(leaf->collectVisits, 1);                  // second frame did not reach it
}

// Viewbox still maps the rects of a dirty descendant into screen space when the
// path to it was found through the mark rather than an exhaustive walk.
TEST(SubtreeDirtyPruning, ViewboxScalingStillAppliesToPrunedCollection) {
    // Outer Border so the Viewbox itself sits under a clean ancestor. The Viewbox
    // is then measured/arranged directly, as ViewboxTests does, so the scale is
    // exactly 100 / 200 = 0.5 regardless of how the outer container would size it.
    Border outer;
    auto* vb = outer.SetChild(std::make_unique<Viewbox>());
    vb->SetStretch(Stretch::Fill);
    auto* leaf = vb->SetChild(std::make_unique<PokeLeaf>(200.0f, 200.0f));
    vb->Measure(100.0f, 100.0f);
    vb->Arrange({0, 0, 100.0f, 100.0f});
    Frame(outer);
    EXPECT_FALSE(Any(outer.SubtreeDirty()));            // premise: settled

    leaf->Poke();
    const FrameResult f = Frame(outer);
    EXPECT_EQ(f.rects.size(), static_cast<size_t>(1));
    if (!f.rects.empty()) {
        // Unscaled the leaf is 200 wide; mapped through 0.5 (+1 DIP of slack each
        // side) it is 102. Reporting the raw 200 would clear the wrong region.
        EXPECT_NEAR(f.rects[0].w, 102.0f, 0.01f);
        EXPECT_NEAR(f.rects[0].h, 102.0f, 0.01f);
    }
}

// ScrollPanel's rail-only repaint sets no dirty_ flag anywhere: it raises a member
// flag plus InvalidateDirty(None). Nested under a clean parent the strip must still
// be reported, which works only because a None invalidation marks Render on the path.
TEST(SubtreeDirtyPruning, ScrollPanelRailRepaintUnderCleanParentIsCollected) {
    StackPanel root;
    auto* panel = root.Emplace<ScrollPanel>();
    panel->SetHeight(300.0f);
    for (int i = 0; i < 20; ++i) panel->Emplace<Button>()->SetHeight(40.0f);
    root.UpdateLayout({0, 0, 400, 300});
    EXPECT_TRUE(panel->MaxVerticalOffset() > 0.0f);

    PointerEventArgs move;
    move.position = Point{200.0f, 150.0f};
    panel->OnPointerMoved(move);
    Frame(root);                                        // consume the wake
    panel->OnAnimationTick(1.0f / 60.0f);              // fade step: rail-only repaint
    EXPECT_FALSE(Any(panel->Dirty()));

    const FrameResult f = Frame(root);
    EXPECT_EQ(f.rects.size(), static_cast<size_t>(1));
    if (!f.rects.empty()) {
        EXPECT_TRUE(f.rects[0].w <= 16.0f);
        EXPECT_EQ(f.rects[0].right(), 400.0f);
    }
}

// An invalidation that lands while the frame is still being cleared (here: from
// inside the clear walk itself) survives into the next frame, both as the element's
// own flag and as the mark on its ancestors, so the next pruned walk reaches it.
TEST(SubtreeDirtyPruning, InvalidateDuringClearSurvivesToNextFrame) {
    StackPanel root;
    auto* section = root.Emplace<StackPanel>();
    auto* leaf = section->Emplace<ReinvalidatingLeaf>();
    root.UpdateLayout({0, 0, 200, 200});
    Frame(root);

    leaf->Poke();
    leaf->armed = true;
    Frame(root);                                        // the clear re-invalidates the leaf
    EXPECT_TRUE(Has(leaf->Dirty(), DirtyFlags::Render));
    EXPECT_TRUE(Has(root.SubtreeDirty(), DirtyFlags::Render));
    EXPECT_TRUE(Has(section->SubtreeDirty(), DirtyFlags::Render));

    const FrameResult next = Frame(root);
    EXPECT_TRUE(Covers(next.rects, leaf->VisualBounds()));
}
