// ClipboardTestHelpers.h — put text on the real clipboard for a test, and put the
// user's text back afterwards.
//
// Paste reads the clipboard and nothing else, and the menu's Cut writes it, so a
// test of either has to go through the process-wide clipboard. Two details make
// that work at all headless:
//
//   * The clipboard must be opened against a real window. OpenClipboard(nullptr)
//     followed by EmptyClipboard leaves the clipboard with no owner, and
//     SetClipboardData then FAILS — a test written that way silently skips forever.
//     ClipboardWindow is a message-only window that exists only to be that owner.
//   * Another process can hold the clipboard open for a moment. Callers treat a
//     false return as "skip", not as a failure.
//
// ClipboardTextGuard saves CF_UNICODETEXT on construction and restores it on
// destruction, so running the suite does not eat what the user last copied. Only
// plain text survives; anything richer they had copied is lost to the test's write.
#pragma once

#include <windows.h>
#include <cstring>
#include <string>

namespace fluent::testclip {

class ClipboardWindow {
public:
    ClipboardWindow() {
        hwnd_ = CreateWindowExW(0, L"STATIC", L"", 0, 0, 0, 0, 0, HWND_MESSAGE,
                                nullptr, GetModuleHandleW(nullptr), nullptr);
    }
    ~ClipboardWindow() { if (hwnd_) DestroyWindow(hwnd_); }
    ClipboardWindow(const ClipboardWindow&) = delete;
    ClipboardWindow& operator=(const ClipboardWindow&) = delete;
    HWND Get() const { return hwnd_; }
private:
    HWND hwnd_ = nullptr;
};

inline bool WriteText(const std::wstring& text) {
    ClipboardWindow owner;
    if (!owner.Get() || !OpenClipboard(owner.Get())) return false;
    EmptyClipboard();
    const SIZE_T bytes = (text.size() + 1) * sizeof(wchar_t);
    bool ok = false;
    if (HGLOBAL mem = GlobalAlloc(GMEM_MOVEABLE, bytes)) {
        if (void* dst = GlobalLock(mem)) {
            std::memcpy(dst, text.c_str(), bytes);
            GlobalUnlock(mem);
            ok = SetClipboardData(CF_UNICODETEXT, mem) != nullptr;
        }
        if (!ok) GlobalFree(mem);   // ownership transfers only on success
    }
    CloseClipboard();
    return ok;
}

// The clipboard's current text, or false when there is none / it is held elsewhere.
inline bool ReadText(std::wstring& out) {
    ClipboardWindow owner;
    if (!owner.Get() || !OpenClipboard(owner.Get())) return false;
    bool ok = false;
    if (HANDLE h = GetClipboardData(CF_UNICODETEXT)) {
        if (const auto* p = static_cast<const wchar_t*>(GlobalLock(h))) {
            out = p;
            ok = true;
            GlobalUnlock(h);
        }
    }
    CloseClipboard();
    return ok;
}

struct ClipboardTextGuard {
    bool hadText = false;
    std::wstring saved;
    ClipboardTextGuard() { hadText = ReadText(saved); }
    ~ClipboardTextGuard() { if (hadText) WriteText(saved); }
    ClipboardTextGuard(const ClipboardTextGuard&) = delete;
    ClipboardTextGuard& operator=(const ClipboardTextGuard&) = delete;
};

} // namespace fluent::testclip
