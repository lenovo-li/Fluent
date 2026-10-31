// AutomationPeer.cpp — peer base and the UIElement automation members.
//
// UIElement's automation methods live here rather than in UIElement.cpp so the
// element core does not depend on the automation layer beyond one include for
// the AutomationState definition its destructor needs.
#include "AutomationPeer.h"

#include <atomic>

#include "AutomationSite.h"
#include "UiaProvider.h"
#include "../core/UIElement.h"
#include "../input/FocusManager.h"

namespace fluent::automation {
void AutomationStateDeleter::operator()(AutomationState* p) const { delete p; }

namespace {
std::atomic<uint32_t> g_nextSerial{1};
std::atomic<size_t> g_liveCount{0};
const std::wstring kEmpty;
}  // namespace

AutomationPeer::AutomationPeer(UIElement& owner) : owner_(owner), serial_(g_nextSerial++) {
    ++g_liveCount;
}

AutomationPeer::~AutomationPeer() {
    // Normally Release() already froze the provider. A peer destroyed any other way
    // (never attached to a site) must still not leave a provider pointing at it.
    if (provider_) FreezeProvider(provider_.Get());
    --g_liveCount;
}

size_t AutomationPeer::LiveCount() { return g_liveCount.load(); }

ControlType AutomationPeer::GetControlType() const {
    return IsControlElement() ? ControlType::Custom : ControlType::Pane;
}

std::wstring AutomationPeer::GetName() const { return owner_.AutomationName(); }

// An element no control mapped is still reachable by keyboard if it is focusable;
// keep it in the control view (as Custom) so a screen reader can land on it.
// Everything else (layout containers, decoration) is raw-view only.
bool AutomationPeer::IsControlElement() const { return owner_.IsFocusable(); }

IUnknown* AutomationPeer::Provider() {
    if (!provider_) CreateElementProvider(*this, provider_.ReleaseAndGetAddressOf());
    return provider_.Get();
}

void AutomationPeer::Release(IAutomationSite* site) {
    if (!provider_) return;
    FreezeProvider(provider_.Get());  // from now on it never reaches this peer
    if (site) site->QueueDisconnect(provider_.Get());
    provider_.Reset();
}

void AutomationPeer::SetFocusFromAutomation() {
    if (FocusManager* fm = owner_.Context().focus) {
        if (owner_.IsFocusable()) fm->SetFocus(&owner_);
    }
}

void AutomationPeer::OnOwnerInvalidated() {
    if (provider_ && owner_.Context().automation)
        owner_.Context().automation->QueueChanged(provider_.Get());
}

}  // namespace fluent::automation

namespace fluent {
using automation::AutomationPeer;
using automation::AutomationState;

AutomationState& UIElement::EnsureAutomationState() {
    if (!automation_) automation_.reset(new AutomationState());
    return *automation_;
}

void UIElement::SetAutomationName(std::wstring name) {
    if (!automation_ && name.empty()) return;
    AutomationState& st = EnsureAutomationState();
    if (st.name == name) return;
    st.name = std::move(name);
    if (st.peer) st.peer->OnOwnerInvalidated();
}
const std::wstring& UIElement::AutomationName() const {
    return automation_ ? automation_->name : automation::kEmpty;
}
void UIElement::SetAutomationId(std::wstring id) {
    if (!automation_ && id.empty()) return;
    EnsureAutomationState().id = std::move(id);
}
const std::wstring& UIElement::AutomationId() const {
    return automation_ ? automation_->id : automation::kEmpty;
}

std::unique_ptr<AutomationPeer> UIElement::CreateAutomationPeer() {
    return std::make_unique<AutomationPeer>(*this);
}

AutomationPeer* UIElement::AutomationPeerIfAny() const {
    return automation_ ? automation_->peer.get() : nullptr;
}

AutomationPeer* UIElement::GetOrCreateAutomationPeer() {
    if (automation_ && automation_->peer) return automation_->peer.get();
    // Only while attached to a window with a site: that is the peer lifetime rule
    // (AutomationSite.h) the provider depends on.
    if (!IsAttached() || !Context().automation) return nullptr;
    AutomationState& st = EnsureAutomationState();
    st.peer = CreateAutomationPeer();
    if (st.peer) SetObserveInvalidation(true);
    return st.peer.get();
}

void UIElement::ReleaseAutomationPeer() {
    if (!automation_ || !automation_->peer) return;
    SetObserveInvalidation(false);
    // Non-virtual, touches only the peer and the site: safe from ~UIElement, where
    // the derived layers are already gone.
    automation_->peer->Release(Context().automation);
    automation_->peer.reset();
}

void UIElement::OnObservedInvalidation() {
    if (automation_ && automation_->peer) automation_->peer->OnOwnerInvalidated();
}

RectDip UIElement::BoundsInHostDip() const {
    RectDip r = bounds_;
    // Every Visual in the tree is a UIElement (UIElement is Visual's only
    // subclass), so the cast is exact.
    for (const Visual* p = Parent(); p; p = p->Parent())
        r = static_cast<const UIElement*>(p)->MapChildRectToSelf(r);
    return r;
}

}  // namespace fluent
