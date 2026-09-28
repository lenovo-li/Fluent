// TreeViewHoverTests.cpp — regression tests for TreeView row hover tracking.
//
// TreeView had click feedback (selection fill + accent bar) but no hover
// feedback at all: OnPointerMoved only fed the scrollbar overlay, so moving the
// pointer over rows produced no visual response. hoverRowIndex_ now tracks the
// row under the pointer and DrawRow paints a subtle fill for it (matching
// ListBox). Pure logic: no DWrite / GPU needed — hover is hit-testing plus an
// index, and Render is not exercised here.

#include "../framework/Test.h"
#include "../../FluentUI/controls/TreeView.h"

using namespace fluent;

namespace {

constexpr float kRowHeight = 28.0f;  // TreeView's default rowHeight_

// Five flat rows, bounds set so hit-testing works.
void FillTree(TreeView& tv, int count = 5) {
    std::vector<TreeViewRow> rows;
    for (int i = 0; i < count; ++i) {
        TreeViewRow r;
        r.id = i;
        r.text = std::to_wstring(i);
        rows.push_back(r);
    }
    tv.SetRows(std::move(rows));
    tv.SetBounds({0, 0, 200, 200});
}

// Move the pointer to the vertical center of `rowIndex`. x is kept well left of
// the scrollbar rail so the move is a row hover, not a bar hover.
void MoveToRow(TreeView& tv, int rowIndex) {
    PointerEventArgs e;
    e.position = Point{50.0f, rowIndex * kRowHeight + kRowHeight * 0.5f};
    tv.OnPointerMoved(e);
}

void MoveTo(TreeView& tv, float x, float y) {
    PointerEventArgs e;
    e.position = Point{x, y};
    tv.OnPointerMoved(e);
}

}  // namespace

TEST(TreeViewHover, NoHoverBeforeAnyPointerMove) {
    TreeView tv; FillTree(tv);
    EXPECT_EQ(tv.HoveredRowIndex(), -1);
}

TEST(TreeViewHover, PointerMoveSetsHoveredRow) {
    TreeView tv; FillTree(tv);
    MoveToRow(tv, 2);
    EXPECT_EQ(tv.HoveredRowIndex(), 2);
}

TEST(TreeViewHover, HoverFollowsPointerAcrossRows) {
    TreeView tv; FillTree(tv);
    MoveToRow(tv, 0);
    EXPECT_EQ(tv.HoveredRowIndex(), 0);
    MoveToRow(tv, 3);
    EXPECT_EQ(tv.HoveredRowIndex(), 3);
    MoveToRow(tv, 1);
    EXPECT_EQ(tv.HoveredRowIndex(), 1);
}

TEST(TreeViewHover, HoverIsIndependentOfSelection) {
    TreeView tv; FillTree(tv);
    tv.SetSelectedIndex(0);
    MoveToRow(tv, 4);
    // Hovering must not move the selection — only a click does that.
    EXPECT_EQ(tv.SelectedIndex(), 0);
    EXPECT_EQ(tv.HoveredRowIndex(), 4);
}

TEST(TreeViewHover, PointerBelowLastRowClearsHover) {
    TreeView tv; FillTree(tv, 3);  // rows occupy y in [0, 84)
    MoveToRow(tv, 1);
    EXPECT_EQ(tv.HoveredRowIndex(), 1);
    // Empty area under the last row: HitRow returns -1, so no row is hovered.
    MoveTo(tv, 50.0f, 150.0f);
    EXPECT_EQ(tv.HoveredRowIndex(), -1);
}

TEST(TreeViewHover, PointerLeaveClearsHover) {
    TreeView tv; FillTree(tv);
    MoveToRow(tv, 2);
    EXPECT_EQ(tv.HoveredRowIndex(), 2);
    // OnPointerLeave is the public entry the InputManager drives; it dispatches to
    // the protected OnPointerLeft hook where the hover is dropped. Going through
    // the public path is what the real pointer does.
    tv.OnPointerEnter();
    tv.OnPointerLeave();
    EXPECT_EQ(tv.HoveredRowIndex(), -1);
}

TEST(TreeViewHover, SetRowsClearsHover) {
    TreeView tv; FillTree(tv);
    MoveToRow(tv, 3);
    EXPECT_EQ(tv.HoveredRowIndex(), 3);
    // A new row set means the stored index names a different row (or none), so it
    // must be dropped rather than painted on whatever slid into that slot.
    FillTree(tv, 2);
    EXPECT_EQ(tv.HoveredRowIndex(), -1);
}

TEST(TreeViewHover, RemoveNodeClearsHover) {
    TreeView tv; FillTree(tv);
    MoveToRow(tv, 4);
    EXPECT_EQ(tv.HoveredRowIndex(), 4);
    tv.RemoveNode(0);
    EXPECT_EQ(tv.HoveredRowIndex(), -1);
}

TEST(TreeViewHover, HoverSurvivesRepeatedMoveOnSameRow) {
    TreeView tv; FillTree(tv);
    MoveToRow(tv, 2);
    MoveToRow(tv, 2);
    MoveToRow(tv, 2);
    EXPECT_EQ(tv.HoveredRowIndex(), 2);
}

TEST(TreeViewHover, HoverTracksCollapsedSubtree) {
    // Hover is a ROW index, and collapsing changes which rows are visible. After a
    // collapse the same screen position must resolve to the row now drawn there.
    TreeView tv;
    std::vector<TreeViewRow> rows;
    rows.push_back(TreeViewRow{1, -1, L"parent", 0, false, true, true, TreeViewIcon::Folder, nullptr});
    rows.push_back(TreeViewRow{2, 1, L"child", 1, false, false, false, TreeViewIcon::None, nullptr});
    rows.push_back(TreeViewRow{3, -1, L"sibling", 0, false, false, false, TreeViewIcon::None, nullptr});
    tv.SetRows(std::move(rows));
    tv.SetBounds({0, 0, 200, 200});

    // Visible order expanded: parent(row 0), child(row 1), sibling(row 2).
    MoveToRow(tv, 1);
    EXPECT_EQ(tv.HoveredRowIndex(), 1);  // the child

    // Collapse the parent: visible order becomes parent(row 0), sibling(row 2).
    tv.SetNodeExpanded(1, false);
    MoveToRow(tv, 1);
    EXPECT_EQ(tv.HoveredRowIndex(), 2);  // visible slot 1 is now the sibling row
}
