// LayoutCostProbe.h — opt-in work counters for one layout pass.
//
// WHY THIS EXISTS. "How much work did this layout pass do" is a different question
// from "how long did it take", and it needs no clock. A resize whose Measure cost is
// spread over 900 cheap calls and one whose cost is 3 expensive calls read the same on
// a timer and call for opposite fixes (cut the traversal vs. fix the one control). The
// counters below answer the first question, and the measure-cache tests
// (MeasureCacheTests, ConstrainAvailableCacheTests) assert on them: a cache that
// silently stops hitting shows up as MeasureCalls rising and MeasureCacheHits falling,
// which no layout-result assertion can see because a cache miss produces the same rect.
//
// WHAT WAS REMOVED. This header used to carry per-control wall-clock timers (an RAII
// Scope placed in TextBlock/TextArea/TreeView/... Measure and OnBoundsChanged, plus the
// DComp surface steps) and a Report() that printed them through the debug trace. The
// trace output was dropped as noise, and with nothing left to read the timers they were
// dead weight on the layout path, so they went too. Re-adding timing should start from
// what question it has to answer rather than from restoring the old key list.
//
// COST WHEN OFF: one bool test per Bump. Nothing allocates and nothing is written.
#pragma once

#include <array>
#include <cstddef>

namespace fluent {

// Incremented from UIElement::MeasureCached / SetBounds, the single choke points every
// panel child passes through.
enum class LayoutCountKey {
    MeasureCalls,      // MeasureCached invocations that ran the real virtual Measure
    MeasureCacheHits,  // MeasureCached invocations served from cachedDesired_
    // SetBounds calls whose rect actually changed, i.e. the ones that dispatched
    // OnBoundsChanged. There is deliberately only ONE arrange counter: a separate
    // "OnBoundsChanged dispatches" tally would be the same number by construction,
    // and two counters that can never disagree are two chances to misread one fact.
    BoundsChanged,
    Count
};

class LayoutCostProbe {
public:
    // Enable/disable collection. Off by default; tests turn it on around the pass
    // they assert on.
    static void SetEnabled(bool on) { enabled_ = on; }
    static bool Enabled() { return enabled_; }

    // Zero every counter. Window::OnLayout calls it before Measure so the numbers
    // describe that pass rather than the session: without it they grow monotonically
    // and a read after a hundred frames looks like one catastrophic frame.
    static void Reset() {
        for (auto& c : counts_) c = 0;
    }

    // Add one to a work counter. A single bool test when collection is off.
    static void Bump(LayoutCountKey key) {
        if (!enabled_) return;
        ++counts_[static_cast<size_t>(key)];
    }

    static int GetCount(LayoutCountKey key) {
        return counts_[static_cast<size_t>(key)];
    }

private:
    // UI-thread only by intent, but the async worker also runs Measure — hence
    // `inline static` plain values rather than thread_local. Races here can only skew
    // a diagnostic count, never the layout itself.
    inline static bool enabled_ = false;
    inline static std::array<int, static_cast<size_t>(LayoutCountKey::Count)> counts_{};
};

} // namespace fluent
