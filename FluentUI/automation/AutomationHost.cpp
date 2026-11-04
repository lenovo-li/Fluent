// AutomationHost.cpp — see AutomationHost.h for the queues and the teardown order.
#include "AutomationHost.h"

#include <algorithm>

#include "AutomationPeer.h"
#include "AutomationTree.h"
#include "UiaProvider.h"
#include "../core/UIElement.h"
#include "../fl_common.h"

namespace fluent::automation {

void ReleaseCaptionProviders(IUiaCore& core, Microsoft::WRL::ComPtr<IUnknown> (&caps)[3]);

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
bool AutomationHost::HostHasOsFocus() const {
    if (!hwnd_) return false;
    const HWND f = GetFocus();
    return f == hwnd_ || (focusProxy_ && f == focusProxy_);
}

// --- Popup windows --------------------------------------------------------------

namespace {
AutomationPeer* PopupContentPeer(IAutomationWindow* w) {
    UIElement* content = w && w->AutomationRootCount() ? w->AutomationRootAt(0) : nullptr;
    return content ? content->GetOrCreateAutomationPeer() : nullptr;
}
}  // namespace

void AutomationHost::OnPopupShown(HWND hwnd) {
    if (torn_down_ || !hwnd || !core_->ClientsAreListening()) return;
    if (!root_) {
        hwnd_ = hwnd;
        const HRESULT hr = CreateWindowRootProvider(hwnd, GetCurrentThreadId(), this,
                                                    root_.ReleaseAndGetAddressOf());
        if (FAILED(hr)) {
            Trace("Uia", "CreateWindowRootProvider(popup) FAILED", hr);
            return;
        }
    }
    AutomationPeer* peer = PopupContentPeer(window_);
    if (peer && peer->PopupOpenedEventId()) QueueEvent(peer->Provider(), peer->PopupOpenedEventId());
}

void AutomationHost::OnPopupHidden() {
    if (torn_down_ || !root_ || !core_->ClientsAreListening()) return;
    UIElement* content = window_ && window_->AutomationRootCount() ? window_->AutomationRootAt(0) : nullptr;
    AutomationPeer* peer = content ? content->AutomationPeerIfAny() : nullptr;  // never create on close
    if (peer && peer->PopupClosedEventId() && peer->HasProvider())
        QueueEvent(peer->Provider(), peer->PopupClosedEventId());
}
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

void AutomationHost::PostAction(IUnknown* provider, PeerAction action, const wchar_t* value,
                                double a, double b) {
    if (torn_down_ || !provider) return;
    actions_.push_back({Ref(provider), action, value ? value : L"", a, b});
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
bool AutomationHost::ClientsAreListening() const { return !torn_down_ && core_->ClientsAreListening(); }
void AutomationHost::QueueInvoked(IUnknown* p) { if (p && !torn_down_) { PushUnique(invoked_, p); RequestFlush(); } }
void AutomationHost::QueueSelected(IUnknown* p) { if (p && !torn_down_) { PushUnique(selected_, p); RequestFlush(); } }

void AutomationHost::QueueEvent(IUnknown* p, int eventId) {
    if (!p || torn_down_ || !root_ || !core_->ClientsAreListening()) return;
    for (const auto& e : events_)
        if (e.first.Get() == p && e.second == eventId) return;
    events_.emplace_back(Ref(p), eventId);
    RequestFlush();
}

void AutomationHost::QueueTrim(IUnknown* p) {
    if (!p || torn_down_) return;
    PushUnique(trims_, p);
    RequestFlush();
}

void AutomationHost::QueueChildrenChanged(IUnknown* parent) {
    // No client ever connected (no root): nobody holds a view of the tree to fix.
    if (!root_ || torn_down_ || !core_->ClientsAreListening()) return;
    if (parent) PushUnique(structure_, parent);
    else structureRoot_ = true;
    RequestFlush();
}


// --- Flush --------------------------------------------------------------------

void AutomationHost::OnFlush() {
    flushPosted_ = false;
    if (torn_down_) return;
    for (int round = 0;
         round < kMaxFlushRounds &&
         (!actions_.empty() || !captionInvokes_.empty() || !disconnects_.empty());
         ++round) {
        std::vector<Action> actions;
        actions.swap(actions_);
        for (const Action& a : actions) RunPeerAction(a.provider.Get(), a.action, a.value, a.a, a.b);
        std::vector<int> caps;
        caps.swap(captionInvokes_);
        for (int hit : caps)
            if (window_) window_->AutomationCaptionInvoke(hit);  // may destroy the window
        if (torn_down_) return;
        FlushDisconnects();
    }
    // Cache trims: run the container's re-check (it releases the excess) whether or
    // not a client listens; property raises inside still honour the listener gate.
    std::vector<Ref> trims;
    trims.swap(trims_);
    for (const Ref& t : trims)
        if (AutomationPeer* p = PeerFromProvider(t.Get())) p->OnFlushRecheck();
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
    for (const Ref& d : batch) {
        const HRESULT hr = core_->DisconnectProvider(d.Get());
        ++disconnected_;
        if (FAILED(hr)) {
            ++disconnectFailures_;
            Trace("Uia", "UiaDisconnectProvider(element) FAILED", hr);
        }
    }
}

void AutomationHost::RaiseEvents() {
    std::vector<Ref> changed;
    changed.swap(changed_);
    // Container re-checks run FIRST, before the event queues are taken: what they
    // queue (ChildrenInvalidated, an InfoBar's LiveRegionChanged, a menu item's
    // focus) then goes out in this flush instead of costing a second posted one.
    // Property raises stay below, after the listener check.
    const bool listening = core_->ClientsAreListening();
    if (listening)
        for (const Ref& c : changed)
            if (AutomationPeer* p = PeerFromProvider(c.Get())) p->OnFlushRecheck();
    std::vector<Ref> invoked, selected;
    invoked.swap(invoked_);
    selected.swap(selected_);
    Ref focus = std::move(focus_);
    std::vector<Ref> structure;
    structure.swap(structure_);
    std::vector<std::pair<Ref, int>> events;
    events.swap(events_);
    const bool structureRoot = structureRoot_, focusRoot = focusRoot_;
    structureRoot_ = focusRoot_ = false;
    // The client stopped listening since these were queued: drop them unread.
    // (The baseline then lags; a later client's first diff may carry an old "old
    // value", which UIA clients treat as advisory.)
    if (!listening) return;
    // A frozen provider (element gone since queuing) has no peer: skipped inside.
    for (const Ref& c : changed) RaiseChangedProperties(core_, c.Get());
    for (const Ref& i : invoked)
        if (PeerFromProvider(i.Get())) core_->RaiseEvent(i.Get(), kInvokedEventId);
    for (const auto& e : events)
        if (PeerFromProvider(e.first.Get())) core_->RaiseEvent(e.first.Get(), e.second);
    for (const Ref& sel : selected)
        if (PeerFromProvider(sel.Get())) core_->RaiseEvent(sel.Get(), kElementSelectedEventId);
    // Structure before focus: a client must be able to find the newly focused node.
    for (const Ref& p : structure)
        if (PeerFromProvider(p.Get())) core_->RaiseChildrenInvalidated(p.Get());
    if (structureRoot && root_) core_->RaiseChildrenInvalidated(root_.Get());
    if (focus && PeerFromProvider(focus.Get())) core_->RaiseEvent(focus.Get(), kFocusChangedEventId);
    else if (focusRoot && root_) core_->RaiseEvent(root_.Get(), kFocusChangedEventId);
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
    // Focus on a list announces its current item, as Narrator expects.
    if (AutomationPeer* item = peer ? peer->FocusedItem() : nullptr) peer = item;
    IUnknown* prov = peer ? peer->Provider() : nullptr;
    if (!prov) return;
    focus_ = prov;  // coalesced: only the last focus change of a batch is raised
    focusRoot_ = false;
    RequestFlush();
}

void AutomationHost::OnWindowFocused() {
    if (!root_ || torn_down_ || !core_->ClientsAreListening()) return;
    if (UIElement* f = FocusedElement()) {
        OnFocusChanged(f);
        return;
    }
    focus_.Reset();
    focusRoot_ = true;
    RequestFlush();
}

// --- Teardown -----------------------------------------------------------------

void AutomationHost::OnWindowDestroying() {
    if (torn_down_) return;
    torn_down_ = true;  // stop serving WM_GETOBJECT; stop queuing actions/events
    actions_.clear();
    captionInvokes_.clear();
    changed_.clear();
    invoked_.clear();
    selected_.clear();
    structure_.clear();
    trims_.clear();
    events_.clear();
    focus_.Reset();
    focusRoot_ = structureRoot_ = false;
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
    ReleaseCaptionProviders(*core_, captions_);
    if (!root_) return;  // no client ever asked: nothing was handed out
    // One summary line per window that had a client: proves element disconnects
    // happened and succeeded without a trace per element (a large tree has hundreds).
    TraceFmt("Uia", "element providers disconnected=%u failed=%u (window lifetime)",
             disconnected_, disconnectFailures_);
    // ReleaseWindowProvider disconnects first and detaches after (see its comment).
    if (hwnd) ReleaseWindowProvider(*core_, hwnd, root_.Get());
    else DetachProvider(root_.Get());
    root_.Reset();
}

}  // namespace fluent::automation
