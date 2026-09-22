// TreeViewIncrementalApiTests.cpp — AddChild/RemoveNode/SetNodeExpanded etc.
#include "../framework/Test.h"
#include "../../FluentUI/controls/TreeView.h"

using namespace fluent;

TEST(TreeViewIncrementalApi, AddRootAutoFillsParentIdAndDepth) {
    TreeView tree;
    tree.AddRoot({.id = 1, .text = L"Root"});
    
    EXPECT_EQ(tree.ItemCount(), 1);
    auto* row = tree.FindNode(1);
    if (!row) return;  // can't continue without the node
    EXPECT_EQ(row->parentId, -1);
    EXPECT_EQ(row->depth, 0);
    EXPECT_EQ(row->text, L"Root");
}

TEST(TreeViewIncrementalApi, AddChildAutoFillsParentIdAndDepth) {
    TreeView tree;
    tree.AddRoot({.id = 1, .text = L"Root"});
    tree.AddChild(1, {.id = 2, .text = L"Child"});
    
    EXPECT_EQ(tree.ItemCount(), 2);
    auto* child = tree.FindNode(2);
    if (!child) return;
    EXPECT_EQ(child->parentId, 1);
    EXPECT_EQ(child->depth, 1);
    EXPECT_EQ(child->text, L"Child");
    
    // Parent should be marked hasChildren
    auto* parent = tree.FindNode(1);
    EXPECT_TRUE(parent->hasChildren);
}

TEST(TreeViewIncrementalApi, AddChildUnderUnknownParentIsNoOp) {
    TreeView tree;
    tree.AddRoot({.id = 1, .text = L"Root"});
    tree.AddChild(999, {.id = 2, .text = L"Orphan"});
    
    EXPECT_EQ(tree.ItemCount(), 1);  // no add happened
    EXPECT_EQ(tree.FindNode(2), nullptr);
}

TEST(TreeViewIncrementalApi, AddChildInsertsAfterParentSubtree) {
    TreeView tree;
    tree.AddRoot({.id = 1, .text = L"Root"});
    tree.AddChild(1, {.id = 2, .text = L"Child1"});
    tree.AddChild(2, {.id = 3, .text = L"Grandchild"});  // grandchild of 1
    tree.AddChild(1, {.id = 4, .text = L"Child2"});       // another child of 1
    
    // Order should be: Root(1), Child1(2), Grandchild(3), Child2(4)
    EXPECT_EQ(tree.ItemCount(), 4);
    if (tree.ItemCount() < 4) return;
    EXPECT_EQ(tree.Items()[0].id, 1);
    EXPECT_EQ(tree.Items()[1].id, 2);
    EXPECT_EQ(tree.Items()[2].id, 3);
    EXPECT_EQ(tree.Items()[3].id, 4);
}

TEST(TreeViewIncrementalApi, RemoveNodeDeletesDescendants) {
    TreeView tree;
    tree.AddRoot({.id = 1, .text = L"Root"});
    tree.AddChild(1, {.id = 2, .text = L"Child"});
    tree.AddChild(2, {.id = 3, .text = L"Grandchild"});
    EXPECT_EQ(tree.ItemCount(), 3);
    
    tree.RemoveNode(2);  // removes Child + Grandchild
    
    EXPECT_EQ(tree.ItemCount(), 1);
    EXPECT_NE(tree.FindNode(1), nullptr);
    EXPECT_EQ(tree.FindNode(2), nullptr);
    EXPECT_EQ(tree.FindNode(3), nullptr);
}

TEST(TreeViewIncrementalApi, RemoveNodeUnknownIdIsNoOp) {
    TreeView tree;
    tree.AddRoot({.id = 1, .text = L"Root"});
    tree.RemoveNode(999);
    EXPECT_EQ(tree.ItemCount(), 1);
}

TEST(TreeViewIncrementalApi, RemoveNodeRefreshesParentHasChildren) {
    TreeView tree;
    tree.AddRoot({.id = 1, .text = L"Root"});
    tree.AddChild(1, {.id = 2, .text = L"OnlyChild"});
    EXPECT_TRUE(tree.FindNode(1)->hasChildren);
    
    tree.RemoveNode(2);
    
    EXPECT_FALSE(tree.FindNode(1)->hasChildren);  // no longer has children
    EXPECT_TRUE(tree.FindNode(1)->expanded);      // leaf forced to expanded=true
}

TEST(TreeViewIncrementalApi, SetNodeExpandedToggleExpandState) {
    TreeView tree;
    tree.AddRoot({.id = 1, .text = L"Root"});
    tree.AddChild(1, {.id = 2, .text = L"Child"});
    tree.FindNode(1)->expanded = true;  // default, make explicit
    
    tree.SetNodeExpanded(1, false);
    EXPECT_FALSE(tree.FindNode(1)->expanded);
    
    tree.SetNodeExpanded(1, true);
    EXPECT_TRUE(tree.FindNode(1)->expanded);
}

TEST(TreeViewIncrementalApi, SetNodeExpandedOnLeafIsNoOp) {
    TreeView tree;
    tree.AddRoot({.id = 1, .text = L"Leaf"});
    tree.FindNode(1)->expanded = true;
    tree.SetNodeExpanded(1, false);
    EXPECT_TRUE(tree.FindNode(1)->expanded);  // leaf can't collapse
}

TEST(TreeViewIncrementalApi, ExpandAllSetsAllParentsExpanded) {
    TreeView tree;
    tree.AddRoot({.id = 1, .text = L"Root"});
    tree.AddChild(1, {.id = 2, .text = L"Child"});
    tree.FindNode(1)->expanded = false;
    
    tree.ExpandAll();
    
    EXPECT_TRUE(tree.FindNode(1)->expanded);
}

TEST(TreeViewIncrementalApi, CollapseAllSetsAllParentsCollapsed) {
    TreeView tree;
    tree.AddRoot({.id = 1, .text = L"Root"});
    tree.AddChild(1, {.id = 2, .text = L"Child"});
    tree.FindNode(1)->expanded = true;
    
    tree.CollapseAll();
    
    EXPECT_FALSE(tree.FindNode(1)->expanded);
}

TEST(TreeViewIncrementalApi, FindNodeReturnsNullptrForUnknownId) {
    TreeView tree;
    tree.AddRoot({.id = 1, .text = L"Root"});
    EXPECT_EQ(tree.FindNode(999), nullptr);
}

TEST(TreeViewIncrementalApi, FindNodeReturnsConstPointer) {
    TreeView tree;
    tree.AddRoot({.id = 1, .text = L"Root"});
    const TreeView& ctree = tree;
    const TreeViewRow* row = ctree.FindNode(1);
    if (!row) return;
    EXPECT_EQ(row->text, L"Root");
}

TEST(TreeViewIncrementalApi, AddChildPreservesSelection) {
    TreeView tree;
    tree.AddRoot({.id = 1, .text = L"Root"});
    tree.SetSelectedIndex(0);  // select Root (visible index 0)
    EXPECT_EQ(tree.SelectedRow()->id, 1);
    
    tree.AddChild(1, {.id = 2, .text = L"Child"});
    
    // Selection should still be on Root (by id)
    EXPECT_EQ(tree.SelectedRow()->id, 1);
}

TEST(TreeViewIncrementalApi, RemoveNodeDropsSelectionIfSelectedWasRemoved) {
    TreeView tree;
    tree.AddRoot({.id = 1, .text = L"Root"});
    tree.AddChild(1, {.id = 2, .text = L"Child"});
    tree.SetSelectedIndex(1);  // select Child (visible index 1)
    EXPECT_EQ(tree.SelectedRow()->id, 2);
    
    tree.RemoveNode(2);
    
    // Selection should fall back (OnItemsChanged's fallback = first visible)
    EXPECT_EQ(tree.SelectedIndex(), 0);  // Root
}
