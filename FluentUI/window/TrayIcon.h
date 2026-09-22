// TrayIcon.h — notification-area ("system tray") icon owned by a Window.
//
// WHY THIS SHARES THE WINDOW'S HWND. A tray icon needs an HWND to deliver its
// callback message to, and the obvious reflex is to create a dedicated
// HWND_MESSAGE window for it — that is what demos/TrayAppDemo did before this
// class existed, and what most tray tutorials show. It is not necessary here, and
// it is actually worse:
//
//   * A hidden window is only needed when nothing else can receive the callback.
//     A top-level window keeps its HWND and keeps receiving messages while hidden
//     (ShowWindow(SW_HIDE) affects visibility, not message delivery), and the
//     whole point of a tray app is that the window is hidden rather than
//     destroyed. So the main window is already a valid, permanent recipient.
//   * HWND_MESSAGE windows do NOT receive broadcast messages. The shell
//     broadcasts the registered "TaskbarCreated" message when Explorer restarts,
//     and every tray icon must be re-added when it arrives or it is gone for the
//     rest of the session. A message-only window never sees it; a top-level window
//     does. HandleTaskbarCreated() below is that recovery path, and it only works
//     because the icon lives on the real window.
//
// So: one HWND, one WndProc, and the callback message is intercepted in
// Window::OnAppMessage — the extension seam NativeWindowHost::HandleMessage
// consults before its own switch.
//
// VERSIONING IS DELIBERATELY LEFT AT THE CLASSIC BEHAVIOR. Calling
// Shell_NotifyIcon(NIM_SETVERSION, NOTIFYICON_VERSION_4) changes the callback
// contract: lParam stops being a plain mouse message and becomes packed
// coordinates, with the event in the low word and the icon id in the high word.
// Version 4 buys better anchoring for touch and per-monitor DPI, which nothing
// here needs, in exchange for a decode step that cannot be tested without a live
// shell. Staying on the classic contract means lParam IS the mouse message, so
// DecodeTrayAction below is a pure function over a documented constant.
#pragma once

#include "../fl_common.h"
#include <shellapi.h>
#include <cstddef>
#include <string>

namespace fluent {

// The private message the shell posts to the owning window for tray events.
//
// WM_APP+n is the documented range for application-private messages. WM_APP+17 is
// ModalFrameHeartbeat's tick (window/ModalFrameHeartbeat.h) — keep this list
// consistent when adding another. Application's WM_APP+1/+2 do not collide: those
// are posted to its own message-only window, never to a Window's HWND.
constexpr UINT WM_FLUENT_TRAY_CALLBACK = WM_APP + 18;

// What the user did to the icon. Mapped from the callback's lParam, which under the
// classic notify-icon contract is the mouse message the shell would have sent.
enum class TrayAction {
    None,         // a callback we do not surface (mouse-move, balloon lifecycle, ...)
    Click,        // left button released
    DoubleClick,  // left button double-clicked
    RightClick,   // right button released — the conventional trigger for a menu
    MiddleClick,  // middle button released
};

struct TrayIconEventArgs {
    // Cursor position in SCREEN PHYSICAL PIXELS, not window DIPs.
    //
    // Deliberately not converted to DIPs like the rest of the framework's input:
    // the only thing an app does with these is position a tray menu, and both
    // TrackPopupMenu and SetWindowPos take screen pixels. Converting to DIPs here
    // would mean every caller converting straight back.
    int screenX = 0;
    int screenY = 0;
    TrayAction action = TrayAction::None;
    // The raw mouse message, for an app that needs a case this enum does not cover.
    UINT rawMessage = 0;
};

// --- Pure helpers, extracted so they are testable without a window -------------

// The action a callback lParam denotes. `lParam` is the mouse message under the
// classic contract; anything we do not surface maps to None.
TrayAction DecodeTrayAction(LPARAM lParam);

// Copy `src` into the fixed-size buffer `dst` (capacity `capChars`, including the
// terminator), TRUNCATING if it does not fit. Returns the number of characters
// written, excluding the terminator.
//
// This exists because every NOTIFYICONDATAW text field is a fixed array — szTip is
// 128 wchar_t, szInfo 256, szInfoTitle 64 — and the reflexive wcscpy_s ABORTS the
// process when the source does not fit rather than truncating. A tooltip built from
// a document path or a log line is not bounded by anything the app controls, so the
// obvious spelling turns a cosmetic overflow into a crash. Truncation is the
// correct behavior for a tooltip; the shell truncates the display anyway.
size_t CopyBounded(wchar_t* dst, size_t capChars, const wchar_t* src);

// --- TrayIcon -----------------------------------------------------------------

// One notification-area icon bound to one HWND.
//
// Owned by Window as a lazily-created member, so a window that never asks for a
// tray icon carries no tray state and makes no shell calls. The class is a thin
// stateful wrapper: it holds the NOTIFYICONDATAW the shell requires us to keep
// (NIM_MODIFY and NIM_DELETE both take the same uID/hWnd pair that NIM_ADD used)
// and knows whether the icon is currently added.
//
// Not copyable: two copies would both try to NIM_DELETE the same uID, and the
// second would fail silently while the app believed it had removed something.
class TrayIcon {
public:
    explicit TrayIcon(HWND owner) : hwnd_(owner) {}
    ~TrayIcon();

    TrayIcon(const TrayIcon&) = delete;
    TrayIcon& operator=(const TrayIcon&) = delete;
    TrayIcon(TrayIcon&&) = delete;
    TrayIcon& operator=(TrayIcon&&) = delete;

    // Add the icon, or update it if already added. `icon` is borrowed, not owned:
    // the shell does not take ownership and neither do we, so it must outlive the
    // icon — use a resource icon (LoadIconW on the module) rather than one you
    // destroy. `tooltip` is truncated to fit szTip.
    HRESULT Set(HICON icon, const std::wstring& tooltip);

    // Update only the tooltip. No-op with a failure HRESULT if not added yet.
    HRESULT SetTooltip(const std::wstring& tooltip);

    // Remove the icon. Safe to call when not added. Idempotent.
    //
    // The shell does not reliably drop a dead window's icon on its own — an icon
    // whose owner exited without NIM_DELETE lingers as a ghost until the user
    // hovers over it. So this must run before the HWND dies, which is why Window
    // calls it from OnDestroying (HWND still valid) and not OnDestroyed.
    void Remove();

    bool IsAdded() const { return added_; }

    // Show a balloon notification on the icon.
    //
    // Sends a COPY of the notify data with NIF_INFO set, never the member. Setting
    // uFlags = NIF_INFO on the persistent struct — the obvious spelling, and what
    // demos/TrayAppDemo did — drops NIF_ICON|NIF_MESSAGE|NIF_TIP from it, so the
    // NEXT NIM_MODIFY tells the shell this icon has no image, no tooltip and no
    // callback message. The bug is invisible until something calls Set again.
    //
    // `title` and `text` are truncated to fit szInfoTitle / szInfo. `iconFlags` is
    // a NIIF_* value (NIIF_INFO / NIIF_WARNING / NIIF_ERROR / NIIF_NONE).
    HRESULT ShowBalloon(const std::wstring& title, const std::wstring& text,
                        DWORD iconFlags = NIIF_INFO);

    // Re-add the icon after an Explorer restart. Called by Window when the
    // registered "TaskbarCreated" broadcast arrives. No-op unless we believed an
    // icon was added — a window that never showed one must not acquire one here.
    void HandleTaskbarCreated();

private:
    HWND hwnd_ = nullptr;
    // The live registration. Kept whole rather than rebuilt per call because
    // NIM_MODIFY/NIM_DELETE must carry the same hWnd+uID as the NIM_ADD.
    NOTIFYICONDATAW data_{};
    bool added_ = false;
};

}  // namespace fluent
