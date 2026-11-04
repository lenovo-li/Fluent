---
paths:
  - "FluentUI/Application.*"
  - "FluentUI/controls/Image.*"
  - "FluentUI/dialogs/**"
  - "FluentUI/services/**"
  - "FluentUIDemo/**"
  - "examples/**"
  - "extensions/InstallerControls/**"
---

### COM lifetime rules

The framework uses COM for WIC (Image decoding), IFileDialog (file picker), and
the ID2D1Bitmap / IDWriteTextLayout / IDCompositionVisual interfaces throughout
the rendering stack. `Application` owns the COM apartment (`CoInitializeEx` /
`CoUninitialize`) and guarantees it outlives every COM interface acquired
through the framework.

**Two lifetime violations and their fixes:**

1. **Static or global `ComPtr<T>` that destructs during CRT teardown** (after
   `main()` returns) calls `Release()` into a dead apartment. **Fix:** Use a
   raw pointer and deliberately leak it. The OS reclaims the memory on exit,
   and we sidestep the teardown-order hazard. See `Image.cpp:22` for the WIC
   factory singleton.

2. **Stack variable in `wWinMain` that destructs after `CoUninitialize`** —
   same symptom, different cause. The old pattern of calling `CoUninitialize()`
   just before `return` looks safe but is not: `return exitCode` triggers stack
   unwinding, which destructs the window *after* COM has shut down. **Fix:**
   RAII guard declared as the first local in `wWinMain`, so it destructs last.
   `Application` owns this guard (`Application::ComApartment`), and its header
   documents the declaration-order contract.

**DWrite / D2D / DComp don't use `CoCreateInstance`** — they're created via
exported functions (`DWriteCreateFactory`, `D2D1CreateFactory`,
`DCompositionCreateDevice`) and theoretically don't need the COM apartment. But
`ID2D1Bitmap` created via `CreateBitmapFromWicBitmap` straddles both worlds:
the D2D object's release path touches WIC's internal state, so `Application`'s
COM lifetime protection covers it too.

When adding a new COM component (e.g., Media Foundation for video), follow the
rule: **any interface obtained via `CoCreateInstance` must be owned by an
object whose lifetime is strictly shorter than `Application`'s.** That means no
static/global `ComPtr`, and the `Application` instance must be declared first
in `wWinMain`.
