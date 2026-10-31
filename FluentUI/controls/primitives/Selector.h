// Selector.h — template base for selection-capable items controls.
//
// Selector<TItem> owns two layers of selection state:
//
//   Single selection — a selectedIndex_ with SelectedIndex()/SetSelectedIndex()
//   (clamp + change-dedup), SelectedItem() reaching into items_, and an
//   OnSelectionChanged() hook the subclass uses to raise its own typed Event and
//   run side effects (sync a list view, scroll a row into view).
//
//   Multiple selection — a selectionMode_ policy, a selectedSet_ (an
//   IndexRangeSet, see below), and a selectionAnchor_ for Shift-range gestures,
//   with SelectedIndices() / SelectedRanges() / SelectedCount() / IsSelected() /
//   ToggleSelection() / RangeSelectTo(). Side effects go through the
//   OnSelectionSetChanged() hook.
//
// The selection index addresses items_ directly (a *data* index). TreeView keeps
// its own visible-index-vs-row-index mapping by overriding SetSelectedIndex — the
// base is still the single owner of the storage and the dedup logic.
//
// Why the multi-select state lives here rather than in each list control:
// TreeView grew it privately first, and a second list control (ListBox) would
// have had to write it a second time — with its own subtly different answers to
// "what does a plain click do to the set", "where does the anchor go on Ctrl
// +click", and "what happens to the set when the items are replaced". Three
// controls would have meant three dialects of the same gesture. The set is the
// authoritative selection in Multiple mode and selectedIndex_ tracks the
// most-recently-touched ("active") index for keyboard navigation, in both.
//
// Header-only like ItemsControl (no .cpp).
#pragma once

#include "ItemsControl.h"
#include <algorithm>
#include <cstddef>
#include <utility>
#include <vector>

namespace fluent {

// Selection policy shared by every Selector. Single is the classic one-index
// selection; Multiple allows Ctrl/Shift to build a set.
enum class SelectionMode { Single, Multiple };

// One inclusive run of selected indices, lo <= hi.
struct IndexRange {
    int lo = 0;
    int hi = -1;
    int Count() const { return hi - lo + 1; }
    bool operator==(const IndexRange& o) const { return lo == o.lo && hi == o.hi; }
};

// IndexRangeSet — a set of non-negative ints stored as sorted, disjoint,
// NON-ADJACENT inclusive ranges.
//
// Why not a hash set of indices. The multi-select set used to be an
// std::unordered_set<int>. Every gesture a user makes on a list produces a
// contiguous run or a small number of them: Shift+click is one run, Ctrl+click
// adds or punches one index, Shift+End over a virtualized 1,000,000-item ListBox
// is ONE run of a million. A per-index hash set turns that last one into a million
// node allocations on the input path (measured ~80 ms per key press in Release,
// i.e. a dozen-plus dropped frames at 240 Hz, for a gesture that carries one
// interval of information). Storing the runs makes the cost track the number of
// runs the user built, which is what the gesture actually encodes.
//
// Invariants (every mutator restores them; the tests pin each one):
//   * ranges_ is sorted by lo, each lo <= hi, all indices >= 0;
//   * no two ranges overlap or touch: ranges_[k].hi + 1 < ranges_[k+1].lo. Touching
//     ranges are merged, so an index set has exactly ONE representation. That is
//     what makes operator== a plain vector compare and RangeCount() meaningful
//     ("how fragmented is the selection"), and it keeps Contains a single binary
//     search with no neighbour to double-check.
//
// Costs, r = RangeCount():
//   Contains          O(log r)
//   AssignRange/Clear O(1) amortized (Clear keeps capacity)
//   AddRange          O(log r) to locate + O(merged) to coalesce, plus the vector
//                     shift of an insert in the middle: O(r) worst case, O(1) when
//                     appending in ascending order (TreeView's range fill does).
//   RemoveRange       same shape; splitting one range into two is one insert.
//   Count             O(r) — it is summed, not cached, so no mutator can leave a
//                     stale cached count behind.
//   ClampTo/InsertAt/EraseAt  O(log r + ranges shifted).
// A vector of ranges rather than a balanced tree: r is small for anything a person
// clicks together (a handful to a few hundred runs), and a contiguous vector with
// binary search beats node chasing at that size by a wide margin. The O(r) middle
// insert only matters for a selection fragmented into tens of thousands of runs,
// which takes tens of thousands of Ctrl+clicks to build.
class IndexRangeSet {
public:
    bool Empty() const { return ranges_.empty(); }
    void Clear() { ranges_.clear(); }

    // Number of selected indices (NOT the number of ranges). Summed in size_t so
    // a set spanning the whole int range cannot overflow.
    size_t Count() const {
        size_t n = 0;
        for (const IndexRange& r : ranges_) n += static_cast<size_t>(r.Count());
        return n;
    }

    // The canonical ranges, ascending. Iterate these rather than expanding to
    // indices whenever the consumer can work per run (draw, delete, serialize).
    const std::vector<IndexRange>& Ranges() const { return ranges_; }
    size_t RangeCount() const { return ranges_.size(); }

    bool Contains(int index) const {
        // First range whose hi >= index; it contains index iff its lo <= index.
        auto it = LowerByHi(index);
        return it != ranges_.end() && it->lo <= index;
    }

    // Replace the whole set with [lo, hi] (empty when lo > hi). The Shift-range
    // gesture is exactly this, which is why Shift+End over a million items is one
    // assignment instead of a million inserts.
    void AssignRange(int lo, int hi) {
        ranges_.clear();
        if (lo < 0) lo = 0;
        if (lo <= hi) ranges_.push_back({lo, hi});
    }

    void Add(int index) { AddRange(index, index); }
    void Remove(int index) { RemoveRange(index, index); }

    // Toggle one index; returns true when it is selected afterwards. Ctrl+click
    // inside a run splits it in two, Ctrl+click next to a run extends (and, if it
    // closes a one-index gap, merges) — both fall out of Add/Remove.
    bool Toggle(int index) {
        if (Contains(index)) { Remove(index); return false; }
        Add(index);
        return true;
    }

    // Union with [lo, hi], merging every range it overlaps OR touches.
    void AddRange(int lo, int hi) {
        if (lo < 0) lo = 0;
        if (lo > hi) return;
        // Ranges that overlap or touch [lo, hi] are those with hi >= lo - 1 and
        // lo <= hi + 1. `first` is the first with r.hi >= lo - 1; widen into lo == 0
        // safely (lo - 1 == -1 never underflows).
        auto first = LowerByHi(lo - 1);
        auto last = first;
        while (last != ranges_.end() && last->lo <= SatInc(hi)) ++last;
        if (first == last) {
            ranges_.insert(first, IndexRange{lo, hi});
            return;
        }
        const int newLo = std::min(lo, first->lo);
        const int newHi = std::max(hi, (last - 1)->hi);
        *first = {newLo, newHi};
        ranges_.erase(first + 1, last);
    }

    // Difference with [lo, hi]. A range that straddles the hole on both sides is
    // split into two, the only case that adds a range.
    void RemoveRange(int lo, int hi) {
        if (lo < 0) lo = 0;
        if (lo > hi || ranges_.empty()) return;
        auto first = LowerByHi(lo);      // first range that can intersect
        if (first == ranges_.end() || first->lo > hi) return;
        auto last = first;
        while (last != ranges_.end() && last->lo <= hi) ++last;  // past last hit
        const IndexRange head = *first;
        const IndexRange tail = *(last - 1);
        // Survivors: the part of the first hit range left of lo, and the part of
        // the last hit range right of hi. Either, both, or neither may exist.
        std::vector<IndexRange>::iterator at = ranges_.erase(first, last);
        if (tail.hi > hi) at = ranges_.insert(at, IndexRange{hi + 1, tail.hi});
        if (head.lo < lo) ranges_.insert(at, IndexRange{head.lo, lo - 1});
    }

    // Drop every index >= count (the item source shrank). Count <= 0 empties.
    void ClampTo(int count) {
        if (count <= 0) { ranges_.clear(); return; }
        RemoveRange(count, kMaxIndex);
    }

    // `n` items were inserted at `index`: every selected index >= index moves up
    // by n, and a range that spanned the insertion point is split around the new
    // (unselected) items. This is what keeps a selection on the same ITEMS across
    // an insert, rather than on the same numbers.
    void InsertAt(int index, int n) {
        if (n <= 0 || index < 0) return;
        auto it = LowerByHi(index);  // first range with hi >= index
        if (it == ranges_.end()) return;
        if (it->lo < index) {        // straddles: split, the right half shifts
            const IndexRange right{index + n, it->hi + n};
            it->hi = index - 1;
            it = ranges_.insert(it + 1, right) + 1;
        }
        for (; it != ranges_.end(); ++it) { it->lo += n; it->hi += n; }
    }

    // `n` items starting at `index` were removed: their indices leave the set,
    // every index above the hole moves down by n, and the two ranges that end up
    // touching across the closed hole are merged (selected 2..4 and 8..9, erase
    // 5..7 -> 2..6 as one run, not two touching runs).
    void EraseAt(int index, int n) {
        if (n <= 0 || index < 0) return;
        const int end = (index > kMaxIndex - n) ? kMaxIndex : index + n - 1;
        RemoveRange(index, end);
        auto it = LowerByHi(index);  // first range entirely above the hole
        for (auto s = it; s != ranges_.end(); ++s) { s->lo -= n; s->hi -= n; }
        if (it != ranges_.begin() && it != ranges_.end() && (it - 1)->hi + 1 == it->lo) {
            (it - 1)->hi = it->hi;
            ranges_.erase(it);
        }
    }

    // Expand to ascending indices. O(Count()) by nature — use Ranges() instead
    // whenever the consumer can.
    std::vector<int> ToIndices() const {
        std::vector<int> out;
        out.reserve(Count());
        for (const IndexRange& r : ranges_)
            for (int i = r.lo; ; ++i) { out.push_back(i); if (i == r.hi) break; }
        return out;
    }

    bool operator==(const IndexRangeSet& o) const { return ranges_ == o.ranges_; }

private:
    static constexpr int kMaxIndex = 0x7fffffff;
    // hi + 1 without overflowing at INT_MAX (a range ending at the top cannot be
    // followed by anything anyway).
    static int SatInc(int v) { return v == kMaxIndex ? v : v + 1; }

    // First range whose hi >= value. hi is sorted because ranges are disjoint.
    std::vector<IndexRange>::iterator LowerByHi(int value) {
        return std::lower_bound(ranges_.begin(), ranges_.end(), value,
            [](const IndexRange& r, int v) { return r.hi < v; });
    }
    std::vector<IndexRange>::const_iterator LowerByHi(int value) const {
        return std::lower_bound(ranges_.begin(), ranges_.end(), value,
            [](const IndexRange& r, int v) { return r.hi < v; });
    }

    std::vector<IndexRange> ranges_;
};

template <class TItem>
class Selector : public ItemsControl<TItem> {
public:
    int SelectedIndex() const { return selectedIndex_; }

    // Set the selected index. Clamps to [-1, count) and is a no-op when unchanged.
    // On a real change it stores the index, collapses the multi-select set onto it
    // (see below), runs the subclass hook, and repaints. Virtual so TreeView can
    // remap a *visible* index onto a row index before delegating to base storage.
    virtual void SetSelectedIndex(int index) {
        int next = ClampIndex(index);
        if (next == selectedIndex_) return;
        int prev = selectedIndex_;
        selectedIndex_ = next;

        // In Multiple mode a plain SetSelectedIndex (arrow key, type-ahead, or a
        // programmatic set) means "select exactly this index" — the same thing a
        // plain click means. The collapse happens HERE, in the base, rather than
        // in each subclass's OnSelectionChanged override: this is the one place
        // every non-modifier selection change funnels through, so the set cannot
        // be left holding indices from a previous gesture, and no subclass has to
        // remember to do it. ToggleSelection / RangeSelectTo deliberately do NOT
        // route through here — they write selectedIndex_ directly, precisely so
        // this collapse does not undo the set they just built.
        if (selectionMode_ == SelectionMode::Multiple) {
            selectedSet_.AssignRange(next, next);  // next == -1 -> empty
            selectionAnchor_ = next;
        }

        OnSelectionChanged(prev, next);
        this->Invalidate();
    }

    // The selected item, or nullptr when nothing is selected / out of range.
    const TItem* SelectedItem() const { return this->ItemAt(selectedIndex_); }

    // --- Multiple selection -------------------------------------------------

    // Switching modes reconciles the two representations rather than leaving one
    // stale, because both are read by rendering and by the public accessors:
    //   → Multiple: seed the set from the current single selection, so the index
    //     the user already had selected stays selected instead of the set
    //     starting empty.
    //   → Single: drop the set. selectedIndex_ (the active index) survives, so
    //     the result is "the item you last touched stays selected", which is the
    //     only choice that needs no arbitrary pick among several selected items.
    void SetSelectionMode(SelectionMode mode) {
        if (selectionMode_ == mode) return;
        selectionMode_ = mode;
        selectedSet_.Clear();
        if (mode == SelectionMode::Multiple) {
            selectedSet_.AssignRange(selectedIndex_, selectedIndex_);
            selectionAnchor_ = selectedIndex_;
        } else {
            selectionAnchor_ = -1;
        }
        this->Invalidate();
    }
    SelectionMode GetSelectionMode() const { return selectionMode_; }

    // The selected indices, ascending. In Single mode this is the single
    // selection (or empty), so a caller that only ever reads this accessor works
    // unchanged in both modes.
    //
    // O(selected): it expands the ranges into one int per selected item, so after a
    // Shift+End over a million-item list it allocates and fills a million ints.
    // Kept because it is the simplest contract for a small list and existing
    // callers use it. Prefer SelectedRanges() / SelectedCount() / IsSelected() on
    // anything that can be large or runs per frame.
    std::vector<int> SelectedIndices() const {
        if (selectionMode_ == SelectionMode::Single)
            return (selectedIndex_ >= 0) ? std::vector<int>{selectedIndex_}
                                         : std::vector<int>{};
        return selectedSet_.ToIndices();
    }

    // The selection as ascending, disjoint, non-touching inclusive ranges —
    // O(ranges), independent of how many items they cover. Same Single-mode rule
    // as SelectedIndices(): the single selection as one one-item range, or empty.
    std::vector<IndexRange> SelectedRanges() const {
        if (selectionMode_ == SelectionMode::Single)
            return (selectedIndex_ >= 0)
                       ? std::vector<IndexRange>{IndexRange{selectedIndex_, selectedIndex_}}
                       : std::vector<IndexRange>{};
        return selectedSet_.Ranges();
    }

    // Number of selected items, O(ranges). Use this instead of
    // SelectedIndices().size(), which materializes every index to count them.
    size_t SelectedCount() const {
        if (selectionMode_ == SelectionMode::Single) return selectedIndex_ >= 0 ? 1u : 0u;
        return selectedSet_.Count();
    }

    // O(log ranges); the per-row highlight test in ListBox/TreeView Render.
    bool IsSelected(int index) const {
        if (selectionMode_ == SelectionMode::Single) return index == selectedIndex_;
        return selectedSet_.Contains(index);
    }

    // Toggle one index's membership (Multiple mode only; no-op in Single). The
    // toggled index becomes both the active index and the new anchor, so a
    // following Shift+click extends from where the Ctrl+click landed.
    void ToggleSelection(int index) {
        if (selectionMode_ != SelectionMode::Multiple) return;
        if (index < 0 || index >= this->ItemCount()) return;
        selectedSet_.Toggle(index);
        selectedIndex_ = index;
        selectionAnchor_ = index;
        OnSelectionSetChanged();
        this->Invalidate();
    }

    // Replace the set with the inclusive range [anchor, index] (Multiple mode
    // only). The anchor stays put, so a second Shift+click extends from the same
    // origin rather than from the previous target.
    //
    // An anchor that no longer addresses an item (the item source shrank under it
    // without the subclass clamping) is treated as absent rather than trusted: a
    // stale anchor at 900 on a list now 10 long would otherwise select indices that
    // name nothing.
    void RangeSelectTo(int index) {
        if (selectionMode_ != SelectionMode::Multiple) return;
        if (index < 0 || index >= this->ItemCount()) return;
        const int anchor =
            (selectionAnchor_ >= 0 && selectionAnchor_ < this->ItemCount())
                ? selectionAnchor_ : index;
        const int lo = std::min(anchor, index);
        const int hi = std::max(anchor, index);
        selectedSet_.Clear();
        AddRangeToSelectionSet(lo, hi);
        selectedIndex_ = index;  // the end of the range becomes the active index
        OnSelectionSetChanged();
        this->Invalidate();
    }

protected:
    // Clamp an arbitrary index to the legal selection range [-1, count).
    int ClampIndex(int index) const {
        int count = this->ItemCount();
        if (index < -1) return -1;
        if (index > count - 1) return count - 1;
        return index;
    }

    // Called after selectedIndex_ actually changes. Subclass raises its Event<>
    // and runs side effects (list-view sync, ensure-visible). Default no-op.
    virtual void OnSelectionChanged(int /*oldIndex*/, int /*newIndex*/) {}

    // Fill selectedSet_ for RangeSelectTo's inclusive [lo, hi] (the set is already
    // cleared). The default adds the whole run as ONE range — O(1) whatever its
    // length — which is right for a flat list. TreeView overrides it: its indices are
    // rows of a pre-order tree, and a collapsed subtree's rows sit numerically
    // INSIDE the range while being invisible, so "every index" selected nodes the
    // user cannot see — and a delete over SelectedRowIndices() would act on them.
    // An override adds the runs it wants with selectedSet_.AddRange(); adding them
    // in ascending order keeps every add an O(1) append.
    virtual void AddRangeToSelectionSet(int lo, int hi) {
        selectedSet_.AddRange(lo, hi);
    }

    // Called after the multi-select SET changes without going through
    // SetSelectedIndex (i.e. from ToggleSelection / RangeSelectTo). Subclass
    // raises its set-changed Event<> and runs side effects. Default no-op.
    virtual void OnSelectionSetChanged() {}

    // Re-seed the set from the current selectedIndex_. For subclasses whose
    // OnItemsChanged replaces items_ wholesale: the old set held indices into
    // the PREVIOUS items_, where the same index now means a different item (or
    // none). Re-seeding rather than only clearing keeps the invariant that in
    // Multiple mode the set agrees with selectedIndex_ — leaving it empty
    // produces an active item that IsSelected() reports as unselected, and the
    // disagreement persists until the next click.
    //
    // Returns whether the SET changed, so the caller can raise its set event: a
    // subscriber holding the previous payload must hear that it is stale. Not raised
    // here, because a subclass's OnSelectionSetChanged may do more than raise (ListBox
    // scrolls the active item into view), which a data replacement must not trigger.
    bool ReseedSelectionSet() {
        IndexRangeSet next;
        if (selectionMode_ == SelectionMode::Multiple && selectedIndex_ >= 0)
            next.AssignRange(selectedIndex_, selectedIndex_);
        selectionAnchor_ =
            (selectionMode_ == SelectionMode::Multiple) ? selectedIndex_ : -1;
        if (next == selectedSet_) return false;
        selectedSet_ = std::move(next);
        return true;
    }

    // Drop every part of the selection that no longer addresses an item, for a
    // subclass whose item count shrank while the surviving indices still name the
    // same items (ListBox::SetItemCount on a virtualized list: items 0..n-1 are
    // still items 0..n-1). Clamps the active index, the set and the anchor
    // together; clamping only selectedIndex_ — which is what ListBox did before —
    // left IsSelected() true for indices past the end and SelectedIndices()
    // reporting items that do not exist. O(log ranges), so shrinking a list
    // with a million-item selection does not walk it.
    //
    // Returns whether the SET changed (see ReseedSelectionSet for why the caller,
    // not this function, raises the event). Shrinking a 1000-item list with 100..999
    // selected to 500 items silently left a subscriber believing 900 were selected.
    bool ClampSelectionToCount(int count) {
        if (count < 0) count = 0;
        if (selectedIndex_ >= count) selectedIndex_ = count - 1;
        const size_t before = selectedSet_.Count();
        selectedSet_.ClampTo(count);
        if (selectionAnchor_ >= count) selectionAnchor_ = count - 1;
        return selectedSet_.Count() != before;
    }

    // Selection storage owned here so subclasses never redeclare it. Using
    // `this->items_` from the dependent base requires the this-> qualification.
    int selectedIndex_ = -1;

    // Multi-select state. In Single mode selectedIndex_ is the only selection and
    // selectedSet_ stays empty; in Multiple mode selectedSet_ is authoritative and
    // selectedIndex_ tracks the most-recently-touched index.
    //
    // selectedSet_ replaced a protected std::unordered_set<int> selectedIndices_. The
    // name changed on purpose, so a subclass written against the old member fails to
    // compile instead of quietly calling a different API: replace insert(i) with
    // Add(i) (or AddRange(lo, hi) for a run), count(i) with Contains(i), erase(i)
    // with Remove(i), size() with Count(), and iteration with Ranges().
    SelectionMode selectionMode_ = SelectionMode::Single;
    IndexRangeSet selectedSet_;
    int selectionAnchor_ = -1;  // origin index for Shift range-select
};

} // namespace fluent
