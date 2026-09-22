// SliderEventsTests.cpp — Slider.ValueChanging vs ValueChanged (event_completion_plan §8).
//
// The split exists so a drag can preview cheaply and commit once: ValueChanging
// fires for every intermediate thumb position, ValueChanged fires only when the
// pointer is released. Keyboard steps and programmatic SetValue are already
// committed values, so they skip straight to ValueChanged with no preview.
//
// The pointer handlers are protected, so input is driven through InputManager the
// way the real window does it — that is also what gives the slider the
// Context().input it needs for CapturePointer, without which a drag would not
// survive the pointer leaving the thumb.

#include "../framework/Test.h"
#include "../../FluentUI/controls/Slider.h"
#include "../../FluentUI/core/UIContext.h"
#include "../../FluentUI/input/InputManager.h"

#include <vector>

using namespace fluent;

namespace {

struct Rec {
    std::vector<float> changed;
    std::vector<float> changing;

    static void OnChanged(void* owner, Slider&, float& v) {
        static_cast<Rec*>(owner)->changed.push_back(v);
    }
    static void OnChanging(void* owner, Slider&, float& v) {
        static_cast<Rec*>(owner)->changing.push_back(v);
    }
};

// A focusable 200x40 slider over 0..100 with step 1, wired to a live InputManager
// so presses route through the real hit-test path and capture works.
struct Fixture {
    InputManager im;
    Slider slider;
    std::vector<UIElement*> roots;
    Rec rec;
    Subscription changedSub, changingSub;

    Fixture() {
        slider.SetMin(0.0f);
        slider.SetMax(100.0f);
        slider.SetStep(1.0f);

        UIContext ctx;
        ctx.input = &im;
        slider.AttachToContext(ctx);

        // Measure and Arrange manually (Slider is not in a panel here)
        slider.Measure(200.0f, 40.0f);
        slider.Arrange({0, 0, 200.0f, 40.0f});

        roots.push_back(&slider);
        im.SetRoots(&roots);

        changedSub = slider.ValueChanged().Subscribe(&rec, &Rec::OnChanged);
        changingSub = slider.ValueChanging().Subscribe(&rec, &Rec::OnChanging);
    }

    ~Fixture() { slider.DetachFromContext(); }

    void Press(float x) {
        im.PointerPressed(Point{x, CentreY()}, PointerButton::Left, ModifierKeys::None);
    }
    void MoveTo(float x) { im.PointerMoved(Point{x, CentreY()}, ModifierKeys::None); }
    void Release(float x) {
        im.PointerReleased(Point{x, CentreY()}, PointerButton::Left, ModifierKeys::None);
    }
    float CentreY() const { return slider.Bounds().y + slider.Bounds().h * 0.5f; }
};

}  // namespace

// A programmatic set is already the committed value: ValueChanged, no preview.
TEST(SliderEvents, ProgrammaticSetValueRaisesChangedOnly) {
    Fixture f;

    f.slider.SetValue(50.0f);

    EXPECT_EQ(f.rec.changed.size(), size_t{1});
    EXPECT_EQ(f.rec.changing.size(), size_t{0});
    EXPECT_NEAR(f.rec.changed[0], 50.0f, 0.01f);
}

// Arrow keys are discrete commits, not a drag — same rule as SetValue.
TEST(SliderEvents, KeyboardStepRaisesChangedOnly) {
    Fixture f;
    f.slider.SetValue(50.0f);
    f.rec.changed.clear();

    KeyEventArgs e;
    e.vk = VK_RIGHT;
    f.slider.OnKeyDownRouted(e);

    EXPECT_EQ(f.rec.changed.size(), size_t{1});
    EXPECT_EQ(f.rec.changing.size(), size_t{0});
    EXPECT_NEAR(f.rec.changed[0], 51.0f, 0.01f);
}

// The whole point of the split: every intermediate position previews, and the
// commit lands exactly once, on release, carrying the final value.
TEST(SliderEvents, DragPreviewsThenCommitsOnce) {
    Fixture f;

    // Press at 25% of the track. This already moves the value, so it previews.
    f.Press(50.0f);
    EXPECT_TRUE(f.rec.changing.size() >= 1);
    EXPECT_EQ(f.rec.changed.size(), size_t{0});

    f.MoveTo(100.0f);
    f.MoveTo(150.0f);

    // Still nothing committed mid-drag — this is what stops a settings flush per pixel.
    EXPECT_EQ(f.rec.changed.size(), size_t{0});
    const size_t previewsBeforeRelease = f.rec.changing.size();
    EXPECT_TRUE(previewsBeforeRelease >= 3);

    f.Release(150.0f);

    // Exactly one commit, and it agrees with the last preview and the live value.
    EXPECT_EQ(f.rec.changed.size(), size_t{1});
    EXPECT_NEAR(f.rec.changed[0], f.rec.changing.back(), 0.01f);
    EXPECT_NEAR(f.rec.changed[0], f.slider.Value(), 0.01f);
    // Release must not emit another preview.
    EXPECT_EQ(f.rec.changing.size(), previewsBeforeRelease);
}

// A press that lands exactly on the current thumb position moves nothing, so the
// release owes no commit. Without the dragChangedValue_ guard this would raise a
// ValueChanged for a click that changed no value.
TEST(SliderEvents, ClickWithoutMovementRaisesNothing) {
    Fixture f;
    // Put the thumb at the centre of the track, then press that exact spot.
    f.slider.SetValue(50.0f);
    f.rec.changed.clear();
    f.rec.changing.clear();

    const float centreX = f.slider.Bounds().x + f.slider.Bounds().w * 0.5f;
    f.Press(centreX);
    f.Release(centreX);

    EXPECT_EQ(f.rec.changing.size(), size_t{0});
    EXPECT_EQ(f.rec.changed.size(), size_t{0});
}

// Two drags in a row: the second must commit too. A one-shot latch that never
// reset would leave the second drag silent.
TEST(SliderEvents, SecondDragAlsoCommits) {
    Fixture f;

    f.Press(50.0f);
    f.MoveTo(100.0f);
    f.Release(100.0f);
    EXPECT_EQ(f.rec.changed.size(), size_t{1});

    f.Press(100.0f);
    f.MoveTo(160.0f);
    f.Release(160.0f);
    EXPECT_EQ(f.rec.changed.size(), size_t{2});
    EXPECT_NEAR(f.rec.changed[1], f.slider.Value(), 0.01f);
}
