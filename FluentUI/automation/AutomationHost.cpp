// AutomationHost.cpp — see AutomationHost.h for the queues and the teardown order.
#include "AutomationHost.h"

#include <algorithm>

#include "AutomationPeer.h"
#include "AutomationTree.h"
#include "UiaProvider.h"
#include "../core/UIElement.h"
#include "../fl_common.h"

namespace fluent::automation {
namespace {
// UIA event ids (uiautomationclient.h), kept here so this file needs no UIA header.
constexpr int kFocusChangedEventId = 20005;     // UIA_AutomationFocusChangedEventId
constexpr int kInvokedEventId = 20009;          // UIA_Invoke_InvokedEventId
constexpr int kElementSelectedEventId = 20012;  // UIA_SelectionItem_ElementSelectedEventId
// A flush that keeps producing work (an action whose handler queues another
// action, forever) must not spin the UI thread; leftover work goes to the next
// posted flush instead.
constexpr int kMaxFlushRounds = 8;

void PushUnique(std::vector<Microsoft::WRL::ComPtr<IUnknown>>& v, IUnknown* p) {
    for (const auto& x : v)
        if (x.Get() == p) return;
    v.emplace_back(p);
}
}  // namespace

AutomationHost::AutomationHost() : core_(&RealUiaCore()) {}

AutomationHost::~AutomationHost() {
    // The window always tears down while its HWND is alive. If it did not (a host
    // object that never created a window), make everything inert before dropping it.
    for (auto& d : disconnects_) FreezeProvider(d.Get());
    if (root_) DetachProvider(root_.Get());
}

bool AutomationHost::OnGetObject(HWND hwnd, WPARAM wp, LPARAM lp, bool destroying,
                                 LRESULT& result) {
    if (torn_down_ || !ShouldServeUiaRoot(lp, destroying)) return false;
    if (!root_) {
        // First client contact: the only allocation UIA causes for a window until a
        // client navigates further. This thread is the window's thread.
        hwnd_ = hwnd;
        const HRESULT hr = CreateWindowRootProvider(hwnd, GetCurrentThreadId(), this,
                                                    root_.ReleaseAndGetAddressOf());
        if (FAILED(hr)) {
            Trace("Uia", "CreateWindowRootProvider FAILED", hr);
            return false;
        }
    }
    result = core_->ReturnProvider(hwnd, wp, lp, root_.Get());
    return true;
}

// --- IAutomationSite: window queries ------------------------------------------

double AutomationHost::Dpi() const {
    const UINT d = hwnd_ ? GetDpiForWindow(hwnd_) : 0;  // 0 = not a window (tests)
    return d ? d : 96.0;
}
POINT AutomationHost::ClientOriginOnScreen() const {
    POINT p{0, 0};
    if (hwnd_) ClientToScreen(hwnd_, &p);
    return p;
}
bool AutomationHost::HostHasOsFocus() const { return hwnd_ && GetFocus() == hwnd_; }
size_t AutomationHost::RootElementCount() const {
    return window_ ? window_->AutomationRootCount() : 0;
}
UIElement* AutomationHost::RootElementAt(size_t i) const {
    return window_ ? window_->AutomationRootAt(i) : nullptr;
}
UIElement* AutomationHost::FocusedElement() const {
    return window_ ? window_->AutomationFocusedElement() : nullptr;
}

// --- IAutomationSite: queues --------------------------------------------------

void AutomationHost::RequestFlush() {
    if (flushPosted_ || torn_down_ || !hwnd_) return;
    flushPosted_ = true;
    core_->PostToHost(hwnd_, WM_FLUENT_UIA_FLUSH);
}

void AutomationHost::QueueDisconnect(IUnknown* frozen) {
    if (!frozen) return;
    disconnects_.emplace_back(frozen);
    RequestFlush();
}

void AutomationHost::PostAction(IUnknown* provider, PeerAction action, const wchar_t* value) {
    if (torn_down_ || !provider) return;
    actions_.push_back({Ref(provider), action, value ? value : L""});
    RequestFlush();
}

// Re-checks run on every invalidation of an element that has a peer, and peers
// outlive the client that created them (Inspect walks the tree once and closes).
// So the listening gate is HERE, per invalidation, not only at peer creation:
// without a listener an invalidation costs this one call and posts nothing.
void AutomationHost::QueueChanged(IUnknown* p) {
    if (!p || torn_down_ || !core_->ClientsAreListening()) return;
    PushUnique(changed_, p);
    RequestFlush();
}
void AutomationHost::QueueInvoked(IUnknown* p) { if (p && !torn_down_) { PushUnique(invoked_, p); RequestFlush(); } }
void AutomationHost::QueueSelected(IUnknown* p) { if (p && !torn_down_) { PushUnique(selected_, p); RequestFlush(); } }


// --- Flush --------------------------------------------------------------------

void AutomationHost::OnFlush() {
    flushPosted_ = false;
    if (torn_down_) return;
    for (int round = 0; round < kMaxFlushRounds && (!actions_.empty() || !disconnects_.empty());
         ++round) {
        std::vector<Action> actions;
        actions.swap(actions_);
        for (const Action& a : actions) RunPeerAction(a.provider.Get(), a.action, a.value);
        FlushDisconnects();
    }
    RaiseEvents();
    // Leftover work after the round cap: post again. But disconnects left only
    // because we are inside SendMessage (a modal loop nested in a sent message
    // still dispatches posted messages) must NOT re-post, or each flush would post
    // the next and spin the thread for as long as the send lasts. They go out with
    // the next flush anything else requests, or at window teardown.
    if (!actions_.empty() || (!disconnects_.empty() && !core_->InSendMessage())) RequestFlush();
}

void AutomationHost::FlushDisconnects() {
    // A posted message is never inside SendMessage, but the destroy path can be.
    if (disconnects_.empty() || core_->InSendMessage()) return;
    std::vector<Ref> batch;
    batch.swap(disconnects_);
    for (const Ref& d : batch) core_->DisconnectProvider(d.Get());
}

void AutomationHost::RaiseEvents() {
    std::vector<Ref> changed, invoked, selected;
    changed.swap(changed_);
    invoked.swap(invoked_);
    selected.swap(selected_);
    Ref focus = std::move(focus_);
    // The client stopped listening since these were queued: drop them unread.
    // (The baseline then lags; a later client's first diff may carry an old "old
    // value", which UIA clients treat as advisory.)
    if (!core_->ClientsAreListening()) return;
    // A frozen provider (element gone since queuing) has no peer: skipped inside.
    for (const Ref& c : changed) RaiseChangedProperties(core_, c.Get());
    for (const Ref& i : invoked)
        if (PeerFromProvider(i.Get())) core_->RaiseEvent(i.Get(), kInvokedEventId);
    for (const Ref& sel : selected)
        if (PeerFromProvider(sel.Get())) core_->RaiseEvent(sel.Get(), kElementSelectedEventId);
    if (focus && PeerFromProvider(focus.Get())) core_->RaiseEvent(focus.Get(), kFocusChangedEventId);
}

void AutomationHost::OnFocusChanged(UIElement* newFocus) {
    // No client has ever asked about this window: no root, so nothing to tell.
    if (!root_ || torn_down_ || !newFocus) return;
    // A client connected once but none listens now: do not create peers on the
    // input path for events nobody will receive.
    if (!core_->ClientsAreListening()) return;
    UIElement* listed = NearestListed(*this, newFocus);
    if (!listed) return;
    AutomationPeer* peer = listed->GetOrCreateAutomationPeer();
    IUnknown* prov = peer ? peer->Provider() : nullptr;
    if (!prov) return;
    focus_ = prov;  // coalesced: only the last focus change of a batch is raised
    RequestFlush();
}

// --- Teardown -----------------------------------------------------------------

void AutomationHost::OnWindowDestroying() {
    if (torn_down_) return;
    torn_down_ = true;  // stop serving WM_GETOBJECT; stop queuing actions/events
    actions_.clear();
    changed_.clear();
    invoked_.clear();
    selected_.clear();
    focus_.Reset();
}

void AutomationHost::OnWindowDestroyed(HWND hwnd) {
    OnWindowDestroying();
    // Element providers frozen by the detach that ran in between.
    if (core_->InSendMessage()) {
        TraceMsg("Uia", "element disconnects skipped on destroy: inside SendMessage");
        disconnects_.clear();  // frozen and inert; UIA releases them at process exit
    } else {
        FlushDisconnects();
    }
    if (!root_) return;  // no client ever asked: nothing was handed out
    // ReleaseWindowProvider disconnects first and detaches after (see its comment).
    if (hwnd) ReleaseWindowProvider(*core_, hwnd, root_.Get());
    else DetachProvider(root_.Get());
    root_.Reset();
}

}  // namespace fluent::automation
