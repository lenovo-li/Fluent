// FrameworkElementConstrainAvailableTests.cpp — verify Min/Max narrows the
// constraint BEFORE Measure, not only the result AFTER.

#include "../framework/Test.h"
#include "../../FluentUI/core/FrameworkElement.h"
#include "../../FluentUI/layout/StackPanel.h"
#include "../../FluentUI/layout/Border.h"
#include <cmath>
#include <limits>
#include <memory>
#include <optional>
using namespace fluent;

// Mock element that records what availW it was measured at, to prove
// ConstrainAvailable runs before Measure.
class MeasureProbe : public FrameworkElement {
public:
    float lastAvailW = -1.0f;
    float lastAvailH = -1.0f;
    void Measure(float availW, float availH) override {
        lastAvailW = availW;
        lastAvailH = availH;
        // Report a fixed desired size (no content logic needed).
        SetDesired({100.0f, 50.0f});
        ClampDesiredSize();
    }
    void Render(const DrawingContext&) override {}  // no visual output needed
};

// --- ClampAvailAxis pure function tests (headless, no element needed) ---

TEST(FrameworkElement, ClampAvailAxisNoConstraints) {
    // No Min/Max set: the offer passes through unchanged.
    std::optional<float> noMin, noMax;
    EXPECT_NEAR(FrameworkElement::ClampAvailAxis(500.0f, noMin, noMax), 500.0f, 0.01f);
    EXPECT_TRUE(std::isinf(FrameworkElement::ClampAvailAxis(
        std::numeric_limits<float>::infinity(), noMin, noMax)));
}

TEST(FrameworkElement, ClampAvailAxisMaxOnly) {
    // MaxWidth 420, offered 800 → 420.
    std::optional<float> noMin, max420 = 420.0f;
    EXPECT_NEAR(FrameworkElement::ClampAvailAxis(800.0f, noMin, max420), 420.0f, 0.01f);
    // Infinite offer is clamped to MaxWidth (the wrapping TextBlock case).
    EXPECT_NEAR(FrameworkElement::ClampAvailAxis(
        std::numeric_limits<float>::infinity(), noMin, max420), 420.0f, 0.01f);
    // Offer smaller than Max passes through.
    EXPECT_NEAR(FrameworkElement::ClampAvailAxis(300.0f, noMin, max420), 300.0f, 0.01f);
}

TEST(FrameworkElement, ClampAvailAxisMinOnly) {
    std::optional<float> min200, noMax;
    min200 = 200.0f;
    // Offer larger than Min passes through.
    EXPECT_NEAR(FrameworkElement::ClampAvailAxis(500.0f, min200, noMax), 500.0f, 0.01f);
    // Offer smaller than Min is lifted to Min.
    EXPECT_NEAR(FrameworkElement::ClampAvailAxis(100.0f, min200, noMax), 200.0f, 0.01f);
}

TEST(FrameworkElement, ClampAvailAxisMinMaxBoth) {
    std::optional<float> min100 = 100.0f, max300 = 300.0f;
    EXPECT_NEAR(FrameworkElement::ClampAvailAxis(200.0f, min100, max300), 200.0f, 0.01f);
    EXPECT_NEAR(FrameworkElement::ClampAvailAxis(50.0f, min100, max300), 100.0f, 0.01f);
    EXPECT_NEAR(FrameworkElement::ClampAvailAxis(400.0f, min100, max300), 300.0f, 0.01f);
}

TEST(FrameworkElement, ClampAvailAxisMinMaxConflict) {
    // Min > Max: Min wins (same as ClampDesiredSize).
    std::optional<float> min400 = 400.0f, max200 = 200.0f;
    EXPECT_NEAR(FrameworkElement::ClampAvailAxis(500.0f, min400, max200), 400.0f, 0.01f);
    EXPECT_NEAR(FrameworkElement::ClampAvailAxis(100.0f, min400, max200), 400.0f, 0.01f);
}

// --- Integration tests: verify ConstrainAvailable actually runs before Measure ---

TEST(FrameworkElement, ConstrainAvailableMaxWidthNarrowsOffer) {
    // The core bug: a child with MaxWidth 420 offered 800 used to measure at 800.
    // After ConstrainAvailable, it measures at 420.
    StackPanel sp;
    MeasureProbe* probe = sp.Emplace<MeasureProbe>();
    probe->SetMaxWidth(420.0f);

    sp.Measure(800.0f, 1000.0f);

    // Probe must have been measured at MaxWidth, not at the raw offer.
    EXPECT_NEAR(probe->lastAvailW, 420.0f, 0.01f);
    EXPECT_NEAR(probe->lastAvailH, 1000.0f, 0.01f);
}

TEST(FrameworkElement, ConstrainAvailableMinWidthLiftsOffer) {
    StackPanel sp;
    MeasureProbe* probe = sp.Emplace<MeasureProbe>();
    probe->SetMinWidth(500.0f);

    sp.Measure(300.0f, 1000.0f);

    // Offered 300, but MinWidth 500 lifts it.
    EXPECT_NEAR(probe->lastAvailW, 500.0f, 0.01f);
}

TEST(FrameworkElement, ConstrainAvailableInfiniteOfferClampedToMax) {
    // A ScrollPanel (or any unbounded container) offers infinite width.
    // An element with MaxWidth must measure at MaxWidth, not infinity.
    StackPanel sp;
    MeasureProbe* probe = sp.Emplace<MeasureProbe>();
    probe->SetMaxWidth(350.0f);

    sp.Measure(std::numeric_limits<float>::infinity(), 1000.0f);

    EXPECT_NEAR(probe->lastAvailW, 350.0f, 0.01f);
}

TEST(FrameworkElement, ConstrainAvailableNoConstraintsPassThrough) {
    // No Min/Max: the raw offer passes through unchanged.
    StackPanel sp;
    MeasureProbe* probe = sp.Emplace<MeasureProbe>();

    sp.Measure(600.0f, 400.0f);

    EXPECT_NEAR(probe->lastAvailW, 600.0f, 0.01f);
    EXPECT_NEAR(probe->lastAvailH, 400.0f, 0.01f);
}

TEST(FrameworkElement, ConstrainAvailableBothAxes) {
    StackPanel sp;
    MeasureProbe* probe = sp.Emplace<MeasureProbe>();
    probe->SetMaxWidth(300.0f);
    probe->SetMinHeight(200.0f);

    sp.Measure(800.0f, 100.0f);

    EXPECT_NEAR(probe->lastAvailW, 300.0f, 0.01f);  // clamped down by MaxWidth
    EXPECT_NEAR(probe->lastAvailH, 200.0f, 0.01f);  // lifted by MinHeight
}

TEST(FrameworkElement, ConstrainAvailableWithMargin) {
    // The panel subtracts margin before calling MeasureCached, so the constraint
    // is applied to the margin-less offer.
    StackPanel sp;
    MeasureProbe* probe = sp.Emplace<MeasureProbe>();
    probe->SetMargin({10, 10, 10, 10});
    probe->SetMaxWidth(300.0f);

    sp.Measure(800.0f, 1000.0f);

    // Panel offers 800 - 20 = 780. ConstrainAvailable clamps that to 300.
    EXPECT_NEAR(probe->lastAvailW, 300.0f, 0.01f);
}

TEST(FrameworkElement, ConstrainAvailableNestedBorder) {
    // A Border with MaxWidth must narrow the offer to its child.
    StackPanel sp;
    Border* border = sp.Emplace<Border>();
    border->SetMaxWidth(250.0f);
    auto probe = std::make_unique<MeasureProbe>();
    MeasureProbe* probePtr = probe.get();
    border->SetChild(std::move(probe));

    sp.Measure(800.0f, 1000.0f);

    // The Border itself was offered 800 but measures at 250.
    // The probe inside the Border measures at the Border's content area (~250).
    EXPECT_TRUE(probePtr->lastAvailW <= 250.0f);
}
