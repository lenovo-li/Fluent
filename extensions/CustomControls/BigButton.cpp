// BigButton.cpp — icon + title + subtitle on top of Button's own chrome.
#include "CustomControls.h"

#include "../../FluentUI/automation/ControlPeers.h"

namespace fluent::custom {

namespace {
// A Button whose subtitle is part of what it shows: HelpText = the subtitle (unless
// the app set a tooltip), so "Install — 1.2 GB required" reads as name + help.
class BigButtonPeer final : public automation::ButtonPeer {
public:
    explicit BigButtonPeer(BigButton& b) : ButtonPeer(b) {}
    std::wstring GetHelpText() const override {
        const std::wstring tip = ButtonPeer::GetHelpText();
        return tip.empty() ? static_cast<const BigButton&>(Owner()).Subtitle() : tip;
    }
};
}  // namespace

std::unique_ptr<automation::AutomationPeer> BigButton::CreateAutomationPeer() {
    return std::make_unique<BigButtonPeer>(*this);
}

void BigButton::Measure(float availW, float availH) {
    (void)availH;
    float w = IsAuto(Width()) ? std::min(availW, 200.0f) : Width();
    SetDesired(SizeDip(w, kHeight));
}

BigButton::Rows BigButton::LayoutRows(const RectDip& bounds, bool hasIcon,
                                      bool hasTitle, bool hasSubtitle) {
    // Stack only the rows that have content, then centre the stack vertically.
    // Centring the block (rather than pinning rows to fixed offsets) keeps a
    // label-only button looking exactly like a plain centred label, and the three
    // rows together (60 DIP) still fit inside the fixed 64 DIP height.
    const float iconH = hasIcon ? kIconRowH : 0.0f;
    const float titleH = hasTitle ? kTitleRowH : 0.0f;
    const float subH = hasSubtitle ? kSubtitleRowH : 0.0f;
    float y = bounds.y + (bounds.h - (iconH + titleH + subH)) * 0.5f;
    Rows r;
    r.icon = RectDip{bounds.x, y, bounds.w, iconH};
    y += iconH;
    r.title = RectDip{bounds.x, y, bounds.w, titleH};
    y += titleH;
    r.subtitle = RectDip{bounds.x, y, bounds.w, subH};
    return r;
}

void BigButton::RenderContent(const DrawingContext& dc) {
    DWriteContext* dw = Dwrite();
    if (!dw) return;

    const std::wstring& title = Text();
    const Rows rows = LayoutRows(Bounds(), !icon_.empty(), !title.empty(),
                                 !subtitle_.empty());

    // The same colour Button uses for its label: onAccent on an Accent fill,
    // textPrimary otherwise, the disabled colour when disabled.
    const ThemeSnapshot& theme = Theme();
    const D2D1_COLOR_F textColor = ButtonTextColor(CurrentKind(), State(),
                                                   AppearanceForRender(), theme.colors);
    // The subtitle is secondary text, except where textSecondary would be wrong:
    // on an Accent fill it has poor contrast, and it has no disabled variant, so
    // a disabled button's subtitle would look enabled. Those fall back to the
    // label colour, which already handles both.
    const bool plainSecondary =
        CurrentKind() != Kind::Accent && State() != VisualState::Disabled;
    const D2D1_COLOR_F subColor = plainSecondary ? theme.colors.textSecondary : textColor;

    auto draw = [&dc](const std::wstring& s, IDWriteTextFormat* fmt, const RectDip& r,
                      const D2D1_COLOR_F& color) {
        if (s.empty() || !fmt || r.h <= 0.0f) return;
        dc.DrawText(s.c_str(), static_cast<UINT32>(s.size()), fmt,
                    D2D1::RectF(r.x, r.y, r.right(), r.bottom()), color);
    };

    // DrawText goes through DrawingContext's typed path, so ancestor opacity
    // (a fading page) applies to all three rows without extra code.
    draw(icon_, dw->Format(kIconRowH), rows.icon, textColor);
    draw(title, dw->Format(theme.typography.bodySize, DWRITE_FONT_WEIGHT_SEMI_BOLD),
         rows.title, textColor);
    draw(subtitle_, dw->Format(theme.typography.captionSize), rows.subtitle, subColor);
}

} // namespace fluent::custom
