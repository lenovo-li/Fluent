// GridTests.cpp — locks the current Grid measure/arrange behavior.
//
// Pure layout math only (no HWND / D3D). Captures how the Grid resolves
// Pixel / Auto / Star tracks, spacing, spans, and cell placement TODAY, so the
// tree refactor has a regression net.

#include "../framework/Test.h"
#include "LayoutTestHelpers.h"
#include "../../FluentUI/layout/Grid.h"

using namespace fluent;

// Two columns: fixed 64px label + star input. Star column takes the leftover.
TEST(Grid, PixelPlusStar_Columns) {
    Grid g;
    g.SetColumns({GridLength::Pixels(64.0f), GridLength::Star()});
    g.SetRows({GridLength::Star()});

    auto* label = g.Emplace<TestLeaf>();
    g.SetCell(label, 0, 0);
    auto* input = g.Emplace<TestLeaf>();
    g.SetCell(input, 0, 1);

    g.UpdateLayout({0.0f, 0.0f, 200.0f, 40.0f});

    // Column 0 is 64 wide at x=0; column 1 fills the rest (200-64=136) at x=64.
    EXPECT_NEAR(label->Bounds().x, 0.0f, 0.01f);
    EXPECT_NEAR(label->Bounds().w, 64.0f, 0.01f);
    EXPECT_NEAR(input->Bounds().x, 64.0f, 0.01f);
    EXPECT_NEAR(input->Bounds().w, 136.0f, 0.01f);
}

// Column spacing sits between adjacent tracks and shrinks star leftover.
TEST(Grid, ColumnSpacing_ReducesStar) {
    Grid g;
    g.SetColumns({GridLength::Pixels(50.0f), GridLength::Star()});
    g.SetRows({GridLength::Star()});
    g.SetColumnSpacing(10.0f);

    auto* a = g.Emplace<TestLeaf>();
    g.SetCell(a, 0, 0);
    auto* b = g.Emplace<TestLeaf>();
    g.SetCell(b, 0, 1);

    g.UpdateLayout({0.0f, 0.0f, 200.0f, 40.0f});

    // used = 50 + 10 spacing; star = 200 - 60 = 140. b starts at 50+10=60.
    EXPECT_NEAR(a->Bounds().w, 50.0f, 0.01f);
    EXPECT_NEAR(b->Bounds().x, 60.0f, 0.01f);
    EXPECT_NEAR(b->Bounds().w, 140.0f, 0.01f);
}

// Two equal-weight star columns split space evenly.
TEST(Grid, TwoStarColumns_SplitEvenly) {
    Grid g;
    g.SetColumns({GridLength::Star(), GridLength::Star()});
    g.SetRows({GridLength::Star()});

    auto* a = g.Emplace<TestLeaf>();
    g.SetCell(a, 0, 0);
    auto* b = g.Emplace<TestLeaf>();
    g.SetCell(b, 0, 1);

    g.UpdateLayout({0.0f, 0.0f, 300.0f, 40.0f});

    EXPECT_NEAR(a->Bounds().w, 150.0f, 0.01f);
    EXPECT_NEAR(b->Bounds().x, 150.0f, 0.01f);
    EXPECT_NEAR(b->Bounds().w, 150.0f, 0.01f);
}

// Weighted stars split in proportion (1:2 -> 100 / 200 of 300).
TEST(Grid, WeightedStars_Proportional) {
    Grid g;
    g.SetColumns({GridLength::Star(1.0f), GridLength::Star(2.0f)});
    g.SetRows({GridLength::Star()});

    auto* a = g.Emplace<TestLeaf>();
    g.SetCell(a, 0, 0);
    auto* b = g.Emplace<TestLeaf>();
    g.SetCell(b, 0, 1);

    g.UpdateLayout({0.0f, 0.0f, 300.0f, 40.0f});

    EXPECT_NEAR(a->Bounds().w, 100.0f, 0.01f);
    EXPECT_NEAR(b->Bounds().w, 200.0f, 0.01f);
}

// An Auto row sizes to the tallest child in it.
TEST(Grid, AutoRow_SizesToContent) {
    Grid g;
    g.SetColumns({GridLength::Star()});
    g.SetRows({GridLength::Auto(), GridLength::Star()});

    auto* top = g.Emplace<TestLeaf>();
    top->SetHeight(28.0f);
    top->SetVAlign(VAlign::Top);
    g.SetCell(top, 0, 0);
    auto* fill = g.Emplace<TestLeaf>();
    g.SetCell(fill, 1, 0);

    g.UpdateLayout({0.0f, 0.0f, 100.0f, 200.0f});

    // Auto row = 28; star row fills the rest (172) at y=28.
    EXPECT_NEAR(top->Bounds().h, 28.0f, 0.01f);
    EXPECT_NEAR(fill->Bounds().y, 28.0f, 0.01f);
    EXPECT_NEAR(fill->Bounds().h, 172.0f, 0.01f);
}

// Column span: a child spanning two columns covers both tracks + interior gap.
TEST(Grid, ColumnSpan_CoversTracksAndGap) {
    Grid g;
    g.SetColumns({GridLength::Pixels(50.0f), GridLength::Pixels(50.0f)});
    g.SetRows({GridLength::Star()});
    g.SetColumnSpacing(10.0f);

    auto* wide = g.Emplace<TestLeaf>();
    g.SetCell(wide, /*row*/ 0, /*col*/ 0, /*rowSpan*/ 1, /*colSpan*/ 2);

    g.UpdateLayout({0.0f, 0.0f, 200.0f, 40.0f});

    // 50 + 10 gap + 50 = 110, starting at x=0.
    EXPECT_NEAR(wide->Bounds().x, 0.0f, 0.01f);
    EXPECT_NEAR(wide->Bounds().w, 110.0f, 0.01f);
}

// No tracks defined: the grid degenerates to a single cell filling its bounds.
TEST(Grid, NoTracks_SingleCellFills) {
    Grid g;

    auto* only = g.Emplace<TestLeaf>();
    g.SetCell(only, 0, 0);

    g.UpdateLayout({5.0f, 7.0f, 120.0f, 80.0f});

    EXPECT_NEAR(only->Bounds().x, 5.0f, 0.01f);
    EXPECT_NEAR(only->Bounds().y, 7.0f, 0.01f);
    EXPECT_NEAR(only->Bounds().w, 120.0f, 0.01f);
    EXPECT_NEAR(only->Bounds().h, 80.0f, 0.01f);
}

// Regression: Auto-height children measured at infinite availH must not report
// inf as their desired size. That poisons track resolution (inf - inf = NaN),
// and the Grid collapses rows to garbage heights. The symptom: cards inside a
// ScrollPanel (which deliberately measures unbounded) rendered at 46~70 DIP with
// overlapping, clipped content. Root cause: FrameworkElement::Measure's default
// path returned availH when Auto, and controls without a Measure override
// (Button, TextBox) inherited that infinite desired size.
TEST(Grid, AutoRowsWithUnboundedHeight_DoNotCollapse) {
    Grid g;
    g.SetColumns({GridLength::Auto()});
    g.SetRows({GridLength::Auto(), GridLength::Auto()});

    // Default-measure leaves: no explicit height, no Measure override, so they
    // hit FrameworkElement::Measure's "fill what's offered" path. Under infinite
    // availH that used to return inf, which summed to inf, then leftover became
    // inf - inf = NaN, and rows resolved to ~46-70 instead of 0.
    auto* a = g.Emplace<TestLeaf>();
    g.SetCell(a, 0, 0);
    auto* b = g.Emplace<TestLeaf>();
    g.SetCell(b, 1, 0);

    // Measure at unbounded height (the ScrollPanel scenario).
    g.Measure(200.0f, std::numeric_limits<float>::infinity());
    const SizeDip& desired = g.Desired();

    // Both leaves have no content and no explicit size, so desired.h should be 0
    // (two rows of 0 height), NOT inf, NOT NaN, NOT a small garbage value.
    EXPECT_TRUE(std::isfinite(desired.h));
    EXPECT_NEAR(desired.h, 0.0f, 0.01f);
}

// Regression: a Star row measured at infinite availH (a Grid inside a ScrollPanel)
// got leftover = inf - used = inf, so the grid's desired height was infinite: the
// ScrollPanel's extent became infinite (tiny thumb, endless empty scroll) and the
// card owning the grid drew nothing. On an unbounded axis Star sizes to content.
TEST(Grid, StarRowWithUnboundedHeight_SizesToContent) {
    Grid g;
    g.SetColumns({GridLength::Auto(), GridLength::Star()});
    g.SetRows({GridLength::Star()});

    auto* tall = g.Emplace<TestLeaf>(20.0f, 44.0f);
    g.SetCell(tall, 0, 0);
    auto* shortLeaf = g.Emplace<TestLeaf>(50.0f, 30.0f);
    g.SetCell(shortLeaf, 0, 1);

    g.Measure(300.0f, std::numeric_limits<float>::infinity());

    EXPECT_TRUE(std::isfinite(g.Desired().h));
    EXPECT_NEAR(g.Desired().h, 44.0f, 0.01f);  // the tallest child in the row
}

// Two Star rows on an unbounded axis: each takes its own content height, plus
// the row spacing between them. Weights do not apply without a finite extent.
TEST(Grid, StarRowsWithUnboundedHeight_StackContent) {
    Grid g;
    g.SetColumns({GridLength::Star()});
    g.SetRows({GridLength::Star(1.0f), GridLength::Star(3.0f)});
    g.SetRowSpacing(8.0f);

    auto* a = g.Emplace<TestLeaf>(10.0f, 20.0f);
    g.SetCell(a, 0, 0);
    auto* b = g.Emplace<TestLeaf>(10.0f, 30.0f);
    g.SetCell(b, 1, 0);

    g.Measure(100.0f, std::numeric_limits<float>::infinity());

    EXPECT_NEAR(g.Desired().h, 58.0f, 0.01f);  // 20 + 8 + 30
}

// The unbounded rule is measure-only: arranged into a finite rect, Star rows still
// share the leftover by weight (the Auto row keeps its content height).
TEST(Grid, StarRow_StillFillsFiniteArrange) {
    Grid g;
    g.SetColumns({GridLength::Star()});
    g.SetRows({GridLength::Auto(), GridLength::Star()});

    auto* top = g.Emplace<TestLeaf>();
    top->SetHeight(28.0f);
    top->SetVAlign(VAlign::Top);
    g.SetCell(top, 0, 0);
    auto* fill = g.Emplace<TestLeaf>();
    g.SetCell(fill, 1, 0);

    g.Measure(100.0f, std::numeric_limits<float>::infinity());
    g.Arrange({0.0f, 0.0f, 100.0f, 200.0f});

    EXPECT_NEAR(fill->Bounds().y, 28.0f, 0.01f);
    EXPECT_NEAR(fill->Bounds().h, 172.0f, 0.01f);
}

// Same rule on the other axis: a Star column measured at infinite availW (a Grid
// in a horizontal StackPanel) reports its content width, not infinity.
TEST(Grid, StarColumnWithUnboundedWidth_SizesToContent) {
    Grid g;
    g.SetColumns({GridLength::Pixels(40.0f), GridLength::Star()});
    g.SetRows({GridLength::Auto()});

    auto* fixed = g.Emplace<TestLeaf>(40.0f, 10.0f);
    g.SetCell(fixed, 0, 0);
    auto* content = g.Emplace<TestLeaf>(70.0f, 10.0f);
    g.SetCell(content, 0, 1);

    g.Measure(std::numeric_limits<float>::infinity(), 100.0f);

    EXPECT_TRUE(std::isfinite(g.Desired().w));
    EXPECT_NEAR(g.Desired().w, 110.0f, 0.01f);  // 40 + 70
}

// Regression: All Star tracks with 0* weight caused them to get zero space,
// making content disappear.
TEST(Grid, AllZeroStarWeights_DistributeEqually) {
    Grid g;
    g.SetColumns({GridLength::Star(0.0f), GridLength::Star(0.0f)});
    g.SetRows({GridLength::Star()});
    
    auto* left = g.Emplace<TestLeaf>();
    g.SetCell(left, 0, 0);
    auto* right = g.Emplace<TestLeaf>();
    g.SetCell(right, 0, 1);
    
    g.UpdateLayout({0, 0, 200, 40});
    
    // Before fix: both columns got 0 width (starWeight sum = 0, leftover not distributed)
    // After fix: leftover distributed equally -> each column gets 100 DIP
    EXPECT_NEAR(left->Bounds().w, 100.0f, 0.1f);
    EXPECT_NEAR(right->Bounds().w, 100.0f, 0.1f);
}

// Removing a child must drop its cell entry. cells_ is keyed by address, and with
// no erase a removed child's (row, col) outlived it: a Clear()+rebuild refresh made
// the map grow forever, and a new child allocated at the recycled address inherited
// the old cell instead of (0,0). ColumnOf/RowOf only look the address up (they never
// dereference it), so querying the dead pointer is how the stale entry is observed
// without depending on when the allocator happens to reuse an address.
TEST(Grid, RemovedChildCellEntryIsDropped) {
    Grid g;
    g.SetColumns({GridLength::Star(), GridLength::Star(), GridLength::Star()});
    g.SetRows({GridLength::Star(), GridLength::Star()});

    auto* keep = g.Emplace<TestLeaf>();
    g.SetCell(keep, 1, 1);
    auto* gone = g.Emplace<TestLeaf>();
    g.SetCell(gone, 1, 2);
    EXPECT_EQ(g.ColumnOf(gone), 2);

    g.RemoveAt(1);                        // destroys `gone`
    EXPECT_EQ(g.ColumnOf(gone), -1);      // its entry went with it
    EXPECT_EQ(g.RowOf(gone), -1);
    EXPECT_EQ(g.ColumnOf(keep), 1);       // the survivor keeps its cell

    g.Clear();                            // destroys `keep`
    EXPECT_EQ(g.ColumnOf(keep), -1);
}
