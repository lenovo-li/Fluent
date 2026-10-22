// NavRail.h — vertical navigation rail: optional header (logo) + icon items.
//
//   auto* rail = grid->Emplace<fluent::NavRail>();
//   rail->SetHeader(std::move(logoImage));          // optional, not selectable
//   rail->SetItems({{L"\xE896", L"Firmware Updates"}, {L"\xE897", L"About", true}});
//   // Payload is int& (the NEW index). STORE the Subscription.
//   navSub_ = rail->SelectionChanged().Subscribe<&MainWindow::OnNav>(this);
//
// VISUALS. Selected item: 3 DIP accent bar on the left + accent-tinted rounded fill.
// Hover: a faint neutral fill. Badge: a small severity-error dot at the top right.
// Every colour comes from Theme(); only geometry constants live here.
//
// ICONS. Items carry a glyph string drawn with "Segoe Fluent Icons" (Win11), falling
// back to "Segoe MDL2 Assets" (Win10). Both share the common codepoints
// (E896 Download, E897 Help, E701 Wifi). The format is created from the DWrite
// factory, which is device-independent, so it survives device loss.
//
// INPUT. Click selects. When focused, Up/Down move a keyboard cursor and Enter/Space
// select it (DESIGN §3.3); the focus ring marks the cursor only after keyboard use. Tooltip: the framework tracks tooltips per element, so the
// rail swaps its tooltip text to the hovered item's label; the text shown is the one
// current when the hover delay elapses.
#pragma once

#include "../../FluentUI/layout/Panel.h"
#include "../../FluentUI/base/Event.h"
#include "../../FluentUI/input/RoutedEvent.h"
#include <dwrite.h>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace fluent {

struct NavRailItem {
    std::wstring glyph;       // icon-font glyph, e.g. L"\xE896"
    std::wstring label;       // page name, used as tooltip
    bool showBadge = false;   // red dot indicator
};

class NavRail : public Panel {
public:
    NavRail();

    // Optional non-selectable element above the items (typically the logo Image).
    // Replaces any previous header. Returns the borrowed pointer.
    FrameworkElement* SetHeader(std::unique_ptr<FrameworkElement> header);

    void SetItems(std::vector<NavRailItem> items);
    const std::vector<NavRailItem>& Items() const { return items_; }
    void SetBadge(int index, bool show);

    // Programmatic selection does NOT raise SelectionChanged (same as WinUI's
    // SelectedItem setter path used for state restore). -1 clears.
    void SetSelectedIndex(int index);
    int SelectedIndex() const { return selectedIndex_; }

    // Rail fill. Default: Theme().colors.windowBackground.
    void SetRailBackground(const D2D1_COLOR_F& c) { background_ = c; Invalidate(); }

    // Raised on user selection (click / Enter / Space) with the new index.
    Event<NavRail, int>& SelectionChanged() { return selectionChanged_; }

    // Panel overrides.
    void Render(const DrawingContext& dc) override;
    UIElement* HitTestDeep(float dipX, float dipY) override;
    void CollectFocusables(std::vector<UIElement*>& out) override;

    // Layout constants, public so tests and hosts can align against them.
    static constexpr float kRailWidth = 88.0f;
    static constexpr float kTopPadding = 12.0f;
    static constexpr float kHeaderGap = 20.0f;
    static constexpr float kItemSize = 56.0f;
    static constexpr float kItemGap = 4.0f;
    static constexpr float kAccentBarWidth = 3.0f;
    static constexpr float kIconSize = 20.0f;
    static constexpr float kBadgeSize = 8.0f;

    // Window-DIP rect of item `index` (empty when out of range / not arranged).
    RectDip ItemRect(int index) const;
    int HitTestItem(float dipX, float dipY) const;

protected:
    SizeDip MeasureOverride(float availW, float availH) override;
    void ArrangeOverride(const RectDip& content) override;

    void OnPointerPressed(PointerEventArgs& e) override;
    void OnPointerMoved(PointerEventArgs& e) override;
    void OnPointerLeft() override;
    void OnKeyDownRouted(KeyEventArgs& e) override;
    void OnFocusChanged() override;

private:
    // Children are managed through SetHeader only.
    using Panel::Add;
    using Panel::Emplace;
    using Panel::InsertAt;
    using Panel::RemoveAt;
    using Panel::Clear;

    void UserSelect(int index);
    void SetHover(int index);
    IDWriteTextFormat* IconFormat();
    void DrawItem(const DrawingContext& dc, int index);

    FrameworkElement* header_ = nullptr;   // owned by children_
    std::vector<NavRailItem> items_;
    int selectedIndex_ = -1;
    int hoverIndex_ = -1;
    int keyIndex_ = 0;                     // keyboard cursor while focused
    // Focus ring only for keyboard focus: a click focuses the rail too (click-to-
    // focus runs before OnPointerPressed), and that must not draw a ring.
    bool keyboardCue_ = false;
    float itemsTop_ = 0.0f;                // window-DIP y of item 0, set in Arrange
    std::optional<D2D1_COLOR_F> background_;
    ComPtr<IDWriteTextFormat> iconFormat_;
    Event<NavRail, int> selectionChanged_;
};

}  // namespace fluent
