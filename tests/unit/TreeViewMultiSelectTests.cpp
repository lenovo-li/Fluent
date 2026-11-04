// TreeViewMultiSelectTests.cpp — regression tests for TreeView multi-select:
// Ctrl+click toggle, Shift+click range, Ctrl+Space, Shift+arrow, mode switching,
// and the SelectionSetChanged event. Pure logic: no DWrite / GPU — multi-select
// only touches the selection model and event raising.

#include "../framework/Test.h"
#include "../../FluentUI/controls/TreeView.h"

using namespace fluent;

namespace {
// Populate a small flat tree (no DWrite needed): five visible rows.
void FillTree(TreeView& tv) {
    std::vector<TreeViewRow> rows;
    for (int i = 0; i < 5; ++i) {
        TreeViewRow r;
        r.id = i;
        r.text = std::to_wstring(i);
        rows.push_back(r);
    }
    tv.SetRows(std::move(rows));
    tv.SetBounds({0, 0, 200, 200});  // bounds must be set for click hit-test
    tv.SetSelectionMode(SelectionMode::Multiple);
}

// Simulate a pointer click at a specific row with modifiers.
void SimulateClick(TreeView& tv, int rowIndex, ModifierKeys mods = ModifierKeys::None) {
    PointerEventArgs e;
    e.button = PointerButton::Left;
    e.modifiers = mods;
    // TreeView maps physical Y to row index via (contentY / rowHeight). The default
    // rowHeight is 28 DIP. Position the click at the row's vertical center.
    const float rowHeight = 28.0f;
    e.position = Point{50.0f, rowIndex * rowHeight + rowHeight / 2.0f};
    tv.OnPointerReleased(e);
}

// Simulate a key press with modifiers.
void SimulateKey(TreeView& tv, int vk, ModifierKeys mods = ModifierKeys::None) {
    KeyEventArgs e;
    e.vk = vk;
    e.modifiers = mods;
    tv.OnKeyDownRouted(e);
}

// Event handlers must be plain function pointers (Event::Handler is
// void(*)(void*, Sender&, Args&)), so a capturing lambda will not convert.
// The captured state travels through the void* owner, matching ButtonBaseTests.
void CaptureSelectionSet(void* ctx, TreeView&, std::vector<int>& indices) {
    *static_cast<std::vector<int>*>(ctx) = indices;
}
}  // namespace

// --- Basic multi-select operations -----------------------------------------

TEST(TreeViewMultiSelect, CtrlClickTogglesSelection) {
    TreeView tv; FillTree(tv);
    tv.SetSelectedIndex(0);  // start at row 0
    EXPECT_EQ(tv.SelectedRowIndices().size(), 1u);
    EXPECT_TRUE(tv.IsRowSelected(0));

    SimulateClick(tv, 2, ModifierKeys::Ctrl);  // Ctrl+click row 2: adds it
    auto sel = tv.SelectedRowIndices();
    EXPECT_EQ(sel.size(), 2u);
    EXPECT_TRUE(tv.IsRowSelected(0));
    EXPECT_TRUE(tv.IsRowSelected(2));

    SimulateClick(tv, 2, ModifierKeys::Ctrl);  // Ctrl+click row 2 again: removes it
    sel = tv.SelectedRowIndices();
    EXPECT_EQ(sel.size(), 1u);
    EXPECT_TRUE(tv.IsRowSelected(0));
    EXPECT_FALSE(tv.IsRowSelected(2));
}

TEST(TreeViewMultiSelect, ShiftClickSelectsRange) {
    TreeView tv; FillTree(tv);
    tv.SetSelectedIndex(1);  // anchor at row 1
    EXPECT_EQ(tv.SelectedRowIndices().size(), 1u);

    SimulateClick(tv, 3, ModifierKeys::Shift);  // Shift+click row 3: selects 1-3
    auto sel = tv.SelectedRowIndices();
    EXPECT_EQ(sel.size(), 3u);
    EXPECT_TRUE(tv.IsRowSelected(1));
    EXPECT_TRUE(tv.IsRowSelected(2));
    EXPECT_TRUE(tv.IsRowSelected(3));
    EXPECT_FALSE(tv.IsRowSelected(0));
}

TEST(TreeViewMultiSelect, PlainClickCollapsesToSingleRow) {
    TreeView tv; FillTree(tv);
    SimulateClick(tv, 0);
    SimulateClick(tv, 1, ModifierKeys::Ctrl);  // select rows 0 and 1
    EXPECT_EQ(tv.SelectedRowIndices().size(), 2u);

    SimulateClick(tv, 3);  // plain click: collapse to row 3 only
    auto sel = tv.SelectedRowIndices();
    EXPECT_EQ(sel.size(), 1u);
    EXPECT_TRUE(tv.IsRowSelected(3));
    EXPECT_FALSE(tv.IsRowSelected(0));
    EXPECT_FALSE(tv.IsRowSelected(1));
}

TEST(TreeViewMultiSelect, CtrlSpaceTogglesActiveRow) {
    TreeView tv; FillTree(tv);
    tv.SetSelectedIndex(2);
    EXPECT_TRUE(tv.IsRowSelected(2));

    SimulateKey(tv, VK_SPACE, ModifierKeys::Ctrl);  // Ctrl+Space: toggle row 2 off
    EXPECT_FALSE(tv.IsRowSelected(2));
    EXPECT_EQ(tv.SelectedRowIndices().size(), 0u);

    SimulateKey(tv, VK_SPACE, ModifierKeys::Ctrl);  // Ctrl+Space again: toggle back on
    EXPECT_TRUE(tv.IsRowSelected(2));
    EXPECT_EQ(tv.SelectedRowIndices().size(), 1u);
}

TEST(TreeViewMultiSelect, ShiftArrowExtendsRange) {
    TreeView tv; FillTree(tv);
    tv.SetSelectedIndex(1);
    EXPECT_EQ(tv.SelectedRowIndices().size(), 1u);

    SimulateKey(tv, VK_DOWN, ModifierKeys::Shift);  // Shift+Down: select 1-2
    auto sel = tv.SelectedRowIndices();
    EXPECT_EQ(sel.size(), 2u);
    EXPECT_TRUE(tv.IsRowSelected(1));
    EXPECT_TRUE(tv.IsRowSelected(2));

    SimulateKey(tv, VK_DOWN, ModifierKeys::Shift);  // Shift+Down again: select 1-3
    sel = tv.SelectedRowIndices();
    EXPECT_EQ(sel.size(), 3u);
    EXPECT_TRUE(tv.IsRowSelected(1));
    EXPECT_TRUE(tv.IsRowSelected(2));
    EXPECT_TRUE(tv.IsRowSelected(3));
}

// --- Mode switching --------------------------------------------------------

TEST(TreeViewMultiSelect, SwitchToMultipleSeedsSetFromCurrent) {
    TreeView tv; FillTree(tv);
    tv.SetSelectionMode(SelectionMode::Single);
    tv.SetSelectedIndex(2);
    EXPECT_EQ(tv.GetSelectionMode(), SelectionMode::Single);

    tv.SetSelectionMode(SelectionMode::Multiple);  // switch: seed from row 2
    EXPECT_EQ(tv.SelectedRowIndices().size(), 1u);
    EXPECT_TRUE(tv.IsRowSelected(2));
}

TEST(TreeViewMultiSelect, SwitchToSingleDropsSet) {
    TreeView tv; FillTree(tv);
    SimulateClick(tv, 0);
    SimulateClick(tv, 2, ModifierKeys::Ctrl);  // select rows 0 and 2
    EXPECT_EQ(tv.SelectedRowIndices().size(), 2u);

    tv.SetSelectionMode(SelectionMode::Single);  // switch: drop the set, keep active
    EXPECT_EQ(tv.GetSelectionMode(), SelectionMode::Single);
    EXPECT_EQ(tv.SelectedIndex(), 2);  // active row was 2 (last touched)
    // SelectedRowIndices in Single mode returns {selectedIndex_}
    EXPECT_EQ(tv.SelectedRowIndices().size(), 1u);
    EXPECT_TRUE(tv.IsRowSelected(2));
}

// --- Event contract --------------------------------------------------------

TEST(TreeViewMultiSelect, SelectionSetChangedRaisesOnToggle) {
    TreeView tv; FillTree(tv);
    tv.SetSelectedIndex(0);

    std::vector<int> capturedIndices;
    auto sub = tv.SelectionSetChanged().Subscribe(&capturedIndices, CaptureSelectionSet);

    SimulateClick(tv, 1, ModifierKeys::Ctrl);  // Ctrl+click: event fires
    EXPECT_EQ(capturedIndices.size(), 2u);
    EXPECT_TRUE(std::find(capturedIndices.begin(), capturedIndices.end(), 0) != capturedIndices.end());
    EXPECT_TRUE(std::find(capturedIndices.begin(), capturedIndices.end(), 1) != capturedIndices.end());
}

TEST(TreeViewMultiSelect, SelectionSetChangedRaisesOnRange) {
    TreeView tv; FillTree(tv);
    tv.SetSelectedIndex(1);

    std::vector<int> capturedIndices;
    auto sub = tv.SelectionSetChanged().Subscribe(&capturedIndices, CaptureSelectionSet);

    SimulateClick(tv, 3, ModifierKeys::Shift);  // Shift+click: event fires
    EXPECT_EQ(capturedIndices.size(), 3u);  // rows 1, 2, 3
}

// --- Edge cases ------------------------------------------------------------

TEST(TreeViewMultiSelect, CtrlClickOutOfBoundsIsNoop) {
    TreeView tv; FillTree(tv);
    tv.SetSelectedIndex(0);
    tv.ToggleRowSelection(99);  // out of bounds: no crash, no change
    EXPECT_EQ(tv.SelectedRowIndices().size(), 1u);
    EXPECT_TRUE(tv.IsRowSelected(0));
}

// Shift+click with no anchor falls back to selecting just the clicked row. Reaching
// that state needs SetSelectedIndex(-1) to actually clear — see the -1 pass-through
// in TreeView::SetSelectedIndex, which the pre-existing clamp-to-0 prevented.
TEST(TreeViewMultiSelect, ShiftClickWithNoAnchorSelectsSingleRow) {
    TreeView tv; FillTree(tv);
    tv.SetSelectedIndex(-1);  // no selection → anchor is -1
    EXPECT_EQ(tv.SelectedIndex(), -1);
    EXPECT_EQ(tv.SelectedRowIndices().size(), 0u);

    SimulateClick(tv, 2, ModifierKeys::Shift);
    auto sel = tv.SelectedRowIndices();
    EXPECT_EQ(sel.size(), 1u);
    EXPECT_TRUE(tv.IsRowSelected(2));
}

// A data swap discards the old set (its indices referred to the old items_) and
// re-seeds from whatever single selection survived the restore-by-id. The point of
// re-seeding rather than leaving it empty: selectedRows_ must not disagree with
// selectedIndex_, or IsRowSelected() would report the active row as unselected.
TEST(TreeViewMultiSelect, SetRowsReseedsSetFromRestoredSelection) {
    TreeView tv; FillTree(tv);
    SimulateClick(tv, 0);
    SimulateClick(tv, 2, ModifierKeys::Ctrl);
    EXPECT_EQ(tv.SelectedRowIndices().size(), 2u);

    FillTree(tv);  // SetRows → OnItemsChanged: discard the set, re-seed from restore
    auto sel = tv.SelectedRowIndices();
    EXPECT_EQ(sel.size(), 1u);
    // The restore is by id: row 2 was the active row and id 2 still exists.
    EXPECT_TRUE(tv.IsRowSelected(2));
    EXPECT_EQ(tv.SelectedIndex(), 2);
}

// --- Single mode ignores modifiers -----------------------------------------

TEST(TreeViewMultiSelect, SingleModeIgnoresCtrl) {
    TreeView tv; FillTree(tv);
    tv.SetSelectionMode(SelectionMode::Single);
    tv.SetSelectedIndex(0);

    SimulateClick(tv, 2, ModifierKeys::Ctrl);  // Ctrl+click in Single mode: just moves
    EXPECT_EQ(tv.SelectedIndex(), 2);
    EXPECT_EQ(tv.SelectedRowIndices().size(), 1u);
    EXPECT_TRUE(tv.IsRowSelected(2));
    EXPECT_FALSE(tv.IsRowSelected(0));
}

TEST(TreeViewMultiSelect, SingleModeIgnoresShift) {
    TreeView tv; FillTree(tv);
    tv.SetSelectionMode(SelectionMode::Single);
    tv.SetSelectedIndex(1);

    SimulateClick(tv, 3, ModifierKeys::Shift);  // Shift+click in Single mode: just moves
    EXPECT_EQ(tv.SelectedIndex(), 3);
    EXPECT_EQ(tv.SelectedRowIndices().size(), 1u);
    EXPECT_TRUE(tv.IsRowSelected(3));
    EXPECT_FALSE(tv.IsRowSelected(1));
}

// --- Ranges skip collapsed subtrees ------------------------------------------
//
// Rows are stored pre-order, so a collapsed node's descendants sit numerically
// INSIDE a Shift range between two visible rows. The range used to take every row
// index in between: two rows highlighted on screen, but SelectedRowIndices() also
// reported the hidden children, and an app deleting "the selection" deleted nodes
// the user never saw.
namespace {
// A (collapsed, 3 children) then B: visible rows are A (row 0) and B (row 4).
void FillCollapsedTree(TreeView& tv) {
    std::vector<TreeViewRow> rows;
    TreeViewRow a; a.id = 0; a.text = L"A"; a.hasChildren = true; a.expanded = false;
    rows.push_back(a);
    for (int i = 1; i <= 3; ++i) {
        TreeViewRow c; c.id = i; c.parentId = 0; c.depth = 1;
        c.text = L"child" + std::to_wstring(i);
        rows.push_back(c);
    }
    TreeViewRow b; b.id = 4; b.text = L"B";
    rows.push_back(b);
    tv.SetRows(std::move(rows));
    tv.SetBounds({0, 0, 200, 200});
    tv.SetSelectionMode(SelectionMode::Multiple);
}
}  // namespace

TEST(TreeViewMultiSelect, ShiftClickRangeSkipsRowsInsideACollapsedNode) {
    TreeView tv; FillCollapsedTree(tv);
    SimulateClick(tv, 0);                        // A (visible 0)
    SimulateClick(tv, 1, ModifierKeys::Shift);   // B (visible 1, row 4)
    auto sel = tv.SelectedRowIndices();
    EXPECT_EQ(sel.size(), 2u);
    EXPECT_TRUE(tv.IsRowSelected(0));
    EXPECT_TRUE(tv.IsRowSelected(4));
    for (int hidden = 1; hidden <= 3; ++hidden)
        EXPECT_FALSE(tv.IsRowSelected(hidden));
}

TEST(TreeViewMultiSelect, ShiftArrowRangeSkipsRowsInsideACollapsedNode) {
    TreeView tv; FillCollapsedTree(tv);
    SimulateClick(tv, 0);                        // A
    SimulateKey(tv, VK_DOWN, ModifierKeys::Shift);
    auto sel = tv.SelectedRowIndices();
    EXPECT_EQ(sel.size(), 2u);
    EXPECT_FALSE(tv.IsRowSelected(2));
    EXPECT_TRUE(tv.IsRowSelected(4));
}

// The range fill coalesces visible rows into runs. A Shift range over a fully
// expanded flat stretch must be ONE range (that is the whole point of range-based
// storage: a long range costs nothing to hold), and a collapsed node inside the
// range must break it into exactly the runs on either side.
TEST(TreeViewMultiSelect, ShiftRangeCoalescesVisibleRowsIntoRuns) {
    TreeView flat; FillTree(flat);                // five visible rows, all consecutive
    SimulateClick(flat, 0);
    SimulateClick(flat, 4, ModifierKeys::Shift);
    EXPECT_EQ(flat.SelectedRanges().size(), 1u);
    EXPECT_EQ(flat.SelectedCount(), 5u);

    // A (row 0, collapsed over rows 1..3), B (row 4), C (row 5), D (row 6): the
    // visible rows in [0, 6] are 0 and 4..6 -> two runs, four rows.
    TreeView tv; FillCollapsedTree(tv);
    TreeViewRow c; c.id = 5; c.text = L"C";
    TreeViewRow d; d.id = 6; d.text = L"D";
    tv.AddRoot(c);
    tv.AddRoot(d);
    tv.SetSelectedIndex(0);                       // A, re-anchors the set
    SimulateClick(tv, 3, ModifierKeys::Shift);    // visible 3 = D (row 6)
    const auto ranges = tv.SelectedRanges();
    EXPECT_EQ(ranges.size(), 2u);
    if (ranges.size() == 2u) {
        EXPECT_TRUE(ranges[0] == (IndexRange{0, 0}));
        EXPECT_TRUE(ranges[1] == (IndexRange{4, 6}));
    }
    EXPECT_EQ(tv.SelectedCount(), 4u);
    EXPECT_FALSE(tv.IsRowSelected(2));
}
