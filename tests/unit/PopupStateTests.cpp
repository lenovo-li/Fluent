// PopupStateTests.cpp — pure-logic tests for the popup state machine rules
// (services/PopupState.h). No HWND / DComp: exercises the CanOpen / CanClose / IsOpenState
// decision functions that PopupHost delegates its transitions to.

#include "../framework/Test.h"
#include "../../FluentUI/services/PopupState.h"
#include "../../FluentUI/services/PopupHost.h"  // PopupRenderCoalescer

using namespace fluent;

// Only a Closed popup may open.
TEST(PopupState, OnlyClosedCanOpen) {
    EXPECT_TRUE(CanOpen(PopupState::Closed));
    EXPECT_FALSE(CanOpen(PopupState::Opening));
    EXPECT_FALSE(CanOpen(PopupState::Open));
    EXPECT_FALSE(CanOpen(PopupState::Closing));  // re-open during close rejected
}

// Only a fully-Open popup begins closing (idempotent elsewhere).
TEST(PopupState, OnlyOpenCanClose) {
    EXPECT_FALSE(CanClose(PopupState::Closed));   // already closed: no-op
    EXPECT_FALSE(CanClose(PopupState::Opening));  // not yet open: no-op
    EXPECT_TRUE(CanClose(PopupState::Open));
    EXPECT_FALSE(CanClose(PopupState::Closing));  // already closing: no-op
}

// IsOpen for external callers is true only in the steady Open state.
TEST(PopupState, IsOpenOnlyInOpenState) {
    EXPECT_FALSE(IsOpenState(PopupState::Closed));
    EXPECT_FALSE(IsOpenState(PopupState::Opening));
    EXPECT_TRUE(IsOpenState(PopupState::Open));
    EXPECT_FALSE(IsOpenState(PopupState::Closing));
}

// The intended happy-path cycle Closed -> Opening -> Open -> Closing -> Closed
// is expressible and each guard agrees at each step.
TEST(PopupState, HappyPathCycleGuards) {
    PopupState s = PopupState::Closed;
    EXPECT_TRUE(CanOpen(s));

    s = PopupState::Opening;    // Open() in progress
    EXPECT_FALSE(CanOpen(s));   // no double-open
    EXPECT_FALSE(CanClose(s));  // not yet closable

    s = PopupState::Open;       // settled open
    EXPECT_TRUE(IsOpenState(s));
    EXPECT_TRUE(CanClose(s));

    s = PopupState::Closing;    // Close() side effects running
    EXPECT_FALSE(CanClose(s));  // reentrant Close is a no-op
    EXPECT_FALSE(CanOpen(s));   // reentrant Open rejected

    s = PopupState::Closed;     // settled closed
    EXPECT_TRUE(CanOpen(s));
}

// --- Popup repaint coalescing ----------------------------------------------------
//
// The popup's invalidate callbacks used to Render() synchronously on every call: a
// mouse move over a ComboBox list repainted the whole popup once per WM_MOUSEMOVE,
// twice when the hovered row changed. Now only the first request posts a render.

// A burst of invalidations (InputManager after the move, the list on a row change,
// a scrollbar fade tick) costs one posted render.
TEST(PopupRenderCoalescer, BurstOfRequestsPostsOnce) {
    PopupRenderCoalescer c;
    EXPECT_TRUE(c.Request());   // first: post the message
    EXPECT_FALSE(c.Request());  // absorbed
    EXPECT_FALSE(c.Request());  // absorbed
    EXPECT_TRUE(c.Take());      // the message renders once
    EXPECT_FALSE(c.Pending());
}

// After the deferred render ran, the next invalidation posts again: coalescing never
// swallows a later change.
TEST(PopupRenderCoalescer, RequestAfterRenderPostsAgain) {
    PopupRenderCoalescer c;
    EXPECT_TRUE(c.Request());
    EXPECT_TRUE(c.Take());
    EXPECT_TRUE(c.Request());
}

// A synchronous Render() (Open, resize) overtakes a queued one; the queued message
// then has nothing left to do and must not paint a second time.
TEST(PopupRenderCoalescer, SynchronousRenderCoversQueuedRequest) {
    PopupRenderCoalescer c;
    EXPECT_TRUE(c.Request());
    c.Covered();
    EXPECT_FALSE(c.Take());
    EXPECT_TRUE(c.Request());  // and the next request posts normally
}
