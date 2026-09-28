// GridSplitterTrackUnitTests.cpp — a drag must not destroy the grid's elasticity.
//
// The bug these tests exist for, as the user saw it: open the GridSplitter gallery
// page, drag the divider, then resize the window. The right-hand pane stops
// following the window edge and a permanent gap opens outside it. Without a drag,
// resizing behaves.
//
// The cause was the write-back unit, not the resize path. Star means "share the
// leftover space"; convert every Star track in a grid to Pixel and the grid has
// nothing left to absorb the extra width a larger window offers, so the slack piles
// up outside the last track. Two earlier fixes froze fewer tracks (both sides, then
// only the shrinking side) and both still reached the all-Pixel state — the second
// one as soon as a drag crossed back over its own anchor, since by then the user has
// dragged in both directions.
//
// So the assertions here are mostly about UNITS surviving a drag, plus one
// end-to-end check that re-arranging at a larger extent still fills it. They run
// headless: Grid layout and GridSplitter input need no device.

#include "../framework/Test.h"
#include "../../FluentUI/controls/GridSplitter.h"
#include "../../FluentUI/layout/Grid.h"
#include "../../FluentUI/input/InputManager.h"

#include <vector>

using namespace fluent;

namespace {

// Grid with three columns (or rows) and a live splitter in the middle track, wired
// to an InputManager so the protected pointer handlers are reachable the way the
// real window reaches them — including CapturePointer, without which a drag would
// not survive the pointer leaving the bar.
struct Fixture {
    InputManager im;
    Grid grid;
    GridSplitter* splitter = nullptr;
    std::vector<UIElement*> roots;

    Fixture(GridLength before, GridLength after,
            GridSplitter::Orientation o = GridSplitter::Orientation::Vertical,
            float w = 400.0f, float h = 300.0f) {
        const GridLength bar = GridLength::Pixels(8.0f);
        if (o == GridSplitter::Orientation::Vertical) {
            grid.SetColumns({before, bar, after});
            grid.SetRows({GridLength::Star()});
            splitter = grid.Emplace<GridSplitter>();
            grid.SetCell(splitter, 0, 1);
        } else {
            grid.SetRows({before, bar, after});
            grid.SetColumns({GridLength::Star()});
            splitter = grid.Emplace<GridSplitter>();
            grid.SetCell(splitter, 1, 0);
        }
        splitter->SetOrientation(o);

        UIContext ctx;
        ctx.input = &im;
        grid.AttachToContext(ctx);
        grid.UpdateLayout({0, 0, w, h});

        roots.push_back(&grid);
        im.SetRoots(&roots);
    }
    ~Fixture() { grid.DetachFromContext(); }

    void Relayout(float w, float h) { grid.UpdateLayout({0, 0, w, h}); }

    float CentreX() const { return splitter->Bounds().x + splitter->Bounds().w * 0.5f; }
    float CentreY() const { return splitter->Bounds().y + splitter->Bounds().h * 0.5f; }

    // One complete drag: press at the splitter centre, move through every waypoint,
    // release at the last one. Waypoints are offsets along the resize axis.
    void DragBy(const std::vector<float>& offsets) {
        const float cx = CentreX(), cy = CentreY();
        const bool vertical = splitter->GetOrientation() == GridSplitter::Orientation::Vertical;
        im.PointerPressed(Point{cx, cy}, PointerButton::Left, ModifierKeys::None);
        float last = 0.0f;
        for (float d : offsets) {
            im.PointerMoved(vertical ? Point{cx + d, cy} : Point{cx, cy + d},
                            ModifierKeys::None);
            last = d;
        }
        im.PointerReleased(vertical ? Point{cx + last, cy} : Point{cx, cy + last},
                           PointerButton::Left, ModifierKeys::None);
    }

    const GridLength& Col(size_t i) const { return grid.ColumnAt(i); }
    const GridLength& Row(size_t i) const { return grid.RowAt(i); }
    float ColSize(size_t i) const { return grid.ResolvedColumnSizes()[i]; }
    float RowSize(size_t i) const { return grid.ResolvedRowSizes()[i]; }
};

}  // namespace

// The core regression. Two Star columns, dragged once in one direction: both must
// still be Star afterwards. This is the assertion the previous fix could not make —
// it converted the shrinking track to Pixel by design.
TEST(GridSplitterTracks, StarPairStaysStarAfterDrag) {
    Fixture f(GridLength::Star(), GridLength::Star());

    f.DragBy({40.0f});

    EXPECT_TRUE(f.Col(0).isStar());
    EXPECT_TRUE(f.Col(2).isStar());
}

// The same, for a drag that crosses back over its own anchor. This is the sequence
// the shrinking-side-only fix could not survive: the rightward move froze the
// right track, the leftward move froze the left one, and the grid ended up with no
// Star track at all even though the net drag was a single direction.
TEST(GridSplitterTracks, StarPairSurvivesDragAcrossAnchor) {
    Fixture f(GridLength::Star(), GridLength::Star());

    f.DragBy({30.0f, 10.0f, -25.0f, -40.0f});

    EXPECT_TRUE(f.Col(0).isStar());
    EXPECT_TRUE(f.Col(2).isStar());
}

// A drag changes the star WEIGHTS, and their sum is conserved: this pair's share of
// the grid relative to any OTHER star track must not shift because of a drag.
TEST(GridSplitterTracks, StarWeightSumIsConserved) {
    Fixture f(GridLength::Star(3.0f), GridLength::Star(1.0f));
    const float sumBefore = f.Col(0).value + f.Col(2).value;

    f.DragBy({-50.0f});

    const float sumAfter = f.Col(0).value + f.Col(2).value;
    EXPECT_NEAR(sumAfter, sumBefore, 0.01f);
    // And the ratio moved the way the drag asked: left shrank, so its weight fell.
    EXPECT_TRUE(f.Col(0).value < 3.0f);
    EXPECT_TRUE(f.Col(2).value > 1.0f);
}

// The end-to-end shape of the user's report: drag, then re-arrange at a larger
// extent. The tracks must still consume the whole content width — a leftover gap is
// the visible symptom.
TEST(GridSplitterTracks, ResizeAfterDragStillFillsWidth) {
    Fixture f(GridLength::Star(), GridLength::Star(), GridSplitter::Orientation::Vertical,
              400.0f, 300.0f);

    f.DragBy({60.0f});
    f.Relayout(700.0f, 300.0f);

    const float total = f.ColSize(0) + f.ColSize(1) + f.ColSize(2);
    EXPECT_NEAR(total, 700.0f, 0.01f);
}

// The drag ratio is remembered across that resize, rather than snapping back to the
// original 1:1. (Both properties have to hold at once: a fix that dropped the drag
// on the floor would also pass the gap test above.)
//
// Note the Relayout before the first read. A drag writes track DEFINITIONS and
// invalidates Measure; ResolvedColumnSizes() is only refreshed by an Arrange, which
// the real app gets from the next scheduled frame and a headless test has to ask for.
TEST(GridSplitterTracks, DragRatioSurvivesResize) {
    Fixture f(GridLength::Star(), GridLength::Star());

    f.DragBy({60.0f});
    f.Relayout(400.0f, 300.0f);
    const float ratioAtDragWidth = f.ColSize(0) / f.ColSize(2);
    EXPECT_TRUE(ratioAtDragWidth > 1.2f);  // left is now clearly the wider pane

    f.Relayout(700.0f, 300.0f);
    EXPECT_NEAR(f.ColSize(0) / f.ColSize(2), ratioAtDragWidth, 0.01f);
}

// Rows behave the same way; the axis is the only difference.
TEST(GridSplitterTracks, HorizontalStarPairStaysStar) {
    Fixture f(GridLength::Star(), GridLength::Star(),
              GridSplitter::Orientation::Horizontal);

    f.DragBy({35.0f, -60.0f});

    EXPECT_TRUE(f.Row(0).isStar());
    EXPECT_TRUE(f.Row(2).isStar());
    const float total = f.RowSize(0) + f.RowSize(1) + f.RowSize(2);
    EXPECT_NEAR(total, 300.0f, 0.01f);
}

// A Pixel track still takes an absolute size — preserving units cuts both ways, and
// a fixed sidebar must not silently become elastic.
TEST(GridSplitterTracks, PixelTrackKeepsPixelUnit) {
    Fixture f(GridLength::Pixels(120.0f), GridLength::Star());

    f.DragBy({30.0f});

    EXPECT_TRUE(f.Col(0).isPixel());
    EXPECT_NEAR(f.Col(0).value, 150.0f, 0.01f);
    // The Star side stays Star, so the grid is still elastic.
    EXPECT_TRUE(f.Col(2).isStar());
}

// Star opposite Pixel: the star track keeps its unit, and the pair still fills a
// wider grid after the drag.
TEST(GridSplitterTracks, StarAgainstPixelStaysElastic) {
    Fixture f(GridLength::Pixels(120.0f), GridLength::Star());

    f.DragBy({-40.0f});
    f.Relayout(600.0f, 300.0f);

    EXPECT_TRUE(f.Col(2).isStar());
    const float total = f.ColSize(0) + f.ColSize(1) + f.ColSize(2);
    EXPECT_NEAR(total, 600.0f, 0.01f);
}

// Dragging far past the edge clamps instead of letting a track reach 0 (a 0 DIP
// track cannot be hit-tested, so the splitter could never be dragged back), and the
// clamp is zero-sum: the pair's total extent is unchanged, which is what keeps
// everything outside the pair from shifting.
TEST(GridSplitterTracks, OverDragClampsAndStaysZeroSum) {
    Fixture f(GridLength::Star(), GridLength::Star());
    const float pairBefore = f.ColSize(0) + f.ColSize(2);

    f.DragBy({5000.0f});
    f.Relayout(400.0f, 300.0f);  // resolved sizes only refresh on Arrange

    EXPECT_TRUE(f.ColSize(2) >= 0.99f);   // clamped at kMinTrackDip, not collapsed to 0
    EXPECT_TRUE(f.ColSize(2) < 2.0f);     // and it really did travel to the floor
    EXPECT_NEAR(f.ColSize(0) + f.ColSize(2), pairBefore, 0.01f);

    // And back the other way, from a fresh anchor.
    f.DragBy({-5000.0f});
    f.Relayout(400.0f, 300.0f);
    EXPECT_TRUE(f.ColSize(0) >= 0.99f);
    EXPECT_TRUE(f.ColSize(0) < 2.0f);
    EXPECT_NEAR(f.ColSize(0) + f.ColSize(2), pairBefore, 0.01f);
}

// An Auto track is the one case a drag does pin: its size comes from its content, so
// there is no parameter to write a drag into. Converting it to Pixel is what makes
// it draggable at all. Documented, asserted, and deliberately not "fixed".
TEST(GridSplitterTracks, AutoTrackBecomesPixel) {
    Fixture f(GridLength::Auto(), GridLength::Star());

    f.DragBy({25.0f});

    EXPECT_TRUE(f.Col(0).isPixel());
    EXPECT_TRUE(f.Col(2).isStar());
}
