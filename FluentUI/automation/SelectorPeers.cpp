// SelectorPeers.cpp — see SelectorPeers.h.
#include "SelectorPeers.h"

#include <algorithm>

#include "../controls/ComboBox.h"
#include "../controls/ListBox.h"
#include "../controls/TabControl.h"
#include "../controls/TreeView.h"

namespace fluent::automation {

namespace {
SelectableContainerPeer& C(const ItemPeer& p) {
    return static_cast<SelectableContainerPeer&>(p.Container());
}
}  // namespace

// --- Item -----------------------------------------------------------------------
std::wstring SelectableItemPeer::GetName() const { return C(*this).ItemName(Index()); }
RectDip SelectableItemPeer::BoundsInHostDip() const {
    // The control's rect is in its own coordinate space; map it through the
    // owner's ancestors exactly like the owner's own bounds (Viewbox etc.).
    const RectDip local = C(*this).ItemRect(Index());
    if (local.isEmpty()) return {};
    const RectDip owner = Owner().Bounds();
    const RectDip mapped = Owner().BoundsInHostDip();
    if (owner.w <= 0 || owner.h <= 0) return local;
    const float sx = mapped.w / owner.w, sy = mapped.h / owner.h;
    return {mapped.x + (local.x - owner.x) * sx, mapped.y + (local.y - owner.y) * sy,
            local.w * sx, local.h * sy};
}
bool SelectableItemPeer::IsSelected() const { return C(*this).IsIndexSelected(Index()); }
void SelectableItemPeer::Select() { C(*this).SelectItem(Index()); }
bool SelectableItemPeer::CanAddRemove() const { return C(*this).CanSelectMultiple(); }
void SelectableItemPeer::AddToSelection() { C(*this).SetItemInSelection(Index(), true); }
void SelectableItemPeer::RemoveFromSelection() { C(*this).SetItemInSelection(Index(), false); }
void SelectableItemPeer::ScrollIntoView() { C(*this).ScrollItemIntoView(Index()); }

// --- Container --------------------------------------------------------------------
std::unique_ptr<ItemPeer> SelectableContainerPeer::MakeItem(size_t key) {
    return std::make_unique<SelectableItemPeer>(*this, key, ItemType());
}
void SelectableContainerPeer::HostToOwner(float& x, float& y) const {
    const RectDip owner = Owner().Bounds();
    const RectDip mapped = Owner().BoundsInHostDip();
    if (owner.w <= 0 || owner.h <= 0 || mapped.w <= 0 || mapped.h <= 0) return;
    x = owner.x + (x - mapped.x) * owner.w / mapped.w;
    y = owner.y + (y - mapped.y) * owner.h / mapped.h;
}
AutomationPeer* SelectableContainerPeer::ItemAtPoint(float x, float y) {
    // O(visible) for a list: rows outside the viewport have empty rects, but a
    // linear scan over a 1M-row list would still be O(n), so containers with many
    // items override with arithmetic (ListBoxPeer).
    HostToOwner(x, y);
    for (size_t i = 0, n = ItemCount(); i < n && i < 512; ++i)
        if (ItemRect(i).contains(x, y)) return ItemAt(i);
    return nullptr;
}

// --- ListBox ------------------------------------------------------------------------
namespace {
ListBox& LB(const AutomationPeer& p) { return static_cast<ListBox&>(p.Owner()); }
}  // namespace
ListBoxPeer::ListBoxPeer(ListBox& o) : SelectableContainerPeer(o) {}
size_t ListBoxPeer::ItemCount() const { return static_cast<size_t>(std::max(0, LB(*this).ItemCount())); }
std::wstring ListBoxPeer::ItemName(size_t i) const { return LB(*this).GetItemText(static_cast<int>(i)); }
RectDip ListBoxPeer::ItemRect(size_t i) const { return LB(*this).ItemRect(static_cast<int>(i)); }
void ListBoxPeer::SelectItem(size_t i) { LB(*this).SetSelectedIndex(static_cast<int>(i)); }
void ListBoxPeer::SetItemInSelection(size_t i, bool on) {
    ListBox& lb = LB(*this);
    if (lb.GetSelectionMode() != SelectionMode::Multiple) {
        if (on) lb.SetSelectedIndex(static_cast<int>(i));
        return;
    }
    if (lb.IsSelected(static_cast<int>(i)) != on) lb.ToggleSelection(static_cast<int>(i));
}
AutomationPeer* ListBoxPeer::ItemAtPoint(float x, float y) {
    HostToOwner(x, y);
    const int i = LB(*this).ItemIndexAt(y);
    return i >= 0 ? ItemAt(static_cast<size_t>(i)) : nullptr;
}
void ListBoxPeer::ScrollItemIntoView(size_t i) { LB(*this).ScrollItemIntoView(static_cast<int>(i)); }
bool ListBoxPeer::CanSelectMultiple() const { return LB(*this).GetSelectionMode() == SelectionMode::Multiple; }
size_t ListBoxPeer::SelectedIndex() const {
    const int s = LB(*this).SelectedIndex();
    return s < 0 ? ItemCount() : static_cast<size_t>(s);
}
size_t ListBoxPeer::SelectedCount() const { return LB(*this).SelectedCount(); }
bool ListBoxPeer::IsIndexSelected(size_t i) const { return LB(*this).IsSelected(static_cast<int>(i)); }
double ListBoxPeer::VerticalPercent() const {
    const ScrollViewer& s = LB(*this).Scroller();
    return s.MaxOffset() > 0 ? std::clamp(100.0 * s.Offset() / s.MaxOffset(), 0.0, 100.0) : -1.0;
}
double ListBoxPeer::VerticalViewSize() const {
    const ScrollViewer& s = LB(*this).Scroller();
    const double vp = LB(*this).Bounds().h, ext = vp + s.MaxOffset();
    return ext > 0 ? std::clamp(100.0 * vp / ext, 0.0, 100.0) : 100.0;
}
void ListBoxPeer::SetScrollPercent(double, double v) {
    ListBox& lb = LB(*this);
    if (v >= 0 && lb.Scroller().MaxOffset() > 0)
        lb.SetScrollOffset(static_cast<float>(v / 100.0 * lb.Scroller().MaxOffset()));
}

// --- TabControl -----------------------------------------------------------------------
namespace {
TabControl& TC(const AutomationPeer& p) { return static_cast<TabControl&>(p.Owner()); }
}  // namespace
TabControlPeer::TabControlPeer(TabControl& o) : SelectableContainerPeer(o) {}
size_t TabControlPeer::ItemCount() const { return static_cast<size_t>(std::max(0, TC(*this).TabCount())); }
std::wstring TabControlPeer::ItemName(size_t i) const { return TC(*this).HeaderAt(static_cast<int>(i)); }
RectDip TabControlPeer::ItemRect(size_t i) const { return TC(*this).HeaderRect(static_cast<int>(i)); }
void TabControlPeer::SelectItem(size_t i) { TC(*this).SetSelectedIndex(static_cast<int>(i)); }
size_t TabControlPeer::SelectedIndex() const {
    const int s = TC(*this).SelectedIndex();
    return s < 0 ? ItemCount() : static_cast<size_t>(s);
}

// --- ComboBox -------------------------------------------------------------------------
namespace {
ComboBox& CB(const AutomationPeer& p) { return static_cast<ComboBox&>(p.Owner()); }
}  // namespace
ComboBoxPeer::ComboBoxPeer(ComboBox& o) : SelectableContainerPeer(o) {}
std::wstring ComboBoxPeer::GetName() const { return Owner().AutomationName(); }
size_t ComboBoxPeer::ItemCount() const { return static_cast<size_t>(std::max(0, CB(*this).ItemCount())); }
std::wstring ComboBoxPeer::ItemName(size_t i) const {
    const auto& items = CB(*this).Items();
    return i < items.size() ? items[i] : std::wstring();
}
void ComboBoxPeer::SelectItem(size_t i) { CB(*this).SetSelectedIndex(static_cast<int>(i)); }
size_t ComboBoxPeer::SelectedIndex() const {
    const int s = CB(*this).SelectedIndex();
    return s < 0 ? ItemCount() : static_cast<size_t>(s);
}
ExpandState ComboBoxPeer::GetExpandState() const {
    return CB(*this).IsDropDownOpen() ? ExpandState::Expanded : ExpandState::Collapsed;
}
void ComboBoxPeer::Expand() { CB(*this).SetDropDownOpen(true); }
void ComboBoxPeer::Collapse() { CB(*this).SetDropDownOpen(false); }
std::wstring ComboBoxPeer::GetValue() const { return CB(*this).Text(); }
bool ComboBoxPeer::IsReadOnly() const { return !CB(*this).IsEditable(); }
// Typing's path, not SetText: TextChanged must reach the app's bound model.
void ComboBoxPeer::SetValue(const std::wstring& v) {
    if (CB(*this).IsEditable()) CB(*this).ReplaceTextAsUserEdit(v);
}

// --- TreeView -------------------------------------------------------------------------
namespace {
TreeView& TV(const AutomationPeer& p) { return static_cast<TreeView&>(p.Owner()); }
const TreeViewRow* RowAt(const TreeView& t, size_t v) {
    const int r = t.RowIndexAtVisible(static_cast<int>(v));
    return r >= 0 ? &t.Items()[static_cast<size_t>(r)] : nullptr;
}
}  // namespace
TreeViewPeer::TreeViewPeer(TreeView& o) : SelectableContainerPeer(o) {}
size_t TreeViewPeer::ItemCount() const { return static_cast<size_t>(TV(*this).VisibleRowCount()); }
size_t TreeViewPeer::KeyAt(size_t i) const {
    const TreeViewRow* r = RowAt(TV(*this), i);
    return r ? static_cast<size_t>(static_cast<unsigned>(r->id)) : SIZE_MAX;
}
size_t TreeViewPeer::IndexOfKey(size_t key) const {
    const int v = TV(*this).VisibleIndexOfId(static_cast<int>(key));
    return v < 0 ? ItemCount() : static_cast<size_t>(v);  // hidden or removed: gone
}
std::unique_ptr<ItemPeer> TreeViewPeer::MakeItem(size_t key) {
    return std::make_unique<TreeItemPeer>(*this, key);
}
std::wstring TreeViewPeer::ItemName(size_t i) const {
    const TreeViewRow* r = RowAt(TV(*this), i);
    return r ? r->text : std::wstring();
}
RectDip TreeViewPeer::ItemRect(size_t i) const { return TV(*this).VisibleRowRect(static_cast<int>(i)); }
void TreeViewPeer::SelectItem(size_t i) { TV(*this).SetSelectedIndex(static_cast<int>(i)); }
void TreeViewPeer::SetItemInSelection(size_t i, bool on) {
    TreeView& t = TV(*this);
    const int r = t.RowIndexAtVisible(static_cast<int>(i));
    if (r < 0) return;
    if (t.GetSelectionMode() != SelectionMode::Multiple) {
        if (on) t.SetSelectedIndex(static_cast<int>(i));
        return;
    }
    if (t.IsRowSelected(r) != on) t.ToggleRowSelection(r);
}
void TreeViewPeer::ScrollItemIntoView(size_t i) { TV(*this).ScrollVisibleRowIntoView(static_cast<int>(i)); }
bool TreeViewPeer::CanSelectMultiple() const {
    return TV(*this).GetSelectionMode() == SelectionMode::Multiple;
}
size_t TreeViewPeer::SelectedIndex() const {
    // Selector::SelectedIndex is a ROW index; items are VISIBLE positions.
    const TreeViewRow* row = TV(*this).SelectedRow();
    const int v = row ? TV(*this).VisibleIndexOfId(row->id) : -1;
    return v < 0 ? ItemCount() : static_cast<size_t>(v);
}
size_t TreeViewPeer::SelectedCount() const { return TV(*this).SelectedCount(); }
bool TreeViewPeer::IsIndexSelected(size_t i) const {
    const int r = TV(*this).RowIndexAtVisible(static_cast<int>(i));
    return r >= 0 && TV(*this).IsRowSelected(r);
}
AutomationPeer* TreeViewPeer::ItemAtPoint(float x, float y) {
    HostToOwner(x, y);
    const int v = TV(*this).VisibleRowAt(y);
    return v >= 0 ? ItemAt(static_cast<size_t>(v)) : nullptr;
}
ExpandState TreeViewPeer::RowExpandState(size_t i) const {
    const TreeViewRow* r = RowAt(TV(*this), i);
    if (!r || !r->hasChildren) return ExpandState::LeafNode;
    return r->expanded ? ExpandState::Expanded : ExpandState::Collapsed;
}
void TreeViewPeer::SetRowExpanded(size_t i, bool expanded) {
    if (const TreeViewRow* r = RowAt(TV(*this), i)) TV(*this).SetNodeExpanded(r->id, expanded);
}
double TreeViewPeer::VerticalPercent() const {
    const ScrollViewer& s = TV(*this).Scroller();
    return s.MaxOffset() > 0 ? std::clamp(100.0 * s.Offset() / s.MaxOffset(), 0.0, 100.0) : -1.0;
}
double TreeViewPeer::VerticalViewSize() const {
    const ScrollViewer& s = TV(*this).Scroller();
    const double vp = TV(*this).Bounds().h, ext = vp + s.MaxOffset();
    return ext > 0 ? std::clamp(100.0 * vp / ext, 0.0, 100.0) : 100.0;
}
void TreeViewPeer::SetScrollPercent(double, double v) {
    TreeView& t = TV(*this);
    if (v >= 0 && t.Scroller().MaxOffset() > 0)
        t.SetScrollOffset(static_cast<float>(v / 100.0 * t.Scroller().MaxOffset()));
}
ExpandState TreeItemPeer::GetExpandState() const {
    return static_cast<TreeViewPeer&>(Container()).RowExpandState(Index());
}
void TreeItemPeer::Expand() { static_cast<TreeViewPeer&>(Container()).SetRowExpanded(Index(), true); }
void TreeItemPeer::Collapse() { static_cast<TreeViewPeer&>(Container()).SetRowExpanded(Index(), false); }

}  // namespace fluent::automation

namespace fluent {
std::unique_ptr<automation::AutomationPeer> TreeView::CreateAutomationPeer() {
    return std::make_unique<automation::TreeViewPeer>(*this);
}

std::unique_ptr<automation::AutomationPeer> ComboBox::CreateAutomationPeer() {
    return std::make_unique<automation::ComboBoxPeer>(*this);
}
std::unique_ptr<automation::AutomationPeer> ListBox::CreateAutomationPeer() {
    return std::make_unique<automation::ListBoxPeer>(*this);
}
std::unique_ptr<automation::AutomationPeer> TabControl::CreateAutomationPeer() {
    return std::make_unique<automation::TabControlPeer>(*this);
}
}  // namespace fluent
