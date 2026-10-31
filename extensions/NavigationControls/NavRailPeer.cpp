// NavRailPeer.cpp — UI Automation for NavRail, written from OUTSIDE the core
// library, the way any extension control adds UIA: override
// CreateAutomationPeer() and return a peer built on the public automation headers
// (SelectorPeers.h supplies the selectable-list machinery).
//
// NavRail is a List of ListItems (its navigation items; section headers are not
// items) with ExpandCollapse for the pane width. Selecting an item goes through
// SetSelectedIndex, the control's own selection path.
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
    size_t ItemCount() const override { return Rail().Items().size(); }
    automation::ControlType ItemType() const override { return automation::ControlType::ListItem; }
    std::wstring ItemName(size_t i) const override {
        const auto& items = Rail().Items();
        return i < items.size() ? items[i].label : std::wstring();
    }
    RectDip ItemRect(size_t i) const override { return Rail().ItemRect(static_cast<int>(i)); }
    void SelectItem(size_t i) override { Rail().SetSelectedIndex(static_cast<int>(i)); }
    bool CanSelectMultiple() const override { return false; }
    size_t SelectedIndex() const override {
        const int s = Rail().SelectedIndex();
        return s < 0 ? ItemCount() : static_cast<size_t>(s);
    }
    size_t SelectedCount() const override { return SelectedIndex() < ItemCount() ? 1 : 0; }
    bool IsIndexSelected(size_t i) const override { return i == SelectedIndex(); }
    AutomationPeer* ItemAtPoint(float x, float y) override {
        const int i = Rail().HitTestItem(x, y);
        return i >= 0 && static_cast<size_t>(i) < ItemCount() ? ItemAt(static_cast<size_t>(i)) : nullptr;
    }

    automation::IExpandCollapsePeer* AsExpandCollapse() override { return this; }
    automation::ExpandState GetExpandState() const override {
        return Rail().IsExpanded() ? automation::ExpandState::Expanded : automation::ExpandState::Collapsed;
    }
    void Expand() override { Rail().SetExpanded(true); }
    void Collapse() override { Rail().SetExpanded(false); }

private:
    NavRail& Rail() const { return static_cast<NavRail&>(Owner()); }
};

}  // namespace

std::unique_ptr<automation::AutomationPeer> NavRail::CreateAutomationPeer() {
    return std::make_unique<NavRailPeer>(*this);
}

}  // namespace fluent
