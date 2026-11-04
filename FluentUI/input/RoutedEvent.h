// RoutedEvent.h — argument structs for tunneling/bubbling input.
//
// A routed event travels the element chain from the hit element up to the root
// (Bubble) after an optional root->target Preview pass (Tunnel). The args carry
// the routing state shared by every phase:
//   * source          — the element currently handling (updated as it walks),
//   * originalSource   — the deepest element the pointer/key actually hit,
//   * handled          — set true by a handler to stop further routing.
// PointerEventArgs / KeyEventArgs add the event-specific payload. These are the
// values passed to the UIElement routing virtuals (OnPointerPressed etc.) and to
// the typed control Events.
#pragma once

#include "InputTypes.h"

namespace fluent {

class UIElement;  // forward: args reference elements without a layering cycle

struct RoutedEventArgs {
    UIElement* source = nullptr;          // element currently on the route
    UIElement* originalSource = nullptr;  // deepest hit element (route origin)
    bool handled = false;                 // set by a handler to stop routing
};

struct PointerEventArgs : RoutedEventArgs {
    Point position;                              // window DIPs
    PointerButton button = PointerButton::None;  // button for press/release
    ModifierKeys modifiers = ModifierKeys::None;
    int wheelDelta = 0;                          // WHEEL_DELTA units (wheel only)
    // 1 = single, 2 = double, 3 = triple (press only; saturates at 3 — see
    // ClickCounter.h). Defaults to 1 so a handler that does not care reads a plain
    // click, and so every existing construction site keeps its previous meaning.
    int clickCount = 1;

    // Lines to scroll per wheel notch, from the system setting (Settings → Bluetooth
    // & devices → Mouse → "Lines to scroll at a time"). Stamped by the window when
    // the event is created, because the window — not the element — owns the HWND-level
    // setting and this header sits below WindowServices in the layering.
    //
    // Defaults to 3 (the Windows default) so a hand-constructed args in a test or a
    // synthetic event still scrolls a sane amount instead of freezing at 0.
    // WHEEL_PAGESCROLL (0xFFFFFFFF) is normalized to 0 by the stamping code, which
    // controls read as "page scroll requested".
    //
    // uint32_t, not UINT: this layer is deliberately free of <windows.h> (see the
    // header comment in InputTypes.h). Spelling it UINT compiles only because the
    // precompiled header force-includes windows.h, and breaks any TU built without it.
    uint32_t wheelScrollLines = 3;

    // Signed scroll distance in lines: positive scrolls content down (offset grows),
    // matching every control's `lines * rowHeight` usage. 0 means the user asked for
    // page-per-notch scrolling.
    float WheelScrollLines() const {
        if (wheelScrollLines == 0) return 0.0f;  // page-scroll request
        // 120 is WHEEL_DELTA. Written as a literal for the same no-windows.h reason.
        constexpr float kWheelDelta = 120.0f;
        return -static_cast<float>(wheelDelta) / kWheelDelta *
               static_cast<float>(wheelScrollLines);
    }
};

struct KeyEventArgs : RoutedEventArgs {
    unsigned vk = 0;                             // virtual-key code (VK_*)
    ModifierKeys modifiers = ModifierKeys::None;
};

}  // namespace fluent
