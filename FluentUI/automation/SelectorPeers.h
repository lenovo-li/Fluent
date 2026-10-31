// SelectorPeers.h — peers for controls whose items are not elements: ListBox (List
// of ListItems), TabControl (Tab of TabItems + the selected page), ComboBox
// (ComboBox of ListItems + ExpandCollapse + Value).
//
// The container peer answers the Selection pattern by index; each row is an
// ItemPeer (ItemPeers.h) created only when a client reaches it, keyed by index.
// Items are SelectionItems that select through the control's own selection API,
// so the control raises its usual SelectionChanged and the selection re-check
// (UiaElementProvider.cpp) raises ElementSelected or Selection_Invalidated.
#pragma once

#include "ItemPeers.h"

namespace fluent {
class ListBox;
class TabControl;
class ComboBox;
class TreeView;
}  // namespace fluent

namespace fluent::automation {

// A row of any index-keyed container below. Behaviour comes from the container
// through these hooks, so one class serves ListItem and TabItem.
class SelectableItemPeer : public ItemPeer, public ISelectionItemPeer {
public:
    SelectableItemPeer(ItemsContainerPeer& c, size_t key, ControlType type)
        : ItemPeer(c, key), type_(type) {}
    ControlType GetControlType() const override { return type_; }
    std::wstring GetName() const override;
    RectDip BoundsInHostDip() const override;
    ISelectionItemPeer* AsSelectionItem() override { return this; }
    bool IsSelected() const override;
    void Select() override;
    bool CanAddRemove() const override;
    void AddToSelection() override;
    void RemoveFromSelection() override;
    AutomationPeer* SelectionContainer() const override { return &Container(); }
    bool CanScrollIntoView() const override { return true; }
    void ScrollIntoView() override;

private:
    ControlType type_;
};

// What a selectable container supplies to its items.
class SelectableContainerPeer : public ItemsContainerPeer, public ISelectionPeer {
public:
    explicit SelectableContainerPeer(UIElement& o) : ItemsContainerPeer(o) {}
    ISelectionPeer* AsSelection() override { return this; }
    bool IsControlElement() const override { return true; }
    std::unique_ptr<ItemPeer> MakeItem(size_t key) override;

    virtual ControlType ItemType() const = 0;
    virtual std::wstring ItemName(size_t i) const = 0;
    virtual RectDip ItemRect(size_t i) const = 0;
    virtual void SelectItem(size_t i) = 0;
    virtual void SetItemInSelection(size_t i, bool on) { if (on) SelectItem(i); }
    virtual void ScrollItemIntoView(size_t i) { UNREFERENCED_PARAMETER(i); }
    AutomationPeer* ItemAtPoint(float x, float y) override;
    size_t FocusedIndex() const override { return SelectedIndex(); }

protected:
    // ItemAtPoint receives HOST DIPs; ItemRect and the controls' hit-tests work in
    // the control's own space. This is the inverse of SelectableItemPeer's
    // BoundsInHostDip mapping (Viewbox scaling), so hit-test and bounds agree.
    void HostToOwner(float& x, float& y) const;
};

class ListBoxPeer final : public SelectableContainerPeer, public IScrollPeer {
public:
    explicit ListBoxPeer(ListBox& o);
    ControlType GetControlType() const override { return ControlType::List; }
    size_t ItemCount() const override;
    ControlType ItemType() const override { return ControlType::ListItem; }
    std::wstring ItemName(size_t i) const override;
    RectDip ItemRect(size_t i) const override;
    void SelectItem(size_t i) override;
    void SetItemInSelection(size_t i, bool on) override;
    void ScrollItemIntoView(size_t i) override;
    bool CanSelectMultiple() const override;
    size_t SelectedIndex() const override;
    size_t SelectedCount() const override;
    bool IsIndexSelected(size_t i) const override;
    // Scroll pattern (vertical only).
    IScrollPeer* AsScroll() override { return this; }
    double HorizontalPercent() const override { return -1; }
    double VerticalPercent() const override;
    double HorizontalViewSize() const override { return 100; }
    double VerticalViewSize() const override;
    void SetScrollPercent(double h, double v) override;
    // O(1): row arithmetic, not a scan (a virtualized list may hold 1M rows).
    AutomationPeer* ItemAtPoint(float x, float y) override;
};

class TabControlPeer final : public SelectableContainerPeer {
public:
    explicit TabControlPeer(TabControl& o);
    ControlType GetControlType() const override { return ControlType::Tab; }
    size_t ItemCount() const override;
    ControlType ItemType() const override { return ControlType::TabItem; }
    std::wstring ItemName(size_t i) const override;
    RectDip ItemRect(size_t i) const override;
    void SelectItem(size_t i) override;
    bool CanSelectMultiple() const override { return false; }
    bool IsSelectionRequired() const override { return true; }
    size_t SelectedIndex() const override;
    size_t SelectedCount() const override { return SelectedIndex() < ItemCount() ? 1 : 0; }
    bool IsIndexSelected(size_t i) const override { return i == SelectedIndex(); }
};

// ComboBox: ExpandCollapse (the dropdown), Selection over its items, and Value
// (the shown text; settable only when editable). Items exist whether or not the
// dropdown is open (WPF model); closed, they are offscreen with empty rects.
class ComboBoxPeer final : public SelectableContainerPeer,
                           public IExpandCollapsePeer,
                           public IValuePeer {
public:
    explicit ComboBoxPeer(ComboBox& o);
    ControlType GetControlType() const override { return ControlType::ComboBox; }
    std::wstring GetName() const override;
    size_t ItemCount() const override;
    ControlType ItemType() const override { return ControlType::ListItem; }
    std::wstring ItemName(size_t i) const override;
    RectDip ItemRect(size_t) const override { return {}; }  // popup rows: not mapped yet
    void SelectItem(size_t i) override;
    bool CanSelectMultiple() const override { return false; }
    size_t SelectedIndex() const override;
    size_t SelectedCount() const override { return SelectedIndex() < ItemCount() ? 1 : 0; }
    bool IsIndexSelected(size_t i) const override { return i == SelectedIndex(); }
    AutomationPeer* ItemAtPoint(float, float) override { return nullptr; }
    // Focus stays on the box itself (its popup is WS_EX_NOACTIVATE).
    size_t FocusedIndex() const override { return ItemCount(); }

    IExpandCollapsePeer* AsExpandCollapse() override { return this; }
    ExpandState GetExpandState() const override;
    void Expand() override;
    void Collapse() override;

    IValuePeer* AsValue() override { return this; }
    std::wstring GetValue() const override;
    bool IsReadOnly() const override;
    void SetValue(const std::wstring& v) override;
};

// TreeView: a Tree whose children are its VISIBLE rows, in display order, flat.
// Each item reports ExpandCollapse from its row (LeafNode without children).
// Keyed by the row's stable id, so expanding a node keeps the item peers of rows
// that only moved, and a collapsed-away row is released like a removed one.
class TreeViewPeer final : public SelectableContainerPeer, public IScrollPeer {
public:
    explicit TreeViewPeer(TreeView& o);
    ControlType GetControlType() const override { return ControlType::Tree; }
    size_t ItemCount() const override;
    size_t KeyAt(size_t i) const override;
    size_t IndexOfKey(size_t key) const override;
    std::unique_ptr<ItemPeer> MakeItem(size_t key) override;
    ControlType ItemType() const override { return ControlType::TreeItem; }
    std::wstring ItemName(size_t i) const override;
    RectDip ItemRect(size_t i) const override;
    void SelectItem(size_t i) override;
    void SetItemInSelection(size_t i, bool on) override;
    void ScrollItemIntoView(size_t i) override;
    bool CanSelectMultiple() const override;
    size_t SelectedIndex() const override;
    size_t SelectedCount() const override;
    bool IsIndexSelected(size_t i) const override;
    AutomationPeer* ItemAtPoint(float x, float y) override;
    bool KeysAreStable() const override { return true; }  // row ids
    ExpandState RowExpandState(size_t i) const;
    void SetRowExpanded(size_t i, bool expanded);
    IScrollPeer* AsScroll() override { return this; }
    double HorizontalPercent() const override { return -1; }
    double VerticalPercent() const override;
    double HorizontalViewSize() const override { return 100; }
    double VerticalViewSize() const override;
    void SetScrollPercent(double h, double v) override;
};

class TreeItemPeer final : public SelectableItemPeer, public IExpandCollapsePeer {
public:
    TreeItemPeer(TreeViewPeer& c, size_t key) : SelectableItemPeer(c, key, ControlType::TreeItem) {}
    IExpandCollapsePeer* AsExpandCollapse() override { return this; }
    ExpandState GetExpandState() const override;
    void Expand() override;
    void Collapse() override;
};

}  // namespace fluent::automation
