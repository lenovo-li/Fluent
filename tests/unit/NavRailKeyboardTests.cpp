// NavRailKeyboardTests.cpp — headless tests for extensions/NavigationControls
// NavRail keyboard navigation in rail (non-pane) mode.
//
// Rail mode used to step the keyboard cursor by one item, so Up/Down parked it on
// a disabled item: the focus ring sat there and Enter did nothing, because
// UserSelect refuses an item that is not Selectable. Pane mode already skipped
// disabled items through PaneKeyStops. The cursor itself is private, so these
// tests observe it the way a user does: move, press Enter, read the selection.

#include "../framework/Test.h"
#include "../../extensions/NavigationControls/NavRail.h"
#include <windows.h>

using namespace fluent;

namespace {

// OnKeyDownRouted is protected (the input manager routes keys); expose it.
struct RailPeer : NavRail {
    void Key(UINT vk) {
        KeyEventArgs e;
        e.vk = vk;
        OnKeyDownRouted(e);
    }
};

// Three items, the middle one disabled; item 0 selected (the cursor starts there).
void Setup(RailPeer& rail) {
    rail.SetItems({{L"\xE80F", L"Home"}, {L"\xE896", L"Updates"}, {L"\xE897", L"About"}});
    rail.SetItemEnabled(1, false);
    rail.SetSelectedIndex(0);
}

}  // namespace

TEST(NavRailKeyboard, DownSkipsADisabledItem) {
    RailPeer rail;
    Setup(rail);
    rail.Key(VK_DOWN);
    rail.Key(VK_RETURN);
    EXPECT_EQ(rail.SelectedIndex(), 2);
}

TEST(NavRailKeyboard, UpSkipsADisabledItem) {
    RailPeer rail;
    Setup(rail);
    rail.SetSelectedIndex(2);
    rail.Key(VK_UP);
    rail.Key(VK_RETURN);
    EXPECT_EQ(rail.SelectedIndex(), 0);
}

TEST(NavRailKeyboard, CursorStaysWhenOnlyDisabledItemsLieBeyond) {
    // [Home, Updates, About(disabled)]: from Updates, Down has no selectable item
    // to reach, so the cursor stays on Updates and Enter selects it. Stepping onto
    // the disabled About would make Enter a no-op and leave Home selected.
    RailPeer rail;
    rail.SetItems({{L"\xE80F", L"Home"}, {L"\xE896", L"Updates"}, {L"\xE897", L"About"}});
    rail.SetItemEnabled(2, false);
    rail.SetSelectedIndex(0);
    rail.Key(VK_DOWN);  // -> Updates
    rail.Key(VK_DOWN);  // nothing selectable below: stay
    rail.Key(VK_RETURN);
    EXPECT_EQ(rail.SelectedIndex(), 1);
}
