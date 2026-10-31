// NavRailPeer.cpp — UI Automation for NavRail, written from OUTSIDE the core
// library, the way any extension control adds UIA: override
// CreateAutomationPeer() and return a peer built on the public automation headers
// (SelectorPeers.h supplies the selectable-list machinery).
//
// NavRail is a List of ListItems with ExpandCollapse for the pane width (pane mode
// only; a rail has no pane). Items use the rail's unified index: Items() first,
// then, in pane mode, FooterItems() (Settings is a footer item and must be
// reachable). A pane-mode section header is listed as a Text item that cannot be
// selected, so Narrator reads the section caption in order.
//
// Select goes through PerformSelect, the click's path: it refuses headers and
// disabled items and raises SelectionChanged, which is what makes the app navigate.
// SetSelectedIndex would move the highlight silently and leave the page unchanged.
#include "NavRail.h"

#include "../../FluentUI/automation/SelectorPeers.h"

namespace fluent {
namespace {

class NavRailPeer final : public automation::SelectableContainerPeer,
                          public automation::IExpandCollapsePeer {
public:
    explicit NavRailPeer(NavRail& o) : SelectableContainerPeer(o) {}
    automation::ControlType GetControlType() const override { return automation::ControlType::List; }
    std::wstring GetName() const override {
        const std::wstring& n = Owner().AutomationName();
        return n.empty() ? L"Navigation" : n;
    }
    size_t ItemCount() const override {
        return Rail().Items().size() + (Rail().PaneMode() ? Rail().FooterItems().size() : 0);
    }
    automation::ControlType ItemType() const override { return automation::ControlType::ListItem; }
    std::unique_ptr<automation::ItemPeer> MakeItem(size_t key) override;
    std::wstring ItemName(size_t i) const override {
        const NavRailItem* item = Item(i);
        return item ? item->label : std::wstring();
    }
    bool ItemIsHeader(size_t i) const {
        const NavRailItem* item = Item(i);
        return item && item->header && Rail().PaneMode();
    }
    bool ItemEnabled(size_t i) const { return Rail().IsItemSelectable(static_cast<int>(i)); }
    RectDip ItemRect(size_t i) const override { return Rail().ItemRect(static_cast<int>(i)); }
    void SelectItem(size_t i) override { Rail().PerformSelect(static_cast<int>(i)); }
    bool CanSelectMultiple() const override { return false; }
    size_t SelectedIndex() const override {
        const int s = Rail().SelectedIndex();
        return s < 0 ? ItemCount() : static_cast<size_t>(s);
    }
    size_t SelectedCount() const override { return SelectedIndex() < ItemCount() ? 1 : 0; }
    bool IsIndexSelected(size_t i) const override { return i == SelectedIndex(); }
    AutomationPeer* ItemAtPoint(float x, float y) override {
        HostToOwner(x, y);
        const int i = Rail().HitTestItem(x, y);
        return i >= 0 && static_cast<size_t>(i) < ItemCount() ? ItemAt(static_cast<size_t>(i)) : nullptr;
    }

    automation::IExpandCollapsePeer* AsExpandCollapse() override { return this; }
    // Outside pane mode there is no pane: LeafNode, so Expand/Collapse are refused.
    automation::ExpandState GetExpandState() const override {
        if (!Rail().PaneMode()) return automation::ExpandState::LeafNode;
        return Rail().IsExpanded() ? automation::ExpandState::Expanded : automation::ExpandState::Collapsed;
    }
    // The toggle button's path: animated, raises ExpandedChanged.
    void Expand() override { Rail().PerformSetPaneExpanded(true); }
    void Collapse() override { Rail().PerformSetPaneExpanded(false); }

private:
    NavRail& Rail() const { return static_cast<NavRail&>(Owner()); }
    const NavRailItem* Item(size_t i) const {
        const auto& items = Rail().Items();
        if (i < items.size()) return &items[i];
        const auto& footer = Rail().FooterItems();
        const size_t f = i - items.size();
        return Rail().PaneMode() && f < footer.size() ? &footer[f] : nullptr;
    }
};

// A row: a ListItem, or a Text section header. Disabled and header rows report
// IsEnabled false, so UIA refuses Select before it reaches the rail.
class NavRailItemPeer final : public automation::SelectableItemPeer {
public:
    NavRailItemPeer(NavRailPeer& c, size_t key)
        : SelectableItemPeer(c, key, automation::ControlType::ListItem) {}
    automation::ControlType GetControlType() const override {
        return Rail().ItemIsHeader(Index()) ? automation::ControlType::Text
                                            : automation::ControlType::ListItem;
    }
    bool IsEnabled() const override { return SelectableItemPeer::IsEnabled() && Rail().ItemEnabled(Index()); }

private:
    NavRailPeer& Rail() const { return static_cast<NavRailPeer&>(Container()); }
};

std::unique_ptr<automation::ItemPeer> NavRailPeer::MakeItem(size_t key) {
    return std::make_unique<NavRailItemPeer>(*this, key);
}

}  // namespace

std::unique_ptr<automation::AutomationPeer> NavRail::CreateAutomationPeer() {
    return std::make_unique<NavRailPeer>(*this);
}

}  // namespace fluent
