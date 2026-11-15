// ScrollViewerSettleTests.cpp — tests that the smooth-scroll tail reaches the
// target without leaving the displayed pixel stuck for multiple consecutive frames.
//
// Background: ScrollViewer::Tick uses an exponential ease-out. Near the end the
// step becomes sub-pixel, and at non-100% DPI the rounded pixel position can hold
// for 4-7 frames before the final 1px jump — a visible "stutter" at 150%/175% DPI
// (reported as TreeView wheel-scroll jitter).
//
// The fix enforces a minimum step of 120 DIP/s so the final few DIP are covered in
// ~45ms at constant speed, eliminating multi-frame pixel holds. These tests pin
// that contract headlessly without a window or GPU.

#include "../framework/Test.h"
#include "../../FluentUI/layout/ScrollViewer.h"

using namespace fluent;

namespace {

// Drive sv through a full AnimateTo animation at `hz` frames per second.
// Returns the worst run of consecutive frames where the *rounded physical pixel*
// position did not change while the offset was still animating.
int MaxPixelStallFrames(ScrollViewer& sv, float targetOffset, float hz, float dpi) {
    sv.AnimateTo(targetOffset);
    int worst = 0, run = 0;
    int prevPx = static_cast<int>(std::roundf(sv.Offset() * dpi));
    const float dt = 1.0f / hz;
    for (int i = 0; i < 2000 && sv.NeedsTick(); ++i) {
        sv.Tick(dt);
        const int px = static_cast<int>(std::roundf(sv.Offset() * dpi));
        if (px == prevPx && sv.Offset() != targetOffset) {
            ++run;
            worst = std::max(worst, run);
        } else {
            run = 0;
        }
        prevPx = px;
    }
    return worst;
}

// A ScrollViewer with 1000 DIP of content in a 300 DIP viewport, offset at 200.
void SetUpSV(ScrollViewer& sv) {
    sv.Arrange({0, 0, 400.0f, 300.0f});
    sv.SetContentHeight(1000.0f);
    sv.SetOffset(200.0f);
}

} // namespace

// At 150% DPI / 120Hz the old threshold caused up to 7 stalled frames before the
// final 1px correction. With the minimum-step tail it must be at most 1.
TEST(ScrollViewerSettle, NoMultiFramePixelHold_150dpi_120Hz) {
    ScrollViewer sv;
    SetUpSV(sv);
    int worst = MaxPixelStallFrames(sv, 228.0f, 120.0f, 1.5f);
    EXPECT_TRUE(worst <= 1);
}

TEST(ScrollViewerSettle, NoMultiFramePixelHold_175dpi_120Hz) {
    ScrollViewer sv;
    SetUpSV(sv);
    int worst = MaxPixelStallFrames(sv, 228.0f, 120.0f, 1.75f);
    EXPECT_TRUE(worst <= 1);
}

TEST(ScrollViewerSettle, NoMultiFramePixelHold_150dpi_60Hz) {
    ScrollViewer sv;
    SetUpSV(sv);
    int worst = MaxPixelStallFrames(sv, 228.0f, 60.0f, 1.5f);
    EXPECT_TRUE(worst <= 1);
}

TEST(ScrollViewerSettle, NoMultiFramePixelHold_100dpi_60Hz) {
    // 100% DPI should also be clean (was already OK, now pinned).
    ScrollViewer sv;
    SetUpSV(sv);
    int worst = MaxPixelStallFrames(sv, 228.0f, 60.0f, 1.0f);
    EXPECT_TRUE(worst <= 1);
}

TEST(ScrollViewerSettle, EventFiredExactlyOncePerSettle) {
    // Guard that the minimum-step change doesn't break the event-once contract
    // already covered in ScrollViewerEventsTests.
    struct Rec { int n = 0; static void On(void* owner, ScrollViewer&, ScrollChangedArgs&)
        { ++static_cast<Rec*>(owner)->n; } };
    ScrollViewer sv;
    SetUpSV(sv);
    Rec rec;
    auto sub = sv.ScrollChanged().Subscribe(&rec, &Rec::On);
    sv.AnimateTo(260.0f);
    for (int i = 0; i < 500 && sv.NeedsTick(); ++i) sv.Tick(1.0f / 120.0f);
    EXPECT_EQ(rec.n, 1);
    EXPECT_NEAR(sv.Offset(), 260.0f, 0.001f);
}
