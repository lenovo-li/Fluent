// TrayIconTests.cpp — unit tests for the pure helpers behind Window's tray icon
// support (window/TrayIcon.h).
//
// SCOPE, AND WHY IT IS THIS NARROW. Shell_NotifyIconW needs a live HWND and a
// running shell, so TrayIcon's add/modify/delete path is not reachable headless —
// per .claude/rules/tests.md, that is stated rather than faked. What IS testable is
// the two pure functions, and both exist because the obvious inline spelling of
// them is wrong in a way no build error catches:
//
//   * DecodeTrayAction — the callback's lParam is a mouse message, and the mapping
//     has one non-obvious entry (WM_CONTEXTMENU for the keyboard-summoned menu).
//   * CopyBounded — NOTIFYICONDATAW's text fields are fixed arrays and wcscpy_s
//     ABORTS on overflow instead of truncating. A tooltip built from a file path is
//     unbounded, so the reflexive spelling turns a long name into a crash.
//
// What needs hardware verification: the icon actually appearing, balloon display,
// the Explorer-restart re-add (HandleTaskbarCreated), and menu placement.

#include "../framework/Test.h"
#include "../../FluentUI/window/TrayIcon.h"
// For WM_FLUENT_MODAL_TICK — the collision test compares the two private messages
// directly rather than restating either constant, so it keeps working if one moves.
#include "../../FluentUI/window/ModalFrameHeartbeat.h"

#include <string>

using namespace fluent;

// --- DecodeTrayAction ---------------------------------------------------------

TEST(TrayIcon, DecodesLeftButtonMessages) {
    EXPECT_TRUE(DecodeTrayAction(WM_LBUTTONUP) == TrayAction::Click);
    EXPECT_TRUE(DecodeTrayAction(WM_LBUTTONDBLCLK) == TrayAction::DoubleClick);
}

TEST(TrayIcon, DecodesRightAndMiddleButtonMessages) {
    EXPECT_TRUE(DecodeTrayAction(WM_RBUTTONUP) == TrayAction::RightClick);
    EXPECT_TRUE(DecodeTrayAction(WM_MBUTTONUP) == TrayAction::MiddleClick);
}

TEST(TrayIcon, KeyboardSummonedMenuDecodesAsRightClick) {
    // The shell sends WM_CONTEXTMENU instead of WM_RBUTTONUP when the menu is
    // summoned from the keyboard (Shift+F10 with the icon focused). An app that
    // only opens its menu on RightClick would be unreachable without a mouse, so
    // this mapping is an accessibility requirement, not a convenience.
    EXPECT_TRUE(DecodeTrayAction(WM_CONTEXTMENU) == TrayAction::RightClick);
}

TEST(TrayIcon, UnsurfacedCallbacksDecodeAsNone) {
    // The shell sends far more than clicks. Mouse-move fires continuously while the
    // cursor crosses the notification area, and the balloon lifecycle messages
    // arrive whether or not anyone is watching — surfacing either as an event would
    // mean a handler firing dozens of times for no user action.
    EXPECT_TRUE(DecodeTrayAction(WM_MOUSEMOVE) == TrayAction::None);
    EXPECT_TRUE(DecodeTrayAction(WM_LBUTTONDOWN) == TrayAction::None);
    EXPECT_TRUE(DecodeTrayAction(WM_RBUTTONDOWN) == TrayAction::None);
    EXPECT_TRUE(DecodeTrayAction(NIN_BALLOONSHOW) == TrayAction::None);
    EXPECT_TRUE(DecodeTrayAction(NIN_BALLOONUSERCLICK) == TrayAction::None);
    EXPECT_TRUE(DecodeTrayAction(0) == TrayAction::None);
}

// --- CopyBounded -------------------------------------------------------------

TEST(TrayIcon, CopyBoundedCopiesShortStringWhole) {
    wchar_t buf[16] = {};
    const size_t n = CopyBounded(buf, 16, L"tray");
    EXPECT_EQ(n, size_t(4));
    EXPECT_TRUE(std::wstring(buf) == L"tray");
}

TEST(TrayIcon, CopyBoundedTruncatesInsteadOfOverflowing) {
    // The case that would abort the process with wcscpy_s. 8 chars of capacity means
    // 7 characters plus the terminator.
    wchar_t buf[8] = {};
    const size_t n = CopyBounded(buf, 8, L"0123456789abcdef");
    EXPECT_EQ(n, size_t(7));
    EXPECT_TRUE(std::wstring(buf) == L"0123456");
    EXPECT_TRUE(buf[7] == L'\0');
}

TEST(TrayIcon, CopyBoundedTerminatesAtExactCapacity) {
    // A source that fills the buffer exactly, leaving room for only the terminator —
    // the boundary an off-by-one would land on.
    wchar_t buf[5] = {};
    const size_t n = CopyBounded(buf, 5, L"abcd");
    EXPECT_EQ(n, size_t(4));
    EXPECT_TRUE(std::wstring(buf) == L"abcd");
    EXPECT_TRUE(buf[4] == L'\0');
}

TEST(TrayIcon, CopyBoundedHandlesEmptyAndNullSource) {
    wchar_t buf[8] = {L'x', L'x', L'x', L'x', L'x', L'x', L'x', L'x'};
    EXPECT_EQ(CopyBounded(buf, 8, L""), size_t(0));
    EXPECT_TRUE(buf[0] == L'\0');

    buf[0] = L'x';
    EXPECT_EQ(CopyBounded(buf, 8, nullptr), size_t(0));
    EXPECT_TRUE(buf[0] == L'\0');
}

TEST(TrayIcon, CopyBoundedRejectsZeroCapacityWithoutWriting) {
    // Capacity 0 has nowhere to put even a terminator, so the only safe action is to
    // write nothing at all. Writing dst[0] = 0 here would be a one-byte overflow.
    wchar_t sentinel = L'k';
    EXPECT_EQ(CopyBounded(&sentinel, 0, L"abc"), size_t(0));
    EXPECT_TRUE(sentinel == L'k');
    EXPECT_EQ(CopyBounded(nullptr, 8, L"abc"), size_t(0));
}

TEST(TrayIcon, CopyBoundedFitsRealNotifyIconFieldSizes) {
    // The actual reason this helper exists: a tooltip or balloon body assembled from
    // a path or a log line is not bounded by anything the app controls. These are
    // the three real field capacities.
    NOTIFYICONDATAW data{};
    const std::wstring huge(1024, L'z');

    const size_t tip = CopyBounded(data.szTip, ARRAYSIZE(data.szTip), huge.c_str());
    EXPECT_EQ(tip, ARRAYSIZE(data.szTip) - 1);

    const size_t info = CopyBounded(data.szInfo, ARRAYSIZE(data.szInfo), huge.c_str());
    EXPECT_EQ(info, ARRAYSIZE(data.szInfo) - 1);

    const size_t title =
        CopyBounded(data.szInfoTitle, ARRAYSIZE(data.szInfoTitle), huge.c_str());
    EXPECT_EQ(title, ARRAYSIZE(data.szInfoTitle) - 1);
}

// --- Callback message identity ------------------------------------------------

TEST(TrayIcon, CallbackMessageDoesNotCollideWithTheModalTick) {
    // Both are WM_APP-relative private messages delivered to the SAME window, so a
    // collision would route tray clicks into the frame heartbeat (or vice versa) —
    // a bug that would present as random repaints rather than anything tray-shaped.
    // ModalFrameHeartbeat's tick is WM_APP+17; this pins that they stay distinct.
    EXPECT_TRUE(WM_FLUENT_TRAY_CALLBACK != WM_FLUENT_MODAL_TICK);
    EXPECT_TRUE(WM_FLUENT_TRAY_CALLBACK >= WM_APP);
}
