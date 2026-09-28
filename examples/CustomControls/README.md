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
   - Exports new controls (`LunarCalendar`, `BigButton`, `LogSlider`)
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
Inherits `Button`, draws a 64dp tall button with icon + two-line text.

**Demonstrates**:
- Reading `CurrentKind()` and `TintOpacity()` to sync with base state
- Calling `RenderBackground()` / `RenderBorder()` for chrome
- Overriding `Measure()` to change natural size
- Overriding `RenderContent()` for custom layout

### LogSlider
Inherits `Slider`, maps linear drag to logarithmic value (1, 10, 100, 1000).

**Demonstrates**:
- Overriding `CoerceValue()` to snap to logarithmic steps
- Reading `IsDragging()` to distinguish user input from programmatic changes
- Using `TrackStart()` / `TrackEnd()` / `ValueToTrackPos()` for custom tick marks

---

## How to Build

### Option 1: Visual Studio Solution
(To be added: `CustomControls.vcxproj`)

1. Build `FluentUI.lib` (Debug or Release)
2. Build `CustomControls.lib` (links against FluentUI.lib)
3. Update `FluentUIDemo.vcxproj` to link `CustomControls.lib`

### Option 2: CMake
(To be added: `CMakeLists.txt`)

```bash
cd examples/CustomControls
cmake -B build -G Ninja
cmake --build build
```

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
examples/CustomControls/
├── README.md                  (this file)
├── CustomControls.h           (public header: exports LunarCalendar, BigButton, LogSlider)
├── LunarCalendar.cpp          (implementation)
├── BigButton.cpp              (implementation)
├── LogSlider.cpp              (implementation)
├── CustomControls.vcxproj     (Visual Studio project, links FluentUI.lib)
└── CMakeLists.txt             (CMake build, links FluentUI target)
```

---

## Integration into FluentUIDemo

Once built, FluentUIDemo can showcase these controls by:

1. Linking `CustomControls.lib`
2. `#include "examples/CustomControls/CustomControls.h"`
3. Adding demo pages:
   - "Lunar Calendar" tab
   - "Big Buttons" tab
   - "Logarithmic Slider" tab

Users see **working examples** of "how to extend FluentUI without forking it".

---

## Status

- ✅ Example implementations exist in `examples/InheritanceExamples.h` (header-only prototypes)
- 🚧 **TODO**: Extract into proper `.cpp` files + build as a real library
- 🚧 **TODO**: Add .vcxproj + CMakeLists.txt
- 🚧 **TODO**: Update FluentUIDemo to link and showcase these controls

---

**Next Steps**: See [Implementation Plan](#implementation-plan) below.
