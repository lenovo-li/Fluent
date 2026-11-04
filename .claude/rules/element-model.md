---
paths:
  - "FluentUI/**"
  - "extensions/**"
  - "tests/**"
---

### Element hierarchy

Four layers, each adding exactly one concern. The split is enforced — don't
collapse it:

```
Visual           visibility, opacity, Render(), dirty flags, invalidation plumbing
  UIElement      bounds (DIPs), hit-test, Measure/Arrange protocol, input, focus,
                 attach/detach lifecycle, per-frame animation ticks
    FrameworkElement   Width/Height/Margin/HAlign/VAlign; concrete leaf layout
      Control          Background/Padding/theme-aware chrome
      Panel            owns children (unique_ptr), recurses render/hit-test/attach
```

`Visual` knows nothing about size or input. `UIElement` knows nothing about
`Width`/`Margin`. Layout is two-pass WPF-style: `Measure(avail)` fills
`desired_`, `Arrange(rect)` sets absolute bounds. Panels call
`MeasureCached()` on children, not `Measure()` — it short-circuits when the
element isn't Measure-dirty and the constraint matches.

### Services via UIContext, not constructor wiring

A control never takes a window pointer or a DWrite pointer in its constructor.
`UIContext` (`core/UIContext.h`) is a flat aggregate of non-owning service
pointers — window, dwrite, animations, input, focus, resourceCache, theme, hwnd,
dpiScale — injected by the framework when the element joins a live tree
(`AttachToContext`) and cleared on detach.

Rules that follow from this:

- Read services through the protected `Context()` / `Dwrite()` / `Window()` /
  `Theme()` accessors, only while attached. Never cache a service pointer past
  the attach period.
- Acquire host registrations in `OnAttachedToTree()` and wrap them in
  `AddContextSubscription()` so they auto-cancel on detach. This is what prevents
  dangling `[this]` callbacks.
- Every context member can be null (headless tests wire only what they exercise).
  `Theme()` is the exception — it falls back to a static default snapshot and
  never returns null.

Controls depend on `WindowServices` (`window/WindowServices.h`), a small
interface, rather than the concrete `NativeWindowHost`. That's what makes controls
testable against a mock host.
