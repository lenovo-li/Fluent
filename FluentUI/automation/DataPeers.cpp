// DataPeers.cpp — see DataPeers.h.
#include "DataPeers.h"

#include <algorithm>

#include "AutomationSite.h"
#include "UiaProvider.h"
#include "../controls/Calendar.h"
#include "../controls/DataGrid.h"
#include "../controls/InfoBar.h"
#include "../controls/LogViewer.h"

namespace fluent::automation {

namespace {
constexpr int kLiveRegionChangedEventId = 20024;  // UIA_LiveRegionChangedEventId

template <class T>
T& Ctl(const AutomationPeer& p) { return static_cast<T&>(p.Owner()); }

// Percent of a scroll range, -1 when the axis cannot scroll (Scroll pattern rule).
double Percent(float offset, float maxOffset) {
    return maxOffset > 0 ? std::clamp(100.0 * offset / maxOffset, 0.0, 100.0) : -1.0;
}
double ViewSize(float view, float maxOffset) {
    const double ext = static_cast<double>(view) + maxOffset;
    return ext > 0 ? std::clamp(100.0 * view / ext, 0.0, 100.0) : 100.0;
}
}  // namespace

// --- InfoBar ----------------------------------------------------------------------

namespace {
// The close button. Invoke may run an app handler that removes the InfoBar (and so
// this item): the provider is held across the call and asked afterwards whether its
// peer survived, the same rule as ButtonPeer.
class InfoBarCloseItem final : public ItemPeer, public IInvokePeer {
public:
    InfoBarCloseItem(ItemsContainerPeer& c, size_t key) : ItemPeer(c, key) {}
    ControlType GetControlType() const override { return ControlType::Button; }
    std::wstring GetName() const override { return L"Close"; }
    RectDip BoundsInHostDip() const override {
        return OwnerRectToHost(Ctl<InfoBar>(*this).CloseButtonRect());
    }
    IInvokePeer* AsInvoke() override { return this; }
    void Invoke() override {
        ComPtr<IUnknown> prov(Provider());
        Ctl<InfoBar>(*this).PerformClose();  // may destroy *this
        AutomationPeer* p = PeerFromProvider(prov.Get());
        if (IAutomationSite* site = p ? p->Owner().AutomationSite() : nullptr)
            site->QueueInvoked(prov.Get());
    }
};

std::wstring Announcement(const InfoBar& b) { return b.Title() + L"\n" + b.Message(); }
}  // namespace

InfoBarPeer::InfoBarPeer(InfoBar& o) : ItemsContainerPeer(o) {
    // Baseline: what the bar says when the client first meets it is not news.
    announced_ = Announcement(o);
    announcedValid_ = true;
}
std::wstring InfoBarPeer::GetName() const {
    const std::wstring& n = Owner().AutomationName();
    if (!n.empty()) return n;
    const InfoBar& b = Ctl<InfoBar>(*this);
    return b.Title().empty() ? b.Message() : b.Title();
}
std::wstring InfoBarPeer::GetHelpText() const {
    const InfoBar& b = Ctl<InfoBar>(*this);
    return b.Title().empty() ? std::wstring() : b.Message();  // else the message is the Name
}
LiveSetting InfoBarPeer::GetLiveSetting() const {
    return Ctl<InfoBar>(*this).GetSeverity() == InfoBar::Severity::Error ? LiveSetting::Assertive
                                                                          : LiveSetting::Polite;
}
size_t InfoBarPeer::ItemCount() const { return Ctl<InfoBar>(*this).IsClosable() ? 1 : 0; }
std::unique_ptr<ItemPeer> InfoBarPeer::MakeItem(size_t key) {
    return std::make_unique<InfoBarCloseItem>(*this, key);
}
void InfoBarPeer::OnFlushRecheck() {
    ItemsContainerPeer::OnFlushRecheck();
    // Runs from the flush while a client listens (and from trims, where QueueEvent's
    // own listener gate drops it). Only a real change of the text is announced.
    std::wstring now = Announcement(Ctl<InfoBar>(*this));
    if (announcedValid_ && now == announced_) return;
    announced_ = std::move(now);
    announcedValid_ = true;
    if (!HasProvider()) return;
    if (IAutomationSite* site = Owner().AutomationSite())
        site->QueueEvent(Provider(), kLiveRegionChangedEventId);
}

// --- DataGrid ---------------------------------------------------------------------

DataGridPeer::DataGridPeer(DataGrid& o) : SelectableContainerPeer(o) {}
size_t DataGridPeer::ItemCount() const {
    return static_cast<size_t>(std::max(0, Ctl<DataGrid>(*this).RowCount()));
}
std::wstring DataGridPeer::ItemName(size_t i) const {
    const DataGrid& g = Ctl<DataGrid>(*this);
    const int cols = std::min(kMaxNameColumns, static_cast<int>(g.Columns().size()));
    std::wstring name;
    for (int c = 0; c < cols; ++c) {
        const std::wstring cell = g.CellText(static_cast<int>(i), c);
        if (cell.empty()) continue;
        if (!name.empty()) name += L", ";
        name += cell;
    }
    return name;
}
RectDip DataGridPeer::ItemRect(size_t i) const {
    // The row band across the viewport, clipped; empty when scrolled out.
    const DataGrid& g = Ctl<DataGrid>(*this);
    const RectDip vp = g.ViewportRect();
    const float top = vp.y + static_cast<float>(i) * g.RowHeight() - g.VerticalOffset();
    const float y0 = std::max(top, vp.y), y1 = std::min(top + g.RowHeight(), vp.bottom());
    if (y1 <= y0) return {};
    return RectDip{vp.x, y0, vp.w, y1 - y0};
}
void DataGridPeer::SelectItem(size_t i) { Ctl<DataGrid>(*this).PerformSelectRow(static_cast<int>(i)); }
void DataGridPeer::SetItemInSelection(size_t i, bool on) {
    DataGrid& g = Ctl<DataGrid>(*this);
    if (g.GetSelectionMode() != DataGrid::SelectionMode::Multiple) {
        if (on) g.PerformSelectRow(static_cast<int>(i));
        return;
    }
    if (g.IsRowSelected(static_cast<int>(i)) != on) g.PerformToggleRowSelection(static_cast<int>(i));
}
void DataGridPeer::ScrollItemIntoView(size_t i) { Ctl<DataGrid>(*this).PerformScrollRowIntoView(static_cast<int>(i)); }
bool DataGridPeer::CanSelectMultiple() const {
    return Ctl<DataGrid>(*this).GetSelectionMode() == DataGrid::SelectionMode::Multiple;
}
size_t DataGridPeer::SelectedIndex() const {
    const int s = Ctl<DataGrid>(*this).SelectedRow();
    return s < 0 ? ItemCount() : static_cast<size_t>(s);
}
size_t DataGridPeer::SelectedCount() const { return Ctl<DataGrid>(*this).SelectedRows().size(); }
bool DataGridPeer::IsIndexSelected(size_t i) const {
    return Ctl<DataGrid>(*this).IsRowSelected(static_cast<int>(i));
}
namespace {
// A grid cell: Custom (what WPF's DataGridCell reports), Name = the cell text,
// HelpText = the column header, so "上海南京路店, 门店" reads as value + column.
// Cells are created on demand per row and released with their row.
class DataGridCellPeer final : public AutomationPeer {
public:
    DataGridCellPeer(AutomationPeer& row, size_t col) : AutomationPeer(row.Owner()), row_(row), col_(col) {}
    ControlType GetControlType() const override { return ControlType::Custom; }
    bool IsControlElement() const override { return true; }
    std::wstring GetName() const override {
        const size_t r = row_.IndexInContainer();
        return Ctl<DataGrid>(*this).CellText(static_cast<int>(r), static_cast<int>(col_));
    }
    std::wstring GetHelpText() const override {
        const auto& cols = Ctl<DataGrid>(*this).Columns();
        return col_ < cols.size() ? cols[col_].header : std::wstring();
    }
    std::wstring GetAutomationId() const override { return {}; }
    bool IsEnabled() const override {
        return row_.IsEnabled() && col_ < Ctl<DataGrid>(*this).Columns().size();
    }
    bool IsKeyboardFocusable() const override { return false; }
    bool HasFocusWithin() const override { return false; }
    RectDip BoundsInHostDip() const override {
        const DataGrid& g = Ctl<DataGrid>(*this);
        return OwnerRectToHostDip(g, g.VisibleCellRect(static_cast<int>(row_.IndexInContainer()),
                                                       static_cast<int>(col_)));
    }
    bool IsOffscreen() const override { return BoundsInHostDip().isEmpty(); }
    AutomationPeer* ItemContainer() const override { return &row_; }
    size_t IndexInContainer() const override { return col_; }
    void SetFocusFromAutomation() override { row_.SetFocusFromAutomation(); }

private:
    AutomationPeer& row_;
    const size_t col_;
};

class DataGridRowPeer final : public SelectableItemPeer, public IItemsPeer {
public:
    DataGridRowPeer(DataGridPeer& c, size_t key) : SelectableItemPeer(c, key, ControlType::DataItem) {}
    IItemsPeer* AsItems() override { return this; }
    size_t ItemCount() const override { return Ctl<DataGrid>(*this).Columns().size(); }
    AutomationPeer* ItemAt(size_t i) override {
        if (i >= ItemCount()) return nullptr;
        if (cells_.size() <= i) cells_.resize(i + 1);
        if (!cells_[i]) cells_[i] = std::make_unique<DataGridCellPeer>(*this, i);
        return cells_[i].get();
    }
    AutomationPeer* ItemAtPoint(float x, float y) override {
        const DataGrid& g = Ctl<DataGrid>(*this);
        const RectDip owner = g.Bounds(), mapped = g.BoundsInHostDip();
        if (owner.w > 0 && mapped.w > 0) x = owner.x + (x - mapped.x) * owner.w / mapped.w;
        UNREFERENCED_PARAMETER(y);
        const int c = g.ColumnIndexAt(x);
        return c >= 0 ? ItemAt(static_cast<size_t>(c)) : nullptr;
    }

protected:
    void ReleaseOwned(IAutomationSite* site) override {
        for (auto& c : cells_)
            if (c) ReleasePeer(*c, site);
        cells_.clear();
    }

private:
    std::vector<std::unique_ptr<DataGridCellPeer>> cells_;  // indexed by column, on demand
};
}  // namespace

bool DataGridPeer::VisibleItemRange(size_t& first, size_t& last) const {
    int f = 0, l = -1;
    Ctl<DataGrid>(*this).VisibleRowRange(f, l);
    if (l < f) {  // nothing visible: the base exposes a first page
        first = SIZE_MAX;
        return true;
    }
    first = static_cast<size_t>(f);
    last = static_cast<size_t>(l);
    return true;
}

std::unique_ptr<ItemPeer> DataGridPeer::MakeItem(size_t key) {
    return std::make_unique<DataGridRowPeer>(*this, key);
}

AutomationPeer* DataGridPeer::ItemAtPoint(float x, float y) {
    HostToOwner(x, y);
    const int r = Ctl<DataGrid>(*this).RowIndexAt(y);
    return r >= 0 ? ItemAt(static_cast<size_t>(r)) : nullptr;
}
double DataGridPeer::HorizontalPercent() const {
    const DataGrid& g = Ctl<DataGrid>(*this);
    return Percent(g.HorizontalOffset(), g.MaxHorizontalOffset());
}
double DataGridPeer::VerticalPercent() const {
    const DataGrid& g = Ctl<DataGrid>(*this);
    return Percent(g.VerticalOffset(), g.MaxVerticalOffset());
}
double DataGridPeer::HorizontalViewSize() const {
    const DataGrid& g = Ctl<DataGrid>(*this);
    return ViewSize(g.ScrollableViewportWidth(), g.MaxHorizontalOffset());  // frozen band excluded
}
double DataGridPeer::VerticalViewSize() const {
    const DataGrid& g = Ctl<DataGrid>(*this);
    return ViewSize(g.ViewportRect().h, g.MaxVerticalOffset());
}
void DataGridPeer::SetScrollPercent(double h, double v) {
    DataGrid& g = Ctl<DataGrid>(*this);
    if (h >= 0 && g.MaxHorizontalOffset() > 0)
        g.SetHorizontalOffset(static_cast<float>(h / 100.0 * g.MaxHorizontalOffset()));
    if (v >= 0 && g.MaxVerticalOffset() > 0)
        g.SetVerticalOffset(static_cast<float>(v / 100.0 * g.MaxVerticalOffset()));
}

// --- Calendar ---------------------------------------------------------------------

namespace {
size_t DateKey(int y, int m, int d) { return static_cast<size_t>(y) * 10000 + m * 100 + d; }
}  // namespace

CalendarPeer::CalendarPeer(Calendar& o) : SelectableContainerPeer(o) {}
size_t CalendarPeer::KeyAt(size_t i) const {
    int y = 0, m = 0, d = 0;
    Ctl<Calendar>(*this).CellDate(static_cast<int>(i), y, m, d);
    return DateKey(y, m, d);
}
size_t CalendarPeer::IndexOfKey(size_t key) const {
    const int y = static_cast<int>(key / 10000), m = static_cast<int>(key / 100 % 100),
              d = static_cast<int>(key % 100);
    const int i = Ctl<Calendar>(*this).CellIndexOf(y, m, d);
    return i < 0 ? ItemCount() : static_cast<size_t>(i);  // paged out: gone
}
std::wstring CalendarPeer::ItemName(size_t i) const {
    int y = 0, m = 0, d = 0;
    Ctl<Calendar>(*this).CellDate(static_cast<int>(i), y, m, d);
    // The long date in the user's locale ("2026年11月1日" / "Sunday, November 1, 2026"),
    // which is what Narrator should read for a day cell, not the bare "1".
    SYSTEMTIME st{};
    st.wYear = static_cast<WORD>(y);
    st.wMonth = static_cast<WORD>(m);
    st.wDay = static_cast<WORD>(d);
    wchar_t buf[128];
    if (GetDateFormatEx(LOCALE_NAME_USER_DEFAULT, DATE_LONGDATE, &st, nullptr, buf, 128, nullptr) > 0)
        return buf;
    return std::to_wstring(y) + L"-" + std::to_wstring(m) + L"-" + std::to_wstring(d);
}
RectDip CalendarPeer::ItemRect(size_t i) const { return Ctl<Calendar>(*this).CellRect(static_cast<int>(i)); }
bool CalendarPeer::ItemEnabled(size_t i) const {
    int y = 0, m = 0, d = 0;
    Ctl<Calendar>(*this).CellDate(static_cast<int>(i), y, m, d);
    return Ctl<Calendar>(*this).IsDateEnabled(y, m, d);
}
void CalendarPeer::SelectItem(size_t i) {
    int y = 0, m = 0, d = 0;
    Ctl<Calendar>(*this).CellDate(static_cast<int>(i), y, m, d);
    Ctl<Calendar>(*this).PerformSelectDate(y, m, d);
}
size_t CalendarPeer::SelectedIndex() const {
    const Calendar& c = Ctl<Calendar>(*this);
    const int i = c.CellIndexOf(c.SelectedYear(), c.SelectedMonth(), c.SelectedDay());
    return i < 0 ? ItemCount() : static_cast<size_t>(i);
}
AutomationPeer* CalendarPeer::ItemAtPoint(float x, float y) {
    HostToOwner(x, y);
    const RectDip b = Owner().Bounds();
    const int i = Ctl<Calendar>(*this).HitTestCell(x - b.x, y - b.y);  // control-relative
    return i >= 0 ? ItemAt(static_cast<size_t>(i)) : nullptr;
}

// --- LogViewer --------------------------------------------------------------------

namespace {
class LogLinePeer final : public ItemPeer {
public:
    LogLinePeer(ItemsContainerPeer& c, size_t key) : ItemPeer(c, key) {}
    ControlType GetControlType() const override { return ControlType::ListItem; }
    std::wstring GetName() const override {
        std::wstring t = Ctl<LogViewer>(*this).LineText(Index());
        if (t.size() > LogViewerPeer::kMaxLineName) t.resize(LogViewerPeer::kMaxLineName);
        return t;
    }
    RectDip BoundsInHostDip() const override {
        return OwnerRectToHost(Ctl<LogViewer>(*this).LineRect(Index()));
    }
    bool CanScrollIntoView() const override { return true; }
    void ScrollIntoView() override {
        if (IsLive()) Ctl<LogViewer>(*this).ScrollLineIntoView(Index());
    }
};
}  // namespace

LogViewerPeer::LogViewerPeer(LogViewer& o) : ItemsContainerPeer(o) {}
size_t LogViewerPeer::ItemCount() const { return Ctl<LogViewer>(*this).LineCount(); }
size_t LogViewerPeer::KeyAt(size_t i) const {
    return static_cast<size_t>(Ctl<LogViewer>(*this).FirstLineSerial() + i);
}
size_t LogViewerPeer::IndexOfKey(size_t key) const {
    const uint64_t first = Ctl<LogViewer>(*this).FirstLineSerial();
    if (key < first) return ItemCount();  // trimmed off the front: gone
    const uint64_t i = key - first;
    return i < ItemCount() ? static_cast<size_t>(i) : ItemCount();
}
std::unique_ptr<ItemPeer> LogViewerPeer::MakeItem(size_t key) {
    return std::make_unique<LogLinePeer>(*this, key);
}
AutomationPeer* LogViewerPeer::ItemAtPoint(float x, float y) {
    // Inverse of OwnerRectToHost (Viewbox): host point -> control space.
    const RectDip owner = Owner().Bounds(), mapped = Owner().BoundsInHostDip();
    if (mapped.h > 0 && owner.h > 0) y = owner.y + (y - mapped.y) * owner.h / mapped.h;
    UNREFERENCED_PARAMETER(x);
    const size_t i = Ctl<LogViewer>(*this).LineIndexAt(y);
    return i < ItemCount() ? ItemAt(i) : nullptr;
}
std::wstring LogViewerPeer::ItemNameAt(size_t i) const {
    std::wstring t = Ctl<LogViewer>(*this).LineText(i);
    if (t.size() > kMaxLineName) t.resize(kMaxLineName);
    return t;
}

bool LogViewerPeer::VisibleItemRange(size_t& first, size_t& last) const {
    const LogViewer& l = Ctl<LogViewer>(*this);
    const RectDip b = l.Bounds();
    first = SIZE_MAX;  // always a window; SIZE_MAX = nothing visible, first page
    last = 0;
    if (b.h <= 0 || l.LineCount() == 0) return true;
    const size_t top = l.LineIndexAt(b.y);
    if (top == SIZE_MAX) return true;
    const size_t bottom = l.LineIndexAt(b.bottom() - 0.5f);
    first = top;
    last = bottom == SIZE_MAX ? l.LineCount() - 1 : bottom;  // short log: empty space below
    return true;
}

double LogViewerPeer::VerticalPercent() const {
    const LogViewer& l = Ctl<LogViewer>(*this);
    return Percent(l.VerticalOffset(), l.MaxVerticalOffset());
}
double LogViewerPeer::VerticalViewSize() const {
    const LogViewer& l = Ctl<LogViewer>(*this);
    return ViewSize(l.Bounds().h, l.MaxVerticalOffset());
}
void LogViewerPeer::SetScrollPercent(double, double v) {
    LogViewer& l = Ctl<LogViewer>(*this);
    if (v >= 0 && l.MaxVerticalOffset() > 0)
        l.SetVerticalOffset(static_cast<float>(v / 100.0 * l.MaxVerticalOffset()));
}

}  // namespace fluent::automation

namespace fluent {
std::unique_ptr<automation::AutomationPeer> InfoBar::CreateAutomationPeer() {
    return std::make_unique<automation::InfoBarPeer>(*this);
}
std::unique_ptr<automation::AutomationPeer> DataGrid::CreateAutomationPeer() {
    return std::make_unique<automation::DataGridPeer>(*this);
}
std::unique_ptr<automation::AutomationPeer> Calendar::CreateAutomationPeer() {
    return std::make_unique<automation::CalendarPeer>(*this);
}
std::unique_ptr<automation::AutomationPeer> LogViewer::CreateAutomationPeer() {
    return std::make_unique<automation::LogViewerPeer>(*this);
}
}  // namespace fluent
