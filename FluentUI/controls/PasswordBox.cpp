// PasswordBox.cpp — password mode plus the WinUI reveal (peek) button.

#include "PasswordBox.h"
#include "../styling/ThemeTokens.h"
#include "../input/InputManager.h"
#include "../graphics/ResourceCache.h"
#include <algorithm>

namespace fluent {

namespace {
// Button plate geometry (DIP). WinUI's reveal button is a square-ish plate inset inside
// the box's border, the same subtle-button look as the TextBox clear button.
constexpr float kButtonW = 28.0f;      // plate width
constexpr float kButtonInset = 4.0f;   // gap between the plate and the box edge (all sides)
// TextBox already keeps kPadX (10 DIP) between the text and the right border. The
// reserve is what the button needs beyond that, plus a small gap so the last glyph
// does not touch the plate: inset + width + gap - padding.
constexpr float kTextPadX = 10.0f;     // must match TextBox's kPadX
constexpr float kTextGap = 4.0f;
constexpr float kReserve = kButtonInset + kButtonW + kTextGap - kTextPadX;

// Eye glyph (DIP): an almond outline with a round pupil. Sized like WinUI's 12 px
// RevealPassword icon in a 32 DIP box.
constexpr float kEyeHalfW = 7.0f;      // half the lens width
constexpr float kEyeCtrlY = 6.5f;      // Bezier control-point height: lens peaks at ~3.25
constexpr float kPupilR = 2.0f;
constexpr float kEyeStroke = 1.2f;
} // namespace

PasswordBox::PasswordBox() {
    SetPassword(true);
}

void PasswordBox::SetPasswordRevealMode(PasswordRevealMode mode) {
    if (revealMode_ == mode) return;
    if (peeking_) EndPeek();
    revealMode_ = mode;
    buttonHovered_ = false;
    ApplyReveal();
    // The reserve depends on the mode, so the scroll offset has to be re-clamped.
    EnsureCaretVisible();
    Invalidate();
}

bool PasswordBox::IsRevealButtonVisible() const {
    return revealMode_ == PasswordRevealMode::Peek && IsPassword() && enabled_ &&
           !text_.empty();
}

float PasswordBox::ReservedRightDip() const {
    return IsRevealButtonVisible() ? kReserve : 0.0f;
}

RectDip PasswordBox::RevealButtonRect() const {
    const float h = std::max(0.0f, bounds_.h - kButtonInset * 2.0f);
    return {bounds_.right() - kButtonInset - kButtonW, bounds_.y + kButtonInset, kButtonW, h};
}

// ---------------------------------------------------------------------------
// Peek
// ---------------------------------------------------------------------------

void PasswordBox::BeginPeek() {
    peeking_ = true;
    // Capture so the release arrives even if the pointer drifts off the button or out
    // of the window; every capture-loss path delivers a canceling release, which is
    // what guarantees the text is masked again.
    if (Context().input) Context().input->CapturePointer(this);
    ApplyReveal();
    Invalidate();
}

void PasswordBox::EndPeek() {
    if (!peeking_) return;
    peeking_ = false;
    if (Context().input && Context().input->Captured() == this)
        Context().input->ReleaseCapture(this);
    ApplyReveal();
    Invalidate();
}

// ---------------------------------------------------------------------------
// Pointer
// ---------------------------------------------------------------------------

void PasswordBox::OnPointerPressed(PointerEventArgs& e) {
    // Handled here and NOT passed to TextBox, so the press neither moves the caret nor
    // starts a drag selection; a double-click on the eye is just a second press. Focus
    // was already given to the box by InputManager before the route reached us.
    if (e.button == PointerButton::Left && IsRevealButtonVisible() &&
        RevealButtonRect().contains(e.position.x, e.position.y)) {
        BeginPeek();
        e.handled = true;
        return;
    }
    TextBox::OnPointerPressed(e);
}

void PasswordBox::OnPointerMoved(PointerEventArgs& e) {
    // Hover only when no gesture is running: a text drag-select passing over the eye
    // must not light it up, and while peeking the pressed plate is what is drawn.
    if (!selecting_ && !peeking_) {
        const bool over = IsRevealButtonVisible() &&
                          RevealButtonRect().contains(e.position.x, e.position.y);
        if (over != buttonHovered_) {
            buttonHovered_ = over;
            Invalidate();
        }
    }
    // Held: keep revealing wherever the pointer goes, as WinUI does; only the release
    // ends it.
    if (peeking_) {
        e.handled = true;
        return;
    }
    TextBox::OnPointerMoved(e);
}

void PasswordBox::OnPointerReleased(PointerEventArgs& e) {
    // Also the canceling release (-1,-1) InputManager sends on deactivation, capture
    // theft and detach.
    if (peeking_) {
        EndPeek();
        e.handled = true;
        return;
    }
    TextBox::OnPointerReleased(e);
}

void PasswordBox::OnPointerLeft() {
    TextBox::OnPointerLeft();
    if (buttonHovered_) {
        buttonHovered_ = false;
        Invalidate();
    }
}

HCURSOR PasswordBox::Cursor() const {
    // WM_SETCURSOR arrives before the WM_MOUSEMOVE that updates buttonHovered_, so the
    // arrow appears one mouse message late at the button's edge. DataGrid's divider
    // cursor has the same lag; it is not visible in practice.
    if (peeking_ || buttonHovered_) {
        static HCURSOR arrow = LoadCursor(nullptr, IDC_ARROW);
        return arrow;
    }
    return TextBox::Cursor();
}

// ---------------------------------------------------------------------------
// Focus / lifetime
// ---------------------------------------------------------------------------

void PasswordBox::OnFocusChanged() {
    TextBox::OnFocusChanged();
    if (!IsFocused()) EndPeek();
}

void PasswordBox::OnDetachedFromTree() {
    // InputManager already sent the canceling release for a held button; this covers a
    // tree with no input manager. Plain state reset, no capture call: Context() is
    // about to be cleared.
    peeking_ = false;
    buttonHovered_ = false;
    ApplyReveal();
    TextBox::OnDetachedFromTree();
}

// ---------------------------------------------------------------------------
// Render
// ---------------------------------------------------------------------------

void PasswordBox::Render(const DrawingContext& dc) {
    TextBox::Render(dc);
    if (!IsRevealButtonVisible()) return;

    const ColorTokens& pal = Theme().colors;
    const RectDip r = RevealButtonRect();

    // Subtle-button plate: transparent at rest, hover / pressed fills from the theme
    // (same tokens NumericUpDown's spin buttons use).
    if (peeking_ || buttonHovered_) {
        const float corner = Theme().spacing.cornerRadiusSmall;
        dc.FillRoundedRect(
            D2D1::RoundedRect(D2D1::RectF(r.x, r.y, r.right(), r.bottom()), corner, corner),
            peeking_ ? pal.controlFillPressed : pal.controlFillHover);
    }

    const D2D1_COLOR_F ink = peeking_ ? pal.textSecondary : EffectiveForeground(pal.textPrimary);
    const float cx = r.x + r.w * 0.5f;
    const float cy = r.y + r.h * 0.5f;

    // The lens outline is static, so it is built once as unit-space geometry in the
    // shared cache and positioned by a transform (ComboBox's chevron does the same).
    ComPtr<ID2D1PathGeometry> lens;
    if (ResourceCache* cache = Context().resourceCache) {
        lens = cache->GetGeometry(GlyphId::EyeOutline, kEyeHalfW * 2.0f,
            [](ID2D1GeometrySink* sink) {
                sink->BeginFigure(D2D1::Point2F(-kEyeHalfW, 0.0f), D2D1_FIGURE_BEGIN_HOLLOW);
                sink->AddQuadraticBezier(D2D1::QuadraticBezierSegment(
                    D2D1::Point2F(0.0f, -kEyeCtrlY), D2D1::Point2F(kEyeHalfW, 0.0f)));
                sink->AddQuadraticBezier(D2D1::QuadraticBezierSegment(
                    D2D1::Point2F(0.0f, kEyeCtrlY), D2D1::Point2F(-kEyeHalfW, 0.0f)));
                sink->EndFigure(D2D1_FIGURE_END_CLOSED);
            });
    }
    if (lens) {
        TransformGuard tg = dc.PushTransform(D2D1::Matrix3x2F::Translation(cx, cy));
        dc.DrawGeometry(lens.Get(), ink, kEyeStroke);
    } else {
        // No cache (headless / detached context): an ellipse reads as the same eye.
        dc.DrawEllipse(D2D1::Ellipse(D2D1::Point2F(cx, cy), kEyeHalfW, kEyeCtrlY * 0.5f),
                       ink, kEyeStroke);
    }
    dc.FillEllipse(D2D1::Ellipse(D2D1::Point2F(cx, cy), kPupilR, kPupilR), ink);
}

} // namespace fluent
