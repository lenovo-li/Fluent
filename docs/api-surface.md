# API surface: what is supported, what is not

FluentUI installs **every** header. That is deliberate and it is not a statement
about support. The library is a static lib that keeps a large amount of inline and
template code in headers, so a consumer's compiler must be able to see the
internal ones: `Window.h` derives from `NativeWindowHost.h`, controls call into
`graphics/` and `composition/` from inline methods, and `Event.h` is a template.
Installing only a curated subset would not protect anyone — it would simply fail
to compile.

The boundary is therefore expressed here, in documentation, plus the
`FLUENTUI_INTERNAL` marker in `Api.h` and in the header comment of each internal
header. Three tiers:

| Tier | Meaning | Compatibility |
|---|---|---|
| **Stable** | The supported application-authoring API. Everything `FluentUI/FluentUI.h` exports. | Source-compatible within a `0.x` minor line. |
| **Extension** | Needed to write a *custom control*, not to write an application. Reachable, documented, but shaped by the framework's needs. | May change in a minor release; changes will be listed in the changelog. |
| **Internal** | Implementation. Installed only because the compiler needs it. | No guarantees. May change or disappear in any release. |

Include `<FluentUI/FluentUI.h>` and nothing else for application code. If you find
yourself including a header directly, check which tier it is in first.

---

## Stable — application authoring

Everything below is exported by the umbrella header.

**Application shell**

| Header | Types |
|---|---|
| `Application.h` | `Application`, `ShutdownMode` |
| `window/Window.h` | `Window`, `WindowEventArgs`, `WindowClosingArgs` |
| `window/DialogWindow.h` | `DialogWindow`, `DialogResult`, `DialogClosingArgs`, `DialogClosedArgs` |
| `window/DialogHost.h` | `DialogHost` |
| `window/WindowState.h` | `WindowState` (placement persistence) |

**Element model and layout**

| Header | Types |
|---|---|
| `core/Visual.h` | `Visual` |
| `core/UIElement.h` | `UIElement` |
| `core/FrameworkElement.h` | `FrameworkElement`, `Thickness`, `HAlign`, `VAlign` |
| `core/Control.h` | `Control` |
| `core/ContentControl.h` | `ContentControl` |
| `core/Layout.h` | `SizeDip`, layout primitives |
| `core/Subscription.h` | `Subscription` |
| `layout/Panel.h` | `Panel` |
| `layout/StackPanel.h` `layout/WrapPanel.h` `layout/Grid.h` `layout/DockPanel.h` `layout/Canvas.h` `layout/Border.h` `layout/ScrollPanel.h` `layout/ScrollViewer.h` | the layout panels |
| `fl_common.h` | `RectDip`, `ComPtr`, trace helpers |

**Controls**

`controls/Button.h`, `CheckBox.h`, `RadioButton.h`, `ToggleSwitch.h`, `Slider.h`,
`ProgressBar.h`, `ComboBox.h`, `ListBox.h`, `TextBlock.h`, `TextBox.h`, `TextArea.h`,
`TreeView.h`, `TabControl.h`, `ContentDialog.h`, `MessageDialog.h`, `MenuBar.h`,
`MenuFlyout.h`, `ToolBar.h`, `StatusBar.h`, `DatePicker.h`, `Calendar.h`, `Image.h`,
`Hyperlink.h`, `GridSplitter.h`.

**Events and theming**

| Header | Types |
|---|---|
| `base/Event.h` | `Event<Sender, Args>`, `SubscriptionBag` |
| `input/RoutedEvent.h` | `RoutedEventArgs` and friends |
| `input/InputTypes.h` | `ModifierKeys`, `Point`, pointer/key arg types |
| `input/DragDrop.h` | `MakeFileDropTarget`, low-level drag/drop types and payload helpers |
| `styling/ThemeTokens.h` | `ThemeSnapshot`, `ThemeInputs`, token groups |

---

## Extension — writing a custom control

Stable enough to build on, but these exist to serve the framework's internals and
will move when the internals do. If you only write applications, you do not need
them.

| Header | Why you would reach for it |
|---|---|
| `graphics/DrawingContext.h` | The only supported way to paint. Required by any `Render()` override. |
| `core/Invalidation.h` | `DirtyFlags` — needed to pick the right flag in `SetProperty`. |
| `controls/primitives/ButtonBase.h` | Base for click-like controls. |
| `controls/primitives/ToggleButton.h` | Base for two/three-state controls. |
| `controls/primitives/RangeBase.h` | Base for numeric-range controls. |
| `controls/primitives/ItemsControl.h` `Selector.h` | Bases for collection controls. |
| `controls/TextEditBase.h` | Shared text-editing behavior (TextBox / TextArea). |
| `animation/AnimatedValue.h` `Animation.h` | Per-frame animated properties. |
| `composition/ICompositionBackend.h` | The compositor seam. **Always null-check** — see CLAUDE.md. |
| `composition/ScrollContentHost.h` | Overscan scrolling engine. |
| `window/WindowServices.h` | The host interface a control depends on (never the concrete window). |
| `core/UIContext.h` | The injected service aggregate. |
| `core/IContextMenu.h` | Context-menu participation. |
| `styling/FocusVisual.h` | Drawing a conforming focus ring. |
| `core/ScrollMath.h` | `EnsureVisibleOffset` and scrolling arithmetic. |

---

## Internal — no guarantees

Installed because the compiler needs them. Not supported; do not build against
them. Each carries a `FLUENTUI_INTERNAL` note in its header comment.

**Native host layer**

- `window/NativeWindowHost.h` — **the primary case.** It is the native host: HWND
  and WndProc, the D3D/D2D/DComp devices, DirectWrite, theming, input routing,
  focus, frame scheduling, per-monitor DPI, title bar, popup and tooltip hosting.
  Its name describes a visual trait while its job is hosting, so subclassing it
  invites reaching past the authoring model into device internals. **Derive from
  `Window` instead.** `Window` and `DialogWindow` are the supported bases.
- `WindowRegistry.h` — `Application`'s window bookkeeping, extracted for
  testability.
- `window/FrameScheduler.h`, `window/FramePacing.h`, `window/DirtyRegion.h` —
  frame coalescing, pacing arithmetic, partial-redraw planning.
- `window/FrameWaiter.h` — the message loop's wait primitive (high-resolution
  waitable timer + message wake). Owned by `Application`.
- `window/ModalFrameHeartbeat.h` — precise frame tick for the system's modal
  move/resize loop, where our loop is not running. Worker thread + waitable timer
  posting a private message. Owned by `NativeWindowHost`.
- `window/WindowAppearance.h` — backdrop/corner/caption resolution.

**Graphics and composition**

- `graphics/D2DContext.h`, `graphics/DCompHost.h`, `graphics/DWriteContext.h`,
  `graphics/ResourceCache.h`, `graphics/DeviceLostController.h`
- `composition/DCompCompositionBackend.h` — the DComp implementation of
  `ICompositionBackend`. Depend on the interface, not this.
- `composition/OverlaySignature.h`

**Input, services, theming internals**

- `input/InputManager.h`, `input/FocusManager.h` — the routing and focus
  authorities. Controls receive events; they do not drive these.
- `services/PopupHost.h`, `services/TooltipService.h`, `services/PopupState.h`,
  `services/PopupGeometry.h`
- `styling/ThemeManager.h` — owns the snapshot. Read tokens via `Theme()`.
- `controls/ProgressSweepVisual.h`

**Diagnostics**

- `diagnostics/SessionLog.h`, `diagnostics/DebugHud.h`,
  `diagnostics/PerformanceCounters.h`, `diagnostics/FrameRing.h`

These are useful and you may well want them, but they are shaped for this
repo's own debugging and change without notice.

---

## Packaging note

The install rule excludes MSBuild output directories (`x64/`, `Win32/`,
`Debug/`, `Release/`). Without that filter, a recursive header install copies
build intermediates into `include/FluentUI/x64/Release/*.tlog`. See the comment
on the `install(DIRECTORY ...)` rule in `CMakeLists.txt`.
