// ListBoxMultiSelectTests.cpp — regression tests for the multi-select ListBox
// inherited from Selector: Ctrl+click toggle, Shift+click range, Ctrl+Space,
// Shift+arrow, mode switching, and the SelectionSetChanged event.
//
// The point of these tests is not that ListBox reimplements the gestures — it
// does not, the logic is Selector's. It is that ListBox *wires* them: the click
// handler reads the modifiers, the key handler reaches the range path before the
// plain switch, and the set hook raises the typed event. Every one of those is a
// per-control wiring decision that the shared implementation cannot make.
//
// Pure logic: no DWrite / GPU. Multi-select only touches the selection model.

#include "../framework/Test.h"
#include "../../FluentUI/controls/ListBox.h"
#include "../../FluentUI/diagnostics/PerformanceCounters.h"

using namespace fluent;

namespace {
// Five items, bounds set (the click hit-test needs them), Multiple mode on.
void FillList(ListBox& lb) {
    std::vector<std::wstring> items;
    for (int i = 0; i < 5; ++i) items.push_back(std::to_wstring(i));
    lb.SetItems(std::move(items));
    lb.SetBounds({0, 0, 200, 200});
    lb.SetSelectionMode(SelectionMode::Multiple);
}

// A left click on `index` with modifiers. Selection happens on RELEASE (that is
// where ListBox::OnPointerReleased puts it), so press is not simulated.
void SimulateClick(ListBox& lb, int index, ModifierKeys mods = ModifierKeys::None) {
    PointerEventArgs e;
    e.button = PointerButton::Left;
    e.modifiers = mods;
    // ListBox maps Y to index via (contentY / itemHeight); default is 32 DIP.
    const float itemHeight = 32.0f;
    e.position = Point{50.0f, index * itemHeight + itemHeight / 2.0f};
    lb.OnPointerReleased(e);
}

void SimulateKey(ListBox& lb, int vk, ModifierKeys mods = ModifierKeys::None) {
    KeyEventArgs e;
    e.vk = vk;
    e.modifiers = mods;
    lb.OnKeyDownRouted(e);
}
// Event handlers must be plain function pointers (Event::Handler is
// void(*)(void*, Sender&, Args&)); capturing lambdas are deliberately rejected.
// The captured state travels through the void* owner, same pattern as
// TreeViewMultiSelectTests.
struct SelectionSetCapture { int count = 0; std::vector<int> last; };
void CaptureSelectionSet(void* ctx, ListBox&, std::vector<int>& sel) {
    auto* c = static_cast<SelectionSetCapture*>(ctx);
    ++c->count;
    c->last = sel;
}
} // namespace

TEST(ListBoxMultiSelect, CtrlClickTogglesSelection) {
    ListBox lb;
    FillList(lb);

    SimulateClick(lb, 1);                          // plain: {1}
    SimulateClick(lb, 3, ModifierKeys::Ctrl);      // add 3: {1,3}
    EXPECT_EQ(lb.SelectedIndices().size(), 2u);
    EXPECT_TRUE(lb.IsSelected(1));
    EXPECT_TRUE(lb.IsSelected(3));
    EXPECT_EQ(lb.SelectedIndex(), 3);              // active follows the gesture

    SimulateClick(lb, 1, ModifierKeys::Ctrl);      // remove 1: {3}
    EXPECT_EQ(lb.SelectedIndices().size(), 1u);
    EXPECT_TRUE(!lb.IsSelected(1));
    EXPECT_TRUE(lb.IsSelected(3));
}

TEST(ListBoxMultiSelect, ShiftClickSelectsRange) {
    ListBox lb;
    FillList(lb);

    SimulateClick(lb, 1);                          // anchor at 1
    SimulateClick(lb, 4, ModifierKeys::Shift);     // {1,2,3,4}
    std::vector<int> sel = lb.SelectedIndices();
    EXPECT_EQ(sel.size(), 4u);
    EXPECT_EQ(sel[0], 1);
    EXPECT_EQ(sel[3], 4);
    EXPECT_EQ(lb.SelectedIndex(), 4);              // range end is active

    // A second Shift+click extends from the SAME anchor, not from the last target.
    SimulateClick(lb, 2, ModifierKeys::Shift);     // {1,2}
    sel = lb.SelectedIndices();
    EXPECT_EQ(sel.size(), 2u);
    EXPECT_EQ(sel[0], 1);
    EXPECT_EQ(sel[1], 2);
}

TEST(ListBoxMultiSelect, PlainClickCollapsesToSingleItem) {
    ListBox lb;
    FillList(lb);

    SimulateClick(lb, 0);
    SimulateClick(lb, 1, ModifierKeys::Ctrl);
    SimulateClick(lb, 2, ModifierKeys::Ctrl);
    EXPECT_EQ(lb.SelectedIndices().size(), 3u);

    SimulateClick(lb, 4);                          // plain click: set collapses
    EXPECT_EQ(lb.SelectedIndices().size(), 1u);
    EXPECT_TRUE(lb.IsSelected(4));
}

TEST(ListBoxMultiSelect, CtrlSpaceTogglesActiveItem) {
    ListBox lb;
    FillList(lb);

    SimulateClick(lb, 2);                                    // {2}, active 2
    SimulateKey(lb, VK_SPACE, ModifierKeys::Ctrl);           // toggle off: {}
    EXPECT_EQ(lb.SelectedIndices().size(), 0u);
    EXPECT_EQ(lb.SelectedIndex(), 2);                        // active does NOT move

    SimulateKey(lb, VK_SPACE, ModifierKeys::Ctrl);           // toggle back on: {2}
    EXPECT_EQ(lb.SelectedIndices().size(), 1u);
    EXPECT_TRUE(lb.IsSelected(2));
}

TEST(ListBoxMultiSelect, ShiftArrowExtendsRange) {
    ListBox lb;
    FillList(lb);

    SimulateClick(lb, 1);                                    // anchor at 1
    SimulateKey(lb, VK_DOWN, ModifierKeys::Shift);           // {1,2}
    EXPECT_EQ(lb.SelectedIndices().size(), 2u);
    SimulateKey(lb, VK_DOWN, ModifierKeys::Shift);           // {1,2,3}
    std::vector<int> sel = lb.SelectedIndices();
    EXPECT_EQ(sel.size(), 3u);
    EXPECT_EQ(sel[0], 1);
    EXPECT_EQ(sel[2], 3);
    EXPECT_EQ(lb.SelectedIndex(), 3);

    // Shift+Home collapses back toward the anchor rather than selecting [0, 3].
    SimulateKey(lb, VK_HOME, ModifierKeys::Shift);           // {0,1}
    sel = lb.SelectedIndices();
    EXPECT_EQ(sel.size(), 2u);
    EXPECT_EQ(sel[0], 0);
    EXPECT_EQ(sel[1], 1);
}

TEST(ListBoxMultiSelect, ShiftEndSelectsToLast) {
    ListBox lb;
    FillList(lb);

    SimulateClick(lb, 2);
    SimulateKey(lb, VK_END, ModifierKeys::Shift);            // {2,3,4}
    std::vector<int> sel = lb.SelectedIndices();
    EXPECT_EQ(sel.size(), 3u);
    EXPECT_EQ(sel[0], 2);
    EXPECT_EQ(sel[2], 4);
}

TEST(ListBoxMultiSelect, SingleModeIgnoresModifiers) {
    // The default mode must behave exactly as it did before multi-select existed:
    // a Ctrl+click is a plain click, and the set accessor reports the one item.
    ListBox lb;
    FillList(lb);
    lb.SetSelectionMode(SelectionMode::Single);

    SimulateClick(lb, 1);
    SimulateClick(lb, 3, ModifierKeys::Ctrl);
    EXPECT_EQ(lb.SelectedIndex(), 3);
    EXPECT_EQ(lb.SelectedIndices().size(), 1u);   // not 2
    EXPECT_TRUE(!lb.IsSelected(1));

    SimulateClick(lb, 0, ModifierKeys::Shift);
    EXPECT_EQ(lb.SelectedIndex(), 0);
    EXPECT_EQ(lb.SelectedIndices().size(), 1u);   // not a range
}

TEST(ListBoxMultiSelect, ModeSwitchReconcilesBothWays) {
    ListBox lb;
    FillList(lb);

    // → Single: the set drops, the active item survives.
    SimulateClick(lb, 1);
    SimulateClick(lb, 3, ModifierKeys::Ctrl);
    lb.SetSelectionMode(SelectionMode::Single);
    EXPECT_EQ(lb.SelectedIndex(), 3);             // last touched stays selected
    EXPECT_EQ(lb.SelectedIndices().size(), 1u);
    EXPECT_TRUE(!lb.IsSelected(1));

    // → Multiple: the set is seeded from the current single selection, so the
    // already-selected item stays selected instead of the set starting empty.
    lb.SetSelectionMode(SelectionMode::Multiple);
    EXPECT_EQ(lb.SelectedIndices().size(), 1u);
    EXPECT_TRUE(lb.IsSelected(3));
    // And the anchor landed there too — a Shift+click extends from 3.
    SimulateClick(lb, 1, ModifierKeys::Shift);
    EXPECT_EQ(lb.SelectedIndices().size(), 3u);   // {1,2,3}
}

TEST(ListBoxMultiSelect, SelectionSetChangedRaisesOncePerGesture) {
    ListBox lb;
    FillList(lb);

    SelectionSetCapture cap;
    auto sub = lb.SelectionSetChanged().Subscribe(&cap, CaptureSelectionSet);

    SimulateClick(lb, 1, ModifierKeys::Ctrl);
    EXPECT_EQ(cap.count, 1);
    EXPECT_EQ(cap.last.size(), 1u);

    SimulateClick(lb, 3, ModifierKeys::Shift);
    EXPECT_EQ(cap.count, 2);
    EXPECT_EQ(cap.last.size(), 3u);            // {1,2,3}

    // A plain click routes through SetSelectedIndex → SelectionChanged,
    // NOT the set event. One event per gesture.
    SimulateClick(lb, 0);
    EXPECT_EQ(cap.count, 2);                   // unchanged
}

TEST(ListBoxMultiSelect, OutOfBoundsGesturesAreNoops) {
    ListBox lb;
    FillList(lb);
    SimulateClick(lb, 2);

    lb.ToggleSelection(-1);
    lb.ToggleSelection(99);
    lb.RangeSelectTo(-1);
    lb.RangeSelectTo(99);
    EXPECT_EQ(lb.SelectedIndices().size(), 1u);   // still just {2}
    EXPECT_TRUE(lb.IsSelected(2));
}

TEST(ListBoxMultiSelect, VirtualizedModeSupportsMultiSelect) {
    // The virtualized path reports its count through the ItemCount() override, and
    // the Selector bounds-checks against that — so the set must work on a list
    // whose items were never materialized into items_.
    ListBox lb;
    lb.SetItemCount(1000);
    lb.ItemTextProvider = [](size_t i) { return std::to_wstring(i); };
    lb.SetBounds({0, 0, 200, 200});
    lb.SetSelectionMode(SelectionMode::Multiple);

    lb.SetSelectedIndex(10);
    lb.ToggleSelection(500);
    lb.ToggleSelection(999);
    EXPECT_EQ(lb.SelectedIndices().size(), 3u);
    EXPECT_TRUE(lb.IsSelected(999));

    lb.ToggleSelection(1000);                     // out of range: no-op
    EXPECT_EQ(lb.SelectedIndices().size(), 3u);
}


// --- Range-based selection storage ------------------------------------------
// Shift+End over a virtualized million-item list used to insert a million
// indices into a hash set on the key-press path (~80 ms per press in Release).
// The set is now one range per run, so the same gesture is O(1).

TEST(ListBoxMultiSelect, ShiftEndOnAMillionItemsIsOneRangeAndFast) {
    constexpr int kItems = 1000000;
    ListBox lb;
    lb.SetItemCount(static_cast<size_t>(kItems));
    lb.ItemTextProvider = [](size_t i) { return std::to_wstring(i); };
    lb.SetBounds({0, 0, 200, 200});
    lb.SetSelectionMode(SelectionMode::Multiple);
    lb.SetSelectedIndex(0);

    // The structural assertions are the real pin: one range, every item counted,
    // both ends selected, nothing past the end.
    SimulateKey(lb, VK_END, ModifierKeys::Shift);
    EXPECT_EQ(lb.SelectedRanges().size(), 1u);
    EXPECT_EQ(lb.SelectedCount(), static_cast<size_t>(kItems));
    EXPECT_TRUE(lb.IsSelected(0));
    EXPECT_TRUE(lb.IsSelected(kItems - 1));
    EXPECT_FALSE(lb.IsSelected(kItems));
    EXPECT_EQ(lb.SelectedIndex(), kItems - 1);

    // Timing as a coarse backstop with a very wide margin, so it cannot flake on
    // a loaded machine or a Debug build: 200 full-range presses must finish in
    // under 200 ms total. The per-index storage took ~80 ms per press in Release
    // (several times that in Debug), so it fails this by orders of magnitude; the
    // range storage takes microseconds per press.
    double ms = 0.0;
    {
        ScopedTimer t(ms);
        for (int i = 0; i < 200; ++i) {
            SimulateKey(lb, VK_HOME, ModifierKeys::Shift);
            SimulateKey(lb, VK_END, ModifierKeys::Shift);
        }
    }
    EXPECT_TRUE(ms < 200.0);
    EXPECT_EQ(lb.SelectedCount(), static_cast<size_t>(kItems));
}

TEST(ListBoxMultiSelect, CtrlClickInsideARangeSplitsIt) {
    ListBox lb;
    FillList(lb);
    SimulateClick(lb, 0);
    SimulateClick(lb, 4, ModifierKeys::Shift);   // {0..4}
    EXPECT_EQ(lb.SelectedRanges().size(), 1u);
    SimulateClick(lb, 2, ModifierKeys::Ctrl);    // {0,1} {3,4}
    const auto r = lb.SelectedRanges();
    EXPECT_EQ(r.size(), 2u);
    if (r.size() == 2u) {
        EXPECT_TRUE(r[0] == (IndexRange{0, 1}));
        EXPECT_TRUE(r[1] == (IndexRange{3, 4}));
    }
    EXPECT_EQ(lb.SelectedCount(), 4u);
    const std::vector<int> expected{0, 1, 3, 4};
    EXPECT_TRUE(lb.SelectedIndices() == expected);  // the vector accessor agrees
}

TEST(ListBoxMultiSelect, ShrinkingItemCountClampsTheSet) {
    // Virtualized items 0..n-1 keep their identity across SetItemCount, so the
    // surviving part of the selection stays and only the tail past the new end
    // goes. Before, only the active index was clamped: IsSelected() stayed true
    // for rows that no longer existed, and growing the list again brought them
    // back selected.
    ListBox lb;
    lb.SetItemCount(1000);
    lb.SetBounds({0, 0, 200, 200});
    lb.SetSelectionMode(SelectionMode::Multiple);
    lb.SetSelectedIndex(100);
    SimulateKey(lb, VK_END, ModifierKeys::Shift);  // {100..999}, anchor 100

    lb.SetItemCount(500);
    EXPECT_EQ(lb.SelectedCount(), 400u);           // 100..499
    EXPECT_FALSE(lb.IsSelected(500));
    EXPECT_EQ(lb.SelectedIndex(), 499);

    lb.SetItemCount(1000);                          // grow back: nothing resurrected
    EXPECT_FALSE(lb.IsSelected(700));
    EXPECT_EQ(lb.SelectedCount(), 400u);

    lb.SetItemCount(50);                            // below the anchor too
    EXPECT_EQ(lb.SelectedCount(), 0u);
    SimulateKey(lb, VK_HOME, ModifierKeys::Shift);  // anchor was clamped to 49
    EXPECT_EQ(lb.SelectedCount(), 50u);             // [0, 49], not a range from 100
}

// A shrink that removes selected items must tell subscribers: their last payload
// lists items that are gone. A shrink that removes nothing selected stays silent.
TEST(ListBoxMultiSelect, ShrinkThatDropsSelectedItemsRaisesTheSetEvent) {
    ListBox lb;
    lb.SetItemCount(1000);
    lb.SetBounds({0, 0, 200, 200});
    lb.SetSelectionMode(SelectionMode::Multiple);
    lb.SetSelectedIndex(100);
    SimulateKey(lb, VK_END, ModifierKeys::Shift);   // {100..999}
    SelectionSetCapture cap;
    auto sub = lb.SelectionSetChanged().Subscribe(&cap, CaptureSelectionSet);

    lb.SetItemCount(2000);                           // grow: nothing removed
    EXPECT_EQ(cap.count, 0);
    lb.SetItemCount(500);                            // drops 500..999
    EXPECT_EQ(cap.count, 1);
    EXPECT_EQ(cap.last.size(), 400u);
}

// Replacing the items is not a shrink: index 1 now names a different item, so the old
// multi-selection must not carry over onto items the user never picked. Re-seeded
// from the active index, like TreeView, and subscribers are told.
TEST(ListBoxMultiSelect, ReplacingItemsReseedsTheSetFromTheActiveItem) {
    ListBox lb;
    std::vector<std::wstring> items(10, L"x");
    lb.SetItems(std::move(items));
    lb.SetBounds({0, 0, 200, 400});
    lb.SetSelectionMode(SelectionMode::Multiple);
    lb.SetSelectedIndex(0);
    SimulateKey(lb, VK_DOWN, ModifierKeys::Shift);
    SimulateKey(lb, VK_DOWN, ModifierKeys::Shift);
    SimulateKey(lb, VK_DOWN, ModifierKeys::Shift);   // {0..3}, active 3
    EXPECT_EQ(lb.SelectedCount(), 4u);
    SelectionSetCapture cap;
    auto sub = lb.SelectionSetChanged().Subscribe(&cap, CaptureSelectionSet);

    std::vector<std::wstring> other(10, L"y");
    lb.SetItems(std::move(other));
    EXPECT_EQ(lb.SelectedCount(), 1u);
    EXPECT_TRUE(lb.IsSelected(3));
    EXPECT_FALSE(lb.IsSelected(0));
    EXPECT_EQ(cap.count, 1);
}

TEST(ListBoxMultiSelect, SingleModeRangesMirrorTheSingleSelection) {
    ListBox lb;
    std::vector<std::wstring> items{L"a", L"b", L"c"};
    lb.SetItems(std::move(items));
    EXPECT_EQ(lb.SelectedRanges().size(), 0u);
    EXPECT_EQ(lb.SelectedCount(), 0u);
    lb.SetSelectedIndex(1);
    const auto r = lb.SelectedRanges();
    EXPECT_EQ(r.size(), 1u);
    if (!r.empty()) EXPECT_TRUE(r[0] == (IndexRange{1, 1}));
    EXPECT_EQ(lb.SelectedCount(), 1u);
}
