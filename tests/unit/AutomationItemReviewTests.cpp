// AutomationItemReviewTests.cpp — UIA actions that must take the USER's path, and
// the events a client must receive when items (rows that are not elements) change.
//
// The rule behind the first group: a programmatic setter in this framework is
// often deliberately silent (NavRail::SetSelectedIndex, Expander::SetExpanded,
// ComboBox::SetText raise nothing), because it is meant for state restore. A UIA
// action is a user acting through an assistive tool, so it must raise the same
// event the click or keystroke raises, or the app never reacts (the page does not
// switch, lazily loaded content never loads, a bound model goes stale). Each test
// therefore subscribes to the app-visible event instead of reading the stored
// value back, which would pass either way.
//
// The second group pins what a client is told about items: ChildrenInvalidated
// when rows are added (not only removed), property changes on the rows themselves
// (a tree row's ExpandCollapseState, a list row's IsSelected), hit-testing through
// a scaled Viewbox, and the cache's behavior under a long walk and after the client
// has gone.
#include "../framework/Test.h"
#include "AutomationTestHelpers.h"

#include "../../FluentUI/automation/AutomationPeer.h"
#include "../../FluentUI/automation/ItemPeers.h"
#include "../../FluentUI/controls/ComboBox.h"
#include "../../FluentUI/controls/Expander.h"
#include "../../FluentUI/controls/ListBox.h"
#include "../../FluentUI/controls/TextBlock.h"
#include "../../FluentUI/controls/TreeView.h"
#include "../../FluentUI/layout/Viewbox.h"
#include "../../extensions/NavigationControls/NavRail.h"

using namespace fluent;
using namespace uiatest;

namespace {
ComPtr<IRawElementProviderFragment> First(UiaFixture& fx) {
    return Nav(Nav(fx.Connect().Get(), NavigateDirection_FirstChild).Get(),
               NavigateDirection_FirstChild);
}
ComPtr<IRawElementProviderFragment> Child(IRawElementProviderFragment* parent, int n) {
    auto c = Nav(parent, NavigateDirection_FirstChild);
    for (int i = 0; i < n && c; ++i) c = Nav(c.Get(), NavigateDirection_NextSibling);
    return c;
}
int Count(const std::vector<int>& v, int id) {
    int n = 0;
    for (int e : v) n += e == id;
    return n;
}
bool HasProp(const RecordingCore& core, int id, const wchar_t* value) {
    for (const auto& p : core.props)
        if (p.first == id && p.second == value) return true;
    return false;
}
void Layout(UiaFixture& fx) {
    fx.root->Measure(400, 300);
    fx.root->Arrange({0, 0, 400, 300});
}
ListBox* MakeList(UiaFixture& fx, size_t rows) {
    auto* lb = fx.root->Emplace<ListBox>();
    lb->SetHeight(100);
    lb->SetItemCount(rows);
    lb->ItemTextProvider = [](size_t i) { return L"Row " + std::to_wstring(i); };
    return lb;
}
}  // namespace

// --- Actions on the user's path ---------------------------------------------------

TEST(AutomationItemReview, NavRailSelectRaisesSelectionChangedAndRefusesHeaders) {
    UiaFixture fx;
    auto* rail = fx.root->Emplace<NavRail>();
    rail->SetPaneMode(true);
    NavRailItem header{L"", L"Section"};
    header.header = true;
    NavRailItem off{L"\xE897", L"Locked"};
    off.enabled = false;
    rail->SetItems({{L"\xE80F", L"Home"}, header, off});
    rail->SetFooterItems({{L"\xE713", L"Settings"}});
    std::vector<int> seen;
    auto sub = rail->SelectionChanged().Subscribe(&seen, [](void* o, NavRail&, int& i) {
        static_cast<std::vector<int>*>(o)->push_back(i);
    });
    fx.Attach();
    Layout(fx);
    auto list = First(fx);
    // Header is a Text item, not a selectable ListItem.
    auto hdr = Child(list.Get(), 1);
    EXPECT_EQ(PropInt(hdr.Get(), UIA_ControlTypePropertyId), UIA_TextControlTypeId);
    EXPECT_EQ(PropBool(hdr.Get(), UIA_IsEnabledPropertyId), 0);
    // Footer (Settings) is reachable, at unified index Items().size().
    auto settings = Child(list.Get(), 3);
    EXPECT_TRUE(PropString(settings.Get(), UIA_NamePropertyId) == L"Settings");
    if (!settings || !hdr) return;

    ComPtr<ISelectionItemProvider> si;
    settings.As(&si);
    EXPECT_EQ(si->Select(), S_OK);
    fx.host.OnFlush();
    EXPECT_EQ(rail->SelectedIndex(), 3);
    EXPECT_EQ(seen.size(), size_t{1});  // the app navigates
    // Selected footer item is reported by the Selection pattern.
    ComPtr<ISelectionProvider> sel;
    list.As(&sel);
    SAFEARRAY* arr = nullptr;
    sel->GetSelection(&arr);
    LONG ub = -1;
    if (arr) SafeArrayGetUBound(arr, 1, &ub);
    EXPECT_EQ(ub, 0L);
    if (arr) SafeArrayDestroy(arr);

    // Header and disabled rows: refused, nothing raised.
    ComPtr<ISelectionItemProvider> hsi, dsi;
    hdr.As(&hsi);
    Child(list.Get(), 2).As(&dsi);
    if (hsi) hsi->Select();
    if (dsi) dsi->Select();
    fx.host.OnFlush();
    EXPECT_EQ(rail->SelectedIndex(), 3);
    EXPECT_EQ(seen.size(), size_t{1});
}

TEST(AutomationItemReview, NavRailPaneExpandRaisesExpandedChangedAndRailModeIsLeaf) {
    UiaFixture fx;
    auto* rail = fx.root->Emplace<NavRail>();
    rail->SetItems({{L"\xE80F", L"Home"}});
    int changes = 0;
    auto sub = rail->ExpandedChanged().Subscribe(&changes, [](void* o, NavRail&, bool&) {
        ++*static_cast<int*>(o);
    });
    fx.Attach();
    auto list = First(fx);
    ComPtr<IExpandCollapseProvider> ec;
    list.As(&ec);
    if (!ec) return;
    ExpandCollapseState st{};
    ec->get_ExpandCollapseState(&st);
    EXPECT_EQ(st, ExpandCollapseState_LeafNode);  // a rail has no pane
    EXPECT_EQ(ec->Expand(), UIA_E_INVALIDOPERATION);

    rail->SetPaneMode(true);
    rail->SetExpanded(false, false);
    EXPECT_EQ(ec->Expand(), S_OK);
    fx.host.OnFlush();
    EXPECT_TRUE(rail->IsExpanded());
    EXPECT_EQ(changes, 1);
}

TEST(AutomationItemReview, ExpanderExpandRaisesExpandedChangedWithUserTransition) {
    UiaFixture fx;
    auto* ex = fx.root->Emplace<Expander>();
    ex->SetHeader(L"More");
    ex->SetContent(std::make_unique<TextBlock>())->SetText(L"body");
    std::vector<bool> seen;
    auto sub = ex->ExpandedChanged().Subscribe(&seen, [](void* o, Expander&, bool& e) {
        static_cast<std::vector<bool>*>(o)->push_back(e);
    });
    fx.Attach();
    ComPtr<IExpandCollapseProvider> ec;
    First(fx).As(&ec);
    if (!ec) return;
    ec->Expand();
    fx.host.OnFlush();
    ec->Collapse();
    fx.host.OnFlush();
    EXPECT_EQ(seen.size(), size_t{2});
    EXPECT_TRUE(seen.size() == 2 && seen[0] && !seen[1]);
}

TEST(AutomationItemReview, EditableComboBoxSetValueRaisesTextChanged) {
    UiaFixture fx;
    auto* cb = fx.root->Emplace<ComboBox>();
    cb->SetEditable(true);
    cb->SetItems({L"Red", L"Green"});
    std::wstring seen;
    auto sub = cb->TextChanged().Subscribe(&seen, [](void* o, ComboBox&, std::wstring& t) {
        *static_cast<std::wstring*>(o) = t;
    });
    fx.Attach();
    ComPtr<IValueProvider> val;
    First(fx).As(&val);
    if (!val) return;
    EXPECT_EQ(val->SetValue(L"Teal"), S_OK);
    fx.host.OnFlush();
    EXPECT_TRUE(cb->Text() == L"Teal");
    EXPECT_TRUE(seen == L"Teal");
}

// --- What a client is told about items ---------------------------------------------

TEST(AutomationItemReview, AppendedRowsRaiseChildrenInvalidatedAndDropShiftedItems) {
    UiaFixture fx;
    auto* lb = MakeList(fx, 10);
    fx.Attach();
    Layout(fx);
    auto list = First(fx);
    auto row5 = Child(list.Get(), 5);
    EXPECT_TRUE(PropString(row5.Get(), UIA_NamePropertyId) == L"Row 5");
    fx.core.events.clear();
    lb->SetItemCount(20);  // rows added, none removed
    fx.host.OnFlush();
    fx.host.OnFlush();
    EXPECT_TRUE(Count(fx.core.events, RecordingCore::kChildrenInvalidated) >= 1);
    // An index-keyed item may now name another row: it is released, not left lying.
    EXPECT_TRUE(PropString(row5.Get(), UIA_NamePropertyId) == L"<error>");
}

TEST(AutomationItemReview, TreeExpandRaisesChildrenInvalidatedAndItemExpandState) {
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
    fx.Attach();
    Layout(fx);
    auto root = Child(First(fx).Get(), 0);
    if (!root) return;
    fx.core.events.clear();
    fx.core.props.clear();
    tv->SetNodeExpanded(1, true);  // keyboard / app path, not UIA
    fx.host.OnFlush();
    fx.host.OnFlush();  // the item re-check queued by the container's flush
    EXPECT_TRUE(Count(fx.core.events, RecordingCore::kChildrenInvalidated) >= 1);
    EXPECT_TRUE(HasProp(fx.core, UIA_ExpandCollapseExpandCollapseStatePropertyId,
                        std::to_wstring(ExpandCollapseState_Expanded).c_str()));
    // Stable (id) keys: the root row kept its provider.
    EXPECT_TRUE(PropString(root.Get(), UIA_NamePropertyId) == L"Root");
}

TEST(AutomationItemReview, RowIsSelectedChangeIsRaisedOnTheRow) {
    UiaFixture fx;
    auto* lb = MakeList(fx, 10);
    fx.Attach();
    Layout(fx);
    auto row1 = Child(First(fx).Get(), 1);
    if (!row1) return;
    fx.core.props.clear();
    lb->SetSelectedIndex(1);
    fx.host.OnFlush();
    fx.host.OnFlush();
    EXPECT_TRUE(HasProp(fx.core, UIA_SelectionItemIsSelectedPropertyId, L"true"));
}

TEST(AutomationItemReview, RowHitTestMapsThroughAScaledViewbox) {
    // A 400x200 list shown in a 200x100 Viewbox: rows paint at half size. Host y=20
    // is inside painted row 1 (rows 16 DIP tall on screen), which is local y=40.
    UiaFixture fx;
    auto* vb = fx.root->Emplace<Viewbox>();
    vb->SetWidth(200);
    vb->SetHeight(100);
    auto* lb = vb->SetChild(std::make_unique<ListBox>());
    lb->SetWidth(400);
    lb->SetHeight(200);
    lb->SetItemCount(10);
    lb->ItemTextProvider = [](size_t i) { return L"Row " + std::to_wstring(i); };
    fx.Attach();
    Layout(fx);
    auto rootFrag = fx.Connect();
    const RectDip host = lb->BoundsInHostDip();
    ComPtr<IRawElementProviderFragmentRoot> fr;
    rootFrag.As(&fr);
    ComPtr<IRawElementProviderFragment> hit;
    EXPECT_EQ(fr->ElementProviderFromPoint(host.x + 10, host.y + 20, &hit), S_OK);
    EXPECT_TRUE(PropString(hit.Get(), UIA_NamePropertyId) == L"Row 1");
}

TEST(AutomationItemReview, TrimKeepsTheRowTheClientIsWalkingFrom) {
    // A client paging through a long list: it reaches rows 0..2000 in order (by
    // scrolling the window along), and the posted flush may run between any two
    // calls. The row it reached last must survive every trim.
    UiaFixture fx;
    auto* lb = MakeList(fx, 5000);
    fx.Attach();
    First(fx);
    auto* peer = static_cast<automation::ItemsContainerPeer*>(lb->AutomationPeerIfAny());
    if (!peer) return;
    for (size_t i = 0; i <= 2000; ++i) {
        peer->ItemAt(i);
        if (i % 100 == 0) fx.host.OnFlush();
    }
    ComPtr<IRawElementProviderFragment> cur;
    peer->ItemAt(2000)->Provider()->QueryInterface(IID_PPV_ARGS(&cur));
    fx.host.OnFlush();
    EXPECT_TRUE(PropString(cur.Get(), UIA_NamePropertyId) == L"Row 2000");
    EXPECT_TRUE(peer->CachedItemCount() <= automation::ItemsContainerPeer::kMaxCachedItems);
}

TEST(AutomationItemReview, CacheIsDroppedAndCountChangesPostNothingAfterClientLeaves) {
    UiaFixture fx;
    auto* lb = MakeList(fx, 100);
    fx.Attach();
    auto list = First(fx);
    Child(list.Get(), 3);
    auto* peer = static_cast<automation::ItemsContainerPeer*>(lb->AutomationPeerIfAny());
    EXPECT_TRUE(peer && peer->CachedItemCount() > 0);
    if (!peer) return;
    fx.core.listening = false;  // the client went away
    lb->SetItemCount(101);
    fx.host.OnFlush();
    EXPECT_EQ(peer->CachedItemCount(), size_t{0});
    // Releasing queued disconnects, which posted a flush; run it, so the count below
    // starts with no flush pending (a pending one would swallow every later post).
    fx.host.OnFlush();
    const int posts = fx.core.flushPosts;
    for (int i = 0; i < 10; ++i) lb->SetItemCount(102 + static_cast<size_t>(i));  // an appending log
    EXPECT_EQ(fx.core.flushPosts, posts);
}

TEST(AutomationItemReview, TrimWithoutAListenerDropsTheWholeCache) {
    // Count unchanged, cache over budget: with a listener the trim keeps the recent
    // rows; with none it must keep nothing (nobody is left to use them).
    UiaFixture fx;
    auto* lb = MakeList(fx, 5000);
    fx.Attach();
    First(fx);
    auto* peer = static_cast<automation::ItemsContainerPeer*>(lb->AutomationPeerIfAny());
    if (!peer) return;
    for (size_t i = 0; i < 1100; ++i) peer->ItemAt(i);
    EXPECT_TRUE(peer->CachedItemCount() > automation::ItemsContainerPeer::kMaxCachedItems);
    fx.core.listening = false;
    fx.host.OnFlush();
    EXPECT_EQ(peer->CachedItemCount(), size_t{0});
}

TEST(AutomationItemReview, ScrollOnAnAxisThatCannotScrollIsInvalidOperation) {
    UiaFixture fx;
    MakeList(fx, 100);
    fx.Attach();
    Layout(fx);
    ComPtr<IScrollProvider> sc;
    First(fx).As(&sc);
    if (!sc) return;
    EXPECT_EQ(sc->Scroll(ScrollAmount_LargeIncrement, ScrollAmount_NoAmount), UIA_E_INVALIDOPERATION);
    EXPECT_EQ(sc->SetScrollPercent(50, UIA_ScrollPatternNoScroll), UIA_E_INVALIDOPERATION);
    EXPECT_EQ(sc->Scroll(ScrollAmount_NoAmount, ScrollAmount_LargeIncrement), S_OK);
}

TEST(AutomationItemReview, PageScrollMovesOneViewportAndRepaints) {
    // 10 rows of 32 = 320 extent, 100 view: max offset 220. One page = 100 DIP.
    UiaFixture fx;
    auto* lb = MakeList(fx, 10);
    fx.Attach();
    Layout(fx);
    ComPtr<IScrollProvider> sc;
    First(fx).As(&sc);
    if (!sc) return;
    lb->ClearDirty();
    EXPECT_EQ(sc->Scroll(ScrollAmount_NoAmount, ScrollAmount_LargeIncrement), S_OK);
    fx.host.OnFlush();
    EXPECT_NEAR(lb->Scroller().Offset(), 100.0f, 1.0f);
    EXPECT_TRUE(lb->Dirty() != DirtyFlags::None);  // the move is painted
}

// --- The exposed window: a client's walk covers the rows near the viewport ---------
//
// Inspect lists every sibling of the focused element. With all rows exposed, focusing
// a row of a 50,000-row DataGrid made it walk 50,000 siblings at ~0.45 ms each (~22 s,
// measured through a real UIA client): Inspect froze. These pin the window.

TEST(AutomationItemReview, LongListWalkIsBoundedToTheViewportWindow) {
    UiaFixture fx;
    MakeList(fx, 50000);
    fx.Attach();
    Layout(fx);
    auto list = First(fx);
    int walked = 0;
    for (auto c = Nav(list.Get(), NavigateDirection_FirstChild); c; c = Nav(c.Get(), NavigateDirection_NextSibling))
        if (++walked > 50000) break;
    EXPECT_TRUE(walked > 3);  // the visible rows, plus a page below
    EXPECT_TRUE(walked <= static_cast<int>(automation::ItemsContainerPeer::kMaxExposedItems));
}

TEST(AutomationItemReview, ScrollingMovesTheWindowAndTellsTheClient) {
    UiaFixture fx;
    auto* lb = MakeList(fx, 50000);
    fx.Attach();
    Layout(fx);
    auto list = First(fx);
    Nav(list.Get(), NavigateDirection_FirstChild);  // the client has seen the rows
    fx.core.events.clear();
    lb->ScrollItemIntoView(30000);
    fx.host.OnFlush();
    fx.host.OnFlush();
    EXPECT_TRUE(Count(fx.core.events, RecordingCore::kChildrenInvalidated) >= 1);
    // The walk now starts near row 30000 and contains it.
    auto first = Nav(list.Get(), NavigateDirection_FirstChild);
    const std::wstring name = PropString(first.Get(), UIA_NamePropertyId);
    const int firstRow = name.size() > 4 ? std::stoi(name.substr(4)) : -1;
    EXPECT_TRUE(firstRow > 29000 && firstRow <= 30000);
    bool found = false;
    int walked = 0;
    for (auto c = first; c && walked < 1000; c = Nav(c.Get(), NavigateDirection_NextSibling), ++walked)
        found |= PropString(c.Get(), UIA_NamePropertyId) == L"Row 30000";
    EXPECT_TRUE(found);
}

TEST(AutomationItemReview, ShortListsStillExposeEveryRow) {
    UiaFixture fx;
    MakeList(fx, 40);  // longer than the viewport, but a small list: no reason to hide rows
    fx.Attach();
    Layout(fx);
    auto last = Nav(First(fx).Get(), NavigateDirection_LastChild);
    EXPECT_TRUE(PropString(last.Get(), UIA_NamePropertyId).size() > 0);
}

// --- ItemContainer / VirtualizedItem: reaching rows outside the window --------------

namespace {
ComPtr<IRawElementProviderSimple> FindByName(IItemContainerProvider* ic, IRawElementProviderSimple* after,
                                             const wchar_t* name) {
    VARIANT v;
    v.vt = VT_BSTR;
    v.bstrVal = SysAllocString(name);
    ComPtr<IRawElementProviderSimple> out;
    ic->FindItemByProperty(after, UIA_NamePropertyId, v, &out);
    VariantClear(&v);
    return out;
}
ComPtr<IRawElementProviderFragment> AsFrag(IRawElementProviderSimple* s) {
    ComPtr<IRawElementProviderFragment> f;
    if (s) s->QueryInterface(IID_PPV_ARGS(&f));
    return f;
}
}  // namespace

TEST(AutomationItemReview, FindItemByNameReachesARowOutsideTheWindowAndRealizeScrollsToIt) {
    UiaFixture fx;
    auto* lb = MakeList(fx, 50000);
    fx.Attach();
    Layout(fx);
    auto list = First(fx);
    ComPtr<IItemContainerProvider> ic;
    list.As(&ic);
    EXPECT_TRUE(ic != nullptr);
    if (!ic) return;
    auto found = FindByName(ic.Get(), nullptr, L"Row 40000");
    auto frag = AsFrag(found.Get());
    EXPECT_TRUE(PropString(frag.Get(), UIA_NamePropertyId) == L"Row 40000");
    EXPECT_EQ(PropBool(frag.Get(), UIA_IsOffscreenPropertyId), 1);
    ComPtr<IVirtualizedItemProvider> vi;
    if (frag) frag.As(&vi);
    EXPECT_TRUE(vi != nullptr);
    if (!vi) return;
    EXPECT_EQ(vi->Realize(), S_OK);
    fx.host.OnFlush();
    EXPECT_EQ(PropBool(frag.Get(), UIA_IsOffscreenPropertyId), 0);  // scrolled into view
    auto* peer = static_cast<automation::ItemsContainerPeer*>(lb->AutomationPeerIfAny());
    EXPECT_TRUE(peer && peer->ExposedBegin() <= 40000 && peer->ExposedEnd() > 40000);  // now in the walk
}

TEST(AutomationItemReview, FindItemStartAfterAndSelectionAndNoMatch) {
    UiaFixture fx;
    auto* lb = MakeList(fx, 1000);
    lb->ItemTextProvider = [](size_t i) { return i % 100 == 7 ? std::wstring(L"seven") : L"Row " + std::to_wstring(i); };
    fx.Attach();
    Layout(fx);
    ComPtr<IItemContainerProvider> ic;
    First(fx).As(&ic);
    if (!ic) return;
    auto a = FindByName(ic.Get(), nullptr, L"seven");
    auto b = FindByName(ic.Get(), a.Get(), L"seven");  // the next match, not the same one
    auto fa = AsFrag(a.Get()), fb = AsFrag(b.Get());
    EXPECT_TRUE(fa && fb && fa.Get() != fb.Get());
    EXPECT_TRUE(FindByName(ic.Get(), nullptr, L"no such row") == nullptr);  // S_OK + null
    // By selection state.
    lb->SetSelectedIndex(512);
    VARIANT v;
    v.vt = VT_BOOL;
    v.boolVal = VARIANT_TRUE;
    ComPtr<IRawElementProviderSimple> sel;
    EXPECT_EQ(ic->FindItemByProperty(nullptr, UIA_SelectionItemIsSelectedPropertyId, v, &sel), S_OK);
    EXPECT_TRUE(PropString(AsFrag(sel.Get()).Get(), UIA_NamePropertyId) == L"Row 512");
    // Unsupported property, and a startAfter that is not one of its items.
    VARIANT e;
    e.vt = VT_I4;
    e.lVal = 1;
    ComPtr<IRawElementProviderSimple> none;
    EXPECT_EQ(ic->FindItemByProperty(nullptr, UIA_ControlTypePropertyId, e, &none), E_INVALIDARG);
    ComPtr<IRawElementProviderSimple> rootSimple;
    fx.Connect().As(&rootSimple);
    EXPECT_EQ(ic->FindItemByProperty(rootSimple.Get(), 0, e, &none), E_INVALIDARG);
}

TEST(AutomationItemReview, ShortContainersDoNotOfferItemContainer) {
    UiaFixture fx;
    auto* tc = fx.root->Emplace<TreeView>();  // windowed: offers it
    UNREFERENCED_PARAMETER(tc);
    fx.Attach();
    Layout(fx);
    ComPtr<IItemContainerProvider> ic;
    auto tree = First(fx);
    tree.As(&ic);
    EXPECT_TRUE(ic != nullptr);
    // Clients ask through GetPatternProvider, not QueryInterface.
    ComPtr<IRawElementProviderSimple> simple;
    tree.As(&simple);
    ComPtr<IUnknown> pat;
    if (simple) simple->GetPatternProvider(UIA_ItemContainerPatternId, &pat);
    EXPECT_TRUE(pat != nullptr);
}
