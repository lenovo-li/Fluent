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
  "Show animations in Windows" setting by default (`SetRespectSystemAnimationSetting`).

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
| NavRail pane mode | `SetPaneMode(true)` (opt-in, Win11 NavigationView look): `SetFooterItems`, `SetExpanded(bool, animate)`, `ExpandedChanged()`, `SetExpandedWidth`, `SetBackButtonVisible/Enabled`, `BackRequested()`, `SetItemEnabled(i, bool)`, item `header = true` (caption / separator), item `tooltip` (overrides the label tooltip), `SetPaneToggleTooltip`, `SetBackButtonTooltip`. Labels / captions that do not fit end in "…" and show the full label on hover |
| PageHost | `AddPage(unique_ptr)` → index, `ShowPage(i)`, `CurrentPage()`, `IsTransitioning()` |
| PageHost animation | `SetTransition(None/Fade/FadeSlide)`, `SetTransitionDuration(sec)`, `SetSlideDistance(dip)`, `SetRespectSystemAnimationSetting(bool)` (default true: cut instantly when Windows "Animation effects" is off) |
| Overlay | `Show(spec)`, `UpdateProgress(percent, message)`, `Hide()` (no event), `IsShown()` |
| Overlay spec | `icon`, `title`, `message`, `primaryText`, `secondaryText`, `showProgress`, `details`, `warning` |
| Overlay result | `Closed()` → `OverlayResultArgs{Primary / Secondary / Dismissed}` |

## Behavior worth knowing

- **Icons** use "Segoe Fluent Icons" (Win11), falling back to "Segoe MDL2 Assets"
  (Win10); pick codepoints present in both (E896 Download, E897 Help, E701 Wifi).
- **Programmatic selection is silent.** Only click / Enter / Space raise
  `SelectionChanged`, so restoring the last page from config does not loop.
  Likewise `SetExpanded` does not raise `ExpandedChanged` (only the ☰ button does).
- **Pane mode indices are unified**: `SetItems` items first, then footer items at
  `Items().size() + i`. Header items and disabled items are never selected and are
  skipped by Up / Down. The pane width eases (`AnimatedValue`) and jumps when
  Windows "Animation effects" is off. Rail mode (the default) is unchanged.
- **Esc** closes with Secondary when there is a secondary button, Dismissed when there
  is only a primary one, and does nothing in the button-less Progress state.
- **Enter on the overlay itself** activates Primary only when it is the sole button,
  so a Confirm dialog never commits on a stray Enter. A focused button handles its
  own Enter/Space.
- **Chaining states.** `Closed` is raised after the hide has started; calling `Show`
  from the handler reverses the fade, so Confirm → Progress has no flash.
- **Fade** follows the theme's `fadeTau` and is skipped when Windows "Show
  animations" is off. A fading-out overlay still blocks input until it collapses.
- **Disabled items in rail mode** keep their tooltip but get no hover fill (a fill would
  promise a click that selection then refuses), and Up / Down skip them the same way pane
  mode does. When only disabled items lie beyond the cursor, the cursor stays put instead
  of parking the focus ring on an item where Enter does nothing.
- **Pane labels are laid out once per text.** Each item keeps its pane label's
  `IDWriteTextLayout` across paints; it is rebuilt only when the label text or the text
  format changes, and the per-frame box size of the width ease only updates the layout's
  max width / height. Before, every hover change rebuilt a layout (plus an ellipsis object
  when trimmed) for every label on every paint.
- **PageHost Fade ticks are Render-only.** `Fade` changes nothing but the two pages'
  opacity, so each tick dirties Render on the pages, not Arrange on the host. Raising
  Arrange there made every Fade frame a full-window redraw plus a re-arrange of both page
  trees. `FadeSlide` still arranges each tick (the incoming page's slot moves).
- **`SetRespectSystemAnimationSetting(false)`** animates even when Windows "Animation
  effects" is off. Default true. Tests use it so the transition paths run on every machine
  instead of silently cutting where the setting happens to be off (CI, a VM).
- **Colors** come from `Theme()` tokens only. Set the accent (e.g. Lenovo blue) through
  `Window::SetThemeConfiguration` and both controls follow it.

## Tests

Headless, in `FluentUITests.exe` (`NavRailKeyboard`, `PageHost`):

| Behavior | Test |
|---|---|
| Up / Down skip disabled items in rail mode | `NavRailKeyboard.DownSkipsADisabledItem`, `NavRailKeyboard.UpSkipsADisabledItem`, `NavRailKeyboard.CursorStaysWhenOnlyDisabledItemsLieBeyond` |
| Fade tick dirties Render, not Arrange | `PageHost.FadeTickDirtiesRenderNotArrange` |
| FadeSlide tick still arranges | `PageHost.FadeSlideTickStillArranges` |
| Hidden pages are measured only when their content changed | `PageHost.HiddenPagesAreNotMeasuredWhenOnlyTheSizeChanges`, `PageHost.ContentChangedWhileHiddenIsLaidOutWhenShown`, `PageHost.HiddenPageWithAChangeIsMeasuredOnceNotEveryFrame` |

Not verified headless (needs a real device): the pane-label layout cache and the
disabled-item hover fill in rail mode. To check on hardware: in the demo, hover a
disabled rail item and confirm no fill appears; with pane mode expanded, move the
pointer up and down the items with the F12 HUD open and confirm CPU Max does not grow
with the number of items.

## Building

`Fluent.slnx` includes `extensions/NavigationControls/NavigationControls.vcxproj`
(solution folder `/extensions/`). Output: `x64/<Config>/NavigationControls.lib`.
Consuming it from another solution: see `INTEGRATION.md`.
