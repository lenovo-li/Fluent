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
class AutomationPeer;

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
    // Multi-select containers override; single-select: add = select, remove refused.
    virtual bool CanAddRemove() const { return false; }
    virtual void AddToSelection() { Select(); }
    virtual void RemoveFromSelection() {}
    // The selection container's peer (a ListBox for its rows), or null.
    virtual AutomationPeer* SelectionContainer() const { return nullptr; }
};
// A container's selection, answered by index so a 1M-row multi-selection is never
// materialized: GetSelection hands out at most kMaxReported items.
class ISelectionPeer {
public:
    virtual ~ISelectionPeer() = default;
    virtual bool CanSelectMultiple() const = 0;
    virtual bool IsSelectionRequired() const { return false; }
    virtual size_t SelectedIndex() const = 0;   // primary selection; >= item count = none
    virtual size_t SelectedCount() const = 0;
    virtual bool IsIndexSelected(size_t i) const = 0;
    static constexpr size_t kMaxReported = 256;
};

class IRangeValuePeer {
public:
    virtual ~IRangeValuePeer() = default;
    virtual double GetValue() const = 0;
    virtual double GetMinimum() const = 0;
    virtual double GetMaximum() const = 0;
    virtual double GetSmallChange() const = 0;
    virtual double GetLargeChange() const = 0;
    virtual bool IsReadOnly() const = 0;
    virtual void SetValue(double v) = 0;  // clamped by the control
};
enum class ExpandState : int32_t { Collapsed = 0, Expanded = 1, PartiallyExpanded = 2, LeafNode = 3 };
class IExpandCollapsePeer {
public:
    virtual ~IExpandCollapsePeer() = default;
    virtual ExpandState GetExpandState() const = 0;
    virtual void Expand() = 0;
    virtual void Collapse() = 0;
};
// Percent-based like UIA's ScrollPattern: 0..100, or -1 (UIA_ScrollPatternNoScroll)
// on an axis that cannot scroll.
class IScrollPeer {
public:
    virtual ~IScrollPeer() = default;
    virtual double HorizontalPercent() const = 0;
    virtual double VerticalPercent() const = 0;
    virtual double HorizontalViewSize() const = 0;  // visible share, 0..100
    virtual double VerticalViewSize() const = 0;
    virtual void SetScrollPercent(double h, double v) = 0;  // -1 = leave that axis
};
// A container whose items are not elements (list rows, tree rows, tabs): the peer
// answers child navigation itself. Children are ItemPeers it owns.
class IItemsPeer {
public:
    virtual ~IItemsPeer() = default;
    virtual size_t ItemCount() const = 0;
    virtual AutomationPeer* ItemAt(size_t i) = 0;   // created on demand, owned by the peer
    // The items child navigation walks: [ExposedBegin, ExposedEnd). A virtualized
    // control exposes the rows near the viewport, not all of them: Inspect lists
    // every sibling of the focused element, and a real client pays ~0.45 ms per row,
    // so a 50,000-row grid froze it for ~22 s. Items outside the window still exist
    // (selection, focus, ScrollItem reach them); they are just not in the walk.
    virtual size_t ExposedBegin() const { return 0; }
    virtual size_t ExposedEnd() const { return ItemCount(); }
};

// Text pattern source (TextBox, TextArea). Offsets are UTF-16 code units, the same
// units the control's caret uses. A password peer exposes no text at all.
class ITextPeer {
public:
    virtual ~ITextPeer() = default;
    virtual const std::wstring& Text() const = 0;
    virtual size_t SelectionStart() const = 0;
    virtual size_t SelectionLength() const = 0;
    virtual void SelectText(size_t start, size_t length) = 0;  // posted action
    // The offset under a host-DIP point (clamped into the text).
    virtual size_t OffsetAtPoint(float x, float y) const = 0;
    virtual bool IsTextReadOnly() const = 0;
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
    virtual IRangeValuePeer* AsRangeValue() { return nullptr; }
    virtual IExpandCollapsePeer* AsExpandCollapse() { return nullptr; }
    virtual IScrollPeer* AsScroll() { return nullptr; }
    virtual IItemsPeer* AsItems() { return nullptr; }
    virtual ISelectionPeer* AsSelection() { return nullptr; }
    virtual ITextPeer* AsText() { return nullptr; }
    // ScrollItem pattern: brings the element/item into view (posted action).
    virtual bool CanScrollIntoView() const { return false; }
    virtual void ScrollIntoView() {}

    // --- What the provider reports, via the peer so ItemPeer can override --------
    // Defaults read the owner element. An ItemPeer (a list row, a tab header) has
    // no element of its own and answers from its container instead.
    virtual RectDip BoundsInHostDip() const;
    virtual bool IsEnabled() const;
    virtual bool IsKeyboardFocusable() const;
    virtual bool HasFocusWithin() const;   // ANDed with the host's OS focus
    virtual bool IsOffscreen() const;
    virtual std::wstring GetHelpText() const;
    virtual std::wstring GetAutomationId() const;
    // A live region (InfoBar) announces its changes; the peer raises
    // LiveRegionChanged itself (IAutomationSite::QueueEvent) when its text changes.
    virtual LiveSetting GetLiveSetting() const { return LiveSetting::Off; }
    // The root content of a popup window (a menu level) raises these when the popup
    // is shown / hidden (UIA_MenuOpenedEventId / UIA_MenuClosedEventId for a menu).
    // 0 = none. Read by AutomationHost::OnPopupShown / OnPopupHidden.
    virtual int PopupOpenedEventId() const { return 0; }
    virtual int PopupClosedEventId() const { return 0; }
    // Non-null only for an item (a row, a tab header, a grid cell): the peer that
    // lists it through AsItems(). IndexInContainer is its position there, which the
    // provider uses for sibling navigation. An item may itself list items (a grid
    // row lists its cells).
    virtual AutomationPeer* ItemContainer() const { return nullptr; }
    virtual size_t IndexInContainer() const { return SIZE_MAX; }
    // Containers: the item under a host-DIP point / the keyboard-focused item, or
    // null (then the container itself answers).
    virtual AutomationPeer* ItemAtPoint(float x, float y) {
        UNREFERENCED_PARAMETER(x);
        UNREFERENCED_PARAMETER(y);
        return nullptr;
    }
    virtual AutomationPeer* FocusedItem() { return nullptr; }

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
    // Runs from the host's flush only (after posted actions, never inside a client
    // call or an invalidation), before the property re-check. Containers release
    // vanished / excess item peers here: releasing one synchronously could destroy
    // the item whose action is on the stack (TreeView Collapse invalidates while
    // the TreeItem's Collapse() runs).
    virtual void OnFlushRecheck() {}

protected:
    // A container's item peers release with it (or when their item disappears).
    // Overrides release their items and then call the base.
    virtual void ReleaseOwned(IAutomationSite* site) { UNREFERENCED_PARAMETER(site); }
    // Lets a container release a peer it owns (Release is otherwise UIElement-only).
    static void ReleasePeer(AutomationPeer& p, IAutomationSite* site) { p.Release(site); }

private:
    friend class fluent::UIElement;
    // Freezes the provider (it stops reaching this peer) and hands it to `site` for
    // a deferred UIA disconnect; null site = freeze only. Releases owned items first.
    void Release(IAutomationSite* site);

    UIElement& owner_;
    const uint32_t serial_;
    ComPtr<IUnknown> provider_;
};

}  // namespace fluent::automation
