// TextBox.h — single-line, self-drawn Fluent text input.
//
// A single-line editor built on TextEditBase (which supplies the text model,
// selection, IME, clipboard, blink, and editing keys). TextBox adds the
// single-line specifics: a non-wrapping layout, horizontal scrolling, left/right
// caret movement, password masking, newline-stripping input, and rendering.
//
// Fully DWrite-rendered into the window content surface (no child HWND). Unlike
// TextArea, TextBox scrolls horizontally over one line and has NOT been migrated to
// the compositor — it draws and blinks on the UI thread.
// Geometry is in DIPs; indices are UTF-16 code-unit offsets.
#pragma once

#include "TextEditBase.h"

namespace fluent {

struct TextBoxTestPeer;  // component test access to CreateLayout (below)

class TextBox : public TextEditBase {
    friend struct TextBoxTestPeer;
public:
    // Password mode: the text is drawn as bullets, Copy / Cut are refused, and the IME
    // is switched off while the box has focus. Safe to toggle while focused; the IME
    // state follows immediately.
    void SetPassword(bool on);
    bool IsPassword() const { return password_; }

    // Element overrides.
    void Render(const DrawingContext& dc) override;
    bool CaretRectDip(RectDip& out) const override;

    // Natural size: WinUI TextBox MinHeight = 32 DIP, single line of body text
    // plus vertical padding. Without this the inherited FrameworkElement::Measure
    // mirrors the available height — which is infinity inside an auto-sized
    // container (StackPanel in a card) and collapses the control to zero.
    void Measure(float availW, float availH) override;

protected:
    // UI Automation peer (automation/ControlPeers.h); defined in ControlPeers.cpp.
    std::unique_ptr<automation::AutomationPeer> CreateAutomationPeer() override;
    // TextEditBase hooks.
    void OnTextLayoutDirty() override {}   // layout is rebuilt on demand (cheap)
    void EnsureCaretVisible() override;
    bool OnNavigationKey(UINT vk, bool shift) override;
    std::wstring SanitizeInput(std::wstring s) const override;
    const std::wstring& DisplayText(std::wstring& scratch) const override;
    // Both key on password_, NOT on whether the bullets are currently revealed: a
    // revealed password is still a password, so a peek must not open the clipboard or
    // let an IME show its reading.
    bool AllowCopyToClipboard() const override { return !password_; }
    bool AllowImeComposition() const override { return !password_; }

    // --- Hooks for PasswordBox (and other subclasses) ----------------------
    // Width (DIP) at the right edge, inside the padding, that the text must not run
    // under — a button drawn inside the box. Read on every use (scroll clamp, caret
    // visibility, text clip), so a subclass whose reserve depends on state (focus,
    // empty text) just returns the current value; after it changes, call
    // EnsureCaretVisible() so the scroll offset is re-clamped to the new width.
    virtual float ReservedRightDip() const { return 0.0f; }

    // Draw a password's real characters instead of bullets, without leaving password
    // mode (clipboard and IME stay refused). Used by PasswordBox's reveal button.
    // No effect when not in password mode.
    void SetMaskRevealed(bool revealed);
    bool IsMaskRevealed() const { return revealed_; }

    // Text viewport within bounds_: origin X (the left padding) and visible width
    // (bounds minus both paddings minus ReservedRightDip()).
    float ContentLeft() const;
    float ContentWidth() const;

public:
    // Routed pointer input: click-to-place + drag-select (with capture).
    void OnPointerPressed(PointerEventArgs& e) override;
    void OnPointerMoved(PointerEventArgs& e) override;
    void OnPointerReleased(PointerEventArgs& e) override;

private:
    ComPtr<IDWriteTextLayout> CreateLayout(const std::wstring& s) const;
    float CaretX(UINT32 index) const;     // caret X (DIP, pre-scroll) for an index
    UINT32 HitIndex(float dipX) const;    // code-unit index nearest dipX
    // TextEditBase hook for the context menu. Single-line, so Y carries no information.
    UINT32 IndexAtPoint(float dipX, float /*dipY*/) const override {
        return HitIndex(dipX);
    }
    void ClampScroll();
    // The string laid out for measuring geometry (password dots, no composition).
    std::wstring GeometryText() const;

    // Bullets are drawn when password_ && !revealed_.
    bool Masked() const { return password_ && !revealed_; }

    bool password_ = false;
    bool revealed_ = false;               // password shown in plain text (peek)
    float scrollX_ = 0.0f;                // horizontal scroll offset (DIP)
};

} // namespace fluent
