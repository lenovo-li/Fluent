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

// Free form of ItemPeer::OwnerRectToHost, for item kinds that are not ItemPeers
// (grid cells).
RectDip OwnerRectToHostDip(const UIElement& owner, const RectDip& local);

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
    size_t IndexInContainer() const override { return Index(); }
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

    // A rect in the owner control's own space mapped to host DIPs through the
    // owner's ancestors (Viewbox scaling), the way the owner's own bounds are.
    RectDip OwnerRectToHost(const RectDip& local) const { return OwnerRectToHostDip(Owner(), local); }

private:
    friend class ItemsContainerPeer;
    ItemsContainerPeer& container_;
    const size_t key_;
    uint64_t lastUse_ = 0;  // container's use clock at the last ItemAt hit (trim order)
};

class ItemsContainerPeer : public AutomationPeer, public IItemsPeer {
public:
    explicit ItemsContainerPeer(UIElement& owner) : AutomationPeer(owner) {}
    IItemsPeer* AsItems() override { return this; }

    // IItemsPeer: ItemAt creates or returns the cached item for index i.
    AutomationPeer* ItemAt(size_t i) override;

    // The exposed window (IItemsPeer): the visible rows plus one page on each side,
    // at most kMaxExposedItems. A container whose items can be many overrides
    // VisibleItemRange; the default (false) exposes every item, right for a handful
    // (tab headers, menu rows, calendar days).
    size_t ExposedBegin() const override;
    size_t ExposedEnd() const override;
    // First and last item at least partly in the viewport, O(1). False = no window
    // (expose everything). A virtualized control returns true always, with
    // first = SIZE_MAX when nothing is visible yet (a first page is exposed).
    virtual bool VisibleItemRange(size_t& first, size_t& last) const {
        UNREFERENCED_PARAMETER(first);
        UNREFERENCED_PARAMETER(last);
        return false;
    }
    static constexpr size_t kMaxExposedItems = 256;

    // ItemContainer pattern support (IItemsPeer). Offered exactly when the container
    // windows its items. ItemNameAt reads an item's name without creating its peer;
    // the default creates it (fine for the few-item containers that do not override).
    bool VirtualizesItems() const override {
        size_t f = 0, l = 0;
        return VisibleItemRange(f, l);
    }
    size_t FindItem(size_t after, const std::wstring* name, int selected) const override;
    virtual std::wstring ItemNameAt(size_t i) const;
    virtual bool ItemSelectedAt(size_t i) const {
        UNREFERENCED_PARAMETER(i);
        return false;
    }

    // Supplied by the control's peer.
    virtual size_t KeyAt(size_t i) const { return i; }
    virtual size_t IndexOfKey(size_t key) const { return key; }  // >= ItemCount() = gone
    // True when a key keeps naming the same row across inserts and removals (a row
    // id). False for the default index keys: there, any change of the item count
    // may have shifted every row under its key ("item 5" was Apple, is now Fig),
    // so the whole cache is released and the client re-reads the children.
    virtual bool KeysAreStable() const { return false; }
    virtual std::unique_ptr<ItemPeer> MakeItem(size_t key) = 0;
    // The keyboard-current item's index, or ItemCount() for none.
    virtual size_t FocusedIndex() const { return ItemCount(); }

    AutomationPeer* FocusedItem() override;
    // Re-check after the owner invalidated. OnOwnerInvalidated only queues;
    // OnFlushRecheck (called from the host's flush, never inside a client call)
    // releases what is gone, raises ChildrenInvalidated when the item count changed,
    // and queues a property re-check for the cached items a client can see (a
    // TreeItem's ExpandCollapseState, a row's IsSelected), which items cannot get
    // any other way: they have no element of their own to invalidate.
    void OnOwnerInvalidated() override;
    void OnFlushRecheck() override;
    size_t CachedItemCount() const { return items_.size(); }

    // Bound on the cache. Past it, the flush releases all but the focused item and
    // the kKeptOnTrim most recently used ones: a client walking 5000 rows keeps the
    // row it stands on (its next Navigate still works) while the rows it walked
    // past are released, which is what virtualized UIA trees do.
    static constexpr size_t kMaxCachedItems = 1024;
    static constexpr size_t kKeptOnTrim = 64;
    // At most this many cached items are re-checked for property changes per
    // flush: the focused one, then the visible ones.
    static constexpr size_t kMaxRecheckedItems = 64;

protected:
    void ReleaseOwned(IAutomationSite* site) override;

private:
    void ReleaseItem(std::map<size_t, std::unique_ptr<ItemPeer>>::iterator it);
    bool ReleaseAll();
    bool TrimToRecent();
    void QueueItemRechecks(IAutomationSite& site);
    std::map<size_t, std::unique_ptr<ItemPeer>> items_;
    bool trimRequested_ = false;
    // Item count a client last saw (at the first ItemAt, then each flush re-check);
    // SIZE_MAX = no client has enumerated items, so a count change concerns nobody.
    size_t lastCount_ = SIZE_MAX;
    // KeyAt(0) at the last re-check, for stable-key containers: a capped log at its
    // line limit drops one line and adds one per append, so the count never changes
    // while every child shifts.
    size_t lastFirstKey_ = SIZE_MAX;
    // The exposed window at the last re-check: scrolling moves it, which is a
    // structure change for the client (the walk now yields other rows).
    size_t lastBegin_ = SIZE_MAX, lastEnd_ = SIZE_MAX;
    void ExposedWindow(size_t& begin, size_t& end) const;
    uint64_t useClock_ = 0;
};

}  // namespace fluent::automation
