// WindowIconTests.cpp — per-DPI window icon sizing and title-bar icon placement
// (window/WindowState.h).
//
// The title-bar icon used to be loaded once at SM_CXICON and shrunk to 16 DIP by
// D2D linear filtering, which erased 1-2px strokes. The window now loads the icon
// frame whose pixel size equals 16 DIP at the current DPI and draws it 1:1 at a
// whole-pixel origin. Both halves are pure arithmetic, so they are tested here:
// a wrong size reintroduces resampling, and a fractional origin makes D2D
// resample even a correctly-sized bitmap.

#include "../framework/Test.h"
#include "../../FluentUI/window/WindowState.h"

using namespace fluent;

// The standard scale steps map onto the frame sizes an .ico normally carries.
TEST(WindowIcon, SmallIconPxAtStandardScales) {
    EXPECT_EQ(WindowIconPx(kSmallIconDip, 96), 16);
    EXPECT_EQ(WindowIconPx(kSmallIconDip, 120), 20);
    EXPECT_EQ(WindowIconPx(kSmallIconDip, 144), 24);
    EXPECT_EQ(WindowIconPx(kSmallIconDip, 192), 32);
}

TEST(WindowIcon, BigIconPxAtStandardScales) {
    EXPECT_EQ(WindowIconPx(kBigIconDip, 96), 32);
    EXPECT_EQ(WindowIconPx(kBigIconDip, 120), 40);
    EXPECT_EQ(WindowIconPx(kBigIconDip, 144), 48);
    EXPECT_EQ(WindowIconPx(kBigIconDip, 192), 64);
}

// 175% (168 DPI): 16 * 1.75 = 28 exactly; 110% (106 DPI) lands on 17.67 -> 18.
// Rounding to nearest keeps the drawn size within half a pixel of 16 DIP.
TEST(WindowIcon, RoundsToNearestOnOddDpi) {
    EXPECT_EQ(WindowIconPx(kSmallIconDip, 168), 28);
    EXPECT_EQ(WindowIconPx(kSmallIconDip, 106), 18);
}

// DPI 0 means "not known yet" everywhere in WindowState.h; treat it as 96.
TEST(WindowIcon, ZeroDpiFallsBackTo96) {
    EXPECT_EQ(WindowIconPx(kSmallIconDip, 0), 16);
    const POINT o = TitleIconOriginPx(10.0f, 32.0f, 16, 0);
    EXPECT_EQ(o.x, 10);
    EXPECT_EQ(o.y, 8);
}

// At 100%: 10 DIP left inset, 16px icon centered in a 32px band.
TEST(WindowIcon, OriginAt96Dpi) {
    const POINT o = TitleIconOriginPx(10.0f, 32.0f, 16, 96);
    EXPECT_EQ(o.x, 10);
    EXPECT_EQ(o.y, 8);
}

// At 150% the DIP math gives x = 15, band 48px, icon 24px -> y = 12. All whole.
TEST(WindowIcon, OriginAt144Dpi) {
    const POINT o = TitleIconOriginPx(10.0f, 32.0f, 24, 144);
    EXPECT_EQ(o.x, 15);
    EXPECT_EQ(o.y, 12);
}

// At 125% the DIP math is fractional: x = 12.5 and (40 - 20) / 2 = 10 for y.
// The origin must still come out integral — this is the case that blurred.
TEST(WindowIcon, OriginIsIntegralAt120Dpi) {
    const POINT o = TitleIconOriginPx(10.0f, 32.0f, 20, 120);
    EXPECT_EQ(o.x, 13);  // 12.5 rounds half up
    EXPECT_EQ(o.y, 10);
}

// An odd leftover splits with the extra pixel below (integer division), never
// a half pixel. 48px band at 150%, icon 23px -> (48 - 23) / 2 = 12.
TEST(WindowIcon, OddRemainderFloors) {
    const POINT o = TitleIconOriginPx(10.0f, 32.0f, 23, 144);
    EXPECT_EQ(o.y, 12);
}

// An icon taller than the band is pinned to the top rather than going negative.
TEST(WindowIcon, IconTallerThanBandClampsToTop) {
    const POINT o = TitleIconOriginPx(10.0f, 12.0f, 16, 96);
    EXPECT_EQ(o.y, 0);
}
