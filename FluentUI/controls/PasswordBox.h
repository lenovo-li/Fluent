// PasswordBox.h — password input: a TextBox in password mode plus a reveal button.
//
// What password mode gives (TextBox::SetPassword): bullets instead of glyphs, Copy and
// Cut refused (keys, context menu), and the IME switched off while the box has focus,
// matching WinUI's PasswordBox. A PasswordBox adds the WinUI reveal ("peek") button.
//
// Usage:
//   auto pwd = std::make_unique<PasswordBox>();      // Peek by default
//   pwd->SetPlaceholder(L"Enter password");
//   pwd->SetMaxLength(128);
//   pwd->SetPasswordRevealMode(PasswordRevealMode::Hidden);   // no eye button
//
// THE REVEAL BUTTON (PasswordRevealMode::Peek). An eye glyph inside the right edge of
// the box. Holding the left button on it shows the plain text; release, capture loss
// (window deactivation, another window taking capture, detach) and focus loss all mask
// it again. Pressing it does not move the caret or start a selection, and focus stays
// in (or moves to) the box, because the press is handled here and never reaches
// TextBox's click-to-place.
//
// Shown while the box holds text and is enabled, regardless of focus. WinUI shows it
// only while focused; that is deliberately NOT copied. InputManager gives an element
// focus BEFORE routing the press to it, so a focus-only button would appear under a
// click that was meant to focus an unfocused box, and that click would land on the
// eye and peek instead of placing the caret. Showing it whenever there is text also
// keeps the text width from changing on every focus change.
//
// The text never runs under the button: ReservedRightDip() reserves its width, and
// TextBox applies that to scroll clamping, caret visibility and the text clip.
//
// A revealed password is still a password: Copy / Cut and the IME stay disabled while
// peeking or in Visible mode, because they key on password mode, not on the mask.
#pragma once
#include "TextBox.h"

namespace fluent {

struct PasswordBoxTestPeer;  // component test access to the button geometry

// WinUI PasswordRevealMode.
enum class PasswordRevealMode {
    Hidden,   // no reveal button; always masked
    Peek,     // reveal button; plain text only while it is held (default)
    Visible,  // always plain text, no button (still refuses copy and IME)
};

class PasswordBox : public TextBox {
    friend struct PasswordBoxTestPeer;
public:
    PasswordBox();

    void SetPasswordRevealMode(PasswordRevealMode mode);
    PasswordRevealMode GetPasswordRevealMode() const { return revealMode_; }

    // Whether the eye button is currently drawn and hit-testable.
    bool IsRevealButtonVisible() const;
    // Whether the button is being held (the text is shown because of a peek).
    bool IsPeeking() const { return peeking_; }

    void Render(const DrawingContext& dc) override;
    void OnPointerPressed(PointerEventArgs& e) override;
    void OnPointerMoved(PointerEventArgs& e) override;
    void OnPointerReleased(PointerEventArgs& e) override;
    // Arrow over the button, I-beam over the text.
    HCURSOR Cursor() const override;

protected:
    float ReservedRightDip() const override;
    void OnFocusChanged() override;
    void OnPointerLeft() override;
    void OnDetachedFromTree() override;

private:
    // Button rect in window DIPs (meaningful only while IsRevealButtonVisible()).
    RectDip RevealButtonRect() const;
    void BeginPeek();
    void EndPeek();
    // Push the mask state the mode and the peek imply down to TextBox.
    void ApplyReveal() { SetMaskRevealed(revealMode_ == PasswordRevealMode::Visible || peeking_); }

    PasswordRevealMode revealMode_ = PasswordRevealMode::Peek;
    bool peeking_ = false;        // button held (pointer captured)
    bool buttonHovered_ = false;  // pointer over the button (hover plate + arrow cursor)
};

} // namespace fluent
