// PixelSnapping.h — Utilities for snapping DIP coordinates to physical pixel boundaries
//
// DirectWrite's ClearType subpixel rendering produces different visual output when text
// is positioned at fractional pixel coordinates. During smooth scrolling with floating-
// point offsets (e.g., 15.7 DIP), text can appear to jitter or shimmer as it moves
// between subpixel positions. Snapping scroll offsets to physical pixel boundaries
// before rendering eliminates this artifact with negligible visual impact (< 0.5 px).
//
// Usage:
//   const float dpiScale = dc.DpiScale();
//   const float snappedY = SnapToPixel(scroll_.Offset(), dpiScale);
//   dc.DrawTextLayout(D2D1::Point2F(x, y - snappedY), layout, color);

#pragma once

#include <cmath>

namespace fluent {

// Snap a DIP coordinate to the nearest physical pixel boundary.
//
// This prevents subpixel rendering jitter when scrolling text. The conversion to
// physical pixels, rounding, and back to DIPs ensures text always renders at integer
// pixel positions, where ClearType produces consistent output.
//
// Example: At 150% DPI (dpiScale = 1.5):
//   SnapToPixel(15.7f, 1.5f) → 15.7 * 1.5 = 23.55 px → round = 24 px → 24 / 1.5 = 16.0 DIP
//
// The 0.3 DIP adjustment (0.45 physical pixels) is imperceptible but eliminates visible
// jitter during scrolling.
inline float SnapToPixel(float dip, float dpiScale) {
    const float px = dip * dpiScale;
    return std::roundf(px) / dpiScale;
}

} // namespace fluent
