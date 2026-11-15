// ControlPeers.h — UI Automation peers for the built-in controls.
//
// Each control overrides UIElement::CreateAutomationPeer() (declared in its own
// header, defined in ControlPeers.cpp so control headers never include this one)
// and returns one of these. Extension libraries derive from them the same way: a
// control derived from Button keeps ButtonPeer unless it overrides
// CreateAutomationPeer, and can subclass ButtonPeer to change only the name.
//
// Every getter reads the control's current state; nothing is cached here.
#pragma once

#include "AutomationPeer.h"
#include "../core/Subscription.h"

namespace fluent {
class ButtonBase;
class ToggleButton;
class RadioButton;
class TextBlock;
class TextEditBase;
class GroupBox;
class ContentControl;
class TextBox;
}  // namespace fluent

namespace fluent::automation {

// Button / BigButton: Button + Invoke. Name = the button's text.
class ButtonPeer : public AutomationPeer, public IInvokePeer {
public:
    explicit ButtonPeer(ButtonBase& owner);
    ControlType GetControlType() const override { return ControlType::Button; }
    std::wstring GetName() const override;
    bool IsControlElement() const override { return true; }
    IInvokePeer* AsInvoke() override { return this; }
    void Invoke() override;
};

// CheckBox (CheckBox) and ToggleSwitch (Button, as WinUI reports it): Toggle.
class TogglePeer : public ButtonPeer, public ITogglePeer {
public:
    TogglePeer(ToggleButton& owner, ControlType type);
    ControlType GetControlType() const override { return type_; }
    IInvokePeer* AsInvoke() override { return nullptr; }  // Toggle, not Invoke
    ITogglePeer* AsToggle() override { return this; }
    ToggleState GetToggleState() const override;
    void Toggle() override;

private:
    ControlType type_;
};

// RadioButton: SelectionItem.
class RadioButtonPeer : public ButtonPeer, public ISelectionItemPeer {
public:
    explicit RadioButtonPeer(RadioButton& owner);
    ControlType GetControlType() const override { return ControlType::RadioButton; }
    IInvokePeer* AsInvoke() override { return nullptr; }
    ISelectionItemPeer* AsSelectionItem() override { return this; }
    bool IsSelected() const override;
    void Select() override;
};

// TextBlock: Text, Name = its text. A label is content, not an interactive control,
// but UIA lists Text in the control view so screen readers read it.
class TextBlockPeer : public AutomationPeer {
public:
    explicit TextBlockPeer(TextBlock& owner);
    ControlType GetControlType() const override { return ControlType::Text; }
    std::wstring GetName() const override;
    bool IsControlElement() const override { return true; }
};

// TextBox / TextArea / PasswordBox: Edit + Value. A password reports no value
// and IsPassword = true; read from the TextBox each time (SetPassword can change it).
class TextEditPeer : public AutomationPeer, public IValuePeer, public ITextPeer {
public:
    // `textBox` is the owner when it is a TextBox (password-capable), else null.
    TextEditPeer(TextEditBase& owner, const TextBox* textBox);
    ControlType GetControlType() const override { return ControlType::Edit; }
    bool IsControlElement() const override { return true; }
    bool IsPassword() const override;
    IValuePeer* AsValue() override { return this; }
    std::wstring GetValue() const override;
    bool IsReadOnly() const override;
    void SetValue(const std::wstring& value) override;
    // TextArea (no TextBox owner) is a document: no ValueChanged.
    bool ReportsValueChanges() const override { return textBox_ != nullptr; }

    // Text pattern. A password exposes no text (and no TextPattern at all).
    ITextPeer* AsText() override { return IsPassword() ? nullptr : this; }
    const std::wstring& Text() const override;
    size_t SelectionStart() const override;
    size_t SelectionLength() const override;
    void SelectText(size_t start, size_t length) override;
    size_t OffsetAtPoint(float x, float y) const override;
    bool IsTextReadOnly() const override { return IsReadOnly(); }

private:
    const TextBox* textBox_;
    // Text_TextChanged / Text_TextSelectionChanged come straight from the control's
    // events (queued, coalesced, listener-gated): no diff, so a keystroke in a
    // multi-MB TextArea never copies or compares the document.
    Subscription textSub_, selSub_;
};

// GroupBox: Group, Name = Header().
class GroupBoxPeer : public AutomationPeer {
public:
    explicit GroupBoxPeer(GroupBox& owner);
    ControlType GetControlType() const override { return ControlType::Group; }
    std::wstring GetName() const override;
    bool IsControlElement() const override { return true; }
};

}  // namespace fluent::automation
