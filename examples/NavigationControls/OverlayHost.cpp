// OverlayHost.cpp
#include "OverlayHost.h"
#include "../../FluentUI/styling/ThemeTokens.h"
#include "../../FluentUI/input/FocusManager.h"
#include "../../FluentUI/core/UIContext.h"
#include <algorithm>
#include <cmath>

namespace fluent {

namespace {
constexpr float kGlyphStroke = 2.0f;
constexpr float kUnboundedH = 100000.0f;  // finite stand-in for an infinite offer

bool IsWithin(const Visual* e, const Visual* root) {
    for (const Visual* p = e; p; p = p->Parent())
        if (p == root) return true;
    return false;
}
}  // namespace

OverlayHost::OverlayHost() {
    // Focusable so Show() can park focus here: keys then route through the overlay
    // instead of whatever page control held focus before it opened.
    SetFocusable(true);
    SetVisible(false);
    SetOpacity(0.0f);

    title_ = Emplace<TextBlock>();
    title_->SetTypographyRole(TypographyRole::Subtitle);
    title_->SetWeight(DWRITE_FONT_WEIGHT_SEMI_BOLD);
    message_ = Emplace<TextBlock>();
    message_->SetWrap(true);
    progress_ = Emplace<ProgressBar>();
    secondary_ = Emplace<Button>();
    primary_ = Emplace<Button>();
    primary_->SetKind(Button::Kind::Accent);
    for (Button* b : {secondary_, primary_}) {
        b->SetMinWidth(kButtonMinWidth);
        b->SetCornerRadius(kPillRadius);
    }
    primarySub_ = primary_->Click().Subscribe<&OverlayHost::OnPrimaryClick>(this);
    secondarySub_ = secondary_->Click().Subscribe<&OverlayHost::OnSecondaryClick>(this);
}

void OverlayHost::Show(const OverlaySpec& spec) {
    spec_ = spec;
    title_->SetText(spec.title);
    title_->SetVisible(!spec.title.empty());
    message_->SetText(spec.message);
    message_->SetVisible(!spec.message.empty());
    progress_->SetVisible(spec.showProgress);
    if (spec.showProgress) progress_->SetValue(0.0f);
    primary_->SetText(spec.primaryText);
    primary_->SetVisible(!spec.primaryText.empty());
    secondary_->SetText(spec.secondaryText);
    secondary_->SetVisible(!spec.secondaryText.empty());

    const bool wasHidden = !IsVisible();
    shown_ = true;
    fadeTarget_ = 1.0f;
    SetVisible(true);
    // A Show during a fade-out reverses it from the current value (no flash).
    if (wasHidden) fade_.SetImmediate(FadeEnabled() ? 0.0f : 1.0f);
    else if (!FadeEnabled()) fade_.SetImmediate(1.0f);
    SetOpacity(fade_);
    InvalidateDirty(DirtyFlags::Measure);
    FocusSelf();
    OutputDebugStringA(spec.showProgress ? "[NavigationControls.OverlayHost] show (progress)\n"
                                         : "[NavigationControls.OverlayHost] show\n");
}

void OverlayHost::UpdateProgress(int percent, const std::wstring& message) {
    if (!shown_ || !spec_.showProgress) return;
    progress_->SetValue(static_cast<float>(std::clamp(percent, 0, 100)));
    spec_.message = message;
    message_->SetText(message);
    message_->SetVisible(!message.empty());
}

void OverlayHost::Hide() { BeginHide(false, OverlayResult::Dismissed); }

void OverlayHost::BeginHide(bool raise, OverlayResult result) {
    if (!shown_) return;
    shown_ = false;
    fadeTarget_ = 0.0f;
    DropFocus();
    if (!FadeEnabled()) {
        fade_.SetImmediate(0.0f);
        SetOpacity(0.0f);
        SetVisible(false);
    } else {
        Invalidate();  // schedules the frame that starts the fade tick
    }
    OutputDebugStringA("[NavigationControls.OverlayHost] hide\n");
    // Raised last: a handler may chain the next state with Show().
    if (raise) {
        OverlayResultArgs args;
        args.result = result;
        closed_.Raise(*this, args);
    }
}

void OverlayHost::FocusSelf() {
    if (!IsAttached()) return;
    if (FocusManager* fm = Context().focus) fm->SetFocus(this);
}

void OverlayHost::DropFocus() {
    if (!IsAttached()) return;
    FocusManager* fm = Context().focus;
    if (!fm) return;
    if (UIElement* f = fm->Focused(); f && IsWithin(f, this)) fm->ClearFocus();
}

void OverlayHost::CycleFocus(int step) {
    if (!IsAttached()) return;
    FocusManager* fm = Context().focus;
    if (!fm) return;
    Button* order[2];
    int n = 0;
    for (Button* b : {secondary_, primary_})
        if (b->IsVisible() && b->IsFocusable()) order[n++] = b;
    if (n == 0) { fm->SetFocus(this); return; }
    int cur = -1;
    for (int i = 0; i < n; ++i)
        if (order[i] == fm->Focused()) cur = i;
    const int next = (cur < 0) ? (step > 0 ? 0 : n - 1) : ((cur + step) % n + n) % n;
    fm->SetFocus(order[next]);
}

bool OverlayHost::FadeEnabled() const {
    BOOL on = TRUE;
    if (!SystemParametersInfoW(SPI_GETCLIENTAREAANIMATION, 0, &on, 0)) return true;
    return on != FALSE;
}

float OverlayHost::CardWidthFor(float slotW) {
    if (!std::isfinite(slotW)) return kCardWidth;
    return std::max(0.0f, std::min(kCardWidth, slotW - kCardMargin * 2.0f));
}

float OverlayHost::CardHeightFor(float cardW, float slotH) {
    const float innerW = std::max(0.0f, cardW - kCardPadding * 2.0f);
    const float offerH = std::isfinite(slotH) ? slotH : kUnboundedH;
    const bool hasIcon = spec_.icon != OverlayIcon::None;

    float titleH = 0.0f;
    if (title_->IsVisible()) {
        const float titleW = std::max(0.0f, innerW - (hasIcon ? kIconSize + kIconGap : 0.0f));
        title_->MeasureCached(titleW, offerH);
        titleH = title_->Desired().h;
    }
    headerH_ = std::max(hasIcon ? kIconSize : 0.0f, titleH);

    float bodyH = 0.0f;
    if (message_->IsVisible()) {
        message_->MeasureCached(innerW, offerH);
        bodyH += message_->Desired().h;
    }
    if (progress_->IsVisible()) {
        progress_->MeasureCached(innerW, offerH);
        bodyH += (bodyH > 0.0f ? kRowGap : 0.0f) + progress_->Desired().h;
    }

    buttonsH_ = 0.0f;
    for (Button* b : {secondary_, primary_}) {
        if (!b->IsVisible()) continue;
        b->MeasureCached(innerW, offerH);
        buttonsH_ = std::max(buttonsH_, b->Desired().h);
    }

    float h = kCardPadding + headerH_;
    if (bodyH > 0.0f) h += (headerH_ > 0.0f ? kRowGap : 0.0f) + bodyH;
    if (buttonsH_ > 0.0f) h += kButtonsGap + buttonsH_;
    h += kCardPadding;
    return std::max(kMinCardHeight, h);
}

SizeDip OverlayHost::MeasureOverride(float availW, float availH) {
    const float cardW = CardWidthFor(availW);
    const float cardH = CardHeightFor(cardW, availH);
    // Fill the slot (it is a full-window layer); the card size is the fallback
    // for an unbounded offer.
    return SizeDip{std::isfinite(availW) ? availW : cardW + kCardMargin * 2.0f,
                   std::isfinite(availH) ? availH : cardH + kCardMargin * 2.0f};
}

void OverlayHost::ArrangeOverride(const RectDip& content) {
    const float cardW = CardWidthFor(content.w);
    const float cardH = CardHeightFor(cardW, content.h);
    card_ = RectDip{content.x + (content.w - cardW) * 0.5f,
                    content.y + std::max(0.0f, (content.h - cardH) * 0.5f), cardW, cardH};

    const float left = card_.x + kCardPadding;
    const float innerW = std::max(0.0f, cardW - kCardPadding * 2.0f);
    const bool hasIcon = spec_.icon != OverlayIcon::None;
    float y = card_.y + kCardPadding;

    iconY_ = y + (headerH_ - kIconSize) * 0.5f;
    if (title_->IsVisible()) {
        const float tx = left + (hasIcon ? kIconSize + kIconGap : 0.0f);
        ArrangeChild(title_, RectDip{tx, y, std::max(0.0f, card_.right() - kCardPadding - tx),
                                     headerH_});
    }
    if (headerH_ > 0.0f) y += headerH_ + kRowGap;

    if (message_->IsVisible()) {
        ArrangeChild(message_, RectDip{left, y, innerW, message_->Desired().h});
        y += message_->Desired().h + kRowGap;
    }
    if (progress_->IsVisible())
        ArrangeChild(progress_, RectDip{left, y, innerW, progress_->Desired().h});

    // Buttons right-aligned on the card's bottom edge: [Secondary] [Primary].
    float right = card_.right() - kCardPadding;
    const float by = card_.bottom() - kCardPadding - buttonsH_;
    for (Button* b : {primary_, secondary_}) {
        if (!b->IsVisible()) continue;
        const float w = std::max(b->Desired().w, kButtonMinWidth);
        ArrangeChild(b, RectDip{right - w, by, w, buttonsH_});
        right -= w + kButtonSpacing;
    }
}

UIElement* OverlayHost::HitTestDeep(float dipX, float dipY) {
    if (!HitTest(dipX, dipY)) return nullptr;
    // Only the live dialog's buttons take input; everything else (scrim, card,
    // texts, and the whole layer while it fades out) is swallowed here.
    if (shown_) {
        for (Button* b : {primary_, secondary_})
            if (b->IsVisible())
                if (UIElement* hit = b->HitTestDeep(dipX, dipY)) return hit;
    }
    return this;
}

void OverlayHost::CollectFocusables(std::vector<UIElement*>& out) {
    if (!IsVisible() || !shown_) return;
    out.push_back(this);
    for (Button* b : {secondary_, primary_})
        if (b->IsVisible()) b->CollectFocusables(out);
}

void OverlayHost::OnPreviewKeyDown(KeyEventArgs& e) {
    // Tunnels root -> focused element, so this sees every key while focus is on
    // the overlay or one of its buttons.
    if (!shown_) return;
    const bool hasPrimary = primary_->IsVisible();
    const bool hasSecondary = secondary_->IsVisible();
    switch (e.vk) {
        case VK_ESCAPE:
            if (hasSecondary) Close(OverlayResult::Secondary);
            else if (hasPrimary) Close(OverlayResult::Dismissed);
            e.handled = true;  // Progress state: swallowed, never cancels
            return;
        case VK_TAB:
            CycleFocus(HasModifier(e.modifiers, ModifierKeys::Shift) ? -1 : 1);
            e.handled = true;
            return;
        case VK_LEFT: case VK_UP:
            CycleFocus(-1); e.handled = true; return;
        case VK_RIGHT: case VK_DOWN:
            CycleFocus(1); e.handled = true; return;
        case VK_RETURN:
        case VK_SPACE:
            // A focused button activates itself (it is further down the route).
            if (e.originalSource != this) return;
            if (hasPrimary && !hasSecondary && e.vk == VK_RETURN)
                Close(OverlayResult::Primary);
            e.handled = true;
            return;
        default:
            return;
    }
}

void OverlayHost::Render(const DrawingContext& dc) {
    const ThemeSnapshot& th = Theme();
    const ColorTokens& c = th.colors;

    // Scrim over the whole layer (nav rail + content; the title bar is not ours).
    const D2D1_COLOR_F scrim = th.dark ? WithAlphaScale(c.windowBackground, kScrimAlphaDark)
                                       : WithAlphaScale(c.textPrimary, kScrimAlphaLight);
    dc.FillRect(D2D1::RectF(bounds_.x, bounds_.y, bounds_.right(), bounds_.bottom()), scrim);

    // Soft shadow: three widening rings at low alpha, then the opaque card.
    const D2D1_COLOR_F shadowBase = th.dark ? c.windowBackground : c.textPrimary;
    for (int i = 3; i >= 1; --i) {
        const float g = static_cast<float>(i) * 2.0f;
        const RectDip s = card_.inflated(g);
        dc.FillRoundedRect(D2D1::RoundedRect(D2D1::RectF(s.x, s.y + g * 0.5f, s.right(),
                                                         s.bottom() + g * 0.5f),
                                             kCardRadius + g, kCardRadius + g),
                           WithAlphaScale(shadowBase, kShadowAlpha));
    }
    // cardFill is translucent (designed for Mica); nothing sits behind this card,
    // so flatten it over the window base to get the solid colour it stands for.
    const D2D1_COLOR_F cardColor = FlattenOver(c.cardFill, c.windowBackground);
    dc.FillRoundedRect(D2D1::RoundedRect(D2D1::RectF(card_.x, card_.y, card_.right(),
                                                     card_.bottom()),
                                         kCardRadius, kCardRadius), cardColor);

    if (spec_.icon != OverlayIcon::None) DrawIcon(dc, card_.x + kCardPadding, iconY_);

    Panel::Render(dc);  // title, message, progress, buttons
}

void OverlayHost::DrawIcon(const DrawingContext& dc, float x, float y) {
    const ColorTokens& c = Theme().colors;
    D2D1_COLOR_F fill = c.severityInfoStroke;
    switch (spec_.icon) {
        case OverlayIcon::Warning: fill = c.severityWarningStroke; break;
        case OverlayIcon::Error:   fill = c.severityErrorStroke; break;
        case OverlayIcon::Success: fill = c.severitySuccessStroke; break;
        default: break;
    }
    const float r = kIconSize * 0.5f;
    const float cx = x + r;
    const float cy = y + r;
    dc.FillEllipse(D2D1::Ellipse(D2D1::Point2F(cx, cy), r, r), fill);

    // Geometric glyphs (no icon font needed), drawn in the on-accent colour.
    const D2D1_COLOR_F ink = c.onAccent;
    auto P = [&](float dx, float dy) { return D2D1::Point2F(cx + dx, cy + dy); };
    switch (spec_.icon) {
        case OverlayIcon::Info:      // "i": dot above a stem
            dc.FillEllipse(D2D1::Ellipse(P(0.0f, -5.0f), 1.6f, 1.6f), ink);
            dc.DrawLine(P(0.0f, -1.5f), P(0.0f, 6.0f), ink, kGlyphStroke + 0.5f);
            break;
        case OverlayIcon::Warning:   // "!": stem above a dot
            dc.DrawLine(P(0.0f, -6.0f), P(0.0f, 1.5f), ink, kGlyphStroke + 0.5f);
            dc.FillEllipse(D2D1::Ellipse(P(0.0f, 5.0f), 1.6f, 1.6f), ink);
            break;
        case OverlayIcon::Error:     // "x"
            dc.DrawLine(P(-4.5f, -4.5f), P(4.5f, 4.5f), ink, kGlyphStroke);
            dc.DrawLine(P(-4.5f, 4.5f), P(4.5f, -4.5f), ink, kGlyphStroke);
            break;
        case OverlayIcon::Success:   // check mark
            dc.DrawLine(P(-5.5f, 0.5f), P(-1.5f, 4.5f), ink, kGlyphStroke);
            dc.DrawLine(P(-1.5f, 4.5f), P(5.5f, -3.5f), ink, kGlyphStroke);
            break;
        default:
            break;
    }
}

bool OverlayHost::WantsAnimationTick() const {
    return IsVisible() && fade_.Animating(fadeTarget_, 0.01f);
}

void OverlayHost::OnAnimationTick(float dtSec) {
    fade_.Approach(fadeTarget_, dtSec, Theme().motion.fadeTau, 0.01f);
    SetOpacity(fade_);
    // Fully faded out: collapse, so the layer stops blocking input and painting.
    if (!shown_ && fade_.value <= 0.0f) SetVisible(false);
    Invalidate();
}

}  // namespace fluent
