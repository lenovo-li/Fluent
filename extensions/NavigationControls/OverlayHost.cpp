// OverlayHost.cpp
#include "OverlayHost.h"
#include "NavTrace.h"
#include "../../FluentUI/styling/ThemeTokens.h"
#include "../../FluentUI/input/FocusManager.h"
#include "../../FluentUI/core/UIContext.h"
#include <algorithm>
#include <cmath>
#include <cstdio>

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
    details_ = Emplace<ScrollPanel>();
    details_->SetPadding(kDetailsPadding);
    details_->SetVisible(false);
    detailsText_ = details_->Emplace<TextBlock>();
    detailsText_->SetWrap(true);
    progress_ = Emplace<ProgressBar>();
    warning_ = Emplace<InfoBar>();
    warning_->SetSeverity(InfoBar::Severity::Warning);
    warning_->SetVisible(false);
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
    detailsText_->SetText(spec.details);
    details_->SetVisible(!spec.details.empty());
    details_->SetVerticalOffset(0.0f);  // new text starts at the top
    warning_->SetMessage(spec.warning);
    warning_->SetVisible(!spec.warning.empty());
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
    NAV_TRACEF("[NavigationControls.OverlayHost] show progress=%d details=%d warning=%d\n",
               spec.showProgress ? 1 : 0, spec.details.empty() ? 0 : 1,
               spec.warning.empty() ? 0 : 1);
}

void OverlayHost::UpdateProgress(int percent, const std::wstring& message) {
    if (!shown_ || !spec_.showProgress) return;
    // The API takes a percentage; progress_ keeps ProgressBar's default 0..1 range.
    progress_->SetValue(static_cast<float>(std::clamp(percent, 0, 100)) / 100.0f);
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
    NAV_TRACEF("[NavigationControls.OverlayHost] hide\n");
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
    // Reading order: the details box first (so the keyboard can reach and scroll
    // it), then the buttons left to right.
    UIElement* const stops[] = {details_, secondary_, primary_};
    UIElement* order[3];
    int n = 0;
    for (UIElement* t : stops)
        if (t->IsVisible() && t->IsFocusable()) order[n++] = t;
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

    auto measureRow = [&](FrameworkElement* e) {
        if (!e->IsVisible()) return 0.0f;
        e->MeasureCached(innerW, offerH);
        return e->Desired().h;
    };
    messageH_ = measureRow(message_);
    progressH_ = measureRow(progress_);
    warningH_ = measureRow(warning_);

    buttonsH_ = 0.0f;
    for (Button* b : {secondary_, primary_}) {
        if (!b->IsVisible()) continue;
        b->MeasureCached(innerW, offerH);
        buttonsH_ = std::max(buttonsH_, b->Desired().h);
    }

    // Body rows stack with kRowGap between them, in the order Arrange places them.
    auto cardHeight = [&](float detailsH) {
        float body = 0.0f;
        for (float rowH : {messageH_, detailsH, progressH_, warningH_}) {
            if (rowH <= 0.0f) continue;
            body += (body > 0.0f ? kRowGap : 0.0f) + rowH;
        }
        float h = kCardPadding + headerH_;
        if (body > 0.0f) h += (headerH_ > 0.0f ? kRowGap : 0.0f) + body;
        if (buttonsH_ > 0.0f) h += kButtonsGap + buttonsH_;
        return h + kCardPadding;
    };

    // The details box takes what is left of the slot, within [min, max]. Below the
    // minimum the card overflows rather than squeezing the buttons off it.
    detailsH_ = 0.0f;
    if (details_->IsVisible()) {
        const float spare = offerH - kCardMargin * 2.0f - cardHeight(0.0f) - kRowGap;
        const float maxH = std::clamp(spare, kDetailsMinHeight, kDetailsMaxHeight);
        // ScrollPanel reports min(content + padding, offer), which is the box height.
        details_->MeasureCached(innerW, maxH);
        detailsH_ = details_->Desired().h;
    }
    return std::max(kMinCardHeight, cardHeight(detailsH_));
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

    // Same order and heights as CardHeightFor; a row with height 0 is not shown.
    auto place = [&](FrameworkElement* e, float rowH) {
        if (rowH <= 0.0f) return;
        ArrangeChild(e, RectDip{left, y, innerW, rowH});
        y += rowH + kRowGap;
    };
    place(message_, messageH_);
    place(details_, detailsH_);
    place(progress_, progressH_);
    place(warning_, warningH_);

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
    // Only the live dialog's buttons and details box take input (the box for wheel,
    // scrollbar drag and click-to-focus); everything else (scrim, card, texts, the
    // warning bar, and the whole layer while it fades out) is swallowed here.
    if (shown_) {
        for (UIElement* t : {static_cast<UIElement*>(primary_),
                             static_cast<UIElement*>(secondary_),
                             static_cast<UIElement*>(details_)})
            if (t->IsVisible())
                if (UIElement* hit = t->HitTestDeep(dipX, dipY)) return hit;
    }
    return this;
}

void OverlayHost::CollectFocusables(std::vector<UIElement*>& out) {
    if (!IsVisible() || !shown_) return;
    out.push_back(this);
    if (details_->IsVisible()) details_->CollectFocusables(out);
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
        case VK_RIGHT: case VK_DOWN:
            // Inside the details box the arrows scroll it (they continue down the
            // route to the ScrollPanel); elsewhere they move between buttons.
            if (IsWithin(e.originalSource, details_)) return;
            CycleFocus(e.vk == VK_LEFT || e.vk == VK_UP ? -1 : 1);
            e.handled = true;
            return;
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

    // Details box: a recessed well (the page colour, flattened onto the card) with a
    // hairline, so long text reads as a separate region of the card. The stroke is
    // inset by half its width to stay inside the box's bounds.
    if (details_->IsVisible()) {
        const RectDip& d = details_->Bounds();
        const D2D1_RECT_F r = D2D1::RectF(d.x + 0.5f, d.y + 0.5f, d.right() - 0.5f,
                                          d.bottom() - 0.5f);
        dc.FillRoundedRect(D2D1::RoundedRect(r, kDetailsRadius, kDetailsRadius),
                           FlattenOver(c.windowBackground, cardColor));
        dc.DrawRoundedRect(D2D1::RoundedRect(r, kDetailsRadius, kDetailsRadius),
                           c.controlStrokeDefault, 1.0f);
    }

    Panel::Render(dc);  // title, message, details, progress, warning, buttons
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
