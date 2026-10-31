// AutomationGeometry.h — pure DIP <-> screen-pixel conversions for UIA.
//
// UIA reports BoundingRectangle and ElementProviderFromPoint in SCREEN PIXELS; the
// framework lays out in host-client DIPs (96-DPI basis). The conversion needs the
// host's client origin in screen pixels and its DPI, both read at query time (a
// window moves and changes DPI; nothing here is cached). Kept as free functions so
// the arithmetic is testable without a window, like PlanRedraw / FramePacing.
#pragma once

#include "../fl_common.h"

namespace fluent::automation {

struct ScreenRect {
    double left = 0, top = 0, width = 0, height = 0;
};

// Host-client DIP rect -> screen pixels. `originX/Y` is ClientToScreen({0,0}),
// `dpi` the host's DPI (96 = 100%).
inline ScreenRect ScreenRectFromHostDip(const RectDip& r, double originX, double originY,
                                        double dpi) {
    const double s = dpi / 96.0;
    return {originX + r.x * s, originY + r.y * s, r.w * s, r.h * s};
}

// Screen pixels -> host-client DIP point (inverse of the above).
struct DipPoint {
    float x = 0, y = 0;
};
inline DipPoint HostDipFromScreen(double sx, double sy, double originX, double originY,
                                  double dpi) {
    const double s = dpi > 0 ? 96.0 / dpi : 1.0;
    return {static_cast<float>((sx - originX) * s), static_cast<float>((sy - originY) * s)};
}

}  // namespace fluent::automation
