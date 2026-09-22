#include "TrayIcon.h"

#include <algorithm>

// Shell_NotifyIconW lives here. FluentUI is a static library, so a consumer would
// otherwise have to add this to its own linker input; the pragma makes the
// dependency travel with the object file that needs it — same pattern as
// Image.cpp (windowscodecs) and DCompHost.cpp (dcomp).
#pragma comment(lib, "shell32.lib")

namespace fluent {

TrayAction DecodeTrayAction(LPARAM lParam) {
    switch (static_cast<UINT>(lParam)) {
        case WM_LBUTTONUP:     return TrayAction::Click;
        case WM_LBUTTONDBLCLK: return TrayAction::DoubleClick;
        // WM_CONTEXTMENU is included because the shell sends it instead of
        // WM_RBUTTONUP when the menu was summoned from the keyboard (Shift+F10 with
        // the icon focused in the notification-area flyout). An app that only
        // handled WM_RBUTTONUP would be inaccessible from the keyboard.
        case WM_RBUTTONUP:
        case WM_CONTEXTMENU:   return TrayAction::RightClick;
        case WM_MBUTTONUP:     return TrayAction::MiddleClick;
        default:               return TrayAction::None;
    }
}

size_t CopyBounded(wchar_t* dst, size_t capChars, const wchar_t* src) {
    if (!dst || capChars == 0) return 0;
    if (!src) {
        dst[0] = L'\0';
        return 0;
    }
    size_t n = 0;
    const size_t maxChars = capChars - 1;  // leave room for the terminator
    while (n < maxChars && src[n] != L'\0') {
        dst[n] = src[n];
        ++n;
    }
    dst[n] = L'\0';
    return n;
}

TrayIcon::~TrayIcon() {
    // Backstop only. Window::OnDestroying removes the icon while the HWND is still
    // valid, which is the path that actually runs in a normal session. This covers
    // a Window destroyed without ever having been shown, or one whose destroy
    // sequence was bypassed. NIM_DELETE against a dead HWND simply fails.
    Remove();
}

HRESULT TrayIcon::Set(HICON icon, const std::wstring& tooltip) {
    if (!hwnd_) {
        TraceMsg("TrayIcon", "Set: no owner HWND");
        return HRESULT_FROM_WIN32(ERROR_INVALID_WINDOW_HANDLE);
    }

    if (!added_) {
        ZeroMemory(&data_, sizeof(data_));
        data_.cbSize = sizeof(NOTIFYICONDATAW);
        data_.hWnd = hwnd_;
        // One icon per window, so a fixed id is enough. It only has to be stable
        // across ADD/MODIFY/DELETE for this hWnd.
        data_.uID = 1;
        data_.uCallbackMessage = WM_FLUENT_TRAY_CALLBACK;
    }
    data_.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
    data_.hIcon = icon;
    CopyBounded(data_.szTip, ARRAYSIZE(data_.szTip), tooltip.c_str());

    const DWORD op = added_ ? NIM_MODIFY : NIM_ADD;
    if (!Shell_NotifyIconW(op, &data_)) {
        // No HRESULT from the shell here — Shell_NotifyIcon returns only BOOL, and
        // GetLastError is not documented to be meaningful for it, so the trace
        // records what we attempted rather than a code that may be stale.
        FL_TRACEF("TrayIcon", "Shell_NotifyIconW(%s) failed",
                  added_ ? "NIM_MODIFY" : "NIM_ADD");
        return E_FAIL;
    }
    added_ = true;
    return S_OK;
}

HRESULT TrayIcon::SetTooltip(const std::wstring& tooltip) {
    if (!added_) return E_FAIL;
    data_.uFlags = NIF_TIP;
    CopyBounded(data_.szTip, ARRAYSIZE(data_.szTip), tooltip.c_str());
    const BOOL ok = Shell_NotifyIconW(NIM_MODIFY, &data_);
    // Restore the full flag set: the member describes the live registration, and
    // leaving it at NIF_TIP would make the next Set/re-add omit icon and callback.
    data_.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
    if (!ok) {
        TraceMsg("TrayIcon", "SetTooltip: Shell_NotifyIconW(NIM_MODIFY) failed");
        return E_FAIL;
    }
    return S_OK;
}

void TrayIcon::Remove() {
    if (!added_) return;
    if (!Shell_NotifyIconW(NIM_DELETE, &data_))
        TraceMsg("TrayIcon", "Shell_NotifyIconW(NIM_DELETE) failed");
    // Cleared regardless of the result: if the delete failed the icon is
    // unreachable anyway (usually because the HWND is already gone), and staying
    // "added" would make every later call retry against a dead registration.
    added_ = false;
}

HRESULT TrayIcon::ShowBalloon(const std::wstring& title, const std::wstring& text,
                              DWORD iconFlags) {
    if (!added_) return E_FAIL;

    // A copy, so the persistent registration keeps its NIF_ICON|NIF_MESSAGE|NIF_TIP
    // flags — see the header for what mutating the member breaks.
    NOTIFYICONDATAW balloon = data_;
    balloon.uFlags = NIF_INFO;
    balloon.dwInfoFlags = iconFlags;
    CopyBounded(balloon.szInfoTitle, ARRAYSIZE(balloon.szInfoTitle), title.c_str());
    CopyBounded(balloon.szInfo, ARRAYSIZE(balloon.szInfo), text.c_str());

    if (!Shell_NotifyIconW(NIM_MODIFY, &balloon)) {
        TraceMsg("TrayIcon", "ShowBalloon: Shell_NotifyIconW(NIM_MODIFY) failed");
        return E_FAIL;
    }
    return S_OK;
}

void TrayIcon::HandleTaskbarCreated() {
    if (!added_) return;
    // Explorer restarted, so the shell has forgotten every icon. Our `added_` flag
    // describes our intent, not the shell's state: re-issue NIM_ADD, not
    // NIM_MODIFY, which would fail against a registration that no longer exists.
    if (!Shell_NotifyIconW(NIM_ADD, &data_))
        TraceMsg("TrayIcon", "TaskbarCreated: re-add failed");
    else
        TraceMsg("TrayIcon", "TaskbarCreated: icon re-added");
}

}  // namespace fluent
