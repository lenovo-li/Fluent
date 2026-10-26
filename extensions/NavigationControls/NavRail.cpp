// NavRail.cpp
#include "NavRail.h"
#include "../../FluentUI/styling/ThemeTokens.h"
#include "../../FluentUI/styling/FocusVisual.h"
#include "../../FluentUI/graphics/DWriteContext.h"
#include "../../FluentUI/window/WindowServices.h"
#include <algorithm>
#include <string>

namespace fluent {

namespace {
constexpr float kSelectedFillAlpha = 0.12f;  // accent wash behind the selected item
constexpr float kItemCorner = 6.0f;

// Pane mode (Win11 NavigationView): neutral washes of the text colour, so the same
// alphas read correctly over light and dark Mica.
constexpr float kPaneHoverAlpha = 0.045f;
constexpr float kPaneSelectedAlpha = 0.07f;
constexpr float kPaneDisabledAlpha = 0.36f;
constexpr float kPaneItemCorner = 4.0f;
constexpr float kPaneIndicatorHeight = 16.0f;
constexpr float kPaneWidthTau = 0.05f;  // ~150 ms to settle

bool FontFamilyInstalled(IDWriteFactory* f, const wchar_t* name) {
    ComPtr<IDWriteFontCollection> fonts;
    if (FAILED(f->GetSystemFontCollection(&fonts, FALSE)) || !fonts) return false;
    UINT32 index = 0;
    BOOL exists = FALSE;
    return SUCCEEDED(fonts->FindFamilyName(name, &index, &exists)) && exists;
}

bool SystemAnimationEnabled() {
    BOOL enabled = TRUE;
    if (!SystemParametersInfoW(SPI_GETCLIENTAREAANIMATION, 0, &enabled, 0)) return true;
    return enabled != FALSE;
}

ComPtr<IDWriteTextFormat> CreateIconFormat(DWriteContext* dw, float size) {
    ComPtr<IDWriteTextFormat> format;
    if (!dw || !dw->Factory()) return format;
    IDWriteFactory* f = dw->Factory();
    const wchar_t* family = FontFamilyInstalled(f, L"Segoe Fluent Icons")
                                ? L"Segoe Fluent Icons" : L"Segoe MDL2 Assets";
    HRESULT hr = f->CreateTextFormat(family, nullptr, DWRITE_FONT_WEIGHT_NORMAL,
                                     DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL,
                                     size, L"", &format);
    if (FAILED(hr) || !format) {
        char buf[96];
        sprintf_s(buf, "[NavigationControls.NavRail] icon format failed hr=0x%08X\n",
                  static_cast<unsigned>(hr));
        OutputDebugStringA(buf);
        return nullptr;
    }
    format->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
    format->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
    format->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
    return format;
}
}  // namespace

NavRail::NavRail() {
    SetFocusable(true);
}

FrameworkElement* NavRail::SetHeader(std::unique_ptr<FrameworkElement> header) {
    if (header_) {
        for (size_t i = 0; i < ChildCount(); ++i)
            if (ChildAt(i) == header_) { RemoveAt(i); break; }
        header_ = nullptr;
    }
    if (header) header_ = Add(std::move(header));
    InvalidateDirty(DirtyFlags::Measure);
    return header_;
}

void NavRail::SetItems(std::vector<NavRailItem> items) {
    items_ = std::move(items);
    if (selectedIndex_ >= TotalCount()) selectedIndex_ = -1;
    hoverIndex_ = -1;
    keyIndex_ = std::max(0, selectedIndex_);
    InvalidateDirty(DirtyFlags::Measure);
}

void NavRail::SetFooterItems(std::vector<NavRailItem> items) {
    footer_ = std::move(items);
    if (selectedIndex_ >= TotalCount()) selectedIndex_ = -1;
    hoverIndex_ = -1;
    InvalidateDirty(DirtyFlags::Measure);
}

const NavRailItem* NavRail::ItemAt(int index) const {
    if (index < 0) return nullptr;
    if (index < static_cast<int>(items_.size())) return &items_[index];
    const int f = index - static_cast<int>(items_.size());
    return f < static_cast<int>(footer_.size()) ? &footer_[f] : nullptr;
}

NavRailItem* NavRail::ItemAt(int index) {
    return const_cast<NavRailItem*>(static_cast<const NavRail*>(this)->ItemAt(index));
}

void NavRail::SetBadge(int index, bool show) {
    NavRailItem* item = ItemAt(index);
    if (!item || item->showBadge == show) return;
    item->showBadge = show;
    Invalidate();
}

void NavRail::SetItemEnabled(int index, bool enabled) {
    NavRailItem* item = ItemAt(index);
    if (!item || item->enabled == enabled) return;
    item->enabled = enabled;
    if (!enabled && hoverIndex_ == index) hoverIndex_ = -1;
    Invalidate();
}

bool NavRail::Selectable(int index) const {
    const NavRailItem* item = ItemAt(index);
    return item && item->enabled && !(paneMode_ && item->header);
}

void NavRail::SetSelectedIndex(int index) {
    if (index < -1 || index >= TotalCount()) return;
    if (index == selectedIndex_) return;
    selectedIndex_ = index;
    if (index >= 0) keyIndex_ = index;
    Invalidate();
}

void NavRail::UserSelect(int index) {
    if (index < 0 || index >= TotalCount() || !Selectable(index)) return;
    keyIndex_ = index;
    if (index == selectedIndex_) { Invalidate(); return; }
    selectedIndex_ = index;
    Invalidate();
    OutputDebugStringA(("[NavigationControls.NavRail] select " +
                        std::to_string(index) + "\n").c_str());
    int payload = index;
    selectionChanged_.Raise(*this, payload);
}

void NavRail::SetHover(int index) {
    if (index == hoverIndex_) return;
    hoverIndex_ = index;
    std::wstring tip;
    if (paneMode_) {
        // Expanded items show their label already; tooltips are for icons only.
        if (index == kBackTarget) tip = backTooltip_;
        else if (index == kToggleTarget) tip = toggleTooltip_;
        else if (const NavRailItem* item = ItemAt(index); item && !expanded_) tip = item->label;
    } else if (index >= 0) {
        tip = items_[index].label;
    }
    SetTooltip(tip);
    Invalidate();
}

// --- Pane mode setters -----------------------------------------------------

void NavRail::SetPaneMode(bool enabled) {
    if (paneMode_ == enabled) return;
    paneMode_ = enabled;
    width_.SetImmediate(expanded_ ? expandedWidth_ : kPaneCompactWidth);
    hoverIndex_ = -1;
    InvalidateDirty(DirtyFlags::Measure);
}

void NavRail::SetExpanded(bool expanded, bool animate) {
    if (expanded_ == expanded) return;
    expanded_ = expanded;
    const float target = expanded_ ? expandedWidth_ : kPaneCompactWidth;
    if (!animate || !IsAttached() || !SystemAnimationEnabled()) {
        width_.SetImmediate(target);
    } else if (WindowServices* w = Window()) {
        w->RequestAnimationUpdate();  // a programmatic change has no input to wake the tick
    }
    char buf[80];
    sprintf_s(buf, "[NavigationControls.NavRail] pane expanded=%d animate=%d\n",
              expanded_ ? 1 : 0, animate ? 1 : 0);
    OutputDebugStringA(buf);
    InvalidateDirty(DirtyFlags::Measure);
}

void NavRail::SetExpandedWidth(float dip) {
    expandedWidth_ = std::max(kPaneCompactWidth, dip);
    if (expanded_) width_.SetImmediate(expandedWidth_);
    InvalidateDirty(DirtyFlags::Measure);
}

void NavRail::SetBackButtonVisible(bool visible) {
    if (backVisible_ == visible) return;
    backVisible_ = visible;
    InvalidateDirty(DirtyFlags::Measure);
}

void NavRail::SetBackButtonEnabled(bool enabled) {
    if (backEnabled_ == enabled) return;
    backEnabled_ = enabled;
    Invalidate();
}

float NavRail::CurrentWidth() const {
    return paneMode_ ? width_.value : kRailWidth;
}

bool NavRail::WantsAnimationTick() const {
    return paneMode_ && width_.Animating(expanded_ ? expandedWidth_ : kPaneCompactWidth, 0.5f);
}

void NavRail::OnAnimationTick(float dtSec) {
    if (!paneMode_) return;
    width_.Approach(expanded_ ? expandedWidth_ : kPaneCompactWidth, dtSec, kPaneWidthTau, 0.5f);
    InvalidateDirty(DirtyFlags::Measure);
}

RectDip NavRail::ItemRect(int index) const {
    if (paneMode_) return PaneTargetRect(index);
    if (index < 0 || index >= static_cast<int>(items_.size())) return {};
    const float x = bounds_.x + (bounds_.w - kItemSize) * 0.5f;
    const float y = itemsTop_ + static_cast<float>(index) * (kItemSize + kItemGap);
    return RectDip{x, y, kItemSize, kItemSize};
}

int NavRail::HitTestItem(float dipX, float dipY) const {
    if (paneMode_) {
        const int t = PaneHitTest(dipX, dipY);
        return t >= 0 ? t : -1;
    }
    for (int i = 0; i < static_cast<int>(items_.size()); ++i)
        if (ItemRect(i).contains(dipX, dipY)) return i;
    return -1;
}

// --- Pane mode geometry ----------------------------------------------------
//
// Top to bottom: [back] [toggle] items... ; footer items pinned to the bottom
// (never above the end of the items). Back / toggle are compact square-ish
// buttons; items span the pane width minus kPaneInset on each side.

float NavRail::PaneItemHeight(const NavRailItem& item) const {
    if (!item.header) return kPaneItemHeight;
    // A header grows from a separator line into a caption as the pane opens, so
    // the items below glide instead of jumping.
    const float t = std::clamp((width_.value - kPaneCompactWidth) /
                               std::max(1.0f, expandedWidth_ - kPaneCompactWidth), 0.0f, 1.0f);
    return kPaneSeparatorHeight + (kPaneHeaderHeight - kPaneSeparatorHeight) * t;
}

RectDip NavRail::PaneTargetRect(int target) const {
    const float x = bounds_.x + kPaneInset;
    const float w = std::max(0.0f, bounds_.w - 2.0f * kPaneInset);
    const float buttonW = kPaneCompactWidth - 2.0f * kPaneInset;
    float y = bounds_.y + kPaneInset;

    if (backVisible_) {
        if (target == kBackTarget) return RectDip{x, y, buttonW, kPaneItemHeight};
        y += kPaneItemHeight + kPaneItemGap;
    } else if (target == kBackTarget) {
        return {};
    }
    if (target == kToggleTarget) return RectDip{x, y, buttonW, kPaneItemHeight};
    y += kPaneItemHeight + kPaneItemGap;

    const int top = static_cast<int>(items_.size());
    for (int i = 0; i < top; ++i) {
        const float h = PaneItemHeight(items_[i]);
        if (i == target) return RectDip{x, y, w, h};
        y += h + kPaneItemGap;
    }
    if (target < top || target >= TotalCount()) return {};

    float footerH = 0.0f;
    for (const NavRailItem& item : footer_) footerH += PaneItemHeight(item) + kPaneItemGap;
    float fy = std::max(y, bounds_.bottom() - kPaneInset - footerH + kPaneItemGap);
    for (int f = 0; f < static_cast<int>(footer_.size()); ++f) {
        const float h = PaneItemHeight(footer_[f]);
        if (top + f == target) return RectDip{x, fy, w, h};
        fy += h + kPaneItemGap;
    }
    return {};
}

int NavRail::PaneHitTest(float dipX, float dipY) const {
    if (backVisible_ && PaneTargetRect(kBackTarget).contains(dipX, dipY)) return kBackTarget;
    if (PaneTargetRect(kToggleTarget).contains(dipX, dipY)) return kToggleTarget;
    for (int i = 0; i < TotalCount(); ++i) {
        const NavRailItem* item = ItemAt(i);
        if (item->header) continue;
        if (PaneTargetRect(i).contains(dipX, dipY)) return i;
    }
    return -1;
}

std::vector<int> NavRail::PaneKeyStops() const {
    std::vector<int> stops;
    if (backVisible_ && backEnabled_) stops.push_back(kBackTarget);
    stops.push_back(kToggleTarget);
    for (int i = 0; i < TotalCount(); ++i)
        if (Selectable(i)) stops.push_back(i);
    return stops;
}

void NavRail::PaneActivate(int target) {
    if (target == kBackTarget) {
        if (!backVisible_ || !backEnabled_) return;
        OutputDebugStringA("[NavigationControls.NavRail] back\n");
        int payload = 0;
        backRequested_.Raise(*this, payload);
    } else if (target == kToggleTarget) {
        SetExpanded(!expanded_);
        bool payload = expanded_;
        expandedChanged_.Raise(*this, payload);
        // Tooltips depend on the state (labels are visible when expanded).
        const int hover = hoverIndex_;
        hoverIndex_ = -1;
        SetHover(hover);
    } else {
        UserSelect(target);
    }
}

SizeDip NavRail::MeasureOverride(float availW, float availH) {
    UNREFERENCED_PARAMETER(availW);
    if (paneMode_) {
        float h = kPaneInset + kPaneItemHeight + kPaneItemGap;
        if (backVisible_) h += kPaneItemHeight + kPaneItemGap;
        for (int i = 0; i < TotalCount(); ++i) h += PaneItemHeight(*ItemAt(i)) + kPaneItemGap;
        return SizeDip{width_.value, h + kPaneInset};
    }
    float h = kTopPadding;
    if (header_ && header_->IsVisible()) {
        header_->MeasureCached(kRailWidth, availH);
        h += header_->Desired().h + kHeaderGap;
    }
    const float n = static_cast<float>(items_.size());
    if (n > 0) h += n * kItemSize + (n - 1.0f) * kItemGap;
    return SizeDip{kRailWidth, h};
}

void NavRail::ArrangeOverride(const RectDip& content) {
    if (paneMode_) {
        // The header element (logo) is a rail-mode feature; park it out of view.
        if (header_) ArrangeChild(header_, RectDip{content.x, content.y, 0.0f, 0.0f});
        return;
    }
    float y = content.y + kTopPadding;
    if (header_ && header_->IsVisible()) {
        const SizeDip d = header_->Desired();
        const float w = std::min(d.w, content.w);
        ArrangeChild(header_, RectDip{content.x + (content.w - w) * 0.5f, y, w, d.h});
        y += d.h + kHeaderGap;
    }
    itemsTop_ = y;
}

UIElement* NavRail::HitTestDeep(float dipX, float dipY) {
    // The header (logo) is decorative: the whole rail is one input target, so
    // hover/tooltip tracking never jumps between the rail and its header.
    return HitTest(dipX, dipY) ? this : nullptr;
}

void NavRail::CollectFocusables(std::vector<UIElement*>& out) {
    // One tab stop for the whole rail; arrows move within it.
    if (IsFocusable() && (paneMode_ || !items_.empty())) out.push_back(this);
}

IDWriteTextFormat* NavRail::IconFormat() {
    if (!iconFormat_) iconFormat_ = CreateIconFormat(Dwrite(), kIconSize);
    return iconFormat_.Get();
}

IDWriteTextFormat* NavRail::PaneIconFormat() {
    if (!paneIconFormat_) paneIconFormat_ = CreateIconFormat(Dwrite(), kPaneIconSize);
    return paneIconFormat_.Get();
}

void NavRail::Render(const DrawingContext& dc) {
    const ColorTokens& c = Theme().colors;
    dc.FillRect(D2D1::RectF(bounds_.x, bounds_.y, bounds_.right(), bounds_.bottom()),
                background_.value_or(c.windowBackground));
    if (paneMode_) {
        DrawPane(dc);
        return;
    }
    // Header (logo) and any other children.
    Panel::Render(dc);
    for (int i = 0; i < static_cast<int>(items_.size()); ++i) DrawItem(dc, i);
}

void NavRail::DrawItem(const DrawingContext& dc, int index) {
    const ColorTokens& c = Theme().colors;
    const NavRailItem& item = items_[index];
    const RectDip r = ItemRect(index);
    const D2D1_RECT_F rf = D2D1::RectF(r.x, r.y, r.right(), r.bottom());
    const bool selected = index == selectedIndex_;

    if (selected) {
        dc.FillRoundedRect(D2D1::RoundedRect(rf, kItemCorner, kItemCorner),
                           WithAlphaScale(c.accent, kSelectedFillAlpha));
        // Accent bar on the item's left edge, the full item height.
        dc.FillRoundedRect(D2D1::RoundedRect(
            D2D1::RectF(r.x, r.y, r.x + kAccentBarWidth, r.bottom()),
            kAccentBarWidth * 0.5f, kAccentBarWidth * 0.5f), c.accent);
    } else if (index == hoverIndex_) {
        dc.FillRoundedRect(D2D1::RoundedRect(rf, kItemCorner, kItemCorner),
                           c.controlFillHover);
    }

    if (IDWriteTextFormat* fmt = IconFormat(); fmt && !item.glyph.empty()) {
        dc.DrawText(item.glyph.c_str(), static_cast<UINT32>(item.glyph.size()), fmt, rf,
                    item.enabled ? c.textPrimary : WithAlphaScale(c.textPrimary, kPaneDisabledAlpha));
    }

    if (item.showBadge) {
        // Top-right of the glyph box, overlapping its corner like the reference.
        const float cx = r.x + r.w * 0.5f + kIconSize * 0.5f;
        const float cy = r.y + r.h * 0.5f - kIconSize * 0.5f;
        dc.FillEllipse(D2D1::Ellipse(D2D1::Point2F(cx, cy), kBadgeSize * 0.5f,
                                     kBadgeSize * 0.5f), c.severityErrorStroke);
    }

    if (IsFocused() && keyboardCue_ && index == keyIndex_) {
        FocusRingSpec spec;
        spec.inset = -2.0f;  // inside the item box: the rail has no room outside it
        spec.cornerRadius = kItemCorner;
        DrawFocusRing(dc, r, c, spec);
    }
}

// --- Pane mode drawing -----------------------------------------------------

void NavRail::DrawPane(const DrawingContext& dc) {
    // Labels are cut by the rail edge while the width animates.
    auto clip = dc.PushClip(D2D1::RectF(bounds_.x, bounds_.y, bounds_.right(), bounds_.bottom()));
    if (backVisible_) DrawPaneButton(dc, kBackTarget, L"\xE72B", backEnabled_);   // Back
    DrawPaneButton(dc, kToggleTarget, L"\xE700", true);                            // GlobalNavButton
    for (int i = 0; i < TotalCount(); ++i) DrawPaneItem(dc, i);

    if (IsFocused() && keyboardCue_) {
        const RectDip r = PaneTargetRect(keyIndex_);
        if (r.w > 0.0f) {
            FocusRingSpec spec;
            spec.inset = -1.0f;
            spec.cornerRadius = kPaneItemCorner;
            DrawFocusRing(dc, r, Theme().colors, spec);
        }
    }
}

void NavRail::DrawPaneButton(const DrawingContext& dc, int target, const wchar_t* glyph,
                             bool enabled) {
    const ColorTokens& c = Theme().colors;
    const RectDip r = PaneTargetRect(target);
    const D2D1_RECT_F rf = D2D1::RectF(r.x, r.y, r.right(), r.bottom());
    if (enabled && hoverIndex_ == target) {
        dc.FillRoundedRect(D2D1::RoundedRect(rf, kPaneItemCorner, kPaneItemCorner),
                           WithAlphaScale(c.textPrimary, kPaneHoverAlpha));
    }
    if (IDWriteTextFormat* fmt = PaneIconFormat()) {
        dc.DrawText(glyph, static_cast<UINT32>(wcslen(glyph)), fmt, rf,
                    enabled ? c.textPrimary : WithAlphaScale(c.textPrimary, kPaneDisabledAlpha));
    }
}

void NavRail::DrawPaneItem(const DrawingContext& dc, int index) {
    const ColorTokens& c = Theme().colors;
    const NavRailItem& item = *ItemAt(index);
    const RectDip r = PaneTargetRect(index);
    if (r.w <= 0.0f || r.h <= 0.0f) return;
    DWriteContext* dw = Dwrite();

    if (item.header) {
        const float t = std::clamp((r.h - kPaneSeparatorHeight) /
                                   (kPaneHeaderHeight - kPaneSeparatorHeight), 0.0f, 1.0f);
        // Collapsed: a hairline. Expanded: the caption (the label clipped by the rail edge).
        if (t < 1.0f) {
            const float y = r.y + r.h * 0.5f;
            dc.FillRect(D2D1::RectF(r.x + 4.0f, y, r.right() - 4.0f, y + 1.0f),
                        WithAlphaScale(c.controlStrokeDefault, 1.0f - t));
        }
        if (t > 0.0f && dw && !item.label.empty()) {
            if (IDWriteTextFormat* fmt = dw->Format(Theme().typography.bodySize,
                    DWRITE_FONT_WEIGHT_SEMI_BOLD, DWRITE_TEXT_ALIGNMENT_LEADING,
                    DWRITE_PARAGRAPH_ALIGNMENT_CENTER, DWRITE_WORD_WRAPPING_NO_WRAP)) {
                const D2D1_RECT_F tr = D2D1::RectF(r.x + 12.0f, r.y, r.right() - 8.0f, r.bottom());
                auto clip = dc.PushClip(tr);
                dc.DrawText(item.label.c_str(), static_cast<UINT32>(item.label.size()), fmt, tr,
                            WithAlphaScale(c.textPrimary, t));
            }
        }
        return;
    }

    const bool selected = index == selectedIndex_;
    const bool hovered = item.enabled && index == hoverIndex_;
    const D2D1_RECT_F rf = D2D1::RectF(r.x, r.y, r.right(), r.bottom());
    if (selected || hovered) {
        const float a = !selected ? kPaneHoverAlpha
                      : hovered   ? kPaneSelectedAlpha * 0.75f  // Win11: selected+hover is lighter
                                  : kPaneSelectedAlpha;
        dc.FillRoundedRect(D2D1::RoundedRect(rf, kPaneItemCorner, kPaneItemCorner),
                           WithAlphaScale(c.textPrimary, a));
    }
    if (selected) {
        // Win11 selection pill: short accent bar centred on the item's left edge.
        const float cy = r.y + r.h * 0.5f;
        dc.FillRoundedRect(D2D1::RoundedRect(
            D2D1::RectF(r.x, cy - kPaneIndicatorHeight * 0.5f, r.x + kAccentBarWidth,
                        cy + kPaneIndicatorHeight * 0.5f),
            kAccentBarWidth * 0.5f, kAccentBarWidth * 0.5f), c.accent);
    }

    const D2D1_COLOR_F ink = item.enabled ? c.textPrimary
                                          : WithAlphaScale(c.textPrimary, kPaneDisabledAlpha);
    // The icon keeps its place (centred in the compact column) at any width.
    const float iconBox = kPaneCompactWidth - 2.0f * kPaneInset;
    const D2D1_RECT_F ir = D2D1::RectF(r.x, r.y, r.x + iconBox, r.bottom());
    if (IDWriteTextFormat* fmt = PaneIconFormat(); fmt && !item.glyph.empty()) {
        dc.DrawText(item.glyph.c_str(), static_cast<UINT32>(item.glyph.size()), fmt, ir, ink);
    }
    if (item.showBadge) {
        const float cx = r.x + iconBox * 0.5f + kPaneIconSize * 0.5f;
        const float cy = r.y + r.h * 0.5f - kPaneIconSize * 0.5f;
        dc.FillEllipse(D2D1::Ellipse(D2D1::Point2F(cx, cy), kBadgeSize * 0.5f,
                                     kBadgeSize * 0.5f), c.severityErrorStroke);
    }
    if (dw && !item.label.empty() && r.w > kPaneLabelOffset) {
        if (IDWriteTextFormat* fmt = dw->Format(Theme().typography.bodySize,
                DWRITE_FONT_WEIGHT_NORMAL, DWRITE_TEXT_ALIGNMENT_LEADING,
                DWRITE_PARAGRAPH_ALIGNMENT_CENTER, DWRITE_WORD_WRAPPING_NO_WRAP)) {
            const D2D1_RECT_F tr = D2D1::RectF(r.x + kPaneLabelOffset, r.y, r.right() - 8.0f,
                                               r.bottom());
            if (tr.right > tr.left) {
                auto clip = dc.PushClip(tr);
                dc.DrawText(item.label.c_str(), static_cast<UINT32>(item.label.size()), fmt, tr, ink);
            }
        }
    }
}

// --- Input -----------------------------------------------------------------

void NavRail::OnFocusChanged() {
    // Focus gained by Tab keeps the cue; a click clears it right after (the
    // input manager focuses the hit element before routing the press).
    keyboardCue_ = IsFocused();
    if (paneMode_ && IsFocused()) {
        // Start the cursor on the selected item, or the first stop.
        const std::vector<int> stops = PaneKeyStops();
        if (std::find(stops.begin(), stops.end(), keyIndex_) == stops.end())
            keyIndex_ = Selectable(selectedIndex_) ? selectedIndex_ : stops.front();
    }
    Invalidate();
}

void NavRail::OnPointerPressed(PointerEventArgs& e) {
    Panel::OnPointerPressed(e);
    if (keyboardCue_) { keyboardCue_ = false; Invalidate(); }
    if (e.handled || e.button != PointerButton::Left) return;
    if (paneMode_) {
        const int target = PaneHitTest(e.position.x, e.position.y);
        if (target == -1) return;
        keyIndex_ = target;
        PaneActivate(target);
        e.handled = true;
        return;
    }
    const int index = HitTestItem(e.position.x, e.position.y);
    if (index >= 0) { UserSelect(index); e.handled = true; }
}

void NavRail::OnPointerMoved(PointerEventArgs& e) {
    if (paneMode_) {
        int target = PaneHitTest(e.position.x, e.position.y);
        if (target == kBackTarget && !backEnabled_) target = -1;
        if (target >= 0 && !Selectable(target)) target = -1;
        SetHover(target);
        return;
    }
    SetHover(HitTestItem(e.position.x, e.position.y));
}

void NavRail::OnPointerLeft() { SetHover(-1); }

void NavRail::OnKeyDownRouted(KeyEventArgs& e) {
    if (paneMode_) {
        const std::vector<int> stops = PaneKeyStops();
        auto it = std::find(stops.begin(), stops.end(), keyIndex_);
        const int pos = it == stops.end() ? 0 : static_cast<int>(it - stops.begin());
        const int last = static_cast<int>(stops.size()) - 1;
        switch (e.vk) {
            case VK_UP:   keyIndex_ = stops[std::max(0, pos - 1)]; Invalidate(); break;
            case VK_DOWN: keyIndex_ = stops[std::min(last, pos + 1)]; Invalidate(); break;
            case VK_RETURN:
            case VK_SPACE: PaneActivate(stops[pos]); break;
            default: return;
        }
        keyboardCue_ = true;
        e.handled = true;
        return;
    }
    const int n = static_cast<int>(items_.size());
    if (n == 0) return;
    switch (e.vk) {
        case VK_UP:   keyIndex_ = std::max(0, keyIndex_ - 1); Invalidate(); break;
        case VK_DOWN: keyIndex_ = std::min(n - 1, keyIndex_ + 1); Invalidate(); break;
        case VK_RETURN:
        case VK_SPACE: UserSelect(keyIndex_); break;
        default: return;
    }
    keyboardCue_ = true;
    e.handled = true;
}

}  // namespace fluent
