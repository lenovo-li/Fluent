// InheritanceExamplesTest.cpp — the inheritance examples must compile AND behave.
//
// tests/unit/InheritanceExamples.h is the code docs/controls/inheritance-guide.md points
// readers at. A doc example nobody compiles rots (see the docs audit: every
// from-memory example in docs/controls was broken), so this file is what keeps it
// honest: if a protected hook is renamed or its signature changes, the build breaks
// here instead of in a user's project.
//
// Headless: no DrawingContext, so paint output is not asserted. What IS asserted is
// the part a subclass relies on without a device — geometry helpers agreeing with the
// hit-test, the Measure override taking effect, and CoerceValue/IsDragging semantics.

#include "../framework/Test.h"
#include "InheritanceExamples.h"

using namespace fluent;

namespace {
// Peers reach protected members through a subclass (protected access is granted via
// `this` of a derived type, never through a base pointer).
struct LogSliderPeer : LogSlider {
    float Coerce(float v) const { return CoerceValue(v); }
    bool Dragging() const { return IsDragging(); }
    float Pos(float v) const { return ValueToTrackPos(v); }
    float Start() const { return TrackStart(); }
    float End() const { return TrackEnd(); }
};

struct CalendarPeer : Calendar {
    RectDip Prev() const { return PrevArrowRect(); }
    RectDip Next() const { return NextArrowRect(); }
    RectDip Nav() const { return NavBarRect(); }
    RectDip Header() const { return WeekdayHeaderRect(); }
    float CellW() const { return CellWidth(); }
};

struct BigButtonPeer : BigButton {
    Button::Kind KindNow() const { return CurrentKind(); }
};
}  // namespace

TEST(InheritanceExamples, LunarCalendarIsACalendar) {
    LunarCalendar cal;
    cal.SetSelectedDate(2026, 9, 26);
    EXPECT_EQ(2026, cal.SelectedYear());
    EXPECT_EQ(9, cal.SelectedMonth());
    EXPECT_EQ(26, cal.SelectedDay());
}

// The arrow rects RenderNavBar paints into are the same rects OnPointerPressed tests.
// A click at the centre of NextArrowRect must advance the month.
TEST(InheritanceExamples, CalendarArrowRectsDriveNavigation) {
    CalendarPeer cal;
    cal.SetViewedMonth(2026, 9);
    cal.Arrange(RectDip{10, 20, 252, 252});

    RectDip next = cal.Next();
    PointerEventArgs e;
    e.button = PointerButton::Left;
    e.position = Point{next.x + next.w * 0.5f, next.y + next.h * 0.5f};
    cal.OnPointerPressed(e);
    EXPECT_TRUE(e.handled);
    EXPECT_EQ(10, cal.ViewedMonth());

    RectDip prev = cal.Prev();
    PointerEventArgs e2;
    e2.button = PointerButton::Left;
    e2.position = Point{prev.x + 1.0f, prev.y + 1.0f};
    cal.OnPointerPressed(e2);
    EXPECT_EQ(9, cal.ViewedMonth());
}

// Geometry helpers tile the control: nav bar, then header, then the cell grid.
TEST(InheritanceExamples, CalendarRegionsTileTheBounds) {
    CalendarPeer cal;
    cal.Arrange(RectDip{0, 0, 350, 300});
    RectDip nav = cal.Nav();
    RectDip hdr = cal.Header();
    EXPECT_NEAR(nav.bottom(), hdr.y, 0.01f);
    EXPECT_NEAR(hdr.bottom(), cal.CellRect(0).y, 0.01f);
    EXPECT_NEAR(350.0f, cal.CellRect(6).right(), 0.01f);  // grid spans full width
    EXPECT_NEAR(50.0f, cal.CellW(), 0.01f);
}

TEST(InheritanceExamples, BigButtonMeasureOverrideTakesEffect) {
    BigButtonPeer btn;
    btn.SetText(L"Main Title");
    btn.SetSubtitle(L"Subtitle");
    btn.Measure(500.0f, 500.0f);  // public, as a parent Panel would call it
    EXPECT_NEAR(64.0f, btn.Desired().h, 0.01f);
    EXPECT_NEAR(200.0f, btn.Desired().w, 0.01f);  // capped
    btn.SetKind(Button::Kind::Accent);
    EXPECT_TRUE(btn.KindNow() == Button::Kind::Accent);
}

TEST(InheritanceExamples, LogSliderSnapsToPowersOfTen) {
    LogSliderPeer s;
    EXPECT_NEAR(1.0f, s.Coerce(0.5f), 0.01f);
    EXPECT_NEAR(1.0f, s.Coerce(2.0f), 0.01f);
    EXPECT_NEAR(10.0f, s.Coerce(9.0f), 0.01f);
    EXPECT_NEAR(100.0f, s.Coerce(150.0f), 0.01f);
    EXPECT_NEAR(10000.0f, s.Coerce(50000.0f), 0.01f);
    s.SetValue(420.0f);
    // log10(420) = 2.623, rounds to 3 → 1000
    EXPECT_NEAR(1000.0f, s.Value(), 0.01f);  // SetValue goes through CoerceValue
    EXPECT_FALSE(s.Dragging());
}

// The example's preview/commit contract: drag steps preview only, and the release of a
// drag that moved the value commits exactly once. The old example committed from an
// IsDragging() else-branch in OnValueChanged, which a drag's release never reaches,
// so a dragged value was never saved; this test fails on that version.
TEST(InheritanceExamples, LogSliderCommitsADragOnRelease) {
    LogSliderPeer s;
    s.SetWidth(300.0f);
    s.SetHeight(32.0f);
    s.Measure(300.0f, 32.0f);
    s.Arrange(RectDip{0, 0, 300, 32});
    const int commitsBefore = s.CommitCount();

    PointerEventArgs press;
    press.position = Point{s.Start() + 1.0f, 16.0f};
    press.button = PointerButton::Left;
    s.OnPointerPressed(press);
    PointerEventArgs move;
    move.position = Point{s.End(), 16.0f};          // drag to the far end: 10000
    s.OnPointerMoved(move);
    EXPECT_TRUE(s.PreviewCount() > 0);
    EXPECT_EQ(s.CommitCount(), commitsBefore);      // mid-drag: preview only

    PointerEventArgs release = move;
    release.button = PointerButton::Left;
    s.OnPointerReleased(release);
    EXPECT_EQ(s.CommitCount(), commitsBefore + 1);
    EXPECT_NEAR(10000.0f, s.LastCommitted(), 0.01f);

    // A programmatic set commits too.
    s.SetValue(10.0f);
    EXPECT_EQ(s.CommitCount(), commitsBefore + 2);
    EXPECT_NEAR(10.0f, s.LastCommitted(), 0.01f);
}

// ValueToTrackPos is the inverse of the pointer mapping: min lands on TrackStart,
// max on TrackEnd. Tick marks drawn there line up with where a click lands.
TEST(InheritanceExamples, SliderValueToTrackPosMatchesTrackEnds) {
    LogSliderPeer s;
    s.Arrange(RectDip{0, 0, 300, 32});
    EXPECT_NEAR(s.Start(), s.Pos(s.Minimum()), 0.01f);
    EXPECT_NEAR(s.End(), s.Pos(s.Maximum()), 0.01f);
}
