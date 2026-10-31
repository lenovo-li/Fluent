// ScrollPanelTests.cpp
#include "../framework/Test.h"
#include "../../FluentUI/layout/ScrollPanel.h"
#include "../../FluentUI/controls/Button.h"
#include "../../FluentUI/controls/TextBlock.h"
#include <cmath>

using namespace fluent;

TEST(ScrollPanel, EmptyPanel) {
    ScrollPanel panel;
    panel.Measure(400.0f, 300.0f);
    EXPECT_EQ(panel.Desired().h, 0.0f);
    EXPECT_EQ(panel.MaxVerticalOffset(), 0.0f);
}

TEST(ScrollPanel, SingleChild) {
    ScrollPanel panel;
    auto* btn = panel.Emplace<Button>();
    btn->SetText(L"Test");
    btn->SetHeight(40.0f);

    panel.Measure(400.0f, 300.0f);
    panel.Arrange({0, 0, 400.0f, 300.0f});  // Required for viewport bounds
    EXPECT_EQ(panel.Desired().h, 40.0f);
    EXPECT_EQ(panel.MaxVerticalOffset(), 0.0f);  // content fits viewport
}

TEST(ScrollPanel, MultipleChildren_NoSpacing) {
    ScrollPanel panel;
    for (int i = 0; i < 5; ++i) {
        auto* btn = panel.Emplace<Button>();
        btn->SetText(L"Button " + std::to_wstring(i));
        btn->SetHeight(40.0f);
    }

    panel.Measure(400.0f, 300.0f);
    panel.Arrange({0, 0, 400.0f, 300.0f});  // Required for viewport bounds
    EXPECT_EQ(panel.Desired().h, 200.0f);  // 5 * 40
    EXPECT_EQ(panel.MaxVerticalOffset(), 0.0f);  // fits in 300
}

TEST(ScrollPanel, MultipleChildren_WithSpacing) {
    ScrollPanel panel;
    panel.SetSpacing(8.0f);
    for (int i = 0; i < 5; ++i) {
        auto* btn = panel.Emplace<Button>();
        btn->SetHeight(40.0f);
    }

    panel.Measure(400.0f, 300.0f);
    // 5 children * 40 + 4 gaps * 8 = 200 + 32 = 232
    EXPECT_EQ(panel.Desired().h, 232.0f);
}

TEST(ScrollPanel, ContentTallerThanViewport) {
    ScrollPanel panel;
    for (int i = 0; i < 10; ++i) {
        auto* btn = panel.Emplace<Button>();
        btn->SetHeight(40.0f);
    }

    panel.Measure(400.0f, 300.0f);
    panel.Arrange(RectDip{0, 0, 400, 300});

    // Content height = 10 * 40 = 400, viewport = 300, max offset = 100
    EXPECT_EQ(panel.MaxVerticalOffset(), 100.0f);
}

TEST(ScrollPanel, ScrollOffset_Clamping) {
    ScrollPanel panel;
    for (int i = 0; i < 10; ++i) {
        auto* btn = panel.Emplace<Button>();
        btn->SetHeight(40.0f);
    }

    panel.Measure(400.0f, 300.0f);
    panel.Arrange(RectDip{0, 0, 400, 300});

    // Try to scroll past the end.
    panel.SetVerticalOffset(200.0f);
    EXPECT_EQ(panel.VerticalOffset(), 100.0f);  // clamped to max

    // Try to scroll before the start.
    panel.SetVerticalOffset(-50.0f);
    EXPECT_EQ(panel.VerticalOffset(), 0.0f);
}

TEST(ScrollPanel, KeyboardNavigation_UpDown) {
    ScrollPanel panel;
    for (int i = 0; i < 20; ++i) {
        auto* btn = panel.Emplace<Button>();
        btn->SetHeight(40.0f);
    }

    panel.Measure(400.0f, 300.0f);
    panel.Arrange(RectDip{0, 0, 400, 300});

    // Down key starts a smooth scroll: it retargets rather than moving the offset
    // outright, so the animation has to be ticked before the offset changes.
    KeyEventArgs down;
    down.vk = VK_DOWN;
    panel.OnKeyDownRouted(down);
    EXPECT_TRUE(down.handled);
    EXPECT_TRUE(panel.WantsAnimationTick());
    for (int i = 0; i < 60 && panel.WantsAnimationTick(); ++i)
        panel.OnAnimationTick(0.016f);
    EXPECT_TRUE(panel.VerticalOffset() > 0.0f);

    // Home key scrolls to top.
    KeyEventArgs home;
    home.vk = VK_HOME;
    panel.OnKeyDownRouted(home);
    EXPECT_TRUE(home.handled);
    EXPECT_EQ(panel.VerticalOffset(), 0.0f);

    // End key scrolls to bottom.
    KeyEventArgs end;
    end.vk = VK_END;
    panel.OnKeyDownRouted(end);
    EXPECT_TRUE(end.handled);
    EXPECT_EQ(panel.VerticalOffset(), panel.MaxVerticalOffset());
}

TEST(ScrollPanel, KeyboardNavigation_PageUpDown) {
    ScrollPanel panel;
    for (int i = 0; i < 30; ++i) {
        auto* btn = panel.Emplace<Button>();
        btn->SetHeight(40.0f);
    }

    panel.Measure(400.0f, 300.0f);
    panel.Arrange(RectDip{0, 0, 400, 300});

    // PageDown scrolls roughly one viewport height.
    KeyEventArgs pgdn;
    pgdn.vk = VK_NEXT;
    panel.OnKeyDownRouted(pgdn);
    EXPECT_TRUE(pgdn.handled);
    for (int i = 0; i < 60 && panel.WantsAnimationTick(); ++i)
        panel.OnAnimationTick(0.016f);
    float afterPageDown = panel.VerticalOffset();
    EXPECT_TRUE(afterPageDown > 200.0f && afterPageDown < 300.0f);

    // PageUp scrolls back.
    KeyEventArgs pgup;
    pgup.vk = VK_PRIOR;
    panel.OnKeyDownRouted(pgup);
    EXPECT_TRUE(pgup.handled);
    for (int i = 0; i < 60 && panel.WantsAnimationTick(); ++i)
        panel.OnAnimationTick(0.016f);
    EXPECT_TRUE(panel.VerticalOffset() < 50.0f);
}

TEST(ScrollPanel, WheelScroll) {
    ScrollPanel panel;
    for (int i = 0; i < 20; ++i) {
        auto* btn = panel.Emplace<Button>();
        btn->SetHeight(40.0f);
    }

    panel.Measure(400.0f, 300.0f);
    panel.Arrange(RectDip{0, 0, 400, 300});

    // Wheel down (negative delta) scrolls content down.
    PointerEventArgs wheelDown;
    wheelDown.wheelDelta = -120;
    panel.OnPointerWheelChanged(wheelDown);
    EXPECT_TRUE(wheelDown.handled);
    // Target offset should be positive (scroll_.AnimateBy sets target, Tick moves toward it).
    // We can't verify the immediate offset without ticking, but we can check it didn't crash.
}

TEST(ScrollPanel, ChildArrangement) {
    ScrollPanel panel;
    panel.SetSpacing(10.0f);

    auto* btn1 = panel.Emplace<Button>();
    btn1->SetHeight(50.0f);
    auto* btn2 = panel.Emplace<Button>();
    btn2->SetHeight(60.0f);
    auto* btn3 = panel.Emplace<Button>();
    btn3->SetHeight(70.0f);

    panel.Measure(400.0f, 500.0f);
    panel.Arrange(RectDip{100, 200, 400, 500});

    // Children should be arranged vertically with spacing:
    // btn1: y = 200, h = 50
    // btn2: y = 260 (200 + 50 + 10), h = 60
    // btn3: y = 330 (260 + 60 + 10), h = 70
    EXPECT_EQ(btn1->Bounds().y, 200.0f);
    EXPECT_EQ(btn1->Bounds().h, 50.0f);
    EXPECT_EQ(btn2->Bounds().y, 260.0f);
    EXPECT_EQ(btn2->Bounds().h, 60.0f);
    EXPECT_EQ(btn3->Bounds().y, 330.0f);
    EXPECT_EQ(btn3->Bounds().h, 70.0f);
}

TEST(ScrollPanel, NeedsAnimationTick_WhenScrolling) {
    ScrollPanel panel;
    for (int i = 0; i < 20; ++i) {
        panel.Emplace<Button>()->SetHeight(40.0f);
    }

    panel.Measure(400.0f, 300.0f);
    panel.Arrange(RectDip{0, 0, 400, 300});

    // Initially no tick needed.
    EXPECT_FALSE(panel.WantsAnimationTick());

    // After starting a scroll, tick is needed.
    PointerEventArgs wheel;
    wheel.wheelDelta = -120;
    panel.OnPointerWheelChanged(wheel);
    EXPECT_TRUE(panel.WantsAnimationTick());
}

TEST(ScrollPanel, MeasureWithUnboundedHeight) {
    ScrollPanel panel;
    for (int i = 0; i < 5; ++i) {
        panel.Emplace<Button>()->SetHeight(40.0f);
    }

    // Measure with unbounded height (parent doesn't constrain).
    panel.Measure(400.0f, std::numeric_limits<float>::infinity());

    // Should report full content height.
    EXPECT_EQ(panel.Desired().h, 200.0f);
}

TEST(ScrollPanel, ResizeShrinks_OffsetClamped) {
    ScrollPanel panel;
    for (int i = 0; i < 20; ++i) {
        panel.Emplace<Button>()->SetHeight(40.0f);
    }

    // Initial: 800 content in 300 viewport, max offset = 500.
    panel.Measure(400.0f, 300.0f);
    panel.Arrange(RectDip{0, 0, 400, 300});
    panel.SetVerticalOffset(500.0f);
    EXPECT_EQ(panel.VerticalOffset(), 500.0f);

    // Resize viewport to 700 (content still 800), max offset now = 100.
    panel.Arrange(RectDip{0, 0, 400, 700});
    EXPECT_EQ(panel.VerticalOffset(), 100.0f);  // clamped
}

TEST(ScrollPanel, SpacingChange_InvalidatesMeasure) {
    ScrollPanel panel;
    panel.Emplace<Button>()->SetHeight(40.0f);
    panel.Emplace<Button>()->SetHeight(40.0f);

    panel.Measure(400.0f, 300.0f);
    EXPECT_EQ(panel.Desired().h, 80.0f);

    panel.SetSpacing(10.0f);
    panel.Measure(400.0f, 300.0f);
    EXPECT_EQ(panel.Desired().h, 90.0f);  // 40 + 10 + 40
}

TEST(ScrollPanel, EmptyPanel_KeyboardDoesNothing) {
    ScrollPanel panel;
    panel.Measure(400.0f, 300.0f);
    panel.Arrange(RectDip{0, 0, 400, 300});

    KeyEventArgs down;
    down.vk = VK_DOWN;
    panel.OnKeyDownRouted(down);
    EXPECT_FALSE(down.handled);  // nothing to scroll
}

// Regression: the scroll offset must live in the children's BOUNDS, not in a
// render-time transform. A D2D transform moves only what D2D draws; a
// compositor-backed child (TextArea, TreeView) is positioned from its bounds by a
// DComp visual and would stay nailed in place while everything else scrolled —
// which is exactly what the demo showed. Asserting on bounds also covers
// hit-testing and dirty-rect reporting, since both read the same bounds.
TEST(ScrollPanel, ScrollOffsetMovesChildBounds) {
    ScrollPanel panel;
    Button* first = nullptr;
    Button* second = nullptr;
    for (int i = 0; i < 10; ++i) {
        auto* btn = panel.Emplace<Button>();
        btn->SetHeight(40.0f);
        if (i == 0) first = btn;
        if (i == 1) second = btn;
    }

    panel.Measure(400.0f, 300.0f);
    panel.Arrange(RectDip{0, 100, 400, 300});  // panel at y=100

    // Unscrolled: children stack from the panel's top edge.
    EXPECT_EQ(first->Bounds().y, 100.0f);
    EXPECT_EQ(second->Bounds().y, 140.0f);

    // Scrolling down by 100 must shift every child UP by 100 in its own bounds.
    panel.SetVerticalOffset(100.0f);
    panel.Arrange(RectDip{0, 100, 400, 300});
    EXPECT_EQ(panel.VerticalOffset(), 100.0f);
    EXPECT_EQ(first->Bounds().y, 0.0f);
    EXPECT_EQ(second->Bounds().y, 40.0f);

    // And the pointer position that now sits over `second` must hit it, with no
    // separate compensation step: bounds are already screen-space.
    UIElement* hit = panel.HitTestDeep(200.0f, 150.0f);
    EXPECT_NE(hit, nullptr);
}

// Moving the offset must request an Arrange. Without it the children keep the
// bounds they were arranged at, so a repaint-only invalidation redraws everything
// exactly where it already was and the view appears frozen.
TEST(ScrollPanel, OffsetChangeRequestsArrange) {
    ScrollPanel panel;
    for (int i = 0; i < 10; ++i)
        panel.Emplace<Button>()->SetHeight(40.0f);

    panel.Measure(400.0f, 300.0f);
    panel.Arrange(RectDip{0, 0, 400, 300});
    panel.ClearDirtySubtree();
    EXPECT_FALSE(panel.AnyDirtyInSubtree(DirtyFlags::Arrange));

    panel.SetVerticalOffset(80.0f);
    EXPECT_TRUE(panel.AnyDirtyInSubtree(DirtyFlags::Arrange));
}

// Regression: AnimateBy sets TARGET, not offset. SyncScrollArrange compared the
// live offset against arrangedOffset_, which hadn't moved yet on the frame the
// wheel notch arrived, so it returned early and never invalidated. The scrollbar
// still painted its new position (it reads offset each frame), producing "only the
// rail scrolls, the content is frozen" until a resize forced OnLayout. The fix:
// compare TargetOffset() instead, which equals target_ when animating.
TEST(ScrollPanel, WheelScrollInvalidatesArrangeImmediately) {
    ScrollPanel panel;
    for (int i = 0; i < 20; ++i)
        panel.Emplace<Button>()->SetHeight(40.0f);

    panel.Measure(400.0f, 300.0f);
    panel.Arrange(RectDip{0, 0, 400, 300});
    panel.ClearDirtySubtree();
    EXPECT_FALSE(panel.AnyDirtyInSubtree(DirtyFlags::Arrange));

    // Wheel event with negative delta (scroll down)
    PointerEventArgs wheelEvent;
    wheelEvent.wheelDelta = -120;
    wheelEvent.position = {200.0f, 150.0f};
    panel.OnPointerWheelChanged(wheelEvent);

    // Must mark Arrange dirty immediately, even though the tween target has moved
    // but the live offset has not yet advanced (that happens on the first tick).
    EXPECT_TRUE(panel.AnyDirtyInSubtree(DirtyFlags::Arrange));
    EXPECT_TRUE(wheelEvent.handled);
}

// Regression: scrollbar hover strip (16 DIP) must start drag, not only the
// visible thumb (3 DIP idle / 7 DIP hovered). Before fix, the rail would light
// up but clicks would miss unless the pointer landed inside the narrow thumb.
TEST(ScrollPanel, Scrollbar_DragStartsInHoverStrip) {
    ScrollPanel panel;
    for (int i = 0; i < 20; ++i) {
        auto* btn = panel.Emplace<Button>();
        btn->SetHeight(40.0f);
    }
    panel.Measure(400.0f, 300.0f);
    panel.Arrange(RectDip{0, 0, 400, 300});
    // Content overflows, scrollbar is present.
    EXPECT_TRUE(panel.MaxVerticalOffset() > 0.0f);

    // Simulate pointer pressed in the hover strip (x=390 is in the rightmost
    // 16 DIP). This must start a drag even though the visible thumb is only
    // 2-7 DIP wide.
    PointerEventArgs e;
    e.button = PointerButton::Left;
    e.position = {390.0f, 150.0f};
    panel.OnPointerPressed(e);
    EXPECT_TRUE(e.handled);  // drag started
}

// Pixel snapping during smooth scroll: fractional scroll offsets are snapped to
// the physical pixel grid before being baked into child bounds, so text rendered
// with ClearType (subpixel hinting) stays stable instead of shimmering. At DPI
// scale 1.0 (the test default), snapping rounds to integer DIPs. A scroll offset
// of 15.7 yields child bounds offset by -16.0, not -15.7.
TEST(ScrollPanel, FractionalScrollOffset_ChildBoundsSnappedToPixels) {
    ScrollPanel panel;
    // Content (4 * 40 = 160) must exceed the viewport (80) or the offset clamps to 0
    // and there is nothing to snap.
    auto* first = panel.Emplace<Button>();
    first->SetHeight(40.0f);
    auto* second = panel.Emplace<Button>();
    second->SetHeight(40.0f);
    for (int i = 0; i < 2; ++i) {
        auto* filler = panel.Emplace<Button>();
        filler->SetHeight(40.0f);
    }

    panel.Measure(400.0f, 80.0f);
    panel.Arrange(RectDip{0, 100, 400, 80});  // y=100 start

    // Scroll to 15.7 (a fractional offset from smooth-scroll interpolation).
    // The underlying ScrollViewer allows it, but Arrange must snap before using it.
    panel.SetVerticalOffset(15.7f);
    panel.Arrange(RectDip{0, 100, 400, 80});

    // The offset is snapped to 16.0, so bounds.y = 100 - 16 = 84.
    // If it were NOT snapped, first->Bounds().y would be 84.3, which D2D rounds
    // differently from frame to frame as the interpolation drifts through subpixel
    // positions, causing text jitter.
    EXPECT_EQ(first->Bounds().y, 84.0f);
    EXPECT_EQ(second->Bounds().y, 124.0f);  // 84 + 40
}

// Wheel distance = system lines-per-notch * wheelStep_. The step is settable because
// ScrollPanel's content is heterogeneous: there is no intrinsic row height to derive
// it from the way ListBox uses itemHeight_.
TEST(ScrollPanel, WheelStep_ScalesScrollDistance) {
    ScrollPanel panel;
    for (int i = 0; i < 20; ++i) {
        auto* btn = panel.Emplace<Button>();
        btn->SetHeight(40.0f);
    }
    panel.Measure(400.0f, 300.0f);
    panel.Arrange(RectDip{0, 0, 400, 300});

    EXPECT_EQ(panel.WheelStep(), 32.0f);  // default matches ListBox item height

    // One notch down. wheelScrollLines defaults to 3 in a hand-built args, so the
    // target is 3 * 32 = 96. AnimateBy sets the tween TARGET; with no animation tick
    // the live offset stays put, so assert on the target the wheel asked for.
    PointerEventArgs e;
    e.wheelDelta = -120;  // one notch toward the content end
    panel.OnPointerWheelChanged(e);
    EXPECT_TRUE(e.handled);
    EXPECT_EQ(panel.TargetVerticalOffset(), 96.0f);

    // A custom step scales it proportionally: 3 * 48 = 144.
    ScrollPanel wide;
    for (int i = 0; i < 20; ++i) {
        auto* btn = wide.Emplace<Button>();
        btn->SetHeight(40.0f);
    }
    wide.SetWheelStep(48.0f);
    wide.Measure(400.0f, 300.0f);
    wide.Arrange(RectDip{0, 0, 400, 300});

    PointerEventArgs e2;
    e2.wheelDelta = -120;
    wide.OnPointerWheelChanged(e2);
    EXPECT_EQ(wide.TargetVerticalOffset(), 144.0f);
}

// Regression: SetWheelStep(0) caused division by zero when WheelScrollLines=0.
// Now enforces minimum of 1.0 DIP.
TEST(ScrollPanel, SetWheelStepZero_IsClamped) {
    ScrollPanel panel;
    for (int i = 0; i < 10; ++i) {
        auto* btn = panel.Emplace<Button>();
        btn->SetHeight(40.0f);
    }
    
    panel.Measure(400.0f, 300.0f);
    panel.Arrange(RectDip{0, 0, 400, 300});
    
    panel.SetWheelStep(0.0f);  // Should be clamped to 1.0
    EXPECT_EQ(panel.WheelStep(), 1.0f);
    
    panel.SetWheelStep(-5.0f);  // Negative also clamped
    EXPECT_EQ(panel.WheelStep(), 1.0f);
    
    panel.SetWheelStep(16.0f);  // Valid value accepted
    EXPECT_EQ(panel.WheelStep(), 16.0f);
}

// --- Measure / Arrange width agreement under the vertical rail ------------------
//
// A stand-in for wrapped text: 4000 DIP^2 of "text" laid out at whatever width it is
// given, 20 DIP per line, so a narrower width means more lines. Records the widths it
// was measured at.
namespace {
class WrapProbe : public FrameworkElement {
public:
    std::vector<float> measuredW;
    void Measure(float availW, float availH) override {
        UNREFERENCED_PARAMETER(availH);
        measuredW.push_back(availW);
        const float lines = std::ceil(4000.0f / std::max(1.0f, availW));
        SetDesired({availW, lines * 20.0f});
    }
    void Render(const DrawingContext&) override {}
    float HeightAt(float w) const { return std::ceil(4000.0f / w) * 20.0f; }
    void Dirty() { InvalidateMeasure(); }
};
}  // namespace

// Regression (LenovoDock firmware card): content overflows, so Arrange reserves the
// 16 DIP rail. The children used to be measured at the full width and arranged at
// the narrower one; wrapped text gained a line its height did not include, and that
// line was clipped.
TEST(ScrollPanel, OverflowMeasuresChildrenAtRailInsetWidth) {
    ScrollPanel panel;
    std::vector<WrapProbe*> probes;
    for (int i = 0; i < 5; ++i) probes.push_back(panel.Emplace<WrapProbe>());

    panel.Measure(400.0f, 300.0f);  // 5 x 200 = 1000 > 300: the rail will show
    panel.Arrange(RectDip{0, 0, 400, 300});

    for (WrapProbe* p : probes) {
        EXPECT_NEAR(p->measuredW.back(), 384.0f, 0.01f);
        EXPECT_NEAR(p->Bounds().w, 384.0f, 0.01f);           // arranged at the same width
        EXPECT_NEAR(p->Bounds().h, p->HeightAt(384.0f), 0.01f);
    }
}

TEST(ScrollPanel, NoOverflowMeasuresChildrenAtFullWidth) {
    ScrollPanel panel;
    auto* p = panel.Emplace<WrapProbe>();  // 4000 / 400 = 10 lines = 200 DIP

    panel.Measure(400.0f, 300.0f);
    panel.Arrange(RectDip{0, 0, 400, 300});

    EXPECT_EQ(panel.MaxVerticalOffset(), 0.0f);
    EXPECT_NEAR(p->measuredW.back(), 400.0f, 0.01f);
    EXPECT_NEAR(p->Bounds().w, 400.0f, 0.01f);
    EXPECT_NEAR(p->Bounds().h, 200.0f, 0.01f);
}

// The second identical Measure starts from the remembered rail decision: one pass.
TEST(ScrollPanel, SteadyOverflowMeasuresOncePerPass) {
    ScrollPanel panel;
    auto* p = panel.Emplace<WrapProbe>();
    for (int i = 0; i < 4; ++i) panel.Emplace<WrapProbe>();

    panel.Measure(400.0f, 300.0f);
    panel.Arrange(RectDip{0, 0, 400, 300});
    p->Dirty();  // defeat the measure cache so every pass is counted
    const size_t before = p->measuredW.size();
    panel.Measure(400.0f, 300.0f);

    EXPECT_EQ(p->measuredW.size(), before + 1);
    EXPECT_NEAR(p->measuredW.back(), 384.0f, 0.01f);
}

// Arranged at another width than measured: the children are re-measured at the
// arranged width rather than given a height computed for a different one.
TEST(ScrollPanel, ArrangeAtOtherWidthRemeasuresChildren) {
    ScrollPanel panel;
    auto* p = panel.Emplace<WrapProbe>();

    panel.Measure(400.0f, 300.0f);           // 200 DIP: fits, no rail
    panel.Arrange(RectDip{0, 0, 250, 300});  // at 250: 16 lines = 320 > 300 → rail

    EXPECT_TRUE(panel.MaxVerticalOffset() > 0.0f);
    EXPECT_NEAR(p->Bounds().w, 234.0f, 0.01f);  // 250 minus the rail strip
    EXPECT_NEAR(p->measuredW.back(), 234.0f, 0.01f);
    EXPECT_NEAR(p->Bounds().h, p->HeightAt(234.0f), 0.01f);
}

// A pointer move over a panel whose content FITS must not start the scrollbar
// wake cycle. There is no rail to draw (ScrollViewer::Render returns early), but an
// unconditional Wake used to run ~1.5 s of fade-in / idle countdown / fade-out
// ticks, each invalidating the whole panel, for nothing on screen.
TEST(ScrollPanel, PointerMoveWithoutOverflowDoesNotWake) {
    ScrollPanel panel;
    panel.Emplace<Button>()->SetHeight(40.0f);
    panel.Measure(400.0f, 300.0f);
    panel.Arrange(RectDip{0, 0, 400, 300});
    EXPECT_EQ(panel.MaxVerticalOffset(), 0.0f);

    PointerEventArgs move;
    move.position = Point{200.0f, 150.0f};
    panel.OnPointerMoved(move);
    EXPECT_FALSE(panel.WantsAnimationTick());
}

// With overflow the move does wake the bar, but a tick that only eases the fade
// must report the RAIL strip as dirty, not the whole panel: reporting VisualBounds
// repainted every visible child on every frame of the fade.
TEST(ScrollPanel, FadeTickDirtiesOnlyTheRail) {
    ScrollPanel panel;
    for (int i = 0; i < 20; ++i) panel.Emplace<Button>()->SetHeight(40.0f);
    panel.Measure(400.0f, 300.0f);
    panel.Arrange(RectDip{0, 0, 400, 300});
    EXPECT_TRUE(panel.MaxVerticalOffset() > 0.0f);

    PointerEventArgs move;
    move.position = Point{200.0f, 150.0f};   // mid-panel, nowhere near the rail
    panel.OnPointerMoved(move);
    EXPECT_TRUE(panel.WantsAnimationTick()); // the bar fades in

    panel.ClearDirtySubtree();
    panel.OnAnimationTick(1.0f / 60.0f);     // fade step, offset unchanged
    EXPECT_FALSE(Any(panel.Dirty()));        // the panel itself is not dirty

    std::vector<RectDip> dirty;
    panel.CollectDirtyBounds(dirty);
    EXPECT_EQ(dirty.size(), static_cast<size_t>(1));
    if (!dirty.empty()) {
        EXPECT_TRUE(dirty[0].w <= 16.0f);              // just the vertical strip
        EXPECT_EQ(dirty[0].right(), 400.0f);
        EXPECT_EQ(dirty[0].h, 300.0f);
    }

    // Once painted, the rail request is consumed.
    panel.ClearDirtySubtree();
    dirty.clear();
    panel.CollectDirtyBounds(dirty);
    EXPECT_TRUE(dirty.empty());
}

// The bar is awake (a pointer move over overflowing content) and then the content
// stops overflowing: the viewport grew. The remaining fade/countdown used to keep
// ticking for ~1.5 s with nothing to draw, and every tick requested a frame with no
// dirty rect (InvalidateRails has no strip to report without overflow), which the
// host turns into a full-window redraw. The first tick after the change must end the
// cycle and repaint just the panel once.
TEST(ScrollPanel, BarAwakeWhenContentStopsOverflowingStopsTicking) {
    ScrollPanel panel;
    for (int i = 0; i < 20; ++i) panel.Emplace<Button>()->SetHeight(40.0f);
    panel.Measure(400.0f, 300.0f);
    panel.Arrange(RectDip{0, 0, 400, 300});
    EXPECT_TRUE(panel.MaxVerticalOffset() > 0.0f);

    PointerEventArgs move;
    move.position = Point{200.0f, 150.0f};
    panel.OnPointerMoved(move);
    panel.OnAnimationTick(1.0f / 60.0f);
    EXPECT_TRUE(panel.WantsAnimationTick());           // premise: the bar is awake

    panel.Measure(400.0f, 2000.0f);                     // now everything fits
    panel.Arrange(RectDip{0, 0, 400, 2000});
    EXPECT_EQ(panel.MaxVerticalOffset(), 0.0f);

    panel.ClearDirtySubtree();
    panel.OnAnimationTick(1.0f / 60.0f);
    EXPECT_FALSE(panel.WantsAnimationTick());          // the cycle ended at once
    EXPECT_TRUE(Has(panel.Dirty(), DirtyFlags::Render));  // one panel repaint, not an empty set
}

// A rail hover set while the content overflowed must be cleared by the next move once
// the content fits. The early return for "no bar" used to skip the hover update too,
// leaving the hover (which holds the idle countdown) set until the pointer left.
TEST(ScrollPanel, PointerMoveAfterOverflowEndsClearsRailHover) {
    ScrollPanel panel;
    for (int i = 0; i < 20; ++i) panel.Emplace<Button>()->SetHeight(40.0f);
    panel.Measure(400.0f, 300.0f);
    panel.Arrange(RectDip{0, 0, 400, 300});

    PointerEventArgs move;
    move.position = Point{395.0f, 150.0f};              // on the vertical rail strip
    panel.OnPointerMoved(move);
    EXPECT_TRUE(panel.ScrollBarHovered());              // premise

    panel.Measure(400.0f, 2000.0f);
    panel.Arrange(RectDip{0, 0, 400, 2000});
    EXPECT_EQ(panel.MaxVerticalOffset(), 0.0f);

    move.position = Point{396.0f, 150.0f};              // still inside the panel
    panel.OnPointerMoved(move);
    EXPECT_FALSE(panel.ScrollBarHovered());
}

// A tick that MOVES the offset still dirties the whole panel: every child moved.
TEST(ScrollPanel, ScrollTickDirtiesWholePanel) {
    ScrollPanel panel;
    for (int i = 0; i < 20; ++i) panel.Emplace<Button>()->SetHeight(40.0f);
    panel.Measure(400.0f, 300.0f);
    panel.Arrange(RectDip{0, 0, 400, 300});

    PointerEventArgs wheel;
    wheel.wheelDelta = -120;
    panel.OnPointerWheelChanged(wheel);
    panel.ClearDirtySubtree();
    panel.OnAnimationTick(1.0f / 60.0f);
    EXPECT_TRUE(Has(panel.Dirty(), DirtyFlags::Render));
}
