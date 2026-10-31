// CustomControlsTests.cpp — headless tests for extensions/CustomControls, the
// template library downstream authors copy. Only behaviour reachable without a
// device is covered: LogSlider's keyboard stepping and BigButton's row layout.
// What the rows look like (glyph rendering, contrast) needs a real window.

#include "../framework/Test.h"
#include "../../extensions/CustomControls/CustomControls.h"
#include <windows.h>

using namespace fluent;
using namespace fluent::custom;

namespace {

void Key(LogSlider& s, UINT vk) {
    KeyEventArgs e;
    e.vk = vk;
    s.OnKeyDownRouted(e);
    EXPECT_TRUE(e.handled);
}

}  // namespace

// --- LogSlider ---------------------------------------------------------------

// Slider's arrows add +/-1 and CoerceValue rounds back to the nearest power of
// ten, so the base arrows never moved the value. Each arrow must now move exactly
// one decade, and stop at the ends.
TEST(CustomControls, LogSliderArrowsStepOneDecade) {
    LogSlider s;
    EXPECT_NEAR(s.Value(), 100.0f, 0.001f);
    Key(s, VK_RIGHT);
    EXPECT_NEAR(s.Value(), 1000.0f, 0.01f);
    Key(s, VK_UP);
    EXPECT_NEAR(s.Value(), 10000.0f, 0.1f);
    Key(s, VK_UP);  // clamped at the maximum
    EXPECT_NEAR(s.Value(), 10000.0f, 0.1f);
    Key(s, VK_LEFT);
    EXPECT_NEAR(s.Value(), 1000.0f, 0.01f);
    Key(s, VK_DOWN);
    Key(s, VK_DOWN);
    Key(s, VK_DOWN);
    EXPECT_NEAR(s.Value(), 1.0f, 0.001f);
    Key(s, VK_DOWN);  // clamped at the minimum
    EXPECT_NEAR(s.Value(), 1.0f, 0.001f);
}

TEST(CustomControls, LogSliderArrowRaisesValueChanged) {
    struct Sink {
        int count = 0;
        float last = 0.0f;
    } sink;
    LogSlider s;
    auto sub = s.ValueChanged().Subscribe(&sink, [](void* o, Slider&, float& v) {
        auto* k = static_cast<Sink*>(o);
        ++k->count;
        k->last = v;
    });
    Key(s, VK_LEFT);
    EXPECT_EQ(sink.count, 1);
    EXPECT_NEAR(sink.last, 10.0f, 0.001f);
}

TEST(CustomControls, LogSliderHomeEndStillReachTheEnds) {
    LogSlider s;
    Key(s, VK_END);
    EXPECT_NEAR(s.Value(), 10000.0f, 0.1f);
    Key(s, VK_HOME);
    EXPECT_NEAR(s.Value(), 1.0f, 0.001f);
}

// --- BigButton ---------------------------------------------------------------

TEST(CustomControls, BigButtonRowsStackInOrderAndFitTheButton) {
    const RectDip b{10.0f, 20.0f, 200.0f, BigButton::kHeight};
    const BigButton::Rows r = BigButton::LayoutRows(b, true, true, true);
    EXPECT_NEAR(r.icon.h, BigButton::kIconRowH, 0.001f);
    EXPECT_NEAR(r.title.h, BigButton::kTitleRowH, 0.001f);
    EXPECT_NEAR(r.subtitle.h, BigButton::kSubtitleRowH, 0.001f);
    EXPECT_NEAR(r.icon.bottom(), r.title.y, 0.001f);
    EXPECT_NEAR(r.title.bottom(), r.subtitle.y, 0.001f);
    EXPECT_TRUE(r.icon.y >= b.y);
    EXPECT_TRUE(r.subtitle.bottom() <= b.bottom());
    // Centred as a block: equal slack above and below.
    EXPECT_NEAR(r.icon.y - b.y, b.bottom() - r.subtitle.bottom(), 0.001f);
    EXPECT_NEAR(r.title.x, b.x, 0.001f);
    EXPECT_NEAR(r.title.w, b.w, 0.001f);
}

TEST(CustomControls, BigButtonLabelOnlyIsCentred) {
    const RectDip b{0.0f, 0.0f, 200.0f, BigButton::kHeight};
    const BigButton::Rows r = BigButton::LayoutRows(b, false, true, false);
    EXPECT_NEAR(r.icon.h, 0.0f, 0.001f);
    EXPECT_NEAR(r.subtitle.h, 0.0f, 0.001f);
    EXPECT_NEAR(r.title.y + r.title.h * 0.5f, b.h * 0.5f, 0.001f);
}

TEST(CustomControls, BigButtonSubtitleGetsARow) {
    // The bug this pins: SetSubtitle/SetIcon stored their strings and nothing drew
    // them. A shown subtitle must get a real row below the title.
    BigButton btn;
    btn.SetText(L"Install");
    btn.SetSubtitle(L"1.2 GB available");
    btn.SetIcon(L"↓");
    EXPECT_TRUE(btn.Subtitle() == L"1.2 GB available");
    EXPECT_TRUE(btn.Icon() == L"↓");
    const RectDip b{0.0f, 0.0f, 200.0f, BigButton::kHeight};
    const BigButton::Rows r = BigButton::LayoutRows(b, !btn.Icon().empty(),
                                                    !btn.Text().empty(),
                                                    !btn.Subtitle().empty());
    EXPECT_TRUE(r.subtitle.h > 0.0f);
    EXPECT_TRUE(r.subtitle.y >= r.title.bottom() - 0.001f);
}
