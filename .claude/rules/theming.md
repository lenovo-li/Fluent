---
paths:
  - "FluentUI/styling/**"
  - "FluentUI/controls/**"
  - "FluentUI/dialogs/**"
  - "FluentUI/window/**"
  - "extensions/**"
---

### Theming

`ThemeSnapshot` (`styling/ThemeTokens.h`) is an immutable value holding typed
token groups plus a `generation` stamp. The host's `ThemeManager` owns one stable
snapshot and **overwrites it in place** on a theme change, bumping the cache
epoch — so the pointer parked on every `UIContext` stays valid and only the
pointee changes. A bumped generation is the signal for theme-derived caches
(text layouts, themed geometry) to rebuild.

Controls read tokens through `Theme()`. Never hard-code a color, corner radius,
font size, or spacing value in a control.

Semi-transparent tokens (`cardFill` is alpha 0.70, meant to sit over Mica) need
someone to supply an opaque base when no material exists. `ResolveBaseFill` does
this for the window; popups pre-flatten via `FlattenOver`. A new surface that
renders themed tokens without a DWM material behind it needs the same treatment.
