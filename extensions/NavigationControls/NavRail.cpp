// NavRail.cpp
#include "NavRail.h"
#include "../../FluentUI/styling/ThemeTokens.h"
#include "../../FluentUI/styling/FocusVisual.h"
#include "../../FluentUI/graphics/DWriteContext.h"
#include <algorithm>
#include <string>

namespace fluent {

namespace {
constexpr float kSelectedFillAlpha = 0.12f;  // accent wash behind the selected item
constexpr float kItemCorner = 6.0f;

bool FontFamilyInstalled(IDWriteFactory* f, const wchar_t* name) {
    ComPtr<IDWriteFontCollection> fonts;
    if (FAILED(f->GetSystemFontCollection(&fonts, FALSE)) || !fonts) return false;
    UINT32 index = 0;
    BOOL exists = FALSE;
    return SUCCEEDED(fonts->FindFamilyName(name, &index, &exists)) && exists;
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
    if (selectedIndex_ >= static_cast<int>(items_.size())) selectedIndex_ = -1;
    hoverIndex_ = -1;
    keyIndex_ = std::max(0, selectedIndex_);
    InvalidateDirty(DirtyFlags::Measure);
}

void NavRail::SetBadge(int index, bool show) {
    if (index < 0 || index >= static_cast<int>(items_.size())) return;
    if (items_[index].showBadge == show) return;
    items_[index].showBadge = show;
    Invalidate();
}

void NavRail::SetSelectedIndex(int index) {
    if (index < -1 || index >= static_cast<int>(items_.size())) return;
    if (index == selectedIndex_) return;
    selectedIndex_ = index;
    if (index >= 0) keyIndex_ = index;
    Invalidate();
}

void NavRail::UserSelect(int index) {
    if (index < 0 || index >= static_cast<int>(items_.size())) return;
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
    SetTooltip(index >= 0 ? items_[index].label : std::wstring());
    Invalidate();
}

RectDip NavRail::ItemRect(int index) const {
    if (index < 0 || index >= static_cast<int>(items_.size())) return {};
    const float x = bounds_.x + (bounds_.w - kItemSize) * 0.5f;
    const float y = itemsTop_ + static_cast<float>(index) * (kItemSize + kItemGap);
    return RectDip{x, y, kItemSize, kItemSize};
}

int NavRail::HitTestItem(float dipX, float dipY) const {
    for (int i = 0; i < static_cast<int>(items_.size()); ++i)
        if (ItemRect(i).contains(dipX, dipY)) return i;
    return -1;
}

SizeDip NavRail::MeasureOverride(float availW, float availH) {
    UNREFERENCED_PARAMETER(availW);
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
    if (IsFocusable() && !items_.empty()) out.push_back(this);
}

IDWriteTextFormat* NavRail::IconFormat() {
    if (iconFormat_) return iconFormat_.Get();
    DWriteContext* dw = Dwrite();
    if (!dw || !dw->Factory()) return nullptr;
    IDWriteFactory* f = dw->Factory();
    const wchar_t* family = FontFamilyInstalled(f, L"Segoe Fluent Icons")
                                ? L"Segoe Fluent Icons" : L"Segoe MDL2 Assets";
    HRESULT hr = f->CreateTextFormat(family, nullptr, DWRITE_FONT_WEIGHT_NORMAL,
                                     DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL,
                                     kIconSize, L"", &iconFormat_);
    if (FAILED(hr) || !iconFormat_) {
        char buf[96];
        sprintf_s(buf, "[NavigationControls.NavRail] icon format failed hr=0x%08X\n",
                  static_cast<unsigned>(hr));
        OutputDebugStringA(buf);
        return nullptr;
    }
    iconFormat_->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
    iconFormat_->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
    iconFormat_->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
    return iconFormat_.Get();
}

void NavRail::Render(const DrawingContext& dc) {
    const ColorTokens& c = Theme().colors;
    dc.FillRect(D2D1::RectF(bounds_.x, bounds_.y, bounds_.right(), bounds_.bottom()),
                background_.value_or(c.windowBackground));
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
                    c.textPrimary);
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

void NavRail::OnFocusChanged() {
    // Focus gained by Tab keeps the cue; a click clears it right after (the
    // input manager focuses the hit element before routing the press).
    keyboardCue_ = IsFocused();
    Invalidate();
}

void NavRail::OnPointerPressed(PointerEventArgs& e) {
    Panel::OnPointerPressed(e);
    if (keyboardCue_) { keyboardCue_ = false; Invalidate(); }
    if (e.handled || e.button != PointerButton::Left) return;
    const int index = HitTestItem(e.position.x, e.position.y);
    if (index >= 0) { UserSelect(index); e.handled = true; }
}

void NavRail::OnPointerMoved(PointerEventArgs& e) {
    SetHover(HitTestItem(e.position.x, e.position.y));
}

void NavRail::OnPointerLeft() { SetHover(-1); }

void NavRail::OnKeyDownRouted(KeyEventArgs& e) {
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
