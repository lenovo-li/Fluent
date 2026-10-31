// ControlPeers.cpp — the built-in controls' peers and their CreateAutomationPeer
// overrides (kept here so control headers never include the peer headers).
#include "ControlPeers.h"

#include "AutomationSite.h"

#include <algorithm>
#include "UiaProvider.h"

#include "../controls/primitives/ButtonBase.h"
#include "../controls/primitives/ToggleButton.h"
#include "../controls/RadioButton.h"
#include "../controls/TextBlock.h"
#include "../controls/TextBox.h"
#include "../controls/ToggleSwitch.h"
#include "../layout/GroupBox.h"

namespace fluent::automation {

// The owner type each peer was built for. Peers only ever see their own control,
// so the downcast from Owner() is exact.
template <class T>
static T& As(const AutomationPeer& p) {
    return static_cast<T&>(p.Owner());
}

ButtonPeer::ButtonPeer(ButtonBase& owner) : AutomationPeer(owner) {}
std::wstring ButtonPeer::GetName() const {
    const std::wstring& n = Owner().AutomationName();
    return n.empty() ? As<ButtonBase>(*this).Text() : n;
}
// Invoked / ElementSelected are raised for UIA-initiated activations (what test
// tools wait for). The activation runs app handlers that may destroy the element,
// and with it this peer, so nothing here touches `this` afterwards: the provider
// is held across the call and asked whether its peer survived (a dead element
// froze it).
static void QueueAfterActivate(IUnknown* provider, bool selected) {
    AutomationPeer* p = PeerFromProvider(provider);
    IAutomationSite* site = p ? p->Owner().AutomationSite() : nullptr;
    if (!site) return;
    if (selected) site->QueueSelected(provider);
    else site->QueueInvoked(provider);
}

void ButtonPeer::Invoke() {
    ComPtr<IUnknown> prov(Provider());
    As<ButtonBase>(*this).PerformActivate();  // may destroy *this
    QueueAfterActivate(prov.Get(), false);
}

TogglePeer::TogglePeer(ToggleButton& owner, ControlType type) : ButtonPeer(owner), type_(type) {}
ToggleState TogglePeer::GetToggleState() const {
    return As<ToggleButton>(*this).IsChecked() ? ToggleState::On : ToggleState::Off;
}
void TogglePeer::Toggle() { As<ToggleButton>(*this).PerformActivate(); }

RadioButtonPeer::RadioButtonPeer(RadioButton& owner) : ButtonPeer(owner) {}
bool RadioButtonPeer::IsSelected() const { return As<RadioButton>(*this).IsSelected(); }
void RadioButtonPeer::Select() {
    // Selecting the selected item is a no-op in UIA; activation would re-raise.
    if (IsSelected()) return;
    ComPtr<IUnknown> prov(Provider());
    As<RadioButton>(*this).PerformActivate();  // may destroy *this
    QueueAfterActivate(prov.Get(), true);
}

TextBlockPeer::TextBlockPeer(TextBlock& owner) : AutomationPeer(owner) {}
std::wstring TextBlockPeer::GetName() const {
    const std::wstring& n = Owner().AutomationName();
    return n.empty() ? As<TextBlock>(*this).Text() : n;
}

namespace {
constexpr int kTextChangedEventId = 20015;           // UIA_Text_TextChangedEventId
constexpr int kTextSelectionChangedEventId = 20014;  // UIA_Text_TextSelectionChangedEventId
void QueueOn(TextEditBase& e, int eventId) {
    AutomationPeer* p = e.AutomationPeerIfAny();
    IAutomationSite* site = e.AutomationSite();
    if (p && site && p->HasProvider()) site->QueueEvent(p->Provider(), eventId);
}
}  // namespace

TextEditPeer::TextEditPeer(TextEditBase& owner, const TextBox* textBox)
    : AutomationPeer(owner), textBox_(textBox) {
    textSub_ = owner.TextChanged().Subscribe(&owner, [](void* o, TextEditBase&, std::wstring&) {
        QueueOn(*static_cast<TextEditBase*>(o), kTextChangedEventId);
    });
    selSub_ = owner.SelectionChanged().Subscribe(
        &owner, [](void* o, TextEditBase&, TextEditBase::SelectionChangedArgs&) {
            QueueOn(*static_cast<TextEditBase*>(o), kTextSelectionChangedEventId);
        });
}
const std::wstring& TextEditPeer::Text() const { return As<TextEditBase>(*this).Text(); }
size_t TextEditPeer::SelectionStart() const { return As<TextEditBase>(*this).SelectionStart(); }
size_t TextEditPeer::SelectionLength() const { return As<TextEditBase>(*this).SelectionLength(); }
void TextEditPeer::SelectText(size_t start, size_t length) {
    auto& t = As<TextEditBase>(*this);
    const size_t n = t.Text().size();
    start = std::min(start, n);
    length = std::min(length, n - start);
    t.Select(static_cast<UINT32>(start), static_cast<UINT32>(length));
}
size_t TextEditPeer::OffsetAtPoint(float x, float y) const {
    return std::min<size_t>(As<TextEditBase>(*this).OffsetAtPoint(x, y), Text().size());
}
bool TextEditPeer::IsPassword() const { return textBox_ && textBox_->IsPassword(); }
std::wstring TextEditPeer::GetValue() const {
    return IsPassword() ? std::wstring() : As<TextEditBase>(*this).Text();
}
bool TextEditPeer::IsReadOnly() const { return As<TextEditBase>(*this).ReadOnly(); }
void TextEditPeer::SetValue(const std::wstring& value) {
    // The user edit path, not SetText: TextChanged must fire and MaxLength / the
    // input filter must apply, or the app's model silently diverges from the field.
    if (!IsReadOnly()) As<TextEditBase>(*this).ReplaceAllAsUserEdit(value);
}

GroupBoxPeer::GroupBoxPeer(GroupBox& owner) : AutomationPeer(owner) {}
std::wstring GroupBoxPeer::GetName() const {
    const std::wstring& n = Owner().AutomationName();
    return n.empty() ? As<GroupBox>(*this).Header() : n;
}

}  // namespace fluent::automation

namespace fluent {
using namespace automation;

std::unique_ptr<AutomationPeer> ButtonBase::CreateAutomationPeer() {
    return std::make_unique<ButtonPeer>(*this);
}
std::unique_ptr<AutomationPeer> ToggleButton::CreateAutomationPeer() {
    return std::make_unique<TogglePeer>(*this, ControlType::CheckBox);
}
std::unique_ptr<AutomationPeer> ToggleSwitch::CreateAutomationPeer() {
    // WinUI reports a ToggleSwitch as a Button with the Toggle pattern.
    return std::make_unique<TogglePeer>(*this, ControlType::Button);
}
std::unique_ptr<AutomationPeer> RadioButton::CreateAutomationPeer() {
    return std::make_unique<RadioButtonPeer>(*this);
}
std::unique_ptr<AutomationPeer> TextBlock::CreateAutomationPeer() {
    return std::make_unique<TextBlockPeer>(*this);
}
std::unique_ptr<AutomationPeer> TextEditBase::CreateAutomationPeer() {
    return std::make_unique<TextEditPeer>(*this, nullptr);
}
std::unique_ptr<AutomationPeer> TextBox::CreateAutomationPeer() {
    return std::make_unique<TextEditPeer>(*this, this);
}
std::unique_ptr<AutomationPeer> GroupBox::CreateAutomationPeer() {
    return std::make_unique<GroupBoxPeer>(*this);
}

}  // namespace fluent
