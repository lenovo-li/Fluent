// ConstrainAvailableCacheTests.cpp — the measure cache is keyed on the
// NARROWED constraint (after ConstrainAvailable), not the raw offer.
//
// Why it matters: during a window resize the raw offer changes every frame
// (800, 801, 802 ...). A child with MaxWidth 420 is measured at 420 each time,
// so keying the cache on the raw offer missed every frame and re-ran Measure
// (for a TextBlock, a fresh DWrite layout) for an identical result. Keyed on the
// narrowed value, those frames are cache hits.

#include "../framework/Test.h"
#include "../../FluentUI/core/FrameworkElement.h"
#include "../../FluentUI/layout/StackPanel.h"
#include "../../FluentUI/diagnostics/LayoutCostProbe.h"
using namespace fluent;

namespace {
// Counts real Measure() runs; a cache hit never reaches this override.
class MeasureCallCounter : public FrameworkElement {
public:
    int measureCallCount = 0;
    void Measure(float, float) override {
        ++measureCallCount;
        SetDesired({100.0f, 50.0f});
        ClampDesiredSize();
    }
    void Render(const DrawingContext&) override {}
};
} // namespace

TEST(ConstrainAvailableCache, ResizePastMaxWidthHitsCache) {
    LayoutCostProbe::SetEnabled(true);
    LayoutCostProbe::Reset();

    StackPanel sp;
    MeasureCallCounter* child = sp.Emplace<MeasureCallCounter>();
    child->SetMaxWidth(420.0f);

    sp.Measure(800.0f, 1000.0f);
    sp.ClearDirtySubtree();   // as a real frame does after layout
    EXPECT_EQ(child->measureCallCount, 1);

    // The panel itself is measured directly, so it re-runs and re-offers the
    // child 801 and then 802. Both narrow to 420: cache hits, no real Measure.
    LayoutCostProbe::Reset();
    sp.Measure(801.0f, 1000.0f);
    sp.ClearDirtySubtree();
    sp.Measure(802.0f, 1000.0f);
    sp.ClearDirtySubtree();

    EXPECT_EQ(child->measureCallCount, 1);
    EXPECT_EQ(LayoutCostProbe::GetCount(LayoutCountKey::MeasureCacheHits), 2);
    EXPECT_EQ(LayoutCostProbe::GetCount(LayoutCountKey::MeasureCalls), 0);

    LayoutCostProbe::SetEnabled(false);
}

TEST(ConstrainAvailableCache, ChangeBelowMaxWidthStillMisses) {
    // Guard against over-caching: once the offer drops below MaxWidth the
    // narrowed value changes, so the child must measure again.
    StackPanel sp;
    MeasureCallCounter* child = sp.Emplace<MeasureCallCounter>();
    child->SetMaxWidth(420.0f);

    sp.Measure(800.0f, 1000.0f);
    sp.ClearDirtySubtree();
    sp.Measure(400.0f, 1000.0f);
    sp.ClearDirtySubtree();

    EXPECT_EQ(child->measureCallCount, 2);
}

TEST(ConstrainAvailableCache, ChangingMaxWidthRemeasures) {
    // SetMaxWidth dirties Measure, so a new limit is never served from a cache
    // entry built under the old one.
    StackPanel sp;
    MeasureCallCounter* child = sp.Emplace<MeasureCallCounter>();
    child->SetMaxWidth(420.0f);

    sp.Measure(800.0f, 1000.0f);
    sp.ClearDirtySubtree();
    child->SetMaxWidth(300.0f);
    sp.Measure(800.0f, 1000.0f);
    sp.ClearDirtySubtree();

    EXPECT_EQ(child->measureCallCount, 2);
}
