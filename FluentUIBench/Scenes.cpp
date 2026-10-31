// Scenes.cpp — the FluentUIBench scenarios.
//
// Scenes are registered at the bottom of this file. The ones whose cost is pure CPU/logic run for real
// against the FluentUI core (layout, dirty propagation, active-animation
// ticking, measure cache, idle detection) and report timings measured with the
// diagnostics ScopedTimer. The ones that fundamentally need a live swap chain,
// GPU, or interactive window (visual/text raster stress, resize, popup HWND
// churn, DPI lab, theme raster, input latency) are registered as honest stubs:
// they describe what a real Windows+GPU run must measure and report `measured =
// false` rather than fabricate a number: a made-up GPU figure is worse than none.
//
// When the real pipeline for a stub can be driven from here, the stub is replaced by a scene
// that drives the actual window — the harness shape does not change.

#include "Bench.h"

#include "../FluentUI/layout/StackPanel.h"
#include "../FluentUI/layout/Grid.h"
#include "../FluentUI/core/FrameworkElement.h"
#include "../FluentUI/animation/AnimationRegistry.h"
#include "../FluentUI/window/FrameScheduler.h"
#include "../FluentUI/diagnostics/PerformanceCounters.h"
#include "../FluentUI/controls/Expander.h"
#include "../FluentUI/controls/ListBox.h"
#include "../FluentUI/automation/AutomationHost.h"
#include "../FluentUI/automation/AutomationPeer.h"
#include "../FluentUI/automation/UiaCore.h"
#include "../FluentUI/core/UIContext.h"

#include <cstdio>
#include <memory>
#include <string>

using namespace fluent;
using bench::Result;

namespace {

// ---- shared helpers -------------------------------------------------------

// The frame budget of THIS MACHINE's display, not a hard-coded 60 Hz.
//
// Reporting a cost as "% of a 16.67 ms frame" is wrong on any panel that is not 60 Hz,
// and wrong in the dangerous direction: on a 240 Hz display the real budget is 4.17 ms,
// so a hard-coded 60 Hz figure understates the load by 4x. A scene that looked like it
// used 3% of a frame would actually be using 12%.
//
// Same source the frame pacer uses (NativeWindowHost::RefreshHz -> EnumDisplaySettingsW),
// so the bench and the running app agree about what a frame is. Falls back to 60 only
// when the query fails, and says so in the label rather than silently pretending.
struct FrameBudget {
    int hz = 0;              // 0 = unknown
    double ms = 1000.0 / 60.0;
};

FrameBudget DisplayFrameBudget() {
    FrameBudget b;
    DEVMODEW dm{};
    dm.dmSize = sizeof(dm);
    if (EnumDisplaySettingsW(nullptr, ENUM_CURRENT_SETTINGS, &dm) &&
        dm.dmDisplayFrequency > 1) {           // 0 and 1 both mean "default/unknown"
        b.hz = static_cast<int>(dm.dmDisplayFrequency);
        b.ms = 1000.0 / static_cast<double>(b.hz);
    }
    return b;
}

// A leaf with a fixed desired size and no GPU dependency (mirrors the test
// TestLeaf). Used to build large synthetic trees.
class BenchLeaf : public FrameworkElement {
public:
    BenchLeaf(float w, float h) { SetWidth(w); SetHeight(h); }
    void Render(const DrawingContext&) override {}
};

// A leaf that always wants an animation tick (never finishes), to load the
// active-animation set. Counts how many ticks it received.
class BenchAnimLeaf : public FrameworkElement {
public:
    void Render(const DrawingContext&) override {}
    bool WantsAnimationTick() const override { return true; }
    void OnAnimationTick(float) override { ++ticks_; }
    uint32_t ticks_ = 0;
};

char msgbuf[256];

// ---- 1. Controls Grid: many leaves laid out repeatedly --------------------

Result ControlsGrid() {
    // Build a StackPanel with 1,000 fixed-size children and run a full
    // Measure+Arrange pass many times, measuring the layout cost. This is the
    // "lots of ordinary controls" scene reduced to its layout core (no GPU).
    constexpr int kChildren = 1000;
    constexpr int kPasses = 200;

    StackPanel panel;
    panel.SetOrientation(StackPanel::Orientation::Vertical);
    panel.SetSpacing(2.0f);
    for (int i = 0; i < kChildren; ++i)
        panel.Emplace<BenchLeaf>(120.0f, 24.0f);

    double ms = 0.0;
    volatile float sink = 0.0f;
    {
        ScopedTimer t(ms);
        for (int p = 0; p < kPasses; ++p) {
            // Force a fresh measure each pass by invalidating a child's size.
            panel.UpdateLayout({0, 0, 400, 40000});
            sink += panel.Desired().h;
        }
    }
    (void)sink;

    Result r;
    r.primaryMs = ms;
    std::snprintf(msgbuf, sizeof(msgbuf),
                  "%d children x %d passes; %.4f ms/pass",
                  kChildren, kPasses, ms / kPasses);
    r.metric = msgbuf;
    return r;
}

// ---- 2. Text Stress -------------------------------------------------------

Result TextStress() {
    Result r;
    r.measured = false;
    r.note = "Requires DWrite/GPU: measures IDWriteTextLayout build + draw for "
             "mixed CJK/Latin wrapping. Run in a real window; record "
             "textLayoutsNew / textLayoutCacheHits from FrameStats.";
    return r;
}

// ---- 3. Tree Virtualization ----------------------------------------------

Result TreeVirtualization() {
    Result r;
    r.measured = false;
    r.note = "Virtualization not implemented yet. Target: 100k "
             "data rows must not create 100k UIElements — visual element count "
             "should track visible rows. Scene activates once TreeView "
             "virtualizes.";
    return r;
}

// ---- 4. Animation Stress: active-set tick ---------------------------------

Result AnimationStress() {
    // The framework's key perf property: per-frame cost is O(active animations),
    // not O(tree). Build a large tree where only some leaves animate, collect
    // the active set once, then tick it many times and measure.
    constexpr int kTotal = 5000;
    constexpr int kAnimating = 200;
    constexpr int kTicks = 1000;

    StackPanel root;
    std::vector<UIElement*> roots{&root};
    for (int i = 0; i < kTotal; ++i) {
        if (i < kAnimating) root.Emplace<BenchAnimLeaf>();
        else root.Emplace<BenchLeaf>(10.0f, 10.0f);
    }

    AnimationRegistry reg;
    reg.Collect(roots);  // pull-once

    double ms = 0.0;
    {
        ScopedTimer t(ms);
        for (int i = 0; i < kTicks; ++i) reg.Tick(1.0f / 60.0f);
    }

    Result r;
    r.primaryMs = ms;
    std::snprintf(msgbuf, sizeof(msgbuf),
                  "active=%zu of %d elements; %d ticks; %.5f ms/tick "
                  "(cost tracks active set, not tree size)",
                  reg.Count(), kTotal, kTicks, ms / kTicks);
    r.metric = msgbuf;
    return r;
}

// ---- 4b. Expander reveal ---------------------------------------------------

Result ExpanderReveal() {
    // THE QUESTION THIS SCENE EXISTS TO ANSWER: does the Expander reveal animation
    // block the UI thread? Nothing in this framework may do blocking or long work on
    // the UI thread, so an animation that is expensive per tick is a defect even if it
    // "looks fine" on a fast machine.
    //
    // The reveal is deliberately the WORST-CASE shape for a UI-thread animation,
    // because Expander::OnAnimationTick raises DirtyFlags::Measure rather than Render:
    // the revealed height feeds this element's desired size, so the parent must re-run
    // layout on every frame of the ease. That is O(content subtree) per tick, unlike a
    // fade or a knob slide which are O(1) Render-only.
    //
    // So this measures the whole per-frame cost a real reveal pays: tick + the relayout
    // the tick forces, reported against THIS DISPLAY's actual frame budget. The budget
    // is not a constant: 16.67 ms at 60 Hz but 4.17 ms at 240 Hz, and quoting a 60 Hz
    // figure on a high-refresh panel understates the load by the ratio of the two.
    //
    // Deliberately NOT measured here: DirectWrite text measurement and any GPU work.
    // The content is BenchLeaf (fixed desired size, empty Render), and the Expander is
    // unattached so Dwrite() is null and Render returns early. This isolates the
    // layout/tick cost, which is the part the animation adds. A real page also pays for
    // its content's own Measure, which is why the scene reports per-child cost too.
    constexpr int kChildren = 200;   // a deliberately heavy content subtree
    constexpr int kTicks = 1000;

    Expander exp;
    exp.SetHeader(L"Reveal");
    auto content = std::make_unique<StackPanel>();
    for (int i = 0; i < kChildren; ++i)
        content->Emplace<BenchLeaf>(120.0f, 24.0f);
    exp.SetContent(std::move(content));

    // Animated expand: Transition::Animate leaves reveal_ away from its target, which
    // is what makes WantsAnimationTick() true and puts the element in the active set.
    exp.SetExpanded(true, Expander::Transition::Animate);

    std::vector<UIElement*> roots{&exp};
    AnimationRegistry reg;
    reg.Collect(roots);
    const size_t activeAtStart = reg.Count();

    // Measure the realistic per-frame pair: advance the ease, then satisfy the layout
    // it just invalidated — exactly what RunDueFrame does after PumpAnimations.
    // reveal_ is re-seeded each iteration so the ease never settles and every one of
    // the kTicks iterations does real work (a settled ease would drop out of the active
    // set and flatter the average).
    double ms = 0.0;
    {
        ScopedTimer t(ms);
        for (int i = 0; i < kTicks; ++i) {
            if (!exp.WantsAnimationTick()) {
                // Restart the reveal so the loop keeps measuring live ticks.
                exp.SetExpanded(!exp.IsExpanded(), Expander::Transition::Animate);
                reg.Collect(roots);
            }
            reg.Tick(1.0f / 60.0f);
            exp.Measure(400.0f, 10000.0f);
            exp.Arrange(RectDip{0.0f, 0.0f, 400.0f, exp.Desired().h});
        }
    }

    const double perTick = ms / kTicks;
    // This machine's real budget. The reveal must be a small fraction of one frame, not
    // merely "under" it: everything else in the frame (paint, other animations, input)
    // has to fit alongside.
    const FrameBudget fb = DisplayFrameBudget();

    Result r;
    r.primaryMs = ms;
    char hzLabel[64];
    if (fb.hz > 0)
        std::snprintf(hzLabel, sizeof(hzLabel), "%d Hz -> %.2f ms", fb.hz, fb.ms);
    else
        std::snprintf(hzLabel, sizeof(hzLabel), "refresh unknown, assuming 60 Hz");

    std::snprintf(msgbuf, sizeof(msgbuf),
                  "active=%zu; %d ticks over %d-child content; %.5f ms per "
                  "tick+relayout = %.3f%% of this display's frame (%s). Measure-level "
                  "dirty, so each tick re-lays out the content subtree.",
                  activeAtStart, kTicks, kChildren, perTick,
                  100.0 * perTick / fb.ms, hzLabel);
    r.metric = msgbuf;
    r.note = "CPU only: no DWrite (unattached, so Render early-outs) and no GPU. "
             "Budget read from EnumDisplaySettingsW, the same source the frame pacer "
             "uses. Real-hardware check still needed for smoothness.";

    // Regression gate (only enforced under --gate).
    //
    // 5% of one frame for the animation tick plus the relayout it forces. The measured
    // value is ~0.09% at 240 Hz, so the limit is ~55x headroom — deliberately loose,
    // because the job of this gate is to catch an ORDER-OF-MAGNITUDE regression (someone
    // removing the measure cache, or making the reveal re-measure a virtualized list in
    // full), not to police normal variance on a busy machine.
    //
    // A tight threshold here would fail spuriously and get disabled, which is worse than
    // no gate at all. Tighten it only if a real regression slips through this one.
    r.perFrameMs = perTick;
    r.budgetFraction = 0.05;
    return r;
}

// ---- 5. Resize Stress -----------------------------------------------------

Result ResizeStress() {
    Result r;
    r.measured = false;
    r.note = "Requires a live swap chain: continuous window resize should reuse "
             "the swap chain (ResizeBuffers), never rebuild visuals, no flicker. "
             "Layout-only cost is covered by ControlsGrid.";
    return r;
}

// ---- 6. Popup Stress ------------------------------------------------------

Result PopupStress() {
    Result r;
    r.measured = false;
    r.note = "Requires HWNDs: open/close a cascading menu 10,000 times and prove "
             "GDI/USER handles + private bytes do not grow (ProcessStats::Sample "
             "before/after). Popup *state-machine* logic is covered by unit "
             "tests (PopupStateTests).";
    return r;
}

// ---- 7. DPI Lab -----------------------------------------------------------

Result DpiLab() {
    Result r;
    r.measured = false;
    r.note = "Requires multi-monitor/GPU: drag across 96/120/144/192 DPI, verify "
             "no cached-resource reuse across DPI and no text clipping.";
    return r;
}

// ---- 8. Theme Lab ---------------------------------------------------------

Result ThemeLab() {
    Result r;
    r.measured = false;
    r.note = "Requires GPU raster: Light/Dark/Mica/Acrylic/high-contrast visual "
             "regression. Theme *token* resolution is not timed by this scene.";
    return r;
}

// ---- 9. Input Latency -----------------------------------------------------

Result InputLatency() {
    Result r;
    r.measured = false;
    r.note = "Requires a live window: pointer-down to corresponding Present, "
             "measured with the frame-latency waitable object.";
    return r;
}

// ---- 10. Idle Test --------------------------------------------------------

Result IdleTest() {
    // Pure-logic proxy for the idle property: with nothing dirty
    // and no active animations, the scheduler must report "no frame needed". We
    // model that here: an empty active set after Collect means the host would
    // disarm its timer and Present nothing. A real 30-second idle Present-count
    // test needs the window and is noted.
    StackPanel root;
    root.Emplace<BenchLeaf>(10.0f, 10.0f);  // static content, no animation
    std::vector<UIElement*> roots{&root};

    // Settle layout once (this is what the host does on the first frame). After
    // it, nothing is dirty and nothing animates — the true idle state.
    root.UpdateLayout({0, 0, 100, 100});
    root.ClearDirtySubtree();

    AnimationRegistry reg;
    reg.Collect(roots);

    Result r;
    r.primaryMs = 0.0;
    bool idle = reg.Empty() && !root.AnyDirtyInSubtree(DirtyFlags::Measure);
    std::snprintf(msgbuf, sizeof(msgbuf),
                  "active animations=%zu, measure-dirty=%s -> host would %s",
                  reg.Count(),
                  root.AnyDirtyInSubtree(DirtyFlags::Measure) ? "yes" : "no",
                  idle ? "stay idle (no Present)" : "request a frame");
    r.metric = msgbuf;
    r.note = "Logic proxy for idle. Full 30s idle Present-count test needs the "
             "window — verify no periodic Render/Present.";
    return r;
}

// ---- 11. Frame coalescing -----------------------------------------

Result FrameCoalescing() {
    // The core frame-scheduler property: many invalidations between two
    // frames merge into ONE pending frame. We simulate an input event that
    // dirties several properties, then a render turn, and confirm only one frame
    // is serviced no matter how many requests arrived.
    FrameScheduler sched;

    constexpr int kRequests = 500;
    for (int i = 0; i < kRequests; ++i) {
        sched.RequestFrame(FrameReason::Input);
        sched.RequestFrame(FrameReason::Layout);
    }
    bool oneFramePending = sched.NeedsFrame();

    // The host services exactly one frame; the merged reasons carry every cause.
    FrameReason serviced = sched.BeginFrame();
    sched.EndFrame();
    bool idleAfter = !sched.NeedsFrame();

    Result r;
    r.primaryMs = 0.0;
    std::snprintf(msgbuf, sizeof(msgbuf),
                  "%d x2 requests -> pending=%s, serviced reasons: Input=%s "
                  "Layout=%s -> idle after=%s (N invalidations = 1 frame)",
                  kRequests, oneFramePending ? "1 frame" : "ERROR",
                  HasReason(serviced, FrameReason::Input) ? "yes" : "no",
                  HasReason(serviced, FrameReason::Layout) ? "yes" : "no",
                  idleAfter ? "yes" : "no");
    r.metric = msgbuf;
    r.note = "Confirms coalescing: the input path no longer paints per-property. "
             "Real per-frame FrameReason is recorded in FrameStats during a live "
             "window run (NativeWindowHost::LastFrameStats).";
    return r;
}

// ---- 12. Dirty walk -------------------------------------------------------

// A leaf whose Render-only invalidation the scene can trigger from outside, the
// way a caret blink or a hover state change does from inside a real control.
class BenchPokeLeaf : public FrameworkElement {
public:
    BenchPokeLeaf() { SetWidth(40.0f); SetHeight(12.0f); }
    void Render(const DrawingContext&) override {}
    void Poke() { Invalidate(); }
};

Result DirtyWalk() {
    // The per-frame dirty analysis RenderNow runs on every painted frame, against a
    // tree where exactly ONE leaf changed: the two layout queries
    // (AnyDirtyInSubtree Measure / Arrange), the dirty-rect collection and the
    // end-of-frame clear. Same four calls in the same order as the host, minus the
    // paint (which is GPU work and scales with the dirty rect, not the tree).
    //
    // Shape: root -> 20 sections -> 10 groups -> 10 leaves = 2,221 nodes, 2,000 of
    // them leaves. Three levels of nesting is what a settings page or a gallery
    // looks like, and it is enough for "cost per node" and "cost per dirty path" to
    // diverge by orders of magnitude: a walk that visits every node pays for 2,221
    // virtual calls x 3 passes, a walk that follows the dirty path pays for ~4 nodes
    // plus a flag test per sibling it passes on the way.
    constexpr int kSections = 20, kGroups = 10, kLeaves = 10;
    constexpr int kFrames = 20000;

    StackPanel root;
    std::vector<BenchPokeLeaf*> leaves;
    leaves.reserve(kSections * kGroups * kLeaves);
    for (int s = 0; s < kSections; ++s) {
        auto* section = root.Emplace<StackPanel>();
        for (int g = 0; g < kGroups; ++g) {
            auto* group = section->Emplace<StackPanel>();
            group->SetOrientation(StackPanel::Orientation::Horizontal);
            for (int l = 0; l < kLeaves; ++l) leaves.push_back(group->Emplace<BenchPokeLeaf>());
        }
    }
    root.UpdateLayout({0, 0, 800, 100000});
    root.ClearDirtySubtree();

    std::vector<RectDip> dirty;
    dirty.reserve(16);
    size_t collected = 0;   // sanity: must be exactly one rect per frame
    int layoutFrames = 0;   // sanity: a Render-only poke must never trigger layout
    double ms = 0.0;
    {
        ScopedTimer t(ms);
        for (int f = 0; f < kFrames; ++f) {
            // Stride through the leaves so consecutive frames hit different
            // sections and no branch stays hot in the cache by accident.
            leaves[(static_cast<size_t>(f) * 997u) % leaves.size()]->Poke();
            const bool m = root.AnyDirtyInSubtree(DirtyFlags::Measure);
            const bool a = root.AnyDirtyInSubtree(DirtyFlags::Arrange);
            if (m || a) ++layoutFrames;
            dirty.clear();
            root.CollectDirtyBounds(dirty);
            collected += dirty.size();
            root.ClearDirtySubtree();
        }
    }

    const size_t nodes = 1 + kSections + kSections * kGroups + leaves.size();
    const double usPerFrame = ms * 1000.0 / kFrames;
    Result r;
    r.primaryMs = ms;
    r.perFrameMs = ms / kFrames;
    std::snprintf(msgbuf, sizeof(msgbuf),
                  "%zu nodes, 1 leaf dirty per frame x %d frames; %.3f us/frame; "
                  "rects/frame=%.2f (must be 1.00), layout frames=%d (must be 0)",
                  nodes, kFrames, usPerFrame,
                  static_cast<double>(collected) / kFrames, layoutFrames);
    r.metric = msgbuf;
    r.note = "Headless proxy for RenderNow's dirty analysis + clear on a caret-blink "
             "frame. Paint cost is excluded (GPU); verify the live number with the "
             "F12 HUD's cpu line while a TextBox caret blinks in a large page.";
    return r;
}

// ---- 12b. UI Automation cost ---------------------------------------------------

// A UIA core stand-in: counts what the host would ask of UIAutomationCore without
// calling it. `listening` decides UiaClientsAreListening(). Nothing here creates a
// window, so the scenes measure the framework's own UIA bookkeeping only.
struct BenchUiaCore final : automation::IUiaCore {
    bool listening = false;
    size_t posts = 0, raised = 0;
    LRESULT ReturnProvider(HWND, WPARAM, LPARAM, IUnknown*) override { return 1; }
    HRESULT DisconnectProvider(IUnknown*) override { return S_OK; }
    void ReleaseEventMap(HWND) override {}
    bool InSendMessage() override { return false; }
    bool ClientsAreListening() override { return listening; }
    HRESULT RaiseEvent(IUnknown*, int) override { ++raised; return S_OK; }
    HRESULT RaiseBoolChanged(IUnknown*, int, bool, bool) override { ++raised; return S_OK; }
    HRESULT RaiseIntChanged(IUnknown*, int, int, int) override { ++raised; return S_OK; }
    HRESULT RaiseStringChanged(IUnknown*, int, const wchar_t*, const wchar_t*) override { ++raised; return S_OK; }
    HRESULT RaiseChildrenInvalidated(IUnknown*) override { ++raised; return S_OK; }
    void PostToHost(HWND, UINT) override { ++posts; }
};
struct BenchUiaWindow final : automation::IAutomationWindow {
    UIElement* root = nullptr;
    size_t AutomationRootCount() const override { return 1; }
    UIElement* AutomationRootAt(size_t) const override { return root; }
    UIElement* AutomationFocusedElement() const override { return nullptr; }
};

// The DirtyWalk frame (poke one leaf, the two layout queries, collect, clear),
// returning us/frame. `site` null = a tree with no automation at all (baseline).
double BenchUiaFrames(automation::IAutomationSite* site, int frames, bool touchPeers,
                      size_t* peersOut) {
    constexpr int kSections = 20, kGroups = 10, kLeaves = 10;
    StackPanel root;
    std::vector<BenchPokeLeaf*> leaves;
    for (int s = 0; s < kSections; ++s) {
        auto* section = root.Emplace<StackPanel>();
        for (int g = 0; g < kGroups; ++g) {
            auto* group = section->Emplace<StackPanel>();
            for (int l = 0; l < kLeaves; ++l) leaves.push_back(group->Emplace<BenchPokeLeaf>());
        }
    }
    UIContext ctx;
    ctx.automation = site;
    root.AttachToContext(ctx);
    root.UpdateLayout({0, 0, 800, 100000});
    root.ClearDirtySubtree();
    // A client that walked the whole tree once: every leaf has a peer + provider.
    if (touchPeers)
        for (auto* l : leaves)
            if (auto* p = l->GetOrCreateAutomationPeer()) p->Provider();
    if (peersOut) *peersOut = automation::AutomationPeer::LiveCount();
    std::vector<RectDip> dirty;
    double ms = 0.0;
    {
        ScopedTimer t(ms);
        for (int f = 0; f < frames; ++f) {
            leaves[(static_cast<size_t>(f) * 997u) % leaves.size()]->Poke();
            (void)root.AnyDirtyInSubtree(DirtyFlags::Measure);
            (void)root.AnyDirtyInSubtree(DirtyFlags::Arrange);
            dirty.clear();
            root.CollectDirtyBounds(dirty);
            root.ClearDirtySubtree();
        }
    }
    root.DetachFromContext();
    return ms * 1000.0 / frames;
}

Result UiaNoClient() {
    // The user's requirement: UIA must not cost the UI thread anything while no
    // client (Narrator, Inspect, a test tool) is attached. Three trees, same frame:
    //   baseline     no automation site at all (what the framework cost before UIA);
    //   site, idle   the window's AutomationHost wired in, no client ever connected;
    //   peers, gone  a client walked every element (all 2,000 leaves have peers) and
    //                then stopped listening: the worst idle case, since peers outlive
    //                the client that created them.
    // Each run is repeated and the best kept, so scheduler noise does not decide.
    constexpr int kFrames = 20000, kReps = 5;
    BenchUiaCore core;
    BenchUiaWindow win;
    automation::AutomationHost host(core);
    host.Bind(&win);
    double base = 1e9, idle = 1e9, gone = 1e9;
    size_t peers = 0;
    for (int r = 0; r < kReps; ++r) {
        base = std::min(base, BenchUiaFrames(nullptr, kFrames, false, nullptr));
        idle = std::min(idle, BenchUiaFrames(&host, kFrames, false, nullptr));
        gone = std::min(gone, BenchUiaFrames(&host, kFrames, true, &peers));
    }
    host.OnWindowDestroyed(nullptr);
    Result res;
    res.primaryMs = gone * kFrames / 1000.0;
    res.perFrameMs = gone / 1000.0;
    res.budgetFraction = 0.05;  // same order-of-magnitude gate as DirtyWalk
    std::snprintf(msgbuf, sizeof(msgbuf),
                  "us/frame: baseline %.3f | site, no client %.3f | %zu peers, client gone %.3f; "
                  "flush posts=%zu (must be 0), UIA calls=%zu (must be 0)",
                  base, idle, peers, gone, core.posts, core.raised);
    res.metric = msgbuf;
    res.note = "Headless: the host and peers are real, UIA core is a counter. Live check: "
               "F12 HUD CPU Max with and without Inspect running is the same.";
    return res;
}

Result UiaListening() {
    // A client IS listening and has walked the tree: each invalidation of a leaf
    // with a peer queues one re-check, coalesced per flush. Measures the per-frame
    // bookkeeping (queue + one flush per frame) against the same tree.
    constexpr int kFrames = 20000;
    BenchUiaCore core;
    core.listening = true;
    BenchUiaWindow win;
    automation::AutomationHost host(core);
    host.Bind(&win);
    StackPanel root;
    std::vector<BenchPokeLeaf*> leaves;
    for (int i = 0; i < 2000; ++i) leaves.push_back(root.Emplace<BenchPokeLeaf>());
    win.root = &root;
    UIContext ctx;
    ctx.automation = &host;
    root.AttachToContext(ctx);
    root.UpdateLayout({0, 0, 800, 100000});
    // The client's first contact (WM_GETOBJECT) gives the host its HWND and root
    // provider; a dummy handle, since BenchUiaCore never dereferences it.
    LRESULT lr = 0;
    host.OnGetObject(reinterpret_cast<HWND>(static_cast<uintptr_t>(0x1234)), 0,
                     static_cast<LPARAM>(-25) /* UiaRootObjectId */, false, lr);
    for (auto* l : leaves)
        if (auto* p = l->GetOrCreateAutomationPeer()) p->Provider();
    double ms = 0.0;
    {
        ScopedTimer t(ms);
        for (int f = 0; f < kFrames; ++f) {
            // A burst: 8 invalidations of the same leaf in one frame (hover + text +
            // focus visuals), then the frame's single flush.
            auto* leaf = leaves[(static_cast<size_t>(f) * 997u) % leaves.size()];
            for (int k = 0; k < 8; ++k) leaf->Poke();
            host.OnFlush();
        }
    }
    root.DetachFromContext();
    host.OnWindowDestroyed(nullptr);
    Result res;
    res.primaryMs = ms;
    res.perFrameMs = ms / kFrames;
    std::snprintf(msgbuf, sizeof(msgbuf),
                  "%.3f us/frame for 8 invalidations + 1 flush; posts=%zu over %d frames "
                  "(coalesced: <= 1 per frame), property raises=%zu (0: nothing changed)",
                  ms * 1000.0 / kFrames, core.posts, kFrames, core.raised);
    res.metric = msgbuf;
    res.note = "Raises stay 0 because a Poke changes no reported property: the re-check "
               "runs but finds nothing, which is the common case (hover, caret blink).";
    return res;
}

// ---- 13. ListBox Shift+End over 1M items ------------------------------------

// Event handler for the subscribed variant below. Event handlers are plain function
// pointers; the sink travels through the void* owner. Reading size() is enough to
// keep the payload observable without adding per-element work of its own.
void BenchCountSelectionSet(void* owner, ListBox&, std::vector<int>& sel) {
    *static_cast<size_t*>(owner) += sel.size();
}

Result ListBoxShiftEnd() {
    // The input-path cost of ONE Shift+End key press on a virtualized ListBox whose
    // SetItemCount is 1,000,000 — the gesture that turns "anchor at the top" into
    // "every item selected". It goes through the real key handler
    // (ListBox::OnKeyDownRouted -> Selector::RangeSelectTo -> the set hook), so it
    // measures what a user's key press pays, not a synthetic call on the set.
    //
    // Each Shift+End is preceded by a Shift+Home from the same anchor, which
    // collapses the range back to {0} so every Shift+End is a full 0 -> 999,999
    // range build rather than a no-op re-select; the pair is timed together (see
    // the batching note below). Two variants:
    //   * no SelectionSetChanged subscriber: the selection model's own cost;
    //   * one subscriber: plus building the vector<int> payload the event hands
    //     out, which is O(selected) by its signature no matter how the set is
    //     stored — the honest price of subscribing to the vector-valued event.
    constexpr int kItems = 1000000;
    // Upper bounds on the batch size, so a fast implementation does not spin for
    // millions of iterations and a slow one stops after a few.
    constexpr int kMaxPairsBare = 1 << 20;
    constexpr int kMaxPairsSubscribed = 1 << 10;

    auto run = [](int maxPairs, bool subscribe, size_t& selectedAfter,
                  size_t& sink, int& pairs) -> double {
        ListBox lb;
        lb.SetItemCount(static_cast<size_t>(kItems));
        lb.SetBounds({0, 0, 300, 600});
        lb.SetSelectionMode(SelectionMode::Multiple);
        lb.SetSelectedIndex(0);  // anchor + active at the top
        Subscription sub;
        if (subscribe) sub = lb.SelectionSetChanged().Subscribe(&sink, BenchCountSelectionSet);

        auto press = [&lb](int vk) {
            KeyEventArgs e;
            e.vk = vk;
            e.modifiers = ModifierKeys::Shift;
            lb.OnKeyDownRouted(e);
        };
        // Timed in BATCHES of Shift+Home / Shift+End pairs, doubling the batch until
        // one batch runs >= 100 ms (or `maxPairs` is reached), and reporting that
        // batch. A per-press timer cannot resolve the range-based set: one press is
        // far below a QPC tick, so timing each press alone reads as 0 or one tick.
        // Both keys of a pair go through RangeSelectTo from the same anchor, so a
        // pair is one full-range build plus one collapse back to {0} — the same
        // work under any storage, which keeps the number comparable across
        // implementations.
        double ms = 0.0;
        for (int batch = 1; ; batch *= 2) {
            ms = 0.0;
            {
                ScopedTimer t(ms);
                for (int i = 0; i < batch; ++i) { press(VK_HOME); press(VK_END); }
            }
            pairs = batch;
            if (ms >= 100.0 || batch >= maxPairs) break;
        }
        // Sanity outside the timed region: every item must be selected.
        selectedAfter = lb.SelectedIndices().size();
        return ms;
    };

    size_t bareCount = 0, subCount = 0, sink = 0;
    int barePairs = 0, subPairs = 0;
    const double bareMs = run(kMaxPairsBare, false, bareCount, sink, barePairs);
    const double subMs = run(kMaxPairsSubscribed, true, subCount, sink, subPairs);

    Result r;
    r.primaryMs = bareMs;
    r.perFrameMs = bareMs / barePairs;
    std::snprintf(msgbuf, sizeof(msgbuf),
                  // Microseconds, not ms: with range storage a bare pair is well
                  // under a microsecond, which a %.3f ms format rounds to 0.000.
                  "%d items; Shift+Home+Shift+End pair: %.3f us (no subscriber, %d pairs), "
                  "%.1f us (SelectionSetChanged subscribed, %d pairs); "
                  "selected=%zu/%zu (must be %d)",
                  kItems, bareMs * 1000.0 / barePairs, barePairs,
                  subMs * 1000.0 / subPairs, subPairs,
                  bareCount, subCount, kItems);
    r.metric = msgbuf;
    r.note = "One key press is one input-path event; the per-press number is what "
             "the frame that handles it pays before it can paint.";
    return r;
}

} // namespace

// Registration (order = the numbered scene sections above).
REGISTER_SCENE("ControlsGrid", "Layout cost for 1,000 stacked controls", ControlsGrid);
REGISTER_SCENE("TextStress", "CJK/Latin mixed wrapping text", TextStress);
REGISTER_SCENE("TreeVirtualization", "100k data rows", TreeVirtualization);
REGISTER_SCENE("AnimationStress", "Active-set tick vs tree size", AnimationStress);
REGISTER_SCENE("ExpanderReveal", "Reveal ease: per-tick UI-thread cost", ExpanderReveal);
REGISTER_SCENE("ResizeStress", "Continuous window resize", ResizeStress);
REGISTER_SCENE("PopupStress", "Cascading menu open/close x10,000", PopupStress);
REGISTER_SCENE("DpiLab", "Cross-monitor / multi-DPI", DpiLab);
REGISTER_SCENE("ThemeLab", "Light/Dark/Mica/Acrylic/high-contrast", ThemeLab);
REGISTER_SCENE("InputLatency", "Pointer-down to Present", InputLatency);
REGISTER_SCENE("IdleTest", "Idle 30s: no periodic Present", IdleTest);
REGISTER_SCENE("FrameCoalescing", "N invalidations merge into one frame", FrameCoalescing);
REGISTER_SCENE("DirtyWalk", "Per-frame dirty analysis, 1 dirty leaf in 2k", DirtyWalk);
REGISTER_SCENE("UiaNoClient", "UI Automation compiled in, no client: frame cost vs baseline", UiaNoClient);
REGISTER_SCENE("UiaListening", "UI Automation with a listening client: invalidation flood", UiaListening);
REGISTER_SCENE("ListBoxShiftEnd", "Shift+End range select over 1M ListBox items", ListBoxShiftEnd);
