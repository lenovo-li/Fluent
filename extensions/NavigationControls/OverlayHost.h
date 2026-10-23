// OverlayHost.h — in-window modal overlay: scrim + centered dialog card.
//
//   // Last child of the root Grid, spanning every row/column, so it is topmost.
//   auto* overlay = root->Emplace<fluent::OverlayHost>();
//   root->SetCell(overlay, 0, 0, 1, 2);
//   closedSub_ = overlay->Closed().Subscribe<&MainWindow::OnOverlayClosed>(this);
//   overlay->Show({fluent::OverlayIcon::Info, L"Firmware Update",
//                  L"This device doesn't need to be updated.", L"OK"});
//
// WHY A PANEL OF REAL CONTROLS. The buttons are fluent::Button (pill-shaped via the
// corner radius), the texts are TextBlocks and the bar is a ProgressBar, so hover
// tint, focus ring, Enter/Space activation and theme changes all come from the
// stock controls. The overlay itself paints only the scrim, the card and the icon.
//
// MODALITY. While shown, HitTestDeep returns the overlay for any point not on one of
// its own controls, so nothing underneath receives pointer input. Show() focuses the
// overlay itself (no focus ring, like the reference UI); Tab / Shift+Tab / arrows
// cycle the card's Tab stops (the details box when shown, then the buttons) and
// never walk back into the page underneath.
//   Esc   -> Secondary when a secondary button exists, else Dismissed when there is
//            any button; ignored in the button-less Progress state (DESIGN §2.4).
//   Enter -> on a focused button, that button. On the overlay itself, Primary only
//            when it is the ONLY button (OK states); Confirm needs an explicit pick.
//
// FADE. Show/Hide ease Visual opacity with Theme().motion.fadeTau. Hide keeps the
// overlay visible (and input-blocking) until the fade reaches zero, then collapses
// it. Show while a hide is fading simply reverses it — a Closed handler can chain
// the next state (Confirm -> Progress) without a flash.
//
// Colors: scrim and card derive from Theme() tokens (no scrim token exists, so it
// is textPrimary at reduced alpha in light mode, windowBackground in dark mode);
// the card is cardFill flattened over windowBackground, because no DWM material
// sits behind it (CLAUDE.md, Theming).
//
// DETAILS AND WARNING. Two optional rows, both off when their string is empty, so a
// spec that sets neither lays out exactly as before:
//   details  long free text (release notes) in an inset box under the message. The
//            box is a ScrollPanel: it grows with the text up to kDetailsMaxHeight and
//            scrolls beyond that. When the window is short it is also the row that
//            gives up height (down to kDetailsMinHeight), because the buttons must
//            stay on the card and the other rows are a line or two each. The box is a
//            Tab stop so keyboard users can scroll it; while it has focus the arrow
//            keys scroll it instead of cycling the buttons.
//   warning  an InfoBar (Warning severity) right above the buttons, for "do not unplug
//            the device" text. It is a separate row rather than part of `message`
//            because UpdateProgress rewrites the message on every tick, and the
//            warning has to stay visible through the whole Progress state.
#pragma once

#include "../../FluentUI/layout/Panel.h"
#include "../../FluentUI/layout/ScrollPanel.h"
#include "../../FluentUI/controls/Button.h"
#include "../../FluentUI/controls/InfoBar.h"
#include "../../FluentUI/controls/TextBlock.h"
#include "../../FluentUI/controls/ProgressBar.h"
#include "../../FluentUI/animation/AnimatedValue.h"
#include "../../FluentUI/base/Event.h"
#include <string>

namespace fluent {

enum class OverlayIcon { None, Info, Warning, Error, Success };
enum class OverlayResult { Primary, Secondary, Dismissed };

struct OverlaySpec {
    OverlayIcon icon = OverlayIcon::None;
    std::wstring title;
    std::wstring message;
    std::wstring primaryText;     // empty = no primary button
    std::wstring secondaryText;   // empty = no secondary button
    bool showProgress = false;    // ProgressBar under the message
    std::wstring details;         // empty = no details box (scrollable, e.g. release notes)
    std::wstring warning;         // empty = no warning bar (kept through UpdateProgress)
};

struct OverlayResultArgs {
    OverlayResult result = OverlayResult::Dismissed;
};

class OverlayHost : public Panel {
public:
    using Spec = OverlaySpec;

    OverlayHost();

    void Show(const OverlaySpec& spec);
    // Progress state only: bar value (0..100) and the line under the title.
    void UpdateProgress(int percent, const std::wstring& message);
    void Hide();
    // True from Show until Hide is called (the fade-out still blocks input).
    bool IsShown() const { return shown_; }
    const OverlaySpec& CurrentSpec() const { return spec_; }

    Event<OverlayHost, OverlayResultArgs>& Closed() { return closed_; }

    // Panel overrides.
    void Render(const DrawingContext& dc) override;
    UIElement* HitTestDeep(float dipX, float dipY) override;
    void CollectFocusables(std::vector<UIElement*>& out) override;
    bool WantsAnimationTick() const override;
    void OnAnimationTick(float dtSec) override;

    // Geometry constants (DIP), public for tests and hosts.
    static constexpr float kCardWidth = 520.0f;
    static constexpr float kCardMargin = 24.0f;   // min gap card <-> window edge
    static constexpr float kCardRadius = 12.0f;
    static constexpr float kCardPadding = 28.0f;
    static constexpr float kMinCardHeight = 200.0f;
    static constexpr float kIconSize = 24.0f;
    static constexpr float kIconGap = 16.0f;
    static constexpr float kRowGap = 16.0f;
    static constexpr float kButtonsGap = 40.0f;
    static constexpr float kButtonMinWidth = 72.0f;
    static constexpr float kButtonSpacing = 12.0f;
    static constexpr float kPillRadius = 16.0f;   // Button min height 32 / 2
    static constexpr float kScrimAlphaLight = 0.10f;
    static constexpr float kScrimAlphaDark = 0.50f;
    static constexpr float kShadowAlpha = 0.035f; // per ring, three rings
    static constexpr float kDetailsMaxHeight = 180.0f;  // ~9 body lines, then it scrolls
    static constexpr float kDetailsMinHeight = 64.0f;   // floor when the window is short
    static constexpr float kDetailsPadding = 12.0f;
    static constexpr float kDetailsRadius = 6.0f;

    RectDip CardRect() const { return card_; }

protected:
    SizeDip MeasureOverride(float availW, float availH) override;
    void ArrangeOverride(const RectDip& content) override;
    void OnPreviewKeyDown(KeyEventArgs& e) override;

private:
    // The children are the fixed card parts; hide the base's child API so a host
    // cannot add elements the card layout knows nothing about.
    using Panel::Add;
    using Panel::Emplace;
    using Panel::InsertAt;
    using Panel::RemoveAt;
    using Panel::Clear;

    void OnPrimaryClick(Button&, RoutedEventArgs&) { Close(OverlayResult::Primary); }
    void OnSecondaryClick(Button&, RoutedEventArgs&) { Close(OverlayResult::Secondary); }
    // Start the fade-out; raises Closed(result) when `raise` is set.
    void BeginHide(bool raise, OverlayResult result);
    void Close(OverlayResult result) { BeginHide(true, result); }
    void CycleFocus(int step);
    void FocusSelf();
    void DropFocus();
    bool FadeEnabled() const;
    static float CardWidthFor(float slotW);
    // Measures the card's children for `cardW`; caches the row heights for Arrange.
    float CardHeightFor(float cardW, float slotH);
    void DrawIcon(const DrawingContext& dc, float x, float y);

    OverlaySpec spec_;
    bool shown_ = false;
    float fadeTarget_ = 0.0f;
    AnimatedValue fade_{0.0f};
    RectDip card_{};
    // Row heights from CardHeightFor, reused by Arrange (0 = row not shown).
    float headerH_ = 0.0f;    // icon/title row
    float messageH_ = 0.0f;
    float detailsH_ = 0.0f;   // details box, already clamped to the space left
    float progressH_ = 0.0f;
    float warningH_ = 0.0f;
    float buttonsH_ = 0.0f;
    float iconY_ = 0.0f;      // window-DIP top of the icon box (Arrange)

    TextBlock* title_ = nullptr;      // children, owned by children_
    TextBlock* message_ = nullptr;
    ScrollPanel* details_ = nullptr;  // inset box; holds detailsText_
    TextBlock* detailsText_ = nullptr;
    ProgressBar* progress_ = nullptr;
    InfoBar* warning_ = nullptr;
    Button* secondary_ = nullptr;
    Button* primary_ = nullptr;

    Event<OverlayHost, OverlayResultArgs> closed_;
    Subscription primarySub_;
    Subscription secondarySub_;
};

}  // namespace fluent
