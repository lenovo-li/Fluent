// DataPeers.h — UI Automation for the controls that draw their own content rows:
// InfoBar (a live region with a close button), DataGrid (rows as DataItems),
// Calendar (day cells), LogViewer (lines).
//
// All four follow ItemPeers.h: the rows are not elements, so each row's peer is
// made on demand when a client reaches it and is cached under a key. The key choice
// is what decides what a client sees when the content changes:
//   * DataGrid: the row index (the grid itself is index-addressed; a re-sort
//     re-numbers rows, and a count change releases every cached row).
//   * Calendar: the date (yyyymmdd). Paging to another month makes the old days'
//     keys vanish, so they are released and ChildrenInvalidated is raised; the
//     spill-over days shared by both months keep their nodes.
//   * LogViewer: the line serial (LogViewer::FirstLineSerial() + index), which
//     survives the front trim of a capped log, so a client reading line 900 keeps
//     it while older lines fall off the top.
//
// Actions take the user's path: Calendar::PerformSelectDate, DataGrid::SetSelectedRow
// / PerformToggleRowSelection, InfoBar::PerformClose all raise the app-visible event a
// click raises.
#pragma once

#include "SelectorPeers.h"

namespace fluent {
class InfoBar;
class DataGrid;
class Calendar;
class LogViewer;
}  // namespace fluent

namespace fluent::automation {

// StatusBar, Name = title, HelpText = message. LiveSetting: Assertive for Error,
// Polite otherwise; a change of title or message raises LiveRegionChanged (one per
// flush, only while a client listens), which is what makes Narrator read a status
// update without moving focus. A closable bar lists its close button as an item
// (Button + Invoke -> InfoBar::PerformClose).
class InfoBarPeer final : public ItemsContainerPeer {
public:
    explicit InfoBarPeer(InfoBar& o);
    ControlType GetControlType() const override { return ControlType::StatusBar; }
    std::wstring GetName() const override;
    std::wstring GetHelpText() const override;
    bool IsControlElement() const override { return true; }
    LiveSetting GetLiveSetting() const override;
    size_t ItemCount() const override;
    std::unique_ptr<ItemPeer> MakeItem(size_t key) override;
    void OnFlushRecheck() override;

private:
    std::wstring announced_;  // title + message last seen by the flush re-check
    bool announcedValid_ = false;
};

// DataGrid: rows are DataItems (Name = the cells joined with ", ", at most
// kMaxNameColumns columns), SelectionItem + ScrollItem; the grid has Selection and
// Scroll (both axes). Column headers and per-cell Grid/Table patterns are not
// exposed yet.
class DataGridPeer final : public SelectableContainerPeer, public IScrollPeer {
public:
    static constexpr int kMaxNameColumns = 16;
    explicit DataGridPeer(DataGrid& o);
    ControlType GetControlType() const override { return ControlType::DataGrid; }
    size_t ItemCount() const override;
    ControlType ItemType() const override { return ControlType::DataItem; }
    std::wstring ItemName(size_t i) const override;
    RectDip ItemRect(size_t i) const override;
    void SelectItem(size_t i) override;
    void SetItemInSelection(size_t i, bool on) override;
    void ScrollItemIntoView(size_t i) override;
    bool CanSelectMultiple() const override;
    size_t SelectedIndex() const override;
    size_t SelectedCount() const override;
    bool IsIndexSelected(size_t i) const override;
    AutomationPeer* ItemAtPoint(float x, float y) override;  // O(1)
    IScrollPeer* AsScroll() override { return this; }
    double HorizontalPercent() const override;
    double VerticalPercent() const override;
    double HorizontalViewSize() const override;
    double VerticalViewSize() const override;
    void SetScrollPercent(double h, double v) override;
};

// Calendar: 42 day cells (DataItem, Name = the long date in the user's locale),
// keyed by date; disabled dates (min/max, blackout) are not enabled.
class CalendarPeer final : public SelectableContainerPeer {
public:
    explicit CalendarPeer(Calendar& o);
    ControlType GetControlType() const override { return ControlType::Calendar; }
    size_t ItemCount() const override { return 42; }
    size_t KeyAt(size_t i) const override;
    size_t IndexOfKey(size_t key) const override;
    bool KeysAreStable() const override { return true; }
    ControlType ItemType() const override { return ControlType::DataItem; }
    std::wstring ItemName(size_t i) const override;
    RectDip ItemRect(size_t i) const override;
    bool ItemEnabled(size_t i) const override;
    void SelectItem(size_t i) override;
    bool CanSelectMultiple() const override { return false; }
    size_t SelectedIndex() const override;
    size_t SelectedCount() const override { return SelectedIndex() < ItemCount() ? 1 : 0; }
    bool IsIndexSelected(size_t i) const override { return i == SelectedIndex(); }
    AutomationPeer* ItemAtPoint(float x, float y) override;
};

// LogViewer: a List whose items are its lines (ListItem, Name = the line's text up
// to kMaxLineName characters, ScrollItem), keyed by line serial; Scroll vertical.
// A live log changes its line count on every append: while a client listens, the
// flush raises one ChildrenInvalidated per frame for it, and none once it leaves.
class LogViewerPeer final : public ItemsContainerPeer, public IScrollPeer {
public:
    static constexpr size_t kMaxLineName = 1024;
    explicit LogViewerPeer(LogViewer& o);
    ControlType GetControlType() const override { return ControlType::List; }
    bool IsControlElement() const override { return true; }
    size_t ItemCount() const override;
    size_t KeyAt(size_t i) const override;
    size_t IndexOfKey(size_t key) const override;
    bool KeysAreStable() const override { return true; }
    std::unique_ptr<ItemPeer> MakeItem(size_t key) override;
    AutomationPeer* ItemAtPoint(float x, float y) override;  // O(log n)
    IScrollPeer* AsScroll() override { return this; }
    double HorizontalPercent() const override { return -1; }
    double VerticalPercent() const override;
    double HorizontalViewSize() const override { return 100; }
    double VerticalViewSize() const override;
    void SetScrollPercent(double h, double v) override;
};

}  // namespace fluent::automation
