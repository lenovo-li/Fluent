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
// select it; the focus ring marks the cursor only after keyboard use. Tooltip: the framework tracks tooltips per element, so the
// rail swaps its tooltip text to the hovered item's label; the text shown is the one
// current when the hover delay elapses.
//
// PANE MODE (opt-in, SetPaneMode(true)) — the Win11 NavigationView / Task Manager
// layout. Off by default, and everything above stays exactly as it was when off.
//
//   [<]  back button          optional, SetBackButtonVisible; raises BackRequested
//   [=]  pane toggle          expands / collapses; raises ExpandedChanged
//   [i]  Label                items: glyph + label (label shown when expanded)
//   ---  / SECTION            header item: separator collapsed, caption expanded
//   ...
//   [*]  Settings             footer items, pinned to the bottom
//
// Labels and captions that do not fit end in "…"; hovering one (header captions
// included) shows the full label as the tooltip, or NavRailItem::tooltip when set.
// Compact width kPaneCompactWidth (icons only, labels as tooltips), expanded width
// SetExpandedWidth (default kPaneExpandedWidth). The width eases between the two;
// it jumps when Windows "Animation effects" is off. Item indices are unified:
// SetItems indices first, then footer items (Items().size() + i). A disabled item
// is dimmed and cannot be selected or reached with the keyboard.
#pragma once

#include "../../FluentUI/layout/Panel.h"
#include "../../FluentUI/base/Event.h"
#include "../../FluentUI/input/RoutedEvent.h"
#include "../../FluentUI/animation/AnimatedValue.h"
#include <dwrite.h>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace fluent {

struct NavRailItem {
    std::wstring glyph;       // icon-font glyph, e.g. L"\xE896"
    std::wstring label;       // page name, used as tooltip (and drawn in pane mode)
    bool showBadge = false;   // red dot indicator
    bool enabled = true;      // false: dimmed, not selectable
    // Pane mode only: a non-selectable section caption (the label), drawn as a
    // separator line when the pane is collapsed. Ignored in rail mode.
    bool header = false;
    // Optional tooltip text. Pane mode: shown whenever set; when empty the label
    // is the tooltip only if it is not fully visible (collapsed, or ellipsised).
    // Rail mode: replaces the label as the tooltip.
    std::wstring tooltip;
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
    // Unified index (footer items included). A disabled selected item stays
    // selected; the host decides where to go instead.
    void SetItemEnabled(int index, bool enabled);

    // Programmatic selection does NOT raise SelectionChanged (same as WinUI's
    // SelectedItem setter path used for state restore). -1 clears.
    void SetSelectedIndex(int index);
    int SelectedIndex() const { return selectedIndex_; }

    // Rail fill. Default: Theme().colors.windowBackground.
    void SetRailBackground(const D2D1_COLOR_F& c) { background_ = c; Invalidate(); }

    // Raised on user selection (click / Enter / Space) with the new index.
    Event<NavRail, int>& SelectionChanged() { return selectionChanged_; }

    // --- Pane mode -------------------------------------------------------
    void SetPaneMode(bool enabled);
    bool PaneMode() const { return paneMode_; }
    // Bottom-pinned items (pane mode only), e.g. Settings.
    void SetFooterItems(std::vector<NavRailItem> items);
    const std::vector<NavRailItem>& FooterItems() const { return footer_; }
    // `animate` = ease the width (still instant when system animations are off).
    // Does not raise ExpandedChanged.
    void SetExpanded(bool expanded, bool animate = true);
    bool IsExpanded() const { return expanded_; }
    void SetExpandedWidth(float dip);
    void SetBackButtonVisible(bool visible);
    void SetBackButtonEnabled(bool enabled);
    void SetPaneToggleTooltip(std::wstring text) { toggleTooltip_ = std::move(text); }
    void SetBackButtonTooltip(std::wstring text) { backTooltip_ = std::move(text); }

    // Raised when the user toggles the pane, with the new state.
    Event<NavRail, bool>& ExpandedChanged() { return expandedChanged_; }
    // Raised when the user activates the back button. Payload is unused.
    Event<NavRail, int>& BackRequested() { return backRequested_; }

    // Panel overrides.
    void Render(const DrawingContext& dc) override;
    UIElement* HitTestDeep(float dipX, float dipY) override;
    void CollectFocusables(std::vector<UIElement*>& out) override;
    bool WantsAnimationTick() const override;
    void OnAnimationTick(float dtSec) override;

    // Layout constants, public so tests and hosts can align against them.
    static constexpr float kRailWidth = 88.0f;
    static constexpr float kTopPadding = 12.0f;
    static constexpr float kHeaderGap = 20.0f;
    static constexpr float kItemSize = 56.0f;
    static constexpr float kItemGap = 4.0f;
    static constexpr float kAccentBarWidth = 3.0f;
    static constexpr float kIconSize = 20.0f;
    static constexpr float kBadgeSize = 8.0f;

    // Pane mode geometry (Win11 NavigationView).
    static constexpr float kPaneCompactWidth = 48.0f;
    static constexpr float kPaneExpandedWidth = 240.0f;
    static constexpr float kPaneItemHeight = 36.0f;
    static constexpr float kPaneItemGap = 4.0f;
    static constexpr float kPaneInset = 4.0f;         // item box inset from the rail edges
    static constexpr float kPaneIconSize = 16.0f;
    static constexpr float kPaneLabelOffset = 44.0f;  // label x from the item box left
    static constexpr float kPaneHeaderHeight = 36.0f; // section caption (expanded)
    static constexpr float kPaneSeparatorHeight = 9.0f;  // section line (collapsed)

    // Window-DIP rect of item `index` (empty when out of range / not arranged).
    RectDip ItemRect(int index) const;
    int HitTestItem(float dipX, float dipY) const;

protected:
    // UI Automation peer (NavRailPeer.cpp): a List of ListItems + ExpandCollapse.
    std::unique_ptr<automation::AutomationPeer> CreateAutomationPeer() override;

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

    // Pane-mode hit targets that are not items.
    static constexpr int kBackTarget = -2;
    static constexpr int kToggleTarget = -3;

    void UserSelect(int index);
    void SetHover(int index);
    IDWriteTextFormat* IconFormat();
    IDWriteTextFormat* PaneIconFormat();
    void DrawItem(const DrawingContext& dc, int index);

    // Pane mode.
    int TotalCount() const { return static_cast<int>(items_.size() + footer_.size()); }
    const NavRailItem* ItemAt(int index) const;
    NavRailItem* ItemAt(int index);
    bool Selectable(int index) const;
    float PaneItemHeight(const NavRailItem& item) const;
    RectDip PaneTargetRect(int target) const;   // item index, kBackTarget or kToggleTarget
    // Header items are hit only for hover (tooltip) when `includeHeaders`.
    int PaneHitTest(float dipX, float dipY, bool includeHeaders = false) const;
    std::vector<int> PaneKeyStops() const;      // keyboard order
    void PaneActivate(int target);
    void DrawPane(const DrawingContext& dc);
    void DrawPaneButton(const DrawingContext& dc, int target, const wchar_t* glyph, bool enabled);
    void DrawPaneItem(const DrawingContext& dc, int index);
    // Single-line label ending in "…" when it does not fit `rect`. Returns true
    // when it was trimmed. `index` selects the per-item layout cache entry.
    bool DrawPaneLabel(const DrawingContext& dc, int index, const std::wstring& text,
                       IDWriteTextFormat* fmt, const D2D1_RECT_F& rect,
                       const D2D1_COLOR_F& color);
    std::wstring PaneTooltipFor(int target) const;
    float CurrentWidth() const;

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
    ComPtr<IDWriteTextFormat> paneIconFormat_;
    Event<NavRail, int> selectionChanged_;

    // Pane mode state.
    bool paneMode_ = false;
    std::vector<NavRailItem> footer_;
    bool expanded_ = false;
    float expandedWidth_ = kPaneExpandedWidth;
    AnimatedValue width_{kPaneCompactWidth};  // current pane width (DIP)
    bool backVisible_ = false;
    bool backEnabled_ = true;
    std::wstring toggleTooltip_;
    std::wstring backTooltip_;
    // Per unified index: the label was ellipsised in the last paint (pane mode).
    std::vector<bool> labelTrimmed_;
    // Per unified index: the pane label's text layout, kept across paints. Every
    // hover change repaints all labels, and building an IDWriteTextLayout (plus an
    // ellipsis inline object when it overflows) per label per paint was pure
    // allocation churn. The layout is rebuilt only when the text or the format
    // changes (a typography change hands out a different cached format pointer);
    // a new box size, as on every frame of the width ease, just updates the
    // layout's max width/height in place. The cached format pointer stays valid
    // because DWriteContext never evicts formats. Device loss does not touch it:
    // DWrite objects are device-independent.
    struct PaneLabelLayout {
        ComPtr<IDWriteTextLayout> layout;
        ComPtr<IDWriteInlineObject> ellipsis;  // created once, on first overflow
        IDWriteTextFormat* fmt = nullptr;      // borrowed, identity only
        std::wstring text;
        float w = -1.0f;
        float h = -1.0f;
        float naturalW = 0.0f;  // untrimmed line width, measured once per build
        bool trimmed = false;
    };
    std::vector<PaneLabelLayout> labelLayouts_;
    Event<NavRail, bool> expandedChanged_;
    Event<NavRail, int> backRequested_;
};

}  // namespace fluent
