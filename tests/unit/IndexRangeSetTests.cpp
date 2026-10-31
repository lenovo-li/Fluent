// IndexRangeSetTests.cpp — the multi-select storage as a pure type.
//
// IndexRangeSet (controls/primitives/Selector.h) replaced a per-index hash set so
// that a Shift range over a million items is one range instead of a million
// inserts. Its correctness rests on one canonical form — sorted, disjoint,
// non-touching inclusive ranges — and every mutator has an edge where that form
// is easy to break: an add that touches a neighbour without merging, a remove
// that should split but truncates, an insert that shifts a straddling range
// without splitting it, an erase that leaves two touching ranges behind. Each
// test below pins one of those edges by comparing the exact range list, not just
// membership, because a non-canonical set can answer Contains() correctly and
// still report the wrong RangeCount(), compare unequal to an identical set, and
// grow without bound under repeated toggles.

#include "../framework/Test.h"
#include "../../FluentUI/controls/primitives/Selector.h"

#include <vector>

using namespace fluent;

namespace {
using R = std::vector<IndexRange>;
bool Is(const IndexRangeSet& s, const R& expected) { return s.Ranges() == expected; }
} // namespace

TEST(IndexRangeSet, AssignRangeIsOneRangeWhateverItsLength) {
    IndexRangeSet s;
    s.AssignRange(0, 999999);
    EXPECT_TRUE(Is(s, R{{0, 999999}}));
    EXPECT_EQ(s.Count(), 1000000u);
    EXPECT_TRUE(s.Contains(0));
    EXPECT_TRUE(s.Contains(999999));
    EXPECT_FALSE(s.Contains(1000000));
    EXPECT_FALSE(s.Contains(-1));

    s.AssignRange(5, 4);  // empty range replaces with nothing
    EXPECT_TRUE(s.Empty());
    s.AssignRange(-1, -1);  // "no selection" index
    EXPECT_TRUE(s.Empty());
}

TEST(IndexRangeSet, AddMergesOverlappingAndTouchingRanges) {
    IndexRangeSet s;
    s.AddRange(10, 12);
    s.AddRange(20, 22);
    EXPECT_TRUE(Is(s, R{{10, 12}, {20, 22}}));

    s.AddRange(13, 13);   // touches the first on its right edge: merge, not a new run
    EXPECT_TRUE(Is(s, R{{10, 13}, {20, 22}}));
    s.Add(19);            // touches the second on its left edge
    EXPECT_TRUE(Is(s, R{{10, 13}, {19, 22}}));
    s.AddRange(14, 18);   // fills the gap exactly: both collapse into one
    EXPECT_TRUE(Is(s, R{{10, 22}}));

    s.AddRange(0, 3);     // before everything, not touching
    s.AddRange(30, 31);   // after everything
    EXPECT_TRUE(Is(s, R{{0, 3}, {10, 22}, {30, 31}}));
    s.AddRange(2, 40);    // swallows all three
    EXPECT_TRUE(Is(s, R{{0, 40}}));
    EXPECT_EQ(s.Count(), 41u);
}

TEST(IndexRangeSet, RemoveSplitsTruncatesAndDrops) {
    IndexRangeSet s;
    s.AssignRange(0, 9);
    s.Remove(5);                    // middle: split into two
    EXPECT_TRUE(Is(s, R{{0, 4}, {6, 9}}));
    s.Remove(0);                    // left edge: truncate
    s.Remove(9);                    // right edge: truncate
    EXPECT_TRUE(Is(s, R{{1, 4}, {6, 8}}));
    s.RemoveRange(3, 7);            // spans the gap: trims both neighbours
    EXPECT_TRUE(Is(s, R{{1, 2}, {8, 8}}));
    s.Remove(8);                    // a one-index range disappears entirely
    EXPECT_TRUE(Is(s, R{{1, 2}}));
    s.RemoveRange(50, 60);          // nothing there: no-op
    EXPECT_TRUE(Is(s, R{{1, 2}}));
    s.RemoveRange(0, 100);
    EXPECT_TRUE(s.Empty());
}

TEST(IndexRangeSet, ToggleAtEdgesKeepsTheCanonicalForm) {
    IndexRangeSet s;
    s.AssignRange(10, 20);
    EXPECT_FALSE(s.Toggle(10));     // left edge off
    EXPECT_FALSE(s.Toggle(20));     // right edge off
    EXPECT_TRUE(Is(s, R{{11, 19}}));
    EXPECT_FALSE(s.Toggle(15));     // middle off: split
    EXPECT_TRUE(Is(s, R{{11, 14}, {16, 19}}));
    EXPECT_TRUE(s.Toggle(15));      // back on: the two halves re-merge into ONE range
    EXPECT_TRUE(Is(s, R{{11, 19}}));
    EXPECT_TRUE(s.Toggle(10));      // adjacent on: extends rather than adding a run
    EXPECT_TRUE(s.Toggle(20));
    EXPECT_TRUE(Is(s, R{{10, 20}}));
}

TEST(IndexRangeSet, InsertShiftsAndSplitsAStraddlingRange) {
    IndexRangeSet s;
    s.AddRange(2, 5);
    s.AddRange(10, 11);
    s.InsertAt(4, 3);    // 3 new items at 4: [2,3] stays, [4,5] -> [7,8], [10,11] -> [13,14]
    EXPECT_TRUE(Is(s, R{{2, 3}, {7, 8}, {13, 14}}));
    EXPECT_FALSE(s.Contains(4));   // the inserted items are NOT selected
    EXPECT_FALSE(s.Contains(6));
    EXPECT_EQ(s.Count(), 6u);      // same items, new numbers

    s.InsertAt(2, 1);    // at a range's first index: the whole range moves, no split
    EXPECT_TRUE(Is(s, R{{3, 4}, {8, 9}, {14, 15}}));
    s.InsertAt(100, 5);  // past everything: no-op
    EXPECT_TRUE(Is(s, R{{3, 4}, {8, 9}, {14, 15}}));
    s.InsertAt(0, 0);    // zero items: no-op
    EXPECT_TRUE(Is(s, R{{3, 4}, {8, 9}, {14, 15}}));
}

TEST(IndexRangeSet, EraseShiftsDownAndMergesAcrossTheClosedHole) {
    IndexRangeSet s;
    s.AddRange(2, 4);
    s.AddRange(8, 9);
    s.EraseAt(5, 3);     // removes the unselected gap 5..7: the runs now touch -> one
    EXPECT_TRUE(Is(s, R{{2, 6}}));
    EXPECT_EQ(s.RangeCount(), 1u);

    s.AddRange(10, 12);  // {2..6, 10..12}
    s.EraseAt(3, 2);     // removes selected 3,4 from inside the first run
    EXPECT_TRUE(Is(s, R{{2, 4}, {8, 10}}));
    s.EraseAt(0, 2);     // removes 0,1 (unselected) and index 2 shifts to 0
    EXPECT_TRUE(Is(s, R{{0, 2}, {6, 8}}));
    s.EraseAt(1, 6);     // removes 1..6, clipping both runs: 0 and 7..8 -> 0 and 1..2, touching
    EXPECT_TRUE(Is(s, R{{0, 2}}));
}

TEST(IndexRangeSet, ClampOnShrinkDropsOnlyTheTail) {
    IndexRangeSet s;
    s.AddRange(0, 3);
    s.AddRange(10, 999999);
    s.ClampTo(500);
    EXPECT_TRUE(Is(s, R{{0, 3}, {10, 499}}));
    s.ClampTo(10);       // exactly at a range's first index: that range goes
    EXPECT_TRUE(Is(s, R{{0, 3}}));
    s.ClampTo(2);
    EXPECT_TRUE(Is(s, R{{0, 1}}));
    s.ClampTo(0);
    EXPECT_TRUE(s.Empty());
}

TEST(IndexRangeSet, ToIndicesExpandsAscending) {
    IndexRangeSet s;
    s.AddRange(7, 8);
    s.Add(1);
    s.AddRange(3, 4);
    const std::vector<int> expected{1, 3, 4, 7, 8};
    EXPECT_TRUE(s.ToIndices() == expected);
    EXPECT_EQ(s.Count(), 5u);
}
