---
paths:
  - "FluentUI/graphics/**"
  - "FluentUI/composition/**"
  - "FluentUI/window/**"
  - "FluentUI/base/**"
  - "FluentUI/core/**"
  - "FluentUI/controls/**"
  - "FluentUI/layout/**"
  - "extensions/**"
---

### Rendering: DComp surface, not a swap chain

Window content is an `IDCompositionVirtualSurface`, deliberately not a swap
chain. Reason: content updates via `Present1` (DXGI queue) and child-visual
transforms via `Commit()` (composition tree) can land on different vsyncs, so
during a resize the background and an independently composited child desync and
visibly shake. Drawing content into a DComp surface means both publish in the
*same* `Commit()` and can't disagree. `graphics/DCompHost.h` owns this.

Consequences worth knowing before touching this layer:

- The surface is **persistent** — pixels outside the update rect keep last
  frame's content. That's what partial redraw relies on.
- `FrameLatencyWaitable()` is permanently `nullptr`; there's no swap chain to
  pace against.
- `BeginContentFrame` returns an atlas-tile offset that must be folded into the
  transform (`Scale(dpi) * Translation(net)`).

Controls draw through `DrawingContext` (`graphics/DrawingContext.h`), a thin
wrapper over the frame's `ID2D1DeviceContext` plus one shared solid brush. The
brush is private, so the typed methods (`FillRoundedRect(rr, color)`,
`DrawTextLayout(...)`) all go through `Faded()` — which is why element opacity
needs no per-control code. `Dc()` is the escape hatch to the raw context
(bitmaps, per-range text brushes, transforms, clips); whatever a control draws
through it must apply `dc.Opacity()` / `dc.Faded()` by hand (Image, TreeView's
custom icons and RichTextBox's run colours do), or it stays opaque inside a faded
parent.

### Partial redraw and the dirty-bounds contract

`window/DirtyRegion.h` is a pure function: given this frame's dirty-rect union it
decides full vs partial redraw and returns a pixel-aligned update rect. Two
contracts feed it, and violating either produces on-screen ghosting:

- **`CollectDirtyBounds` must cover every pixel the element paints.** Nothing
  widens the reported region afterwards. An element that paints outside
  `bounds_` (focus ring, a stroke centered on the bounds edge, an oversized drag
  thumb) must override `VisualOverflowDip()`; `VisualBounds()` then inflates
  automatically. Current declarations: Button, ComboBox, DataGrid, Expander,
  InfoBar, NumericUpDown and Rating 3.75 (the default `FocusRingPadDip`),
  CheckBox/RadioButton/ToggleSwitch 4.75, `TextEditBase` 1.75 (inherited by
  TextBox and TextArea), Hyperlink 3.5 (or the underline's overhang below a
  short explicit Height, whichever is larger), Slider ≈14.2.
- **Overflow must be declared unconditionally**, not only while focused — the
  frame where focus leaves still needs those pixels cleared.

`VisualOverflowDip()` is deliberately one virtual feeding *both* the dirty-rect
report and `Panel::Render`'s cull, so the "what gets cleared" and "what gets
redrawn" judgments cannot drift apart. They did once, and that was a ghosting
bug class.

The redraw region is snapped to whole pixels inside `PlanRedraw` because
`ClipGuard` uses antialiased clipping — a fractional clip edge gives boundary
pixels partial coverage, leaving a faint 1px seam tracing the dirty rect.

### Compositor offload

Animations that must survive a busy UI thread run on the DirectComposition
compositor thread. Controls reach it through `ICompositionBackend` (obtained from
`WindowServices::Composition()`), never by naming a DComp type. `Composition()`
returns null on a host without composition or mid device-loss — **every caller
must null-check and fall back to its UI-thread path**. ProgressBar's
indeterminate sweep is the current example. See `composition/ICompositionBackend.h`
for usage rules (coordinate space, transform premultiply, surface drawing) and
`docs/learn/` for compositor integration patterns and ScrollContentHost removal
history.

### Opacity

Three levels, and picking the wrong one is a real bug:

- `DrawingContext::WithOpacity` / `Faded()` — per-primitive alpha multiply. The
  choke point for every typed draw method; raw `Dc()` draws apply it by hand.
- `Visual::SetOpacity` + `RenderWithOpacity` — what containers call on children
  (`Panel::Render` uses `RenderWithOpacity`, not `Render`). Opacity 1.0 takes the
  original path at zero cost; 0.0 skips `Render` entirely.
- `EffectiveOpacity()` (multiplies the ancestor chain) — **only** for pixels drawn
  through a composition visual instead of the parent's `DrawingContext`. Ancestor
  opacity folded into that context never reaches such a visual, so the owner must
  push `EffectiveOpacity()` to `ICompositionVisual::SetOpacity` itself. No control
  does this today (ProgressBar's sweep visual ignores ancestor opacity); a new
  composition-visual user should.

Opacity is appearance-only: layout and hit-testing are unaffected (use
`SetVisible(false)` to remove from layout). It carries a Render-only dirty flag —
never Measure. Alpha is straight, not premultiplied; D2D premultiplies
internally, and doing it by hand makes every faded control go dark.
