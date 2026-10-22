# InstallerControls

Installer wizard controls for FluentUI, built as a separate static library that
links `FluentUI.lib` (same pattern as `extensions/CustomControls`).
Namespace: `fluent::wizard` (`fluent::installer` is taken by FluentInstallerCore).

- **WizardControl**: multi-page container with a Back / Next(Finish) / Cancel bar,
  optional "Step N of M" indicator, fade transition, validation and navigation lock.
- **FolderPicker**: `IFileOpenDialog` + `FOS_PICKFOLDERS` wrapper.

Live demo: FluentUIDemo → 基础控件 → **Installer Controls** (nav id 113).
Headless tests: `tests/unit/WizardControlTests.cpp` (`FluentUITests.exe WizardControl`).

## Quick start

```cpp
#include "extensions/InstallerControls/InstallerControls.h"
using namespace fluent;
using namespace fluent::wizard;

class InstallerWindow : public Window {
protected:
    void OnInitialize() override {
        auto wizard = std::make_unique<WizardControl>();
        wizard_ = wizard.get();
        wizard_->AddPage(L"Welcome", BuildWelcomePage());
        wizard_->AddPage(L"Install", BuildProgressPage());
        wizard_->AddPage(L"Done",    BuildDonePage());
        wizard_->SetShowStepIndicator(true);

        // Handler payload is Args& (int&), never by value. STORE the Subscription:
        // discarding it unregisters the handler immediately.
        pageSub_ = wizard_->PageChanged().Subscribe(this,
            [](void* owner, WizardControl& w, int& page) {
                if (page == 1) {
                    w.SetNavigationLocked(true);  // no Back/Next/Cancel while installing
                    static_cast<InstallerWindow*>(owner)->StartInstall();
                }
            });
        finishSub_ = wizard_->Finishing().Subscribe(this,
            [](void* owner, WizardControl&, RoutedEventArgs&) {
                static_cast<InstallerWindow*>(owner)->Close();
            });
        SetRoot(std::move(wizard));  // the wizard fills the client area
    }

    // Called on the UI thread (e.g. via Application::Post) when the worker finishes:
    void OnInstallDone() { wizard_->SetNavigationLocked(false); wizard_->GoToPage(2); }

private:
    void StartInstall();  // your code: start the worker thread
    WizardControl* wizard_ = nullptr;
    Subscription pageSub_, finishSub_;
};
```

## API summary

| Area | API |
|---|---|
| Pages | `AddPage(title, unique_ptr<T>)` → `T*`, `PageCount`, `CurrentPage`, `PageTitle(i)`, `PageContent(i)` |
| Navigation | `Next`, `Previous`, `GoToPage`, `Advance` (= the Next/Finish button), `Cancel`, `CanGoNext`, `CanGoPrevious`, `IsLastPage` |
| Validation | `Navigating` event (`WizardNavigatingArgs{from, to, cancel}`; `to == kFinish` for Finish), `virtual bool ValidateCurrentPage()` |
| Hook | `virtual void OnPageChanged(int oldPage, int newPage)` |
| Events | `PageChanged` (`int&` new index), `Finishing`, `Cancelled` |
| Chrome | `SetButtonLabels(back, next, cancel, finish)`, `SetCancelVisible`, `SetNavigationLocked`, `BackButton()` / `NextButton()` / `CancelButton()` |
| Step indicator | `SetShowStepIndicator`, `SetStepIndicatorFormat(L"第 {current}/{total} 步 · {title}")`, `StepIndicatorText()` |
| Transition | `SetTransitionStyle(Fade / None)`, `SetRespectSystemAnimations(bool)` |
| FolderPicker | `FolderPicker::ShowDialog(hwnd, title = L"Select Folder", defaultPath = L"")` → path, or empty on cancel |

## Behavior worth knowing

- **Only Next / Finish validate.** `Previous` and `GoToPage` never do: going back must
  always work, and `GoToPage` is the host's programmatic path.
- **The lock blocks user intent, not code.** While locked the buttons are disabled and
  `Next` / `Previous` / `Advance` / `Cancel` are no-ops; `GoToPage` still works.
- **Hidden pages stay attached.** Non-current pages are `SetVisible(false)`: they cost no
  layout, paint or input, and keep their state across Back/Next.
- **Focus.** Focus inside a page being hidden, or on a button that becomes disabled or
  hidden, is cleared. Tab order is page content first, then Back / Next / Cancel.
- **Fade.** The first page never fades in. Page switches fade with the theme's `fadeTau`.
  The fade is skipped when Windows "Show animations" is off.
- **Height.** The wizard fills whatever it is given. Inside a stacking or scrolling
  parent, give it (or a wrapping `Border`) an explicit height.
- **FolderPicker is modal and blocking**, and needs COM initialized on the calling
  thread (`fluent::Application` does this).
- **Step format tokens are literal text**, not printf: `{current}`, `{total}`, `{title}`.
  A page title containing `{current}` or `%s` is shown as-is.

## Building

`Fluent.slnx` includes `extensions/InstallerControls/InstallerControls.vcxproj`.
Output: `x64/<Config>/InstallerControls.lib`. To consume it from another solution,
reference both projects:

```xml
<ItemGroup>
  <ProjectReference Include="..\Fluent\FluentUI\FluentUI.vcxproj" />
  <ProjectReference Include="..\Fluent\extensions\InstallerControls\InstallerControls.vcxproj" />
</ItemGroup>
```

The library is not part of the CMake build (neither is CustomControls).

## Status

Implemented 2026-09-29. This README and the header comments in `WizardControl.h` are
the reference; there is no separate design document.

## License

Same as FluentUI (see root LICENSE file).
