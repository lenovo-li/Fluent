// HudOverlay.h — the F12 performance HUD, drawn and committed off the UI thread.
//
// See diagnostics/HudFeed.h for why the HUD left the UI thread. This class is the
// thread and the GPU objects; the data and the liveness logic are in HudFeed.h.
//
// HOW IT GETS ON SCREEN WITHOUT TOUCHING THE WINDOW'S CONTENT. A window can carry
// two DirectComposition targets, one per `topmost` value, and DWM stacks the topmost
// one above the other. The window content (DCompHost) uses the non-topmost target;
// the HUD creates the topmost one, on its own DComp device, from its own thread, and
// commits it independently. So:
//   * the UI thread never draws, lays out or commits anything for the HUD;
//   * a stalled UI thread does not stop the HUD from committing;
//   * the HUD never dirties the content surface, so partial redraw keeps working
//     and the frames measured are the app's own.
// The content target used to be the topmost one. That only matters relative to child
// HWNDs, and FluentUI creates none (popups and dialogs are top-level windows), so
// moving it to non-topmost changes nothing visible.
//
// THREADING. Every D3D/D2D/DWrite/DComp object the HUD uses is created, used and
// released on the HUD thread; the D2D factory is single-threaded and private to it.
// The only cross-thread state is HudFeed (mutex), the acknowledged ping sequence
// (atomic), and the stop event. Window geometry is read with GetClientRect /
// GetDpiForWindow, which do not send messages, so they answer even while the UI
// thread is stuck.
//
// LIFETIME. Start() spawns the thread, Stop() signals and joins it; the thread
// destroys its target on the way out, which removes the HUD from the screen. The
// owning window must Stop() before its HWND is destroyed (NativeWindowHost does it
// in WM_DESTROY and its destructor). Idle cost when the HUD is off: zero — no thread.
// When it is on, the HUD thread wakes ~10x a second and posts one ping message,
// which is the only thing it asks of the UI thread.
//
// DEVICE LOSS. Any failed draw or commit drops all GPU objects; the next tick
// rebuilds them. A HUD that silently stops after a driver reset would be a
// diagnostic that lies, so failure is retried rather than latched.
#pragma once

#include <windows.h>

#include <atomic>
#include <thread>

#include "../diagnostics/HudFeed.h"

namespace fluent {

// The HUD thread's liveness ping (wParam = sequence). WM_APP+17/18/19 are taken by
// ModalFrameHeartbeat, TrayIcon and PopupHost; see the list in services/PopupHost.h.
constexpr UINT WM_FLUENT_HUD_PING = WM_APP + 20;

class HudOverlay {
public:
    HudOverlay() = default;
    ~HudOverlay() { Stop(); }
    HudOverlay(const HudOverlay&) = delete;
    HudOverlay& operator=(const HudOverlay&) = delete;

    // The HUD thread posts WM_FLUENT_HUD_PING to `hwnd`; the window's handler must
    // call AcknowledgePing(wParam). Idempotent while running.
    void Start(HWND hwnd);
    void Stop();
    bool Running() const { return worker_.joinable(); }

    // UI thread, at the end of a painted frame. One copy under a mutex.
    void Publish(const HudSnapshot& s) { feed_.Publish(s); }

    // UI thread, from the ping message handler. Records which ping was answered and
    // when, so the latency excludes the HUD thread's own polling delay.
    void AcknowledgePing(WPARAM seq);

private:
    void ThreadMain();

    HWND hwnd_ = nullptr;
    HANDLE stopEvent_ = nullptr;
    std::thread worker_;
    HudFeed feed_;
    std::atomic<uint32_t> ackSeq_{0};
    std::atomic<int64_t> ackQpc_{0};
};

}  // namespace fluent
