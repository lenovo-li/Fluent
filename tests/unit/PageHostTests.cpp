// PageHostTests.cpp — headless tests for extensions/NavigationControls PageHost
// measure: which pages are measured on a layout pass, what sizes the host, and
// that content changed while a page was hidden is laid out when it is shown.
//
// The resize cost these pin: PageHost used to measure every page on every layout.
// A new width misses every MeasureCached, so a window-edge drag re-measured all
// hidden pages each frame. Visible pages only now — but a hidden page whose content
// changes must still be measured on THAT frame, because the host clears dirty flags
// tree-wide at frame end and the page's intermediate panels would otherwise keep a
// stale cached size (see PageHost::MeasureOverride).

#include "../framework/Test.h"
#include "../../extensions/NavigationControls/PageHost.h"
#include "../../FluentUI/layout/StackPanel.h"
#include "../../FluentUI/core/UIContext.h"
#include <memory>

using namespace fluent;

namespace {

// Counts real Measure calls. A MeasureCached hit never reaches Measure, so the
// count is exactly "how many times layout did the work".
class CountingLeaf : public FrameworkElement {
public:
    int measures = 0;
    void Measure(float availW, float availH) override {
        ++measures;
        FrameworkElement::Measure(availW, availH);
    }
    void Render(const DrawingContext&) override {}
};

// page (StackPanel) > group (StackPanel) > leaf. The middle panel is the point:
// it is the cache that goes stale when a hidden leaf's change is not serviced.
struct Page {
    StackPanel* page = nullptr;
    StackPanel* group = nullptr;
    CountingLeaf* leaf = nullptr;
};

struct Rig {
    PageHost host;
    Page pages[3];
    explicit Rig(float leafH0 = 20.0f, float leafH1 = 20.0f, float leafH2 = 20.0f) {
        const float heights[3] = {leafH0, leafH1, leafH2};
        for (int i = 0; i < 3; ++i) {
            auto page = std::make_unique<StackPanel>();
            page->SetOrientation(StackPanel::Orientation::Vertical);
            Page& p = pages[i];
            p.page = page.get();
            p.group = page->Emplace<StackPanel>();
            p.group->SetOrientation(StackPanel::Orientation::Vertical);
            p.leaf = p.group->Emplace<CountingLeaf>();
            p.leaf->SetHeight(heights[i]);
            host.AddPage(std::move(page));
        }
        host.ShowPage(0);  // first ShowPage is always instant
    }
    // One host frame: layout, then the tree-wide dirty clear NativeWindowHost does
    // after painting. Tests that skip the clear cannot see the stale-cache bug.
    void Frame(float w, float h) {
        host.UpdateLayout({0.0f, 0.0f, w, h});
        host.ClearDirtySubtree();
    }
};

}  // namespace

TEST(PageHost, HiddenPagesAreNotMeasuredWhenOnlyTheSizeChanges) {
    Rig r;
    r.Frame(400.0f, 300.0f);
    const int hidden1 = r.pages[1].leaf->measures;
    const int hidden2 = r.pages[2].leaf->measures;
    const int shown = r.pages[0].leaf->measures;

    // A resize drag: a new width every frame.
    for (float w : {410.0f, 420.0f, 430.0f, 440.0f}) r.Frame(w, 300.0f);

    EXPECT_EQ(r.pages[0].leaf->measures, shown + 4);   // visible: every frame
    EXPECT_EQ(r.pages[1].leaf->measures, hidden1);     // hidden: never
    EXPECT_EQ(r.pages[2].leaf->measures, hidden2);
}

// Visit `index` and come back to page 0, so that page has warm measure caches at
// the current size. A page that was never shown has none, and nothing in it can go
// stale — a test that skips this step passes even with the bug present.
void VisitAndLeave(Rig& r, int index, float w, float h) {
    r.host.ShowPage(index);
    r.Frame(w, h);
    r.host.ShowPage(0);
    r.Frame(w, h);
}

TEST(PageHost, ContentChangedWhileHiddenIsLaidOutWhenShown) {
    Rig r;
    r.Frame(400.0f, 300.0f);
    VisitAndLeave(r, 1, 400.0f, 300.0f);
    EXPECT_NEAR(r.pages[1].group->Desired().h, 20.0f, 0.01f);  // cache is warm

    // Page 1 is hidden. Its content changes and one frame runs at the SAME size,
    // ending in the tree-wide dirty clear.
    r.pages[1].leaf->SetHeight(50.0f);
    r.Frame(400.0f, 300.0f);

    // Shown at the same size: the group's cache must not hand back the old 20.
    r.host.ShowPage(1);
    r.Frame(400.0f, 300.0f);
    EXPECT_NEAR(r.pages[1].group->Desired().h, 50.0f, 0.01f);
    EXPECT_NEAR(r.pages[1].leaf->Bounds().h, 50.0f, 0.01f);
}

TEST(PageHost, HiddenPageWithAChangeIsMeasuredOnceNotEveryFrame) {
    // The dirty-page exception costs one measure per content change, not one per
    // frame: once the change is serviced, resizing skips the page again.
    Rig r;
    r.Frame(400.0f, 300.0f);
    VisitAndLeave(r, 2, 400.0f, 300.0f);
    r.pages[2].leaf->SetHeight(30.0f);
    const int before = r.pages[2].leaf->measures;
    r.Frame(400.0f, 300.0f);
    EXPECT_EQ(r.pages[2].leaf->measures, before + 1);
    for (float w : {410.0f, 420.0f, 430.0f}) r.Frame(w, 300.0f);
    EXPECT_EQ(r.pages[2].leaf->measures, before + 1);
}

TEST(PageHost, DesiredSizeFollowsTheVisiblePage) {
    // Hidden pages no longer size the host: page 1 is far taller but not shown.
    Rig r(20.0f, 300.0f, 20.0f);
    r.Frame(400.0f, 500.0f);
    EXPECT_NEAR(r.host.Desired().h, 20.0f, 0.01f);

    r.host.ShowPage(1);
    r.Frame(400.0f, 500.0f);
    EXPECT_NEAR(r.host.Desired().h, 300.0f, 0.01f);
}

TEST(PageHost, PageShownAfterAResizeIsMeasuredAtTheNewSize) {
    // Skipped while hidden, so it last measured at 400. Showing it must lay it out
    // at the current width, not reuse the 400-wide result.
    Rig r;
    r.Frame(400.0f, 300.0f);
    r.Frame(600.0f, 300.0f);
    r.host.ShowPage(2);
    r.Frame(600.0f, 300.0f);
    EXPECT_NEAR(r.pages[2].leaf->Bounds().w, 600.0f, 0.01f);
}

TEST(PageHost, BothPagesAreMeasuredDuringATransition) {
    // The leaving page still paints mid-transition, so it has to follow a resize
    // too. A transition needs an attached host and the system "Animation effects"
    // setting on; with it off ShowPage cuts instantly and there is nothing to test.
    Rig r;
    r.host.SetTransition(PageTransition::Fade);
    UIContext ctx;
    ctx.dpiScale = 1.0f;
    r.host.AttachToContext(ctx);
    r.Frame(400.0f, 300.0f);
    r.host.ShowPage(1);
    if (!r.host.IsTransitioning()) return;  // animation effects disabled here

    const int leaving = r.pages[0].leaf->measures;
    const int incoming = r.pages[1].leaf->measures;
    const int hidden = r.pages[2].leaf->measures;
    r.Frame(420.0f, 300.0f);
    EXPECT_EQ(r.pages[0].leaf->measures, leaving + 1);
    EXPECT_EQ(r.pages[1].leaf->measures, incoming + 1);
    EXPECT_EQ(r.pages[2].leaf->measures, hidden);
}
