// PopupDismissDispatchTests.cpp — NativeWindowHost::InvokeOwnedHook, the slot logic
// every host call site uses to run the active popup's dismiss hook.
//
// Pure: a std::function slot plus its generation counter, driven exactly the way
// RegisterActivePopupDismiss and its Subscription drive them. No window.

#include "../framework/Test.h"
#include "../../FluentUI/window/NativeWindowHost.h"

#include <functional>

using namespace fluent;

namespace {
using Hook = std::function<bool(int)>;

// Mirrors RegisterActivePopupDismiss: store, bump, return the token the
// Subscription would compare against.
unsigned Register(Hook& slot, unsigned& gen, Hook h) {
    slot = std::move(h);
    return ++gen;
}
// Mirrors the Subscription's release lambda.
void Unregister(Hook& slot, unsigned& gen, unsigned token) {
    if (gen == token) { slot = nullptr; ++gen; }
}
}  // namespace

// The case the reorder of PopupHost::Close made reachable: the dismiss hook closes
// the popup, the closed handler REOPENS it (registering a new hook), and the hook
// returns true. The new hook must survive — the old in-place call cleared the slot
// unconditionally on `true`, so the reopened popup could not be light-dismissed.
TEST(PopupDismiss, HookThatReopensKeepsTheNewHook) {
    Hook slot;
    unsigned gen = 0;
    unsigned token = 0;
    int secondCalls = 0;
    token = Register(slot, gen, [&](int) {
        Unregister(slot, gen, token);  // control resets its Subscription on close
        Register(slot, gen, [&](int) { ++secondCalls; return true; });  // reopened
        return true;
    });

    EXPECT_TRUE(NativeWindowHost::InvokeOwnedHook(slot, gen, 0));
    EXPECT_TRUE(static_cast<bool>(slot));      // the reopened popup's hook
    EXPECT_TRUE(NativeWindowHost::InvokeOwnedHook(slot, gen, 0));
    EXPECT_EQ(secondCalls, 1);
}

// A hook that handles the event and registers nothing leaves the slot empty, and the
// stale Subscription released afterwards cannot clear a hook registered later.
TEST(PopupDismiss, HandledHookClearsSlotAndRetiresItsToken) {
    Hook slot;
    unsigned gen = 0;
    const unsigned token = Register(slot, gen, [](int) { return true; });
    EXPECT_TRUE(NativeWindowHost::InvokeOwnedHook(slot, gen, 0));
    EXPECT_FALSE(static_cast<bool>(slot));

    Register(slot, gen, [](int) { return true; });
    Unregister(slot, gen, token);              // stale release
    EXPECT_TRUE(static_cast<bool>(slot));
}

// A hook that declines (the click landed inside its popup) stays registered.
TEST(PopupDismiss, DecliningHookStaysRegistered) {
    Hook slot;
    unsigned gen = 0;
    Register(slot, gen, [](int) { return false; });
    EXPECT_FALSE(NativeWindowHost::InvokeOwnedHook(slot, gen, 0));
    EXPECT_TRUE(static_cast<bool>(slot));
}
