// AutomationSelectorPeerTests.cpp — item peers for ListBox and TabControl:
// navigation into rows that are not elements, Selection / SelectionItem /
// ScrollItem, the event choice for large selections, and the bounded item cache.
#include "../framework/Test.h"
#include "AutomationTestHelpers.h"

#include "../../FluentUI/automation/AutomationPeer.h"
#include "../../FluentUI/automation/ItemPeers.h"
#include "../../FluentUI/controls/Button.h"
#include "../../FluentUI/controls/ComboBox.h"
#include "../../FluentUI/controls/ListBox.h"
#include "../../FluentUI/controls/TabControl.h"
#include "../../FluentUI/controls/TreeView.h"

using namespace fluent;
using namespace uiatest;

namespace {
ComPtr<IRawElementProviderFragment> First(UiaFixture& fx) {
    return Nav(Nav(fx.Connect().Get(), NavigateDirection_FirstChild).Get(),
               NavigateDirection_FirstChild);
}
ListBox* MakeList(UiaFixture& fx, size_t rows) {
    auto* lb = fx.root->Emplace<ListBox>();
    lb->SetHeight(100);  // ~3 rows of 32
    lb->SetItemCount(rows);
    lb->ItemTextProvider = [](size_t i) { return L"Row " + std::to_wstring(i); };
    return lb;
}
}  // namespace

TEST(AutomationSelector, ListRowsAreListItemsReachedWithoutElements) {
    UiaFixture fx;
    auto* lb = MakeList(fx, 1000000);  // a million rows: nothing per row exists
    fx.Attach();
    fx.root->Measure(400, 300);
    fx.root->Arrange({0, 0, 400, 300});
    auto list = First(fx);
    EXPECT_EQ(PropInt(list.Get(), UIA_ControlTypePropertyId), UIA_ListControlTypeId);
    auto row0 = Nav(list.Get(), NavigateDirection_FirstChild);
    EXPECT_EQ(PropInt(row0.Get(), UIA_ControlTypePropertyId), UIA_ListItemControlTypeId);
    EXPECT_TRUE(PropString(row0.Get(), UIA_NamePropertyId) == L"Row 0");
    auto row1 = Nav(row0.Get(), NavigateDirection_NextSibling);
    EXPECT_TRUE(PropString(row1.Get(), UIA_NamePropertyId) == L"Row 1");
    // Parent of a row is the list; a row's rect lies inside the list.
    EXPECT_TRUE(Nav(row1.Get(), NavigateDirection_Parent).Get() == list.Get());
    UiaRect lr{}, rr{};
    list->get_BoundingRectangle(&lr);
    row1->get_BoundingRectangle(&rr);
    EXPECT_TRUE(rr.height > 0 && rr.top >= lr.top && rr.top + rr.height <= lr.top + lr.height + 0.5);
    // The walk covers the rows near the viewport, not a million: the last child is
    // the end of the exposed window, offscreen (below the viewport) with an empty rect.
    auto* lpeer = static_cast<automation::ItemsContainerPeer*>(lb->AutomationPeerIfAny());
    const size_t end = lpeer ? lpeer->ExposedEnd() : 0;
    EXPECT_TRUE(end > 3 && end <= automation::ItemsContainerPeer::kMaxExposedItems);
    auto last = Nav(list.Get(), NavigateDirection_LastChild);
    EXPECT_TRUE(PropString(last.Get(), UIA_NamePropertyId) == L"Row " + std::to_wstring(end - 1));
    EXPECT_EQ(PropBool(last.Get(), UIA_IsOffscreenPropertyId), 1);
    // Only the rows a client touched have peers.
    auto* peer = static_cast<automation::ItemsContainerPeer*>(lb->AutomationPeerIfAny());
    EXPECT_TRUE(peer && peer->CachedItemCount() <= 3);
}

TEST(AutomationSelector, SelectingARowGoesThroughTheListAndRaisesElementSelected) {
    UiaFixture fx;
    auto* lb = MakeList(fx, 10);
    fx.Attach();
    auto list = First(fx);
    auto row2 = Nav(Nav(Nav(list.Get(), NavigateDirection_FirstChild).Get(),
                        NavigateDirection_NextSibling).Get(),
                    NavigateDirection_NextSibling);
    ComPtr<ISelectionItemProvider> si;
    EXPECT_TRUE(SUCCEEDED(row2.As(&si)));
    EXPECT_EQ(si->Select(), S_OK);
    fx.host.OnFlush();
    EXPECT_EQ(lb->SelectedIndex(), 2);
    BOOL sel = FALSE;
    si->get_IsSelected(&sel);
    EXPECT_TRUE(sel == TRUE);
    fx.host.OnFlush();  // the re-check queued by the selection change
    bool elementSelected = false;
    for (int e : fx.core.events) elementSelected |= e == UIA_SelectionItem_ElementSelectedEventId;
    EXPECT_TRUE(elementSelected);
    // Selection pattern on the list reports the row.
    ComPtr<ISelectionProvider> lsel;
    list.As(&lsel);
    SAFEARRAY* arr = nullptr;
    EXPECT_EQ(lsel->GetSelection(&arr), S_OK);
    LONG ub = -1;
    if (arr) SafeArrayGetUBound(arr, 1, &ub);
    EXPECT_EQ(ub, 0L);  // exactly one
    if (arr) SafeArrayDestroy(arr);
}

TEST(AutomationSelector, HugeMultiSelectionIsOneInvalidatedEventAndACappedArray) {
    UiaFixture fx;
    auto* lb = MakeList(fx, 1000000);
    lb->SetSelectionMode(SelectionMode::Multiple);
    fx.Attach();
    auto list = First(fx);
    fx.core.events.clear();
    lb->SetSelectedIndex(0);
    lb->RangeSelectTo(999999);  // Shift+End over a million rows
    fx.host.OnFlush();
    size_t invalidated = 0, selected = 0;
    for (int e : fx.core.events) {
        invalidated += e == UIA_Selection_InvalidatedEventId;
        selected += e == UIA_SelectionItem_ElementSelectedEventId;
    }
    EXPECT_EQ(invalidated, static_cast<size_t>(1));
    EXPECT_EQ(selected, static_cast<size_t>(0));
    ComPtr<ISelectionProvider> lsel;
    list.As(&lsel);
    SAFEARRAY* arr = nullptr;
    lsel->GetSelection(&arr);
    LONG ub = -1;
    if (arr) SafeArrayGetUBound(arr, 1, &ub);
    EXPECT_EQ(ub + 1, static_cast<LONG>(automation::ISelectionPeer::kMaxReported));
    if (arr) SafeArrayDestroy(arr);
}

TEST(AutomationSelector, ItemCacheIsBoundedAndShrinkReleasesVanishedRows) {
    UiaFixture fx;
    auto* lb = MakeList(fx, 5000);
    fx.Attach();
    auto list = First(fx);
    // Reach every row through the peer (a client reaches far rows by scrolling,
    // selection or focus; the walk itself only covers the exposed window).
    auto* peer = static_cast<automation::ItemsContainerPeer*>(lb->AutomationPeerIfAny());
    if (!peer) return;
    for (size_t i = 0; i < 5000; ++i) peer->ItemAt(i);
    // Reaching rows never evicts (a client call may be running on the evicted
    // item); the trim it requested runs in the next flush.
    EXPECT_TRUE(fx.core.flushPosts > 0);
    fx.host.OnFlush();
    EXPECT_TRUE(peer->CachedItemCount() <= automation::ItemsContainerPeer::kMaxCachedItems);
    // Shrink: rows past the new count vanish and their providers are disconnected.
    ComPtr<IRawElementProviderFragment> row4999;
    peer->ItemAt(4999)->Provider()->QueryInterface(IID_PPV_ARGS(&row4999));
    lb->SetItemCount(10);
    fx.host.OnFlush();
    EXPECT_TRUE(PropString(row4999.Get(), UIA_NamePropertyId) == L"<error>");
    EXPECT_TRUE(peer->CachedItemCount() <= 10);
}

TEST(AutomationSelector, TabItemsThenTheSelectedPage) {
    UiaFixture fx;
    auto* tc = fx.root->Emplace<TabControl>();
    tc->AddTab(L"General", std::make_unique<Button>())->SetText(L"OnGeneral");
    tc->AddTab(L"Advanced", std::make_unique<Button>())->SetText(L"OnAdvanced");
    fx.Attach();
    auto tab = First(fx);
    EXPECT_EQ(PropInt(tab.Get(), UIA_ControlTypePropertyId), UIA_TabControlTypeId);
    auto t0 = Nav(tab.Get(), NavigateDirection_FirstChild);
    auto t1 = Nav(t0.Get(), NavigateDirection_NextSibling);
    auto page = Nav(t1.Get(), NavigateDirection_NextSibling);
    EXPECT_EQ(PropInt(t0.Get(), UIA_ControlTypePropertyId), UIA_TabItemControlTypeId);
    EXPECT_TRUE(PropString(t1.Get(), UIA_NamePropertyId) == L"Advanced");
    EXPECT_TRUE(PropString(page.Get(), UIA_NamePropertyId) == L"OnGeneral");
    // Back from the page lands on the last tab item.
    EXPECT_TRUE(Nav(page.Get(), NavigateDirection_PreviousSibling).Get() == t1.Get());
    ComPtr<ISelectionItemProvider> si;
    t1.As(&si);
    si->Select();
    fx.host.OnFlush();
    EXPECT_EQ(tc->SelectedIndex(), 1);
    auto newPage = Nav(tab.Get(), NavigateDirection_LastChild);
    EXPECT_TRUE(PropString(newPage.Get(), UIA_NamePropertyId) == L"OnAdvanced");
}

TEST(AutomationSelector, ComboBoxExposesItemsSelectionAndExpandState) {
    UiaFixture fx;
    auto* cb = fx.root->Emplace<ComboBox>();
    cb->SetItems({L"Red", L"Green", L"Blue"});
    cb->SetAutomationName(L"Color");
    fx.Attach();
    auto box = First(fx);
    EXPECT_EQ(PropInt(box.Get(), UIA_ControlTypePropertyId), UIA_ComboBoxControlTypeId);
    EXPECT_TRUE(PropString(box.Get(), UIA_NamePropertyId) == L"Color");
    ComPtr<IExpandCollapseProvider> ec;
    EXPECT_TRUE(SUCCEEDED(box.As(&ec)));
    ExpandCollapseState st{};
    ec->get_ExpandCollapseState(&st);
    EXPECT_EQ(st, ExpandCollapseState_Collapsed);
    // Items exist while closed (WPF model), offscreen.
    auto blue = Nav(box.Get(), NavigateDirection_LastChild);
    EXPECT_TRUE(PropString(blue.Get(), UIA_NamePropertyId) == L"Blue");
    EXPECT_EQ(PropBool(blue.Get(), UIA_IsOffscreenPropertyId), 1);
    ComPtr<ISelectionItemProvider> si;
    blue.As(&si);
    si->Select();
    fx.host.OnFlush();
    EXPECT_EQ(cb->SelectedIndex(), 2);
    ComPtr<IValueProvider> val;
    box.As(&val);
    BSTR v = nullptr;
    val->get_Value(&v);
    EXPECT_TRUE(v && wcscmp(v, L"Blue") == 0);
    SysFreeString(v);
    BOOL ro = FALSE;
    val->get_IsReadOnly(&ro);
    EXPECT_TRUE(ro == TRUE);  // not editable
}

TEST(AutomationSelector, TreeItemsAreVisibleRowsWithExpandCollapse) {
    UiaFixture fx;
    auto* tv = fx.root->Emplace<TreeView>();
    tv->SetHeight(200);
    TreeViewRow a;
    a.id = 1;
    a.text = L"Root";
    a.expanded = false;
    tv->AddRoot(a);
    TreeViewRow c;
    c.id = 2;
    c.text = L"Child";
    tv->AddChild(1, c);
    TreeViewRow b;
    b.id = 3;
    b.text = L"Other";
    tv->AddRoot(b);
    fx.Attach();
    fx.root->Measure(400, 300);
    fx.root->Arrange({0, 0, 400, 300});
    auto tree = First(fx);
    EXPECT_EQ(PropInt(tree.Get(), UIA_ControlTypePropertyId), UIA_TreeControlTypeId);
    auto root = Nav(tree.Get(), NavigateDirection_FirstChild);
    auto other = Nav(root.Get(), NavigateDirection_NextSibling);
    EXPECT_TRUE(PropString(root.Get(), UIA_NamePropertyId) == L"Root");
    EXPECT_TRUE(PropString(other.Get(), UIA_NamePropertyId) == L"Other");  // child hidden
    EXPECT_TRUE(root && other);
    if (!root || !other) return;  // navigation broken: the rest would dereference null
    ComPtr<IExpandCollapseProvider> ec, leaf;
    root.As(&ec);
    other.As(&leaf);
    ExpandCollapseState st{};
    ec->get_ExpandCollapseState(&st);
    EXPECT_EQ(st, ExpandCollapseState_Collapsed);
    leaf->get_ExpandCollapseState(&st);
    EXPECT_EQ(st, ExpandCollapseState_LeafNode);
    EXPECT_EQ(leaf->Expand(), UIA_E_INVALIDOPERATION);

    EXPECT_EQ(ec->Expand(), S_OK);
    fx.host.OnFlush();
    ec->get_ExpandCollapseState(&st);
    EXPECT_EQ(st, ExpandCollapseState_Expanded);
    // Child now between Root and Other; Other kept its provider (id-keyed).
    auto child = Nav(root.Get(), NavigateDirection_NextSibling);
    EXPECT_TRUE(PropString(child.Get(), UIA_NamePropertyId) == L"Child");
    if (!child) return;
    EXPECT_TRUE(Nav(child.Get(), NavigateDirection_NextSibling).Get() == other.Get());
    EXPECT_TRUE(PropString(other.Get(), UIA_NamePropertyId) == L"Other");

    // Collapse again: Child's item is released (frozen), Other still valid.
    ec->Collapse();
    fx.host.OnFlush();
    fx.host.OnFlush();
    EXPECT_TRUE(PropString(child.Get(), UIA_NamePropertyId) == L"<error>");
    EXPECT_TRUE(PropString(other.Get(), UIA_NamePropertyId) == L"Other");

    // Select by item, reported back by visible position.
    ComPtr<ISelectionItemProvider> si;
    other.As(&si);
    EXPECT_TRUE(si != nullptr);
    if (si) si->Select();
    fx.host.OnFlush();
    EXPECT_TRUE(tv->SelectedRow() && tv->SelectedRow()->id == 3);
}

TEST(AutomationSelector, HitTestOnAListRowReturnsTheRowNotTheList) {
    UiaFixture fx;
    MakeList(fx, 50);
    fx.Attach();
    fx.root->Measure(400, 300);
    fx.root->Arrange({0, 0, 400, 300});
    auto rootFrag = fx.Connect();
    auto list = Nav(Nav(rootFrag.Get(), NavigateDirection_FirstChild).Get(), NavigateDirection_FirstChild);
    UiaRect lr{};
    list->get_BoundingRectangle(&lr);
    ComPtr<IRawElementProviderFragmentRoot> fr;
    rootFrag.As(&fr);
    ComPtr<IRawElementProviderFragment> hit;
    // 40 DIP into the list = row 1 (32-DIP rows).
    EXPECT_EQ(fr->ElementProviderFromPoint(lr.left + 10, lr.top + 40, &hit), S_OK);
    EXPECT_EQ(PropInt(hit.Get(), UIA_ControlTypePropertyId), UIA_ListItemControlTypeId);
    EXPECT_TRUE(PropString(hit.Get(), UIA_NamePropertyId) == L"Row 1");
}

TEST(AutomationSelector, ActionsOnAGoneItemDoNothing) {
    // A row collapsed away / removed keeps its provider until the flush releases it;
    // in between, its index is ItemCount() and must never reach the control.
    UiaFixture fx;
    auto* lb = MakeList(fx, 5);
    fx.Attach();
    auto last = Nav(First(fx).Get(), NavigateDirection_LastChild);
    ComPtr<ISelectionItemProvider> si;
    last.As(&si);
    lb->SetItemCount(2);  // row 4 is gone, not yet pruned
    // The provider is still connected until the flush prunes it, but the item is
    // gone: refused at the call, and nothing is posted.
    EXPECT_EQ(si->Select(), UIA_E_ELEMENTNOTENABLED);
    fx.host.OnFlush();
    EXPECT_EQ(lb->SelectedIndex(), -1);
    // After the flush the provider is frozen.
    EXPECT_EQ(si->Select(), UIA_E_ELEMENTNOTAVAILABLE);
}
