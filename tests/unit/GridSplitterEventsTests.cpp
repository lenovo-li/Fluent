// GridSplitterEventsTests.cpp — GridSplitter drag events.
//
// DragStarted / DragDelta / DragCompleted. The scenario these exist for is layout
// persistence: an app saves the new column widths when the user finishes dragging,
// and DragDelta.deltaDip is the cumulative offset from the drag anchor.
//
// The pointer handlers are protected, so input is driven through InputManager the
// way the real window does it. That also gives the splitter the `Context().input`
// it needs for CapturePointer — without which a drag would not survive the pointer
// leaving the 4 DIP bar, and OnPointerMoved would never be reached.

#include "../framework/Test.h"
#include "../../FluentUI/controls/GridSplitter.h"
#include "../../FluentUI/layout/Grid.h"
#include "../../FluentUI/input/InputManager.h"

#include <vector>

using namespace fluent;

namespace {

struct DragRec {
    int started = 0;
    int completed = 0;
    std::vector<float> deltas;

    static void OnStarted(void* owner, GridSplitter&, RoutedEventArgs&) {
        static_cast<DragRec*>(owner)->started++;
    }
    static void OnDelta(void* owner, GridSplitter&, GridSplitterDragDeltaArgs& a) {
        static_cast<DragRec*>(owner)->deltas.push_back(a.deltaDip);
    }
    static void OnCompleted(void* owner, GridSplitter&, RoutedEventArgs&) {
        static_cast<DragRec*>(owner)->completed++;
    }
};

// A Grid with a live, draggable splitter in the middle track, wired to an
// InputManager. 400x300; the splitter column is 8 DIP wide, so with two Star tracks
// either side it lands at x = 196..204 — the press below aims at its centre.
//
// Vertical (the default) means the bar itself is vertical and the drag resizes
// COLUMNS, which is the axis the x-deltas in these tests move along.
struct Fixture {
    InputManager im;
    Grid grid;
    GridSplitter* splitter = nullptr;
    std::vector<UIElement*> roots;
    DragRec rec;
    Subscription startSub, deltaSub, endSub;

    Fixture(GridSplitter::Orientation o = GridSplitter::Orientation::Vertical) {
        if (o == GridSplitter::Orientation::Vertical) {
            grid.SetColumns({GridLength::Star(), GridLength::Pixels(8.0f), GridLength::Star()});
            grid.SetRows({GridLength::Star()});
            splitter = grid.Emplace<GridSplitter>();
            grid.SetCell(splitter, 0, 1);
        } else {
            grid.SetRows({GridLength::Star(), GridLength::Pixels(8.0f), GridLength::Star()});
            grid.SetColumns({GridLength::Star()});
            splitter = grid.Emplace<GridSplitter>();
            grid.SetCell(splitter, 1, 0);
        }
        splitter->SetOrientation(o);

        UIContext ctx;
        ctx.input = &im;
        grid.AttachToContext(ctx);

        grid.UpdateLayout({0, 0, 400, 300});

        roots.push_back(&grid);
        im.SetRoots(&roots);

        startSub = splitter->DragStarted().Subscribe(&rec, &DragRec::OnStarted);
        deltaSub = splitter->DragDelta().Subscribe(&rec, &DragRec::OnDelta);
        endSub = splitter->DragCompleted().Subscribe(&rec, &DragRec::OnCompleted);
    }

    ~Fixture() { grid.DetachFromContext(); }

    void Press(float x, float y) {
        im.PointerPressed(Point{x, y}, PointerButton::Left, ModifierKeys::None);
    }
    void MoveTo(float x, float y) { im.PointerMoved(Point{x, y}, ModifierKeys::None); }
    void Release(float x, float y) {
        im.PointerReleased(Point{x, y}, PointerButton::Left, ModifierKeys::None);
    }
    // Centre of the splitter's own bounds, whatever the layout resolved them to.
    float CentreX() const { return splitter->Bounds().x + splitter->Bounds().w * 0.5f; }
    float CentreY() const { return splitter->Bounds().y + splitter->Bounds().h * 0.5f; }
};

}  // namespace

// The three events fire in order, once each for start and completion, and once per
// move in between.
TEST(GridSplitterEvents, DragLifecycle) {
    Fixture f;
    const float cx = f.CentreX(), cy = f.CentreY();

    f.Press(cx, cy);
    EXPECT_EQ(f.rec.started, 1);
    EXPECT_EQ(f.rec.deltas.size(), size_t{0});
    EXPECT_EQ(f.rec.completed, 0);

    f.MoveTo(cx + 20.0f, cy);
    EXPECT_EQ(f.rec.deltas.size(), size_t{1});
    EXPECT_NEAR(f.rec.deltas[0], 20.0f, 0.01f);

    f.MoveTo(cx + 35.0f, cy);
    EXPECT_EQ(f.rec.deltas.size(), size_t{2});
    // Cumulative from the anchor, not the 15 DIP increment since the last move.
    EXPECT_NEAR(f.rec.deltas[1], 35.0f, 0.01f);

    f.Release(cx + 35.0f, cy);
    EXPECT_EQ(f.rec.completed, 1);
    EXPECT_EQ(f.rec.started, 1);   // no spurious re-raise
}

// The delta is signed: dragging back past the anchor reports a negative offset.
TEST(GridSplitterEvents, BackwardDragReportsNegativeDelta) {
    Fixture f;
    const float cx = f.CentreX(), cy = f.CentreY();

    f.Press(cx, cy);
    f.MoveTo(cx - 30.0f, cy);

    EXPECT_EQ(f.rec.deltas.size(), size_t{1});
    EXPECT_NEAR(f.rec.deltas[0], -30.0f, 0.01f);
}

// A horizontal splitter resizes ROWS, so its delta tracks the pointer's Y.
TEST(GridSplitterEvents, HorizontalSplitterDeltaFollowsY) {
    Fixture f(GridSplitter::Orientation::Horizontal);
    const float cx = f.CentreX(), cy = f.CentreY();

    f.Press(cx, cy);
    f.MoveTo(cx, cy + 30.0f);

    EXPECT_EQ(f.rec.deltas.size(), size_t{1});
    EXPECT_NEAR(f.rec.deltas[0], 30.0f, 0.01f);
}

// Hovering is not dragging. Without a press there is no anchor, so a move must not
// resize anything or report a delta.
TEST(GridSplitterEvents, NoDeltaWithoutPress) {
    Fixture f;
    const float cx = f.CentreX(), cy = f.CentreY();

    f.MoveTo(cx + 20.0f, cy);
    EXPECT_EQ(f.rec.deltas.size(), size_t{0});
    EXPECT_EQ(f.rec.started, 0);

    f.Release(cx + 20.0f, cy);
    EXPECT_EQ(f.rec.completed, 0);
}

// Destroying the Subscription detaches the handler; a later drag does not call it.
TEST(GridSplitterEvents, SubscriptionRAII) {
    Fixture f;
    const float cx = f.CentreX(), cy = f.CentreY();

    f.Press(cx, cy);
    f.Release(cx, cy);
    EXPECT_EQ(f.rec.started, 1);

    f.startSub.reset();

    f.Press(cx, cy);
    f.Release(cx, cy);
    EXPECT_EQ(f.rec.started, 1);      // unchanged
    EXPECT_EQ(f.rec.completed, 2);    // still subscribed, so this one did fire
}
