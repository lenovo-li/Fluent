// ItemPeers.cpp — see ItemPeers.h.
#include "ItemPeers.h"

#include <algorithm>
#include <vector>

#include "AutomationSite.h"
#include "../core/UIElement.h"

namespace fluent::automation {

// --- ItemPeer -------------------------------------------------------------------

ItemPeer::ItemPeer(ItemsContainerPeer& container, size_t key)
    : AutomationPeer(container.Owner()), container_(container), key_(key) {}

size_t ItemPeer::Index() const { return container_.IndexOfKey(key_); }
bool ItemPeer::IsLive() const { return Index() < container_.ItemCount(); }
AutomationPeer* ItemPeer::ItemContainer() const { return &container_; }
bool ItemPeer::IsKeyboardFocusable() const { return container_.IsKeyboardFocusable(); }
bool ItemPeer::HasFocusWithin() const {
    return container_.HasFocusWithin() && container_.FocusedIndex() == Index();
}
bool ItemPeer::IsOffscreen() const {
    return container_.IsOffscreen() || BoundsInHostDip().isEmpty();
}
void ItemPeer::SetFocusFromAutomation() { container_.SetFocusFromAutomation(); }
RectDip OwnerRectToHostDip(const UIElement& o, const RectDip& local) {
    if (local.isEmpty()) return {};
    const RectDip owner = o.Bounds();
    const RectDip mapped = o.BoundsInHostDip();
    if (owner.w <= 0 || owner.h <= 0) return local;
    const float sx = mapped.w / owner.w, sy = mapped.h / owner.h;
    return {mapped.x + (local.x - owner.x) * sx, mapped.y + (local.y - owner.y) * sy,
            local.w * sx, local.h * sy};
}

// --- ItemsContainerPeer ---------------------------------------------------------

AutomationPeer* ItemsContainerPeer::ItemAt(size_t i) {
    if (i >= ItemCount()) return nullptr;
    const size_t key = KeyAt(i);
    if (lastCount_ == SIZE_MAX) {  // a client now knows the count, first key and window
        lastCount_ = ItemCount();
        lastFirstKey_ = lastCount_ ? KeyAt(0) : SIZE_MAX;
        ExposedWindow(lastBegin_, lastEnd_);
    }
    auto it = items_.find(key);
    if (it != items_.end()) {
        it->second->lastUse_ = ++useClock_;
        return it->second.get();
    }
    // Over budget: do NOT release here. ItemAt runs inside a client's COM call,
    // possibly on behalf of an item that is itself in the cache (Navigate from row i
    // asks for row i+1); releasing it now would destroy the peer whose method is on
    // the stack. Ask for a trim from the next flush instead (OnOwnerInvalidated).
    if (items_.size() >= kMaxCachedItems && !trimRequested_) {
        trimRequested_ = true;
        if (IAutomationSite* site = Owner().AutomationSite())
            if (HasProvider()) site->QueueTrim(Provider());
    }
    auto item = MakeItem(key);
    if (!item) return nullptr;
    item->lastUse_ = ++useClock_;
    AutomationPeer* raw = item.get();
    items_.emplace(key, std::move(item));
    return raw;
}

void ItemsContainerPeer::ExposedWindow(size_t& begin, size_t& end) const {
    const size_t n = ItemCount();
    size_t first = 0, last = 0;
    if (!VisibleItemRange(first, last) || n == 0) {
        begin = 0;
        end = n;
        return;
    }
    if (first >= n || last < first) {
        // Not laid out yet, or nothing visible: a first page, so the list is not empty.
        begin = 0;
        end = std::min(n, size_t{64});
        return;
    }
    last = std::min(last, n - 1);
    const size_t page = last - first + 1;
    const size_t margin = std::min(page, (kMaxExposedItems - std::min(page, kMaxExposedItems)) / 2);
    begin = first > margin ? first - margin : 0;
    end = std::min(n, last + 1 + margin);
    if (end - begin > kMaxExposedItems) end = begin + kMaxExposedItems;
}
std::wstring ItemsContainerPeer::ItemNameAt(size_t i) const {
    // Creates the peer: only for containers that do not override (few items).
    AutomationPeer* p = const_cast<ItemsContainerPeer*>(this)->ItemAt(i);
    return p ? p->GetName() : std::wstring();
}

size_t ItemsContainerPeer::FindItem(size_t after, const std::wstring* name, int selected) const {
    const size_t n = ItemCount();
    for (size_t i = after == SIZE_MAX ? 0 : after + 1; i < n; ++i) {
        if (selected >= 0 && ItemSelectedAt(i) != (selected == 1)) continue;
        if (name && ItemNameAt(i) != *name) continue;
        return i;
    }
    return SIZE_MAX;
}

size_t ItemsContainerPeer::ExposedBegin() const {
    size_t b = 0, e = 0;
    ExposedWindow(b, e);
    return b;
}
size_t ItemsContainerPeer::ExposedEnd() const {
    size_t b = 0, e = 0;
    ExposedWindow(b, e);
    return e;
}

AutomationPeer* ItemsContainerPeer::FocusedItem() {
    const size_t i = FocusedIndex();
    return i < ItemCount() ? ItemAt(i) : nullptr;
}

void ItemsContainerPeer::OnOwnerInvalidated() {
    // Synchronous, on every invalidation of the control: queue only. A count change
    // a client could have seen asks for a flush-time re-check even with no listener
    // (it frees the cache and freezes providers of rows that are gone). A count that
    // did not change asks for nothing here, so an animating list a client walked
    // once and left posts nothing; after the flush that found no listener,
    // lastCount_ is SIZE_MAX again and even count changes post nothing.
    if (lastCount_ != SIZE_MAX && ItemCount() != lastCount_ && HasProvider()) {
        if (IAutomationSite* site = Owner().AutomationSite()) site->QueueTrim(Provider());
    }
    AutomationPeer::OnOwnerInvalidated();
}

void ItemsContainerPeer::OnFlushRecheck() {
    IAutomationSite* site = Owner().AutomationSite();
    const size_t count = ItemCount();
    const size_t firstKey = count ? KeyAt(0) : SIZE_MAX;
    const bool countChanged = lastCount_ != SIZE_MAX && count != lastCount_;
    const bool firstMoved = lastCount_ != SIZE_MAX && firstKey != lastFirstKey_;
    size_t wBegin = 0, wEnd = 0;
    ExposedWindow(wBegin, wEnd);
    const bool windowMoved = lastCount_ != SIZE_MAX && lastBegin_ != SIZE_MAX &&
                             (wBegin != lastBegin_ || wEnd != lastEnd_);
    bool removed = false;
    if (!site || !site->ClientsAreListening()) {
        // Nobody left to use the cache: drop it, and stop watching the count.
        ReleaseAll();
        lastCount_ = SIZE_MAX;
        lastFirstKey_ = SIZE_MAX;
        lastBegin_ = lastEnd_ = SIZE_MAX;
        trimRequested_ = false;
        return;
    }
    lastCount_ = count;
    lastFirstKey_ = firstKey;
    lastBegin_ = wBegin;
    lastEnd_ = wEnd;
    if (countChanged && !KeysAreStable()) {
        // Index keys may all have shifted; no cached item is known to be right.
        removed = ReleaseAll();
    } else {
        // An item whose key no longer maps to an index is gone (row removed, items
        // replaced, subtree collapsed).
        for (auto it = items_.begin(); it != items_.end();) {
            auto next = std::next(it);
            if (IndexOfKey(it->first) >= count) {
                ReleaseItem(it);
                removed = true;
            }
            it = next;
        }
    }
    if (trimRequested_ || items_.size() > kMaxCachedItems) {
        trimRequested_ = false;
        removed = TrimToRecent() || removed;
    }
    // Rows added (an append, a TreeView expand) release nothing but change the
    // children just as much.
    if ((removed || countChanged || firstMoved || windowMoved) && HasProvider())
        site->QueueChildrenChanged(Provider());
    QueueItemRechecks(*site);
}

bool ItemsContainerPeer::ReleaseAll() {
    if (items_.empty()) return false;
    for (auto it = items_.begin(); it != items_.end();) {
        auto next = std::next(it);
        ReleaseItem(it);
        it = next;
    }
    return true;
}

bool ItemsContainerPeer::TrimToRecent() {
    if (items_.size() <= kKeptOnTrim) return false;
    // Use stamp of the kKeptOnTrim-th most recent item: everything older goes.
    std::vector<uint64_t> uses;
    uses.reserve(items_.size());
    for (const auto& kv : items_) uses.push_back(kv.second->lastUse_);
    std::nth_element(uses.begin(), uses.begin() + (kKeptOnTrim - 1), uses.end(),
                     [](uint64_t a, uint64_t b) { return a > b; });
    const uint64_t cutoff = uses[kKeptOnTrim - 1];
    const size_t focused = FocusedIndex();
    const size_t keep = focused < ItemCount() ? KeyAt(focused) : SIZE_MAX;
    bool released = false;
    for (auto it = items_.begin(); it != items_.end();) {
        auto next = std::next(it);
        if (it->first != keep && it->second->lastUse_ < cutoff) {
            ReleaseItem(it);
            released = true;
        }
        it = next;
    }
    return released;
}

void ItemsContainerPeer::QueueItemRechecks(IAutomationSite& site) {
    // The focused item first (what a screen reader announces), then cached items
    // with a visible rect. Bounded, so a client that cached 1024 rows costs at most
    // kMaxRecheckedItems name reads per invalidation of the container.
    size_t queued = 0;
    const size_t focused = FocusedIndex();
    const size_t focusKey = focused < ItemCount() ? KeyAt(focused) : SIZE_MAX;
    auto queue = [&](ItemPeer& item) {
        if (!item.HasProvider()) return;
        site.QueueChanged(item.Provider());
        ++queued;
    };
    auto f = items_.find(focusKey);
    if (f != items_.end()) queue(*f->second);
    for (auto& kv : items_) {
        if (queued >= kMaxRecheckedItems) break;
        if (kv.first == focusKey || kv.second->BoundsInHostDip().isEmpty()) continue;
        queue(*kv.second);
    }
}

void ItemsContainerPeer::ReleaseItem(std::map<size_t, std::unique_ptr<ItemPeer>>::iterator it) {
    ReleasePeer(*it->second, Owner().AutomationSite());
    items_.erase(it);
}

void ItemsContainerPeer::ReleaseOwned(IAutomationSite* site) {
    for (auto& kv : items_) ReleasePeer(*kv.second, site);
    items_.clear();
}

}  // namespace fluent::automation
