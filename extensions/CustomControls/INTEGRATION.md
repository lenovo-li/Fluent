# Integration Guide: CustomControls → FluentUIDemo

**Status**: Built and linked. `CustomControls.vcxproj` is in `Fluent.slnx`, the
demo references it, and the demo's **Custom Controls** page
(`FluentUIDemo/pages/CustomControlsPage.cpp`) shows every control. The test
project references it too (`tests/unit/CustomControlsTests.cpp`).

---

## What This Is

`extensions/CustomControls/` is a **separate library** demonstrating how end users should extend FluentUI by inheriting controls. It contains four working examples:

1. **LunarCalendar**: adds lunar dates below Gregorian days
2. **BigButton**: 64dp tall button with icon + title + subtitle
3. **LogSlider**: snaps to powers of ten (1, 10, 100, 1000); arrow keys step one decade
4. **SpinnerControl**: a fully custom animated control (derives `Control`)

These are **real implementations** (`.h` + `.cpp` files), not header-only stubs.

---

## Architecture

```
FluentUI.lib (core framework, unchanged)
    ↓ #include FluentUI headers, link FluentUI.lib
CustomControls.lib (user extension library, this directory)
    ↓ #include CustomControls.h, link both libs
FluentUIDemo.exe (demo application)
```

**Why separate?**
- ✅ Clear boundary: FluentUI = stable core, CustomControls = user code
- ✅ Users can fork CustomControls without touching FluentUI
- ✅ Demonstrates the real integration path
- ❌ Don't bloat FluentUI.lib with examples

---

## How It Is Wired (MSBuild)

- `CustomControls.vcxproj` is a static library with `$(SolutionDir)` on the
  include path and a `ProjectReference` to `..\..\FluentUI\FluentUI.vcxproj`.
  The `<Project>` GUID inside that reference must equal `FluentUI.vcxproj`'s
  `ProjectGuid` (`{dcce72dd-3d48-4f95-9746-89b4ec20677e}`); MSBuild resolves by
  path, but Visual Studio matches references by GUID. Keep it in step when you
  copy this project as a template.
- `Fluent.slnx` lists it under the `/extensions/` folder.
- `FluentUIDemo.vcxproj` and `tests/FluentUITests.vcxproj` each carry a
  `ProjectReference` to it.

Using the controls from an app:

```cpp
#include "../../extensions/CustomControls/CustomControls.h"

using namespace fluent::custom;

// Panels own their children: Add takes std::unique_ptr and returns the raw pointer.
auto* lunarCal = panel->Add(std::make_unique<LunarCalendar>());
lunarCal->SetSelectedDate(2026, 9, 27);

auto* bigBtn = panel->Add(std::make_unique<BigButton>());
bigBtn->SetText(L"Download");
bigBtn->SetSubtitle(L"1.2 GB available");
bigBtn->SetIcon(L"⬇");
bigBtn->SetWidth(200.0f);

auto* logSlider = panel->Emplace<LogSlider>();   // Emplace = Add(make_unique<T>())
logSlider->SetValue(100.0f);
```

---

## CMake

There is no CMake build for this library. The root `CMakeLists.txt` builds only
`FluentUI` and `FluentSettings` (plus `examples/Minimal`); the demo and the tests
are MSBuild-only, so nothing in CMake consumes CustomControls. A downstream CMake
project builds its own copy with `add_library(... STATIC ...)` over these `.cpp`
files and `target_link_libraries(... PUBLIC FluentUI::FluentUI)` (or the in-tree
`FluentUI` target).

---

## Testing

- `tests/unit/CustomControlsTests.cpp` links this library and covers what runs
  headless: LogSlider's keyboard stepping and BigButton's row layout.
- `tests/unit/InheritanceExamplesTest.cpp` compile-checks the protected extension
  points through `tests/unit/InheritanceExamples.h`, a separate header-only
  variant of similar controls.
- What the controls look like (glyphs, colours, the lunar text) needs the demo on
  a real display: open the **Custom Controls** page.
