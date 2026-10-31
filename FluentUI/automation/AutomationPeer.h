// AutomationPeer.h — the UI Automation face of one element, without COM.
//
// Every UIElement can have at most one peer, created LAZILY the first time a UIA
// client navigates to it (or it receives focus while a client listens). Without a
// client, no peer exists and an element pays one null pointer. Controls customise
// UIA by overriding UIElement::CreateAutomationPeer() (protected, stable API; see
// docs/controls/inheritance-guide.md) and returning a subclass of this class.
//
// A peer answers from the element's CURRENT state on every call (no snapshot, no
// tree walk per property), so getters stay O(1). The COM provider
// (UiaElementProvider.cpp) only converts types and owns no semantics.
//
// Patterns are small interfaces a peer subclass also implements; the As*() hooks
// return `this` cast, or null when unsupported. Pattern ACTIONS (Invoke, Toggle,
// ...) are never called from inside a COM call: the provider posts them and the
// host runs them from a later message (AutomationSite.h explains why).
#pragma once

#include <memory>
#include <string>

#include "../fl_common.h"
#include "AutomationTypes.h"

namespace fluent {
class UIElement;
}

namespace fluent::automation {

class IAutomationSite;

class IInvokePeer {
public:
    virtual ~IInvokePeer() = default;
    virtual void Invoke() = 0;
};
class ITogglePeer {
public:
    virtual ~ITogglePeer() = default;
    virtual ToggleState GetToggleState() const = 0;
    virtual void Toggle() = 0;
};
class IValuePeer {
public:
    virtual ~IValuePeer() = default;
    virtual std::wstring GetValue() const = 0;
    virtual bool IsReadOnly() const = 0;
    virtual void SetValue(const std::wstring& value) = 0;
    // False for documents (multi-line text): the value is still readable, but
    // ValueChanged is not raised, so a keystroke never copies the whole text.
    virtual bool ReportsValueChanges() const { return true; }
};
class ISelectionItemPeer {
public:
    virtual ~ISelectionItemPeer() = default;
    virtual bool IsSelected() const = 0;
    virtual void Select() = 0;
};

class AutomationPeer;

// Per-element automation storage behind UIElement::automation_ (lazily allocated).
struct AutomationState {
    std::wstring name;
    std::wstring id;
    std::unique_ptr<AutomationPeer> peer;
};

class AutomationPeer {
public:
    explicit AutomationPeer(UIElement& owner);
    virtual ~AutomationPeer();
    AutomationPeer(const AutomationPeer&) = delete;
    AutomationPeer& operator=(const AutomationPeer&) = delete;

    UIElement& Owner() const { return owner_; }
    // Process-unique, assigned at creation (not the element address, which the
    // allocator reuses), so a re-created element never inherits an old RuntimeId.
    uint32_t Serial() const { return serial_; }

    virtual ControlType GetControlType() const;
    // Default: Owner().AutomationName(), else the element's own text if any.
    virtual std::wstring GetName() const;
    // Control view = what screen readers walk. Layout containers are raw-only.
    virtual bool IsControlElement() const;
    virtual bool IsContentElement() const { return IsControlElement(); }
    virtual bool IsPassword() const { return false; }

    virtual IInvokePeer* AsInvoke() { return nullptr; }
    virtual ITogglePeer* AsToggle() { return nullptr; }
    virtual IValuePeer* AsValue() { return nullptr; }
    virtual ISelectionItemPeer* AsSelectionItem() { return nullptr; }

    // The COM provider for this peer, created on first use (IUnknown, not AddRef'd).
    IUnknown* Provider();
    bool HasProvider() const { return provider_ != nullptr; }

    // Peers alive in the process. The zero-cost tests assert this stays 0.
    static size_t LiveCount();

    // Programmatic focus (UIA SetFocus), run from a posted action.
    virtual void SetFocusFromAutomation();
    // Called (posted, coalesced) after the owner invalidated while a client may be
    // watching; the host re-reads properties and raises only real changes.
    virtual void OnOwnerInvalidated();

private:
    friend class fluent::UIElement;
    // Freezes the provider (it stops reaching this peer) and hands it to `site` for
    // a deferred UIA disconnect; null site = freeze only. Called only by UIElement.
    void Release(IAutomationSite* site);

    UIElement& owner_;
    const uint32_t serial_;
    ComPtr<IUnknown> provider_;
};

}  // namespace fluent::automation
