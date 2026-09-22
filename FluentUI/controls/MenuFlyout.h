// MenuFlyout.h — Fluent popup menu (context menu / dropdown / submenu host).
//
// A MenuFlyout shows a list of MenuItems in a floating card (PopupHost). It is
// the single class behind context menus, command-button dropdowns, and (from
// stage 3) cascading submenus — mirroring WinUI's MenuFlyout, where a
// "context menu" is just one way to open it.
//
// Two entry points:
//   * ShowAt(screenX, screenY)      — context menu anchored at the cursor.
//   * ShowBelow(anchorScreenRect)   — dropdown anchored below a control
//                                     (used by MenuBar and command buttons).
//
// The flyout owns a stack of levels (each level = one PopupHost + one list
// view). Stage 2 opens only the root level; stage 3 pushes submenu levels on
// hover / Right-arrow. The whole chain is dismissed as a unit: clicking outside
// every popup, losing activation to another app, moving the parent window, or
// pressing Esc. The parent window keeps focus, so keys are routed in via
// WindowServices::RegisterActivePopupKeyHandler.
#pragma once

#include "../core/Control.h"
#include "../core/Subscription.h"
#include "../core/IContextMenu.h"
#include "../base/Event.h"
#include "../input/RoutedEvent.h"
#include "../graphics/DWriteContext.h"
#include <string>
#include <vector>
#include <functional>
#include <memory>
#include <optional>

namespace fluent {

class WindowServices;
class PopupHost;

// One row in a menu. A separator is a thin non-interactive divider. A non-empty
// submenu makes this a parent item (drawn with a right chevron); clicking it
// opens the child level instead of invoking. Otherwise Invoked() event fires on click.
//
// Migration note (P1 event model unification): MenuItem was a struct with a public
// std::function<void()> onInvoke field. It is now a class with an Event<> + RAII
// subscription. Old code that set `item.onInvoke = [...]` must change to
// `item.Invoked().Subscribe(...)` and store the returned Subscription.
//
// Temporary compat: SetOnInvoke(std::function<void()>) is provided (deprecated) and
// forwards to the event, so existing code compiles but with a deprecation warning.
// Why the event lives behind a unique_ptr: MenuItem is a VALUE type. Menus are
// authored as `std::vector<MenuItem>`, built with push_back(std::move(item)), and
// nested one vector inside another (`submenu`). Event<> is deliberately
// non-copyable AND non-movable — subscriptions capture its address, so it must
// never relocate. Holding one by value would make MenuItem non-movable too and
// break every `std::vector<MenuItem>` in the codebase.
//
// The indirection fixes that: the vector moves the pointer, the Event stays put at
// its heap address, and outstanding Subscriptions keep pointing at a live object.
// The node is allocated lazily on the first Invoked() call, so a menu of plain
// separators or submenu parents costs nothing extra.
class MenuItem {
public:
    MenuItem() = default;

    // Movable (what vectors need). Copy is deliberately NOT provided: copying an
    // item would have to either share one event (two rows firing each other's
    // handlers) or silently drop the subscriptions. Both are wrong, and neither is
    // needed — menus are built by move.
    MenuItem(MenuItem&&) noexcept = default;
    MenuItem& operator=(MenuItem&&) noexcept = default;
    MenuItem(const MenuItem&) = delete;
    MenuItem& operator=(const MenuItem&) = delete;

    std::wstring text;
    std::wstring accelerator;        // right-aligned hint, e.g. L"Ctrl+C"
    bool separator = false;
    bool enabled = true;
    bool checked = false;            // draws a leading check glyph
    std::vector<MenuItem> submenu;   // non-empty => parent item (chevron)

    bool HasSubmenu() const { return !submenu.empty(); }
    bool Selectable() const { return enabled && !separator; }

    static MenuItem Sep() { MenuItem m; m.separator = true; m.enabled = false; return m; }

    // Fired when this leaf item is chosen (click, or Enter while selected).
    //
    //     Subscription sub_;   // MUST be stored — dropping it unsubscribes
    //     sub_ = item.Invoked().Subscribe(this, [](void* o, MenuItem&, RoutedEventArgs&) {
    //         static_cast<MyDialog*>(o)->OnCut();
    //     });
    //
    // The handler is a captureless function pointer (Event<>'s zero-allocation
    // contract); pass state via the owner pointer. For the common "just run this
    // lambda" case, AddInvokeHandler below owns the subscription for you.
    Event<MenuItem, RoutedEventArgs>& Invoked() {
        if (!invoked_) invoked_ = std::make_unique<Event<MenuItem, RoutedEventArgs>>();
        return *invoked_;
    }

    // Convenience for the authoring style this replaces (`item.onInvoke = [...]`).
    // Takes a capturing callable, keeps it alive inside this item, and subscribes it
    // to Invoked() — so the caller has no Subscription to store. Additive: call it
    // more than once and every callback runs, which is the multi-subscriber property
    // the old single `std::function` field could not offer.
    //
    // This is the one place a heap-allocated closure is accepted, and only because
    // menu authoring happens once per menu build, never per frame.
    void AddInvokeHandler(std::function<void()> cb) {
        if (!cb) return;
        if (!handlers_) handlers_ = std::make_unique<HandlerList>();
        handlers_->list.push_back(std::move(cb));
        // Subscribe once; the thunk walks every registered handler. Subscribing per
        // handler would need one Subscription each for no benefit.
        if (!handlers_->sub.has_value()) {
            handlers_->sub = Invoked().Subscribe(
                handlers_.get(), [](void* owner, MenuItem&, RoutedEventArgs&) {
                    // Copy first: a handler may rebuild the menu (and destroy this
                    // list) while we are iterating it.
                    auto snapshot = static_cast<HandlerList*>(owner)->list;
                    for (auto& fn : snapshot) if (fn) fn();
                });
        }
    }

    // Raise Invoked(). Called by MenuFlyout when the row is chosen; also useful to
    // drive an item programmatically (a keyboard accelerator invoking the same
    // action as its menu row). No-op when nothing is subscribed.
    void RaiseInvoked() {
        if (!invoked_) return;              // nobody subscribed: nothing to raise
        RoutedEventArgs args;               // MenuItem is not a UIElement, so both
        args.source = nullptr;              // source fields stay null.
        args.originalSource = nullptr;
        invoked_->Raise(*this, args);
    }

    bool HasInvokeHandler() const {
        return invoked_ && invoked_->HasSubscribers();
    }

private:
    // Heap node so its address survives MenuItem being moved inside a vector.
    std::unique_ptr<Event<MenuItem, RoutedEventArgs>> invoked_;

    // Storage for AddInvokeHandler's closures, also heap-stable for the same reason:
    // the subscription's owner pointer is this node's address.
    struct HandlerList {
        std::vector<std::function<void()>> list;
        std::optional<Subscription> sub;
    };
    std::unique_ptr<HandlerList> handlers_;
};

class MenuFlyout : public IContextMenu {
public:
    MenuFlyout();
    ~MenuFlyout() override;

    // IContextMenu: a UIElement owning this menu feeds it the tree context on
    // attach. Forwards to SetContext (same service set), so a control's
    // right-click menu is wired with no manual SetContext call from the app.
    void SetOwnerContext(const UIContext& ctx) override { SetContext(ctx); }

    // Provide the tree service context (HINSTANCE/HWND/D2D/DWrite via
    // Context().window + the popup-dismiss/key registries). A MenuFlyout is not a
    // tree node itself (it is owned by a MenuBar or used standalone), so its owner
    // forwards its own Context() here once attached. The flyout attaches each list
    // view to this context, so the list's inherited Dwrite() works at measure time
    // (before PopupHost re-injects its own context on SetContent). Replaces the old
    // manual SetDWrite + Attach(WindowServices*) pair (roadmap §6.2).
    void SetContext(const UIContext& ctx) { ctx_ = ctx; window_ = ctx.window; }

    // Replace the menu items. Closes the menu if open.
    //
    // Takes ownership: the items are moved into the flyout. For callers whose items
    // outlive the flyout (e.g. a MenuBar rebuilding its own stable list on every
    // open), the borrowing overload below avoids the copy/move.
    void SetItems(std::vector<MenuItem> items);

    // Borrow items from a vector the caller owns and keeps stable. The flyout does
    // not copy — it just points at the caller's storage, which must outlive the
    // flyout's open period. MenuBar uses this to reopen the same menu multiple times
    // without copying the vector each time. Close() clears the borrow.
    void SetItemsBorrow(const std::vector<MenuItem>* items);


    // Open as a context menu with the top-left corner at the cursor (flips /
    // clamps to the work area). screenX/screenY are physical screen pixels.
    // Overrides IContextMenu::ShowAt.
    void ShowAt(int screenX, int screenY) override;

    // Open as a dropdown directly below anchorScreenRect (physical pixels),
    // left-aligned with it; flips above if it would overflow the work area.
    void ShowBelow(const RECT& anchorScreenRect);

    // Close the entire chain.
    void Close();
    bool IsOpen() const;

    // Fired after the whole chain closes (from any dismiss path).
    //
    // Prefer subscribing to this event over SetOnClosed. The callback API is kept for
    // compatibility but will be removed in a future version.
    Event<MenuFlyout, RoutedEventArgs>& Closed() { return closed_; }

    [[deprecated("Use Closed() event instead")]]
    void SetOnClosed(std::function<void()> cb) { onClosed_ = std::move(cb); }

    // Owner-supplied dismiss guard: a click at the given screen point that
    // returns true keeps the menu open (the owner handles it — e.g. a MenuBar
    // title toggling/switching its own dropdown). Clicks inside the menu's own
    // popup windows are always kept open regardless of this guard.
    void SetDismissGuard(std::function<bool(int screenX, int screenY)> cb) {
        dismissGuard_ = std::move(cb);
    }

    // Menu card / content opacity [0..1], default 1.0 (opaque). Applied to every
    // level, including submenu levels opened later. A menu is opaque by default:
    // its popup HWND has no material behind it, so translucency there shows the
    // parent window through (see PopupHost::SetCardOpacity).
    void SetCardOpacity(float opacity);
    float CardOpacity() const { return cardOpacity_; }
    void SetContentOpacity(float opacity);
    float ContentOpacity() const { return contentOpacity_; }

private:
    // A single open level: its popup window, its list-view content, and the
    // index (in the parent level) of the item that spawned it (-1 for the root).
    struct Level {
        std::unique_ptr<PopupHost> popup;
        std::unique_ptr<Control> list;          // MenuListView (defined in .cpp)
        const std::vector<MenuItem>* items = nullptr;
        int parentIndex = -1;
    };

    // Open the root level for `items_` anchored at `anchor`. preferBelow places
    // it below the anchor (dropdown); otherwise the anchor is a cursor point.
    void OpenRoot(const RECT& anchor);

    // Open the submenu of item `itemIndex` in level `levelIndex` as a new,
    // deeper level (stage 3). No-op in stage 2 wiring paths.
    void OpenSubmenu(int levelIndex, int itemIndex);

    // Hover moved to `itemIndex` in `levelIndex` (stage 3 opens/closes submenus).
    void OnItemHovered(int levelIndex, int itemIndex);

    // Right/Left arrow at `levelIndex` (stage 3 enters/leaves a submenu level).
    void OnLeftRight(int levelIndex, bool right);

    // Close every level at or deeper than `fromLevel` (keeps shallower ones).
    void CloseFrom(int fromLevel);

    // Invoke item `itemIndex` of level `levelIndex`: fire its callback, close all.
    void InvokeItem(int levelIndex, int itemIndex);

    // Build a MenuListView bound to `items` with callbacks wired to this flyout.
    std::unique_ptr<Control> MakeListView(const std::vector<MenuItem>* items,
                                          int levelIndex);

    // Measure the popup card size (DIP) needed for `items`.
    void MeasureLevel(Control* list, float& outW, float& outH) const;

    // Window hooks: dismiss (click-outside / deactivate / move) + key routing.
    void RegisterWindowHooks();
    void ClearWindowHooks();
    bool HandleKey(UINT vk);      // routed from the parent window
    bool HandleDismiss(int reason, HWND otherHwnd, int screenX, int screenY);

    // True if `screenX/screenY` is inside any open level's popup window.
    bool PointInAnyPopup(int screenX, int screenY) const;
    // True if `hwnd` is one of our open level windows.
    bool IsOwnPopup(HWND hwnd) const;

    // The service context handed down by the owner (MenuBar or the window),
    // replacing the old SetDWrite/Attach injection. window_ is cached from it for
    // the many popup-positioning call sites; list views are attached to ctx_ so
    // their inherited Dwrite() works at measure time (before PopupHost injects).
    UIContext ctx_;
    WindowServices* window_ = nullptr;
    std::vector<MenuItem> items_;
    const std::vector<MenuItem>* borrowedItems_ = nullptr;  // non-null = using borrow
    std::vector<Level> levels_;      // levels_[0] = root; deeper = submenus
    // Closed-but-not-yet-destroyed levels. Closing a level can happen while we are
    // *inside* that PopupHost's WndProc (a menu item's onInvoke closes the chain
    // during pointer-release routing); destroying the PopupHost — and its
    // InputManager — right then would unwind back through freed objects
    // (use-after-free). So CloseFrom only *hides* a level and parks it here; the
    // actual destruction happens later at a safe point (the next OpenRoot, or the
    // destructor), never on the popup's own call stack. Mirrors ComboBox keeping
    // its hidden popup alive.
    std::vector<Level> retired_;
    void ReapRetired();  // defined in .cpp (Level owns a unique_ptr<PopupHost>,
                         // incomplete here — its dtor must run where it is complete)
    // Own the window's key + dismiss registrations while the menu chain is open.
    // Reset on close (and by ~MenuFlyout), so the window never holds a dangling
    // [this] callback after this flyout is destroyed.
    Subscription keySub_;
    Subscription dismissSub_;
    Event<MenuFlyout, RoutedEventArgs> closed_;
    std::function<void()> onClosed_;  // deprecated, kept for compat; forwarded to closed_
    // Owner-supplied hit test: a click at these screen pixels keeps the menu open
    // (the owner, e.g. a MenuBar, handles the toggle/switch itself). May be null.
    std::function<bool(int, int)> dismissGuard_;
    // Opacity applied to every level's popup, remembered here so a submenu opened
    // after the setter still gets it (levels are created lazily on hover).
    float cardOpacity_ = 1.0f;
    float contentOpacity_ = 1.0f;
    // Push cardOpacity_/contentOpacity_ onto a freshly created level's popup.
    void ApplyOpacityTo(Level& level);
};

} // namespace fluent
