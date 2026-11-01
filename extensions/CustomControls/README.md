# CustomControls — User-Defined Control Extensions

**Purpose**: Demonstrates how users can **reference FluentUI.lib** and build their own custom controls in a **separate library**, without modifying the FluentUI source tree.

---

## Architecture

```
FluentUI (core library)
    ↓ (links against)
CustomControls (user library, this example)
    ↓ (links against)
FluentUIDemo (application)
```

This is the **recommended pattern** for extending FluentUI:

1. **FluentUI.lib** remains unchanged (your core framework)
2. **Users create their own library** (like this `CustomControls` example) that:
   - `#include`s FluentUI headers
   - Links against `FluentUI.lib`
   - Exports new controls (`LunarCalendar`, `BigButton`, `LogSlider`, `SpinnerControl`)
3. **Demo or end-user app** links both libraries

---

## Why Not Put Custom Controls in FluentUI Source?

| Approach | Pros | Cons |
|----------|------|------|
| **❌ In FluentUI source tree** | Convenient for demos | • Blurs the line between framework and user code<br>• FluentUI.lib grows with every example<br>• Users see "library controls" vs "examples" mixed together |
| **✅ Separate library (this)** | • Clear boundary: FluentUI = framework, CustomControls = user extensions<br>• Users can fork/modify this library without touching FluentUI<br>• Demonstrates the real integration path | Requires one extra build step |

---

## What's Included

### LunarCalendar
Inherits `Calendar`, adds lunar date display below Gregorian days.

**Demonstrates**:
- Calling `Calendar::RenderCell()` to reuse base rendering
- Reading `CellInfo` struct for pre-computed state
- Drawing additional text via `DWriteContext`

### BigButton
Inherits `Button`, draws a 64dp tall button with an icon, a title and a subtitle
(`SetIcon`, `SetText`, `SetSubtitle`; empty rows take no space).

**Demonstrates**:
- Reading `CurrentKind()` and `State()` to sync text colour with base state
- Keeping Button's own `RenderBackground()` / `RenderBorder()` chrome
- Overriding `Measure()` to change natural size
- Overriding `RenderContent()` for custom layout

### LogSlider
Inherits `Slider`, snaps the value to powers of ten (1, 10, 100, 1000). The
track stays linear in value, so dragging favours the large decades; the arrow
keys step exactly one decade.

**Demonstrates**:
- Overriding `CoerceValue()` to snap to logarithmic steps
- Overriding `OnKeyDownRouted()` when the base `Step()` no longer fits the grid
- Using `ValueToTrackPos()` and `TrackCenterline()` to draw a tick mark at each power of ten

### SpinnerControl
Derives `Control` directly: a fully custom animated control (`Start` / `Stop`, `SetSpeed`,
`SetDiameter`). `WantsAnimationTick()` is `IsRunning()`, so a stopped spinner costs nothing.

---

## UI Automation

Each control adds its peer from this library (`CreateAutomationPeer()` on the public
`FluentUI/automation/*.h` headers), the way an app's own control library does:

- **BigButton** is a Button (Invoke goes through the click path). Its subtitle, which it
  draws but a plain Button peer does not know about, is the HelpText unless a tooltip is set.
- **LunarCalendar** derives from `CalendarPeer`: a day cell's name is the long Gregorian date
  followed by the lunar text drawn under it (`LunarText()`), so a reader hears what the cell shows.
- **LogSlider** needs nothing: Slider's RangeValue peer reads and sets the value, and
  `CoerceValue` snaps a UIA SetValue to a decade like a drag.
- **SpinnerControl** is a ProgressBar with no value (indeterminate, as WinUI reports
  ProgressRing), named by `SetAutomationName` (default "Busy"). It is in the control view only
  while running.

Tested in `tests/unit/AutomationExtensionPeerTests.cpp`; a mutation of each peer (dropping the subtitle, the lunar text, the step name, the dialog flag, ...) fails a test.

## Tests

Headless, in `FluentUITests.exe` (`CustomControls`, `InheritanceExamples`). Only what is
reachable without a device is covered; glyph rendering and contrast need a real window.

| Behavior | Test |
|---|---|
| LogSlider arrows step exactly one decade and stop at the ends | `CustomControls.LogSliderArrowsStepOneDecade` |
| A LogSlider arrow raises `ValueChanged` | `CustomControls.LogSliderArrowRaisesValueChanged` |
| Home / End still reach the ends | `CustomControls.LogSliderHomeEndStillReachTheEnds` |
| BigButton rows stack in order inside the 64 DIP button | `CustomControls.BigButtonRowsStackInOrderAndFitTheButton` |
| A label-only BigButton is centred | `CustomControls.BigButtonLabelOnlyIsCentred` |
| A subtitle gets its own row | `CustomControls.BigButtonSubtitleGetsARow` |
| A drag on the inheritance-guide LogSlider commits on release | `InheritanceExamples.LogSliderCommitsADragOnRelease` |

Before this, the base Slider arrows did nothing on LogSlider: they add +/-1, and
`CoerceValue` rounded 100 + 1 straight back to 100. BigButton drew only its title;
the icon and subtitle setters existed but nothing painted them, and `SetIcon` asked for a
Measure pass although the height is fixed. Not verified headless: what the three rows
look like, and the subtitle colour on an Accent or disabled button. Check on hardware on
the demo's Custom Controls page.

---

## How to Build

`CustomControls.vcxproj` is part of `Fluent.slnx`; building the solution builds
it after `FluentUI` and links it into `FluentUIDemo` and `FluentUITests`. There is
no CMake build for this directory (see `INTEGRATION.md`).

---

## How to Use in Your Own Project

1. **Build FluentUI.lib** (or install it as a package)
2. **Create your own library project**:
   - Add FluentUI's include path: `FluentUI/`
   - Link against `FluentUI.lib` + Windows libs (d2d1, dwrite, dcomp, windowscodecs, etc.)
3. **Write your custom controls** (like the examples here)
4. **Link your app** against both `FluentUI.lib` and your custom library

---

## File Structure

```
extensions/CustomControls/
├── README.md                  (this file)
├── INTEGRATION.md             (how the library is wired into the solution)
├── CustomControls.h           (public header: LunarCalendar, BigButton, LogSlider)
├── SpinnerControl.h           (public header: SpinnerControl)
├── LunarCalendar.cpp          (implementation)
├── BigButton.cpp              (implementation)
├── LogSlider.cpp              (implementation)
├── SpinnerControl.cpp         (implementation)
└── CustomControls.vcxproj     (Visual Studio project, references FluentUI.vcxproj)
```

---

## Integration into FluentUIDemo

The demo links `CustomControls.lib` and shows all four controls on its
**Custom Controls** page (`FluentUIDemo/pages/CustomControlsPage.cpp`): working
examples of "how to extend FluentUI without forking it".
