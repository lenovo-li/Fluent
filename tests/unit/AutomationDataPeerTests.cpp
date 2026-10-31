// AutomationDataPeerTests.cpp — UIA for InfoBar, DataGrid, Calendar and LogViewer.
//
// Each test checks what a client (or the app) actually receives: the event an action
// must raise (Closed, SelectionChanged), the UIA event a change must produce
// (LiveRegionChanged, ChildrenInvalidated), and which cached nodes survive a content
// change. Reading a stored value back would pass even when the action took a silent
// path, which is the defect class these peers were written against.
#include "../framework/Test.h"
#include "AutomationTestHelpers.h"

#include "../../FluentUI/automation/AutomationPeer.h"
#include "../../FluentUI/controls/Calendar.h"
#include "../../FluentUI/controls/DataGrid.h"
#include "../../FluentUI/controls/InfoBar.h"
#include "../../FluentUI/controls/LogViewer.h"

using namespace fluent;
using namespace uiatest;

namespace {
constexpr int kLiveRegionChanged = 20024;  // UIA_LiveRegionChangedEventId

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
void Layout(UiaFixture& fx) {
    fx.root->Measure(400, 300);
    fx.root->Arrange({0, 0, 400, 300});
}
void FlushTwice(UiaFixture& fx) {  // actions/re-checks, then what they queued
    fx.host.OnFlush();
    fx.host.OnFlush();
}
}  // namespace

// --- InfoBar ----------------------------------------------------------------------

TEST(AutomationData, InfoBarIsALiveStatusBarWithAnInvokableCloseButton) {
    UiaFixture fx;
    auto* bar = fx.root->Emplace<InfoBar>();
    bar->SetTitle(L"Saved");
    bar->SetMessage(L"All changes were written.");
    bar->SetClosable(true);
    int closed = 0;
    auto sub = bar->Closed().Subscribe(&closed, [](void* o, InfoBar&, RoutedEventArgs&) {
        ++*static_cast<int*>(o);
    });
    fx.Attach();
    Layout(fx);
    auto f = First(fx);
    EXPECT_EQ(PropInt(f.Get(), UIA_ControlTypePropertyId), UIA_StatusBarControlTypeId);
    EXPECT_TRUE(PropString(f.Get(), UIA_NamePropertyId) == L"Saved");
    EXPECT_TRUE(PropString(f.Get(), UIA_HelpTextPropertyId) == L"All changes were written.");
    EXPECT_EQ(PropInt(f.Get(), UIA_LiveSettingPropertyId), static_cast<long>(Polite));
    bar->SetSeverity(InfoBar::Severity::Error);
    EXPECT_EQ(PropInt(f.Get(), UIA_LiveSettingPropertyId), static_cast<long>(Assertive));

    auto close = Nav(f.Get(), NavigateDirection_FirstChild);
    EXPECT_EQ(PropInt(close.Get(), UIA_ControlTypePropertyId), UIA_ButtonControlTypeId);
    ComPtr<IInvokeProvider> inv;
    if (close) close.As(&inv);
    EXPECT_TRUE(inv != nullptr);
    if (!inv) return;
    UiaRect r{};
    close->get_BoundingRectangle(&r);
    EXPECT_TRUE(r.width > 0);  // the painted close button, not an empty rect
    EXPECT_EQ(inv->Invoke(), S_OK);
    fx.host.OnFlush();
    EXPECT_EQ(closed, 1);  // the app's dismiss handler ran
    fx.host.OnFlush();
    EXPECT_EQ(Count(fx.core.events, UIA_Invoke_InvokedEventId), 1);
}

TEST(AutomationData, InfoBarTextChangeRaisesOneLiveRegionChangedOnlyWhileListening) {
    UiaFixture fx;
    auto* bar = fx.root->Emplace<InfoBar>();
    bar->SetMessage(L"Connecting…");
    fx.Attach();
    Layout(fx);
    auto f = First(fx);
    EXPECT_TRUE(PropString(f.Get(), UIA_NamePropertyId) == L"Connecting…");  // no title: message is the name
    fx.core.events.clear();
    bar->SetMessage(L"Connected");
    bar->SetMessage(L"Connected, 3 devices");  // two changes in one frame: one announcement
    FlushTwice(fx);
    EXPECT_EQ(Count(fx.core.events, kLiveRegionChanged), 1);
    // An invalidation that leaves the text alone (a severity change): nothing.
    bar->SetSeverity(InfoBar::Severity::Warning);
    FlushTwice(fx);
    EXPECT_EQ(Count(fx.core.events, kLiveRegionChanged), 1);
    // Nobody listening: nothing, and nothing queued.
    fx.core.listening = false;
    bar->SetMessage(L"Disconnected");
    FlushTwice(fx);
    EXPECT_EQ(Count(fx.core.events, kLiveRegionChanged), 1);
}

// --- DataGrid ---------------------------------------------------------------------

namespace {
DataGrid* MakeGrid(UiaFixture& fx, int rows) {
    auto* g = fx.root->Emplace<DataGrid>();
    g->SetHeight(150);
    std::vector<DataGrid::Column> cols;
    for (int i = 0; i < 3; ++i) cols.push_back({L"C" + std::to_wstring(i), 100.0f, DataGrid::Align::Left});
    g->SetColumns(std::move(cols));
    g->SetRowCount(rows);
    g->SetRowHeight(28.0f);
    g->SetCellProvider([](int r, int c) { return L"r" + std::to_wstring(r) + L"c" + std::to_wstring(c); });
    return g;
}
}  // namespace

TEST(AutomationData, DataGridRowsAreDataItemsNamedByTheirCellsAndSelectRaisesTheEvent) {
    UiaFixture fx;
    auto* g = MakeGrid(fx, 100);
    int changes = 0;
    auto sub = g->SelectionChanged().Subscribe(&changes, [](void* o, DataGrid&, RoutedEventArgs&) {
        ++*static_cast<int*>(o);
    });
    fx.Attach();
    Layout(fx);
    auto grid = First(fx);
    EXPECT_EQ(PropInt(grid.Get(), UIA_ControlTypePropertyId), UIA_DataGridControlTypeId);
    auto row2 = Child(grid.Get(), 2);
    EXPECT_EQ(PropInt(row2.Get(), UIA_ControlTypePropertyId), UIA_DataItemControlTypeId);
    EXPECT_TRUE(PropString(row2.Get(), UIA_NamePropertyId) == L"r2c0, r2c1, r2c2");
    ComPtr<ISelectionItemProvider> si;
    if (row2) row2.As(&si);
    if (!si) return;
    si->Select();
    fx.host.OnFlush();
    EXPECT_EQ(g->SelectedRow(), 2);
    EXPECT_EQ(changes, 1);

    // Multiple mode: AddToSelection takes Ctrl+click's path.
    g->SetSelectionMode(DataGrid::SelectionMode::Multiple);
    ComPtr<ISelectionItemProvider> si4;
    Child(grid.Get(), 4).As(&si4);
    if (!si4) return;
    si4->AddToSelection();
    fx.host.OnFlush();
    EXPECT_TRUE(g->IsRowSelected(2) && g->IsRowSelected(4));
    EXPECT_EQ(changes, 2);
}

TEST(AutomationData, DataGridHitTestAndScroll) {
    UiaFixture fx;
    auto* g = MakeGrid(fx, 100);
    fx.Attach();
    Layout(fx);
    auto rootFrag = fx.Connect();
    ComPtr<IRawElementProviderFragmentRoot> fr;
    rootFrag.As(&fr);
    const RectDip vp = g->ViewportRect();
    ComPtr<IRawElementProviderFragment> hit;
    EXPECT_EQ(fr->ElementProviderFromPoint(vp.x + 10, vp.y + 28 * 1 + 5, &hit), S_OK);
    EXPECT_TRUE(PropString(hit.Get(), UIA_NamePropertyId) == L"r1c0, r1c1, r1c2");

    ComPtr<IScrollProvider> sc;
    First(fx).As(&sc);
    if (!sc) return;
    double v = -2;
    sc->get_VerticalScrollPercent(&v);
    EXPECT_NEAR(v, 0.0, 0.01);
    EXPECT_EQ(sc->SetScrollPercent(UIA_ScrollPatternNoScroll, 100), S_OK);
    fx.host.OnFlush();
    EXPECT_NEAR(g->VerticalOffset(), g->MaxVerticalOffset(), 0.5f);
    EXPECT_TRUE(g->MaxVerticalOffset() > 0);
}

// --- Calendar ---------------------------------------------------------------------

TEST(AutomationData, CalendarDaysAreDateKeyedCellsAndSelectTakesTheClickPath) {
    UiaFixture fx;
    auto* c = fx.root->Emplace<Calendar>();
    c->SetSelectedDate(2026, 11, 15);
    c->SetMaxDate(2026, 11, 20);
    int changes = 0;
    auto sub = c->SelectionChanged().Subscribe(&changes, [](void* o, Calendar&, RoutedEventArgs&) {
        ++*static_cast<int*>(o);
    });
    fx.Attach();
    Layout(fx);
    auto cal = First(fx);
    EXPECT_EQ(PropInt(cal.Get(), UIA_ControlTypePropertyId), UIA_CalendarControlTypeId);
    auto d10 = Child(cal.Get(), c->CellIndexOf(2026, 11, 10));
    EXPECT_EQ(PropInt(d10.Get(), UIA_ControlTypePropertyId), UIA_DataItemControlTypeId);
    EXPECT_TRUE(PropString(d10.Get(), UIA_NamePropertyId).find(L"10") != std::wstring::npos);
    auto d25 = Child(cal.Get(), c->CellIndexOf(2026, 11, 25));
    EXPECT_EQ(PropBool(d25.Get(), UIA_IsEnabledPropertyId), 0);  // past the max date

    ComPtr<ISelectionItemProvider> si;
    if (d10) d10.As(&si);
    if (!si) return;
    si->Select();
    fx.host.OnFlush();
    EXPECT_EQ(c->SelectedDay(), 10);
    EXPECT_EQ(changes, 1);
    ComPtr<ISelectionItemProvider> off;
    if (d25) d25.As(&off);
    if (off) off->Select();
    fx.host.OnFlush();
    EXPECT_EQ(c->SelectedDay(), 10);  // refused
    EXPECT_EQ(changes, 1);

    // Paging to December: November's days (not shown as spill-over) are released,
    // and the client is told the children changed.
    fx.core.events.clear();
    c->SetMaxDate(2027, 1, 1);
    c->NextMonth();
    FlushTwice(fx);
    EXPECT_TRUE(Count(fx.core.events, RecordingCore::kChildrenInvalidated) >= 1);
    EXPECT_TRUE(PropString(d10.Get(), UIA_NamePropertyId) == L"<error>");
}

// --- LogViewer --------------------------------------------------------------------

TEST(AutomationData, LogLinesKeepTheirNodesAcrossTheFrontTrim) {
    UiaFixture fx;
    auto* log = fx.root->Emplace<LogViewer>();
    log->SetHeight(200);
    log->SetMaxLines(3);
    const D2D1_COLOR_F white{1, 1, 1, 1};
    for (const wchar_t* t : {L"alpha", L"bravo", L"charlie"}) log->AppendLine({{t, white}});
    fx.Attach();
    Layout(fx);
    auto list = First(fx);
    EXPECT_EQ(PropInt(list.Get(), UIA_ControlTypePropertyId), UIA_ListControlTypeId);
    auto a = Child(list.Get(), 0);
    auto c = Child(list.Get(), 2);
    EXPECT_TRUE(PropString(a.Get(), UIA_NamePropertyId) == L"alpha");
    EXPECT_TRUE(PropString(c.Get(), UIA_NamePropertyId) == L"charlie");
    log->AppendLine({{L"delta", white}});  // count stays 3: alpha falls off the front
    FlushTwice(fx);
    EXPECT_TRUE(PropString(a.Get(), UIA_NamePropertyId) == L"<error>");
    EXPECT_TRUE(PropString(c.Get(), UIA_NamePropertyId) == L"charlie");  // same line, new index
    EXPECT_TRUE(PropString(Child(list.Get(), 2).Get(), UIA_NamePropertyId) == L"delta");
    EXPECT_TRUE(Count(fx.core.events, RecordingCore::kChildrenInvalidated) >= 1);
}

TEST(AutomationData, DataGridSelectCommitsALiveEditLikeAClick) {
    UiaFixture fx;
    auto* g = MakeGrid(fx, 20);
    g->SetEditingEnabled(true);
    int edits = 0;
    auto sub = g->CellEdited().Subscribe(&edits, [](void* o, DataGrid&, RoutedEventArgs&) {
        ++*static_cast<int*>(o);
    });
    fx.Attach();
    Layout(fx);
    auto row4 = Child(First(fx).Get(), 4);
    g->BeginEdit(1, 0);
    EXPECT_TRUE(g->IsEditing());
    ComPtr<ISelectionItemProvider> si;
    if (row4) row4.As(&si);
    if (!si) return;
    si->Select();
    fx.host.OnFlush();
    EXPECT_FALSE(g->IsEditing());  // the edit on row 1 ended
    EXPECT_EQ(edits, 1);           // and was committed, not dropped
    EXPECT_EQ(g->SelectedRow(), 4);
}

TEST(AutomationData, DataGridHorizontalViewSizeExcludesFrozenColumns) {
    // Viewport 400 wide, 200 of it frozen; 1000 of columns => 800 scroll in a 200 band.
    UiaFixture fx;
    auto* g = fx.root->Emplace<DataGrid>();
    g->SetHeight(150);
    g->SetWidth(400);
    g->SetHAlign(HAlign::Left);
    std::vector<DataGrid::Column> cols;
    for (int i = 0; i < 10; ++i) {
        DataGrid::Column c{L"C" + std::to_wstring(i), 100.0f, DataGrid::Align::Left};
        c.frozen = i < 2;
        cols.push_back(c);
    }
    g->SetColumns(std::move(cols));
    g->SetRowCount(5);
    fx.Attach();
    Layout(fx);
    ComPtr<IScrollProvider> sc;
    First(fx).As(&sc);
    if (!sc) return;
    double view = 0;
    sc->get_HorizontalViewSize(&view);
    const double band = g->ScrollableViewportWidth(), maxX = g->MaxHorizontalOffset();
    EXPECT_TRUE(maxX > 0);
    EXPECT_NEAR(view, 100.0 * band / (band + maxX), 0.5);
    EXPECT_TRUE(band < g->ViewportRect().w);  // the frozen band was taken off
}

TEST(AutomationData, LogPointBelowTheLastLineIsNoLine) {
    UiaFixture fx;
    auto* log = fx.root->Emplace<LogViewer>();
    log->SetHeight(200);
    const D2D1_COLOR_F white{1, 1, 1, 1};
    log->AppendLine({{L"one", white}});
    log->AppendLine({{L"two", white}});
    fx.Attach();
    Layout(fx);
    const RectDip b = log->Bounds();
    EXPECT_EQ(log->LineIndexAt(b.y + 1.0f), size_t{0});
    EXPECT_EQ(log->LineIndexAt(b.y + 150.0f), SIZE_MAX);  // empty space under "two"
}

TEST(AutomationData, CappedLogAtItsLimitStillReportsChildrenChanged) {
    // At the cap every append drops one line and adds one: the count never changes.
    // A client that cached a line in the middle (not the one that falls off) must
    // still be told the children changed.
    UiaFixture fx;
    auto* log = fx.root->Emplace<LogViewer>();
    log->SetHeight(200);
    log->SetMaxLines(5);
    const D2D1_COLOR_F white{1, 1, 1, 1};
    for (int i = 0; i < 5; ++i) log->AppendLine({{L"line " + std::to_wstring(i), white}});
    fx.Attach();
    Layout(fx);
    // LastChild caches only that line: walking from the first child would cache
    // line 0 too, and its trim alone would raise the event.
    auto mid = Nav(First(fx).Get(), NavigateDirection_LastChild);
    EXPECT_TRUE(PropString(mid.Get(), UIA_NamePropertyId) == L"line 4");
    fx.core.events.clear();
    log->AppendLine({{L"line 5", white}});
    FlushTwice(fx);
    EXPECT_TRUE(Count(fx.core.events, RecordingCore::kChildrenInvalidated) >= 1);
    EXPECT_TRUE(PropString(mid.Get(), UIA_NamePropertyId) == L"line 4");  // same line, kept
}

TEST(AutomationData, InfoBarAnnouncementGoesOutInTheSameFlush) {
    UiaFixture fx;
    auto* bar = fx.root->Emplace<InfoBar>();
    bar->SetMessage(L"a");
    fx.Attach();
    Layout(fx);
    First(fx);
    fx.core.events.clear();
    bar->SetMessage(L"b");
    fx.host.OnFlush();  // one flush, not two
    EXPECT_EQ(Count(fx.core.events, kLiveRegionChanged), 1);
}
