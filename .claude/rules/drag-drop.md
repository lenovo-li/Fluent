---
paths:
  - "FluentUI/input/**"
  - "FluentUI/window/**"
  - "FluentUI/base/**"
---

### Drag and drop

`input/DragDrop.h` defines `IFluentDropTarget` — the interface a control
implements to accept drops — and `GetDroppedFiles` / `GetDroppedText`, which
extract payload from an `IDataObject`. `NativeWindowHost` implements the COM
`IDropTarget` and routes `DragEnter` / `DragOver` / `Drop` to the element tree via
`HitTestDropTarget` (which walks the tree like `HitTest`, looking for the deepest
element that has a drop handler *and* contains the point).

A control opts into drops by calling `SetDropTarget(handler)`, where `handler` is
an `IFluentDropTarget` lambda or object. The handler receives `OnDragEnter` /
`OnDragOver` / `OnDragLeave` / `OnDrop` with a `DragEventArgs` holding the mouse
position, the requested effect, and the `IDataObject`. Return the effect you
accept (`DROPEFFECT_COPY` / `LINK` / `MOVE` / `NONE`); the window reports it back
to the drag source.

Three rules:

- **Hit-testing respects visibility and enabled state.** A disabled or invisible
  element is never a drop target, even if it has a handler. The check happens in
  `UIElement::HitTestDropTarget`, not in the control, so you cannot override it.
- **Child handlers win over panel handlers.** `Panel::HitTestDropTarget` recurses
  into children first (front to back) and only returns itself if no child
  accepted. A `StackPanel` with a drop handler that contains a `TextArea` with its
  own handler will route to the `TextArea` when the drop is inside it — no
  explicit Z-order check needed.
- **Cache the `IDataObject` carefully.** `DragEnter` supplies it; `DragOver` does
  not (COM rule — the object is expensive to marshal). `NativeWindowHost` caches it
  in `dragData_` and passes the same pointer to every `OnDragOver` in that drag
  session. If a control wants payload during hover (to show a preview), it must
  either extract on `OnDragEnter` or be prepared to fail gracefully in
  `OnDragOver`. `OnDrop` gets a fresh `IDataObject` and is the authoritative
  source.

The one platform quirk: `RegisterDragDrop` requires a message loop pumping; it
cannot work before the window is shown. `NativeWindowHost` registers in
`WM_CREATE` and revokes in `WM_DESTROY`. A control can assume drop routing works
once `OnAttachedToTree` has fired.
