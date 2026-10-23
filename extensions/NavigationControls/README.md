# NavigationControls

Navigation controls for FluentUI, built as a separate static library that links
`FluentUI.lib` (same pattern as `extensions/CustomControls` and
`extensions/InstallerControls`). Namespace: `fluent`.

- **NavRail**: vertical navigation rail. Optional header element (logo), icon items,
  selected item = 3 DIP accent bar + accent-tinted rounded fill, hover fill, red-dot
  badge, per-item tooltip, keyboard cursor (Up/Down + Enter/Space).
- **OverlayHost**: in-window modal overlay. Scrim over the whole layer plus a centered
  520 DIP card (icon, title, message, optional scrollable details box, optional
  ProgressBar, optional warning bar, pill buttons). Blocks all input underneath, keeps
  focus inside the card, fades in/out.
- **PageHost**: multi-page container with animated transitions. Supports None/Fade/FadeSlide
  modes. Only the current page responds to input during transitions. Respects system
  "Show animations in Windows" setting.

## Quick start

```cpp
#include "extensions/NavigationControls/NavigationControls.h"
using namespace fluent;

// Root grid: rail in column 0, page in column 1, overlay spanning both (added last
// so it is topmost).
auto root = std::make_unique<Grid>();
root->AddColumn(GridLength::Auto());
root->AddColumn(GridLength::Star(1));
root->AddRow(GridLength::Star(1));

auto* rail = root->Emplace<NavRail>();
root->SetCell(rail, 0, 0);
auto logo = std::make_unique<Image>();
logo->SetSource(L"logo.png");
logo->SetWidth(48); logo->SetHeight(48);
rail->SetHeader(std::move(logo));
rail->SetItems({{L"\xE896", L"Firmware Updates"}, {L"\xE897", L"About", true}});
rail->SetSelectedIndex(0);  // programmatic: does not raise SelectionChanged

auto* pageHost = root->Emplace<PageHost>();
root->SetCell(pageHost, 0, 1);
pageHost->SetTransition(PageTransition::FadeSlide);  // default, optional
pageHost->AddPage(std::move(firmwarePage));
pageHost->AddPage(std::move(aboutPage));
pageHost->ShowPage(0);

auto* overlay = root->Emplace<OverlayHost>();
root->SetCell(overlay, 0, 0, 1, 2);

// Payloads arrive as Args& (int& / OverlayResultArgs&). STORE the Subscription:
// discarding it unregisters the handler immediately.
navSub_ = rail->SelectionChanged().Subscribe<&MainWindow::OnNav>(this);
closedSub_ = overlay->Closed().Subscribe<&MainWindow::OnOverlayClosed>(this);

void MainWindow::OnNav(NavRail& rail, int& index) {
    pageHost->ShowPage(index);
}

overlay->Show({OverlayIcon::Info, L"Firmware Update",
               L"This device doesn't need to be updated.", L"OK"});
```

Progress state (no buttons; Esc is ignored):

```cpp
OverlaySpec spec;
spec.icon = OverlayIcon::Info;
spec.title = L"Updating firmware";
spec.message = L"Please keep the dock connected.";
spec.showProgress = true;
overlay->Show(spec);
overlay->UpdateProgress(45, L"Writing firmware... 45%");
```

Details box and warning bar (both optional; leave empty to omit):

```cpp
OverlaySpec spec;
spec.icon = OverlayIcon::Info;
spec.title = L"Firmware Update";
spec.message = L"1.2.00 -> 1.3.00";
spec.details = releaseNotes;        // long text: scrolls past ~9 lines, Tab-reachable
spec.warning = L"Do not unplug the dock until the update finishes.";
spec.primaryText = L"Update";
spec.secondaryText = L"Cancel";
overlay->Show(spec);
```

`warning` survives `UpdateProgress` (only the message is rewritten), so the same
warning can stay on screen through the whole Progress state.

## API summary

| Area | API |
|---|---|
| NavRail data | `SetItems({glyph, label, showBadge})`, `SetBadge(i, bool)`, `SetHeader(unique_ptr)` |
| NavRail selection | `SetSelectedIndex(i)` (silent), `SelectedIndex()`, `SelectionChanged()` (`int&`, user only) |
| NavRail look | `SetRailBackground(color)` (default `windowBackground`), constants `kRailWidth` etc. |
| PageHost | `AddPage(unique_ptr)` → index, `ShowPage(i)`, `CurrentPage()`, `IsTransitioning()` |
| PageHost animation | `SetTransition(None/Fade/FadeSlide)`, `SetTransitionDuration(sec)`, `SetSlideDistance(dip)` |
| Overlay | `Show(spec)`, `UpdateProgress(percent, message)`, `Hide()` (no event), `IsShown()` |
| Overlay spec | `icon`, `title`, `message`, `primaryText`, `secondaryText`, `showProgress`, `details`, `warning` |
| Overlay result | `Closed()` → `OverlayResultArgs{Primary / Secondary / Dismissed}` |

## Behavior worth knowing

- **Icons** use "Segoe Fluent Icons" (Win11), falling back to "Segoe MDL2 Assets"
  (Win10); pick codepoints present in both (E896 Download, E897 Help, E701 Wifi).
- **Programmatic selection is silent.** Only click / Enter / Space raise
  `SelectionChanged`, so restoring the last page from config does not loop.
- **Esc** closes with Secondary when there is a secondary button, Dismissed when there
  is only a primary one, and does nothing in the button-less Progress state.
- **Enter on the overlay itself** activates Primary only when it is the sole button,
  so a Confirm dialog never commits on a stray Enter. A focused button handles its
  own Enter/Space.
- **Chaining states.** `Closed` is raised after the hide has started; calling `Show`
  from the handler reverses the fade, so Confirm → Progress has no flash.
- **Fade** follows the theme's `fadeTau` and is skipped when Windows "Show
  animations" is off. A fading-out overlay still blocks input until it collapses.
- **Colors** come from `Theme()` tokens only. Set the accent (e.g. Lenovo blue) through
  `Window::SetThemeConfiguration` and both controls follow it.

## Building

`Fluent.slnx` includes `extensions/NavigationControls/NavigationControls.vcxproj`
(solution folder `/extensions/`). Output: `x64/<Config>/NavigationControls.lib`.
Consuming it from another solution: see `INTEGRATION.md`.
