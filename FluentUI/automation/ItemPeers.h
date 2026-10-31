// ItemPeers.h — UIA nodes for things that are not elements: list rows, tree rows,
// tab headers, combo-box entries.
//
// A virtualized control draws its items itself; there is no UIElement per row, and
// there must not be one per row for UIA either (O(visible) rule). So the control's
// peer is an ItemsContainerPeer that creates an ItemPeer per item ON DEMAND, when a
// client navigates to it, keyed by a value the control chooses (an index for a
// ListBox, a stable row id for a TreeView). Items are cached; the cache is bounded
// (kMaxCachedItems) so a client enumerating 100k rows cannot grow it without limit,
// and an item whose key stops existing is released (frozen + disconnected) like a
// removed element.
//
// Children of a container = its items first, then its exposed element children
// (TabControl: the tab headers, then the selected page). The provider walks that
// combined list.
#pragma once

#include <map>

#include "AutomationPeer.h"

namespace fluent::automation {

class ItemsContainerPeer;

class ItemPeer : public AutomationPeer {
public:
    ItemPeer(ItemsContainerPeer& container, size_t key);
    size_t Key() const { return key_; }
    ItemsContainerPeer& Container() const { return container_; }
    // Current index in the container's item list (index-keyed containers: the key).
    size_t Index() const;
    // False once the item's key no longer maps to a row (removed, collapsed away)
    // and before the flush released it. Actions on a gone item do nothing: an index
    // of ItemCount() must never reach the control's selection/expand API.
    bool IsLive() const;
    bool IsEnabled() const override { return IsLive() && AutomationPeer::IsEnabled(); }

    AutomationPeer* ItemContainer() const override;
    bool IsControlElement() const override { return true; }
    // An item is focusable with its container; it has focus when the container has
    // and the container reports it as its focused item.
    bool IsKeyboardFocusable() const override;
    bool HasFocusWithin() const override;
    std::wstring GetHelpText() const override { return {}; }
    std::wstring GetAutomationId() const override { return {}; }
    // Items report their own rect; offscreen = empty rect or container offscreen.
    bool IsOffscreen() const override;
    void SetFocusFromAutomation() override;

private:
    ItemsContainerPeer& container_;
    const size_t key_;
};

class ItemsContainerPeer : public AutomationPeer, public IItemsPeer {
public:
    explicit ItemsContainerPeer(UIElement& owner) : AutomationPeer(owner) {}
    IItemsPeer* AsItems() override { return this; }

    // IItemsPeer: ItemAt creates or returns the cached item for index i.
    AutomationPeer* ItemAt(size_t i) override;

    // Supplied by the control's peer.
    virtual size_t KeyAt(size_t i) const { return i; }
    virtual size_t IndexOfKey(size_t key) const { return key; }  // >= ItemCount() = gone
    virtual std::unique_ptr<ItemPeer> MakeItem(size_t key) = 0;
    // The keyboard-current item's index, or ItemCount() for none.
    virtual size_t FocusedIndex() const { return ItemCount(); }

    AutomationPeer* FocusedItem() override;
    // Re-check after the owner invalidated: drop items whose key vanished, then the
    // base re-check (which raises property changes).
    void OnOwnerInvalidated() override;  // queues only
    void OnFlushRecheck() override;      // prunes / trims
    size_t CachedItemCount() const { return items_.size(); }

    // Bound on the cache. Past it, every cached item but the requested one is
    // released: the client sees earlier rows as gone and re-fetches them, which is
    // what virtualized UIA trees do.
    static constexpr size_t kMaxCachedItems = 1024;

protected:
    void ReleaseOwned(IAutomationSite* site) override;

private:
    void ReleaseItem(std::map<size_t, std::unique_ptr<ItemPeer>>::iterator it);
    std::map<size_t, std::unique_ptr<ItemPeer>> items_;
    bool trimRequested_ = false;
    size_t lastCount_ = SIZE_MAX;  // item count at the last flush re-check
};

}  // namespace fluent::automation
