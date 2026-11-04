// HudFeed.h — the data path between the UI thread and the off-thread HUD.
//
// WHY THE HUD LEFT THE UI THREAD. The HUD used to be drawn inside RenderNow, which
// made it useless at exactly the moment it was needed: when the UI thread stalls,
// the HUD stalls with it, so a 900ms freeze shows up as "the numbers stopped
// moving" rather than as a number. Worse, keeping it live changed what was being
// measured — every frame re-requested the next one (the window never idled) and
// forced a full-window redraw (the partial-redraw path never ran), so the FPS, ren
// and draw readings described the HUD's own workload, not the app's.
//
// The HUD now runs on its own thread with its own device and composition target
// (HudOverlay). The UI thread's entire share of the work is one Publish() at the
// end of a painted frame — a ~1KB copy under an uncontended mutex. Everything that
// costs anything (percentiles, formatting, text layout, drawing, commit) happens on
// the HUD thread.
//
// What lives here is the pure part, testable headless:
//   * HudSnapshot / HudFeed   — the published frame data and its hand-off.
//   * HudPingTracker          — "is the UI thread answering right now", by ping.
//   * FormatHudStatusLine     — the liveness line under the frame numbers.
//
// WHY A PING AND NOT HangDetector's BEAT. HangDetector watches the gap since the
// UI thread last did work (a frame, a dispatched message, a wake from the pump)
// and relies on the UI thread announcing its idle waits. That is enough for a
// watchdog with a 5-second threshold, but a HUD wants a live number while it is
// happening, at a 250 ms resolution, and "time since the last beat" cannot tell a
// thread busy with a long frame from one waiting in a loop that never said it was
// idle. A ping asks the question
// directly — the HUD thread posts a message, the UI thread acknowledges it when its
// loop gets to it — so the answer is the UI thread's actual responsiveness whether
// or not anything is being painted. Posted messages are also dispatched by the
// system modal loop (drag-resize, window move), so a resize drag still answers.
//
// Only one ping is ever in flight. Posting on every tick while the UI thread is
// stuck would queue a backlog that it then drains in a burst — the same failure
// ModalFrameHeartbeat bounds itself against.
#pragma once

#include <cstdint>
#include <cstdio>
#include <mutex>
#include <string>

#include "FrameRing.h"
#include "PerformanceCounters.h"

namespace fluent {

// Everything the HUD draws, captured on the UI thread at the end of a painted frame.
// The rings are copied whole (2 x 120 floats) rather than reduced to percentiles
// here: nth_element over 120 samples is cheap, but it is still work, and the point
// of this design is that the UI thread does none of it.
struct HudSnapshot {
    FrameStats stats;              // last completed frame's phase split and counts
    FrameRing<120> cpu;            // frame build cost (ms)
    FrameRing<120> interval;       // frame-to-frame cadence (ms)
    uint64_t frameCount = 0;       // frames painted since the window was created
    int64_t lastFrameEndQpc = 0;   // QPC at the end of that frame; 0 = none yet
    float titleBarHeightDip = 0.0f;  // the HUD sits just below the caption band
};

// Single-slot hand-off. A mutex rather than a seqlock: the payload is ~1KB, the
// reader takes it at most ~10 times a second, and a seqlock over non-atomic fields
// is a data race by the letter of the C++ memory model even when it works in
// practice. The UI thread never waits more than one ~1KB copy.
class HudFeed {
public:
    void Publish(const HudSnapshot& s) {
        std::lock_guard<std::mutex> lock(mutex_);
        snapshot_ = s;
        hasData_ = true;
    }

    // Copies the latest snapshot into `out`. False until the first Publish.
    bool Read(HudSnapshot& out) const {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!hasData_) return false;
        out = snapshot_;
        return true;
    }

private:
    mutable std::mutex mutex_;
    HudSnapshot snapshot_;
    bool hasData_ = false;
};

// Ping bookkeeping for the HUD thread. Time is passed in (ms on any monotonic
// clock) so the logic runs without sleeping. Sequence 0 is reserved for "nothing
// acknowledged yet", so the first ping is 1.
class HudPingTracker {
public:
    // Returns the sequence number to post, or 0 if a ping is still unanswered —
    // never more than one in the queue.
    uint32_t NextPing(double nowMs) {
        if (inFlight_) return 0;
        ++seq_;
        if (seq_ == 0) seq_ = 1;  // wrapped: skip the reserved value
        inFlight_ = true;
        postedMs_ = nowMs;
        return seq_;
    }

    // Feed the UI thread's latest acknowledgement. `ackMs` is when the UI thread
    // handled it (same clock as nowMs), so the latency does not include however long
    // the HUD thread took to notice. A stale ack (an older sequence) is ignored.
    void Observe(uint32_t ackedSeq, double ackMs) {
        if (!inFlight_ || ackedSeq != seq_) return;
        inFlight_ = false;
        lastLatencyMs_ = ackMs - postedMs_;
        if (lastLatencyMs_ < 0.0) lastLatencyMs_ = 0.0;
        hasLatency_ = true;
    }

    // A post that failed (window gone, queue full) must not wedge the tracker into
    // "in flight forever", which would read as a permanent stall.
    void CancelInFlight() { inFlight_ = false; }

    bool HasLatency() const { return hasLatency_; }
    double LastLatencyMs() const { return lastLatencyMs_; }
    // How long the current ping has gone unanswered; 0 when none is outstanding.
    double PendingMs(double nowMs) const { return inFlight_ ? nowMs - postedMs_ : 0.0; }

private:
    uint32_t seq_ = 0;
    bool inFlight_ = false;
    double postedMs_ = 0.0;
    double lastLatencyMs_ = 0.0;
    bool hasLatency_ = false;
};

// An unanswered ping older than this is reported as a stall. 250ms is well past
// anything a healthy loop does (a ping normally answers in well under a frame) and
// short enough that a freeze is flagged while it is still happening.
constexpr double kHudStallThresholdMs = 250.0;

// The HUD's last line: how the UI thread is doing right now, which the frame
// numbers above it cannot say (they only change when a frame is painted).
//   pendingMs      unanswered ping age (HudPingTracker::PendingMs)
//   latencyMs      last answered ping's latency; < 0 = none answered yet
//   lastFrameAgoMs time since the last painted frame; < 0 = no frame yet
inline std::string FormatHudStatusLine(double pendingMs, double latencyMs,
                                       double lastFrameAgoMs) {
    char buf[128];
    if (pendingMs >= kHudStallThresholdMs) {
        // Stalled: the numbers above are frozen at the last frame before the stall.
        std::snprintf(buf, sizeof(buf), "UI STALLED %.0f ms  (above: stale)", pendingMs);
        return std::string(buf);
    }
    char ping[32];
    if (latencyMs >= 0.0) std::snprintf(ping, sizeof(ping), "%.1fms", latencyMs);
    else std::snprintf(ping, sizeof(ping), "--");
    if (lastFrameAgoMs < 0.0) {
        std::snprintf(buf, sizeof(buf), "UI ping %s  no frame yet", ping);
    } else if (lastFrameAgoMs >= 1000.0) {
        // Idle: nothing has painted for a while. Saying so keeps old numbers from
        // being read as current.
        std::snprintf(buf, sizeof(buf), "UI ping %s  idle %.1fs", ping, lastFrameAgoMs / 1000.0);
    } else {
        std::snprintf(buf, sizeof(buf), "UI ping %s  last frame %.0fms ago", ping, lastFrameAgoMs);
    }
    return std::string(buf);
}

}  // namespace fluent
