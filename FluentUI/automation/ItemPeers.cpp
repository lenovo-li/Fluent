// ItemPeers.cpp — see ItemPeers.h.
#include "ItemPeers.h"

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

// --- ItemsContainerPeer ---------------------------------------------------------

AutomationPeer* ItemsContainerPeer::ItemAt(size_t i) {
    if (i >= ItemCount()) return nullptr;
    const size_t key = KeyAt(i);
    auto it = items_.find(key);
    if (it != items_.end()) return it->second.get();
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
    AutomationPeer* raw = item.get();
    items_.emplace(key, std::move(item));
    return raw;
}

AutomationPeer* ItemsContainerPeer::FocusedItem() {
    const size_t i = FocusedIndex();
    return i < ItemCount() ? ItemAt(i) : nullptr;
}

void ItemsContainerPeer::OnOwnerInvalidated() {
    // Synchronous, on every invalidation of the control: queue only. When the item
    // count changed and items are cached, ask for a flush-time prune even with no
    // listener (it frees memory and freezes providers of rows that are gone); a
    // count that did not change asks for nothing, so an animating list a client
    // walked once and left posts nothing.
    if (!items_.empty() && ItemCount() != lastCount_ && HasProvider()) {
        if (IAutomationSite* site = Owner().AutomationSite()) site->QueueTrim(Provider());
    }
    AutomationPeer::OnOwnerInvalidated();
}

void ItemsContainerPeer::OnFlushRecheck() {
    lastCount_ = ItemCount();
    // An item whose key no longer maps to an index is gone (row removed, items
    // replaced, subtree collapsed).
    bool removed = false;
    for (auto it = items_.begin(); it != items_.end();) {
        auto next = std::next(it);
        if (IndexOfKey(it->first) >= ItemCount()) {
            ReleaseItem(it);
            removed = true;
        }
        it = next;
    }
    // The bounded cache (see ItemAt). Keep the focused item: that is what a screen
    // reader is sitting on.
    if (trimRequested_ || items_.size() > kMaxCachedItems) {
        trimRequested_ = false;
        const size_t keep = FocusedIndex() < ItemCount() ? KeyAt(FocusedIndex()) : SIZE_MAX;
        for (auto it = items_.begin(); it != items_.end();) {
            auto next = std::next(it);
            if (it->first != keep) {
                ReleaseItem(it);
                removed = true;
            }
            it = next;
        }
    }
    if (removed && HasProvider()) {
        if (IAutomationSite* site = Owner().AutomationSite()) site->QueueChildrenChanged(Provider());
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
