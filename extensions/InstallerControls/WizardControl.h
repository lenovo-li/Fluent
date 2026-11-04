// WizardControl.h — multi-page installer wizard: pages + Back/Next(Finish)/Cancel bar.
//
//   auto* wizard = root->Emplace<fluent::wizard::WizardControl>();
//   wizard->AddPage(L"Welcome", std::make_unique<StackPanel>());
//   wizard->AddPage(L"Options", std::make_unique<Grid>());
//   // Payload arrives as Args& (int&), never by value: Event<S,A>::Handler is
//   // void(*)(void*, S&, A&). STORE the Subscription or it unregisters at once.
//   pageSub_ = wizard->PageChanged().Subscribe(this,
//       [](void* owner, WizardControl&, int& page) { ... });
//
// LAYOUT (top to bottom): optional step indicator row, the current page (fills the
// remaining height), a hairline divider, then the button bar with Back, Next/Finish
// and Cancel right-aligned. Buttons size to their labels (Button::Measure), so
// localized labels such as "上一步" are never clipped.
//
// PAGE VISIBILITY. All pages stay attached to the live tree; non-current pages are
// SetVisible(false). Deliberately simpler than TabControl's detach dance: Panel's
// hit-test, focus collection, animation collection and dirty-rect walk all skip
// collapsed children, so a hidden page costs no paint or input and no layout on a
// resize (it is measured only on a frame where its own content changed, so that
// change is not lost to the frame-end dirty clear; see MeasureOverride), but it KEEPS
// whatever it acquired in OnAttachedToTree. Installer pages are few and hold state
// that must survive Back/Next (a chosen path, a checkbox), so staying attached is
// the right trade. A wizard with dozens of heavy pages would want TabControl's way.
//
// NAVIGATION RULES.
//   * Next() and Finish validate first: the Navigating event (cancel = true vetoes),
//     then the ValidateCurrentPage() virtual. Either one keeps the user in place.
//   * Previous() and GoToPage() never validate. Going back must always work, and
//     GoToPage is the host's programmatic path ("install done -> Complete page").
//   * On the last page the Next button reads Finish and raises Finishing instead of
//     advancing.
//   * SetNavigationLocked(true) disables all three buttons and makes the user-intent
//     entry points (Next / Previous / Advance / Cancel) no-ops, but leaves GoToPage
//     working: the installing page locks, code calls GoToPage when the work is done.
//
// FOCUS. When the page holding keyboard focus is hidden, focus is cleared, so
// keystrokes never reach a control the user can no longer see. Tab order is the
// current page's content first, then Back, Next, Cancel.
//
// FADE. Switching pages fades the new page in with the theme's fadeTau. Disabled by
// SetTransitionStyle(None), and skipped automatically when Windows' "Show animations"
// setting is off (SPI_GETCLIENTAREAANIMATION) unless SetRespectSystemAnimations(false).
// The fade never gates rendering: the page is drawn at the current fade value, and a
// fade value of 0 draws nothing for exactly the frames the animation is running.
#pragma once

#include "../../FluentUI/layout/Panel.h"
#include "../../FluentUI/controls/Button.h"
#include "../../FluentUI/controls/TextBlock.h"
#include "../../FluentUI/animation/AnimatedValue.h"
#include "../../FluentUI/base/Event.h"
#include "../../FluentUI/input/RoutedEvent.h"
#include <memory>
#include <string>
#include <vector>

namespace fluent {
namespace wizard {

// One page: its title and its content element.
struct PageEntry {
    std::wstring title;
    FrameworkElement* content = nullptr;  // borrowed; owned by Panel::children_
};

// Payload of WizardControl::Navigating. Raised before a VALIDATED move: Next, or
// Finish (to == kFinish). Set cancel = true to keep the user on `from`.
struct WizardNavigatingArgs {
    static constexpr int kFinish = -1;
    int from = -1;
    int to = -1;
    bool cancel = false;
};

class WizardControl : public Panel {
public:
    enum class TransitionStyle { None, Fade };

    WizardControl();

    // Add a page. The content is owned by this control; the returned pointer is
    // borrowed (valid for the control's lifetime). The first page added becomes
    // current. Null content is ignored and returns nullptr.
    template <typename T>
    T* AddPage(std::wstring title, std::unique_ptr<T> content) {
        if (!content) return nullptr;
        T* raw = Add(std::move(content));
        pages_.push_back(PageEntry{std::move(title), raw});
        if (currentPage_ < 0) {
            GoToPage(0);
        } else {
            raw->SetVisible(false);
            // The old last page no longer is: its button must stop reading Finish,
            // and the step total changed.
            UpdateChrome();
        }
        InvalidateDirty(DirtyFlags::Measure);
        return raw;
    }

    int PageCount() const { return static_cast<int>(pages_.size()); }
    int CurrentPage() const { return currentPage_; }
    // Title / content of page `index`; empty string / nullptr when out of range.
    const std::wstring& PageTitle(int index) const;
    FrameworkElement* PageContent(int index) const;

    // Navigation (see NAVIGATION RULES above).
    void Next();
    void Previous();
    void GoToPage(int index);
    bool CanGoNext() const;
    bool CanGoPrevious() const;
    bool IsLastPage() const;
    // Exactly what the Next/Finish button does (advance, or Finish on the last
    // page), for keyboard shortcuts or a custom button. Cancel() raises Cancelled
    // exactly as the Cancel button does.
    void Advance();
    void Cancel();

    // Button labels. Defaults: "Back", "Next", "Cancel", "Finish".
    void SetButtonLabels(std::wstring back, std::wstring next,
                         std::wstring cancel, std::wstring finish);
    const std::wstring& BackLabel() const { return labelBack_; }
    const std::wstring& NextLabel() const { return labelNext_; }
    const std::wstring& CancelLabel() const { return labelCancel_; }
    const std::wstring& FinishLabel() const { return labelFinish_; }

    // Disable all buttons (e.g. while installing). GoToPage still works.
    void SetNavigationLocked(bool locked);
    bool NavigationLocked() const { return navigationLocked_; }

    // Hide/show the Cancel button (e.g. hide it on the Complete page).
    void SetCancelVisible(bool visible);

    // "Step N of M: <title>" row above the page. The format uses named tokens
    // {current} (1-based), {total} and {title}, each replaced wherever it appears —
    // NOT printf specifiers, so a translated format can reorder or omit them and a
    // stray '%' can never read garbage off the stack. E.g. L"第 {current}/{total} 步".
    void SetShowStepIndicator(bool show);
    bool ShowStepIndicator() const { return showStepIndicator_; }
    void SetStepIndicatorFormat(std::wstring format);
    // The indicator's current text (empty when hidden). Exposed for tests/hosts.
    std::wstring StepIndicatorText() const;

    void SetTransitionStyle(TransitionStyle style);
    TransitionStyle Transition() const { return transitionStyle_; }
    void SetRespectSystemAnimations(bool respect) { respectSystemAnimations_ = respect; }
    // Current fade value of the page (1 = fully shown). Exposed for tests.
    float PageOpacity() const { return pageFade_; }

    // The built-in buttons, for per-app styling (SetKind, SetMinWidth, ...). Do not
    // re-subscribe their Click to change navigation; use the events below.
    Button* BackButton() const { return btnBack_; }
    Button* NextButton() const { return btnNext_; }
    Button* CancelButton() const { return btnCancel_; }

    // Events. PageChanged carries the NEW page index. Navigating is cancelable and
    // precedes validated moves. Finishing: Finish clicked on the last page (after
    // validation). Cancelled: Cancel clicked.
    Event<WizardControl, int>& PageChanged() { return pageChanged_; }
    Event<WizardControl, WizardNavigatingArgs>& Navigating() { return navigating_; }
    Event<WizardControl, RoutedEventArgs>& Finishing() { return finishing_; }
    Event<WizardControl, RoutedEventArgs>& Cancelled() { return cancelled_; }

    // Panel overrides.
    void Render(const DrawingContext& dc) override;
    void CollectFocusables(std::vector<UIElement*>& out) override;
    bool WantsAnimationTick() const override;
    void OnAnimationTick(float dtSec) override;

    // Layout constants, public so tests assert against the same numbers.
    static constexpr float kButtonBarHeight = 56.0f;
    static constexpr float kStepIndicatorHeight = 36.0f;
    static constexpr float kButtonSpacing = 8.0f;
    static constexpr float kButtonMinWidth = 96.0f;
    static constexpr float kBarInsetX = 16.0f;

protected:
    // Validation hook: return false to keep the user on the current page.
    virtual bool ValidateCurrentPage() { return true; }
    // Called after the page index changed, before PageChanged is raised.
    virtual void OnPageChanged(int oldPage, int newPage) {
        UNREFERENCED_PARAMETER(oldPage);
        UNREFERENCED_PARAMETER(newPage);
    }

    SizeDip MeasureOverride(float availW, float availH) override;
    void ArrangeOverride(const RectDip& content) override;
    // UI Automation peer (defined in this library's .cpp, on the public automation
    // headers: the extension path for UIA, see inheritance-guide.md).
    std::unique_ptr<automation::AutomationPeer> CreateAutomationPeer() override;

private:
    // Hide the base's child API: pages must go through AddPage so pages_ and
    // children_ stay in sync (same reason TabControl hides them).
    using Panel::Add;
    using Panel::Emplace;
    using Panel::InsertAt;
    using Panel::RemoveAt;
    using Panel::Clear;

    void OnBackClick(Button&, RoutedEventArgs&) { Previous(); }
    void OnNextClick(Button&, RoutedEventArgs&) { Advance(); }
    void OnCancelClick(Button&, RoutedEventArgs&) { Cancel(); }

    // Raise Navigating + call ValidateCurrentPage; true when the move may proceed.
    bool ConfirmLeave(int to);
    // Sync button enabled state, the Next/Finish label and the indicator text.
    void UpdateChrome();
    // Clear focus if it sits inside `page` (which is about to be hidden).
    void DropFocusFrom(FrameworkElement* page);
    bool FadeEnabled() const;
    float IndicatorHeight() const { return showStepIndicator_ ? kStepIndicatorHeight : 0.0f; }

    std::vector<PageEntry> pages_;
    int currentPage_ = -1;
    // Divider line above the button bar, recorded in Arrange so Render draws exactly
    // where the bar was placed (padding-aware), in window DIPs.
    float dividerY_ = 0.0f;
    float dividerX0_ = 0.0f;
    float dividerX1_ = 0.0f;

    Button* btnBack_ = nullptr;     // owned by children_
    Button* btnNext_ = nullptr;
    Button* btnCancel_ = nullptr;
    TextBlock* stepIndicator_ = nullptr;
    bool showStepIndicator_ = false;
    bool navigationLocked_ = false;
    std::wstring stepFormat_ = L"Step {current} of {total}: {title}";

    std::wstring labelBack_ = L"Back";
    std::wstring labelNext_ = L"Next";
    std::wstring labelCancel_ = L"Cancel";
    std::wstring labelFinish_ = L"Finish";

    TransitionStyle transitionStyle_ = TransitionStyle::Fade;
    bool respectSystemAnimations_ = true;
    AnimatedValue pageFade_{1.0f};

    Event<WizardControl, int> pageChanged_;
    Event<WizardControl, WizardNavigatingArgs> navigating_;
    Event<WizardControl, RoutedEventArgs> finishing_;
    Event<WizardControl, RoutedEventArgs> cancelled_;

    Subscription backSub_;
    Subscription nextSub_;
    Subscription cancelSub_;
};

} // namespace wizard
} // namespace fluent
