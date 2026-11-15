// RangeAndContainerPeers.h — peers for value ranges, scrolling and simple
// containers: Slider / LogSlider / NumericUpDown / ProgressBar
// (RangeValue), ScrollPanel (Pane + Scroll), Expander (Group + ExpandCollapse),
// Hyperlink (Invoke + Value = the URI), StatusBar and Image.
//
// Same contract as ControlPeers.h: each control declares CreateAutomationPeer in
// its header and defines it in RangeAndContainerPeers.cpp; getters read live state.
#pragma once

#include "AutomationPeer.h"

namespace fluent {
class RangeBase;
class ScrollPanel;
class Expander;
class Hyperlink;
class StatusBar;
class Image;
}  // namespace fluent

namespace fluent::automation {

// RangeBase-derived controls. SmallChange = the control's step (1 when it has
// none); LargeChange = a tenth of the range, matching Page Up/Down conventions.
class RangeBasePeer : public AutomationPeer, public IRangeValuePeer {
public:
    RangeBasePeer(RangeBase& owner, ControlType type, float step, bool readOnly);
    ControlType GetControlType() const override { return type_; }
    bool IsControlElement() const override { return true; }
    IRangeValuePeer* AsRangeValue() override { return this; }
    double GetValue() const override;
    double GetMinimum() const override;
    double GetMaximum() const override;
    double GetSmallChange() const override { return step_; }
    double GetLargeChange() const override;
    bool IsReadOnly() const override { return readOnly_; }
    void SetValue(double v) override;

private:
    ControlType type_;
    float step_;
    bool readOnly_;  // ProgressBar: displays a value, the user cannot set it
};

// ScrollPanel: a Pane (not "Custom") with the Scroll pattern. It is focusable for
// keyboard scrolling, which is why the default peer used to report it as an
// unnamed Custom control.
class ScrollPanelPeer : public AutomationPeer, public IScrollPeer {
public:
    explicit ScrollPanelPeer(ScrollPanel& owner);
    ControlType GetControlType() const override { return ControlType::Pane; }
    bool IsControlElement() const override { return true; }
    IScrollPeer* AsScroll() override { return this; }
    double HorizontalPercent() const override;
    double VerticalPercent() const override;
    double HorizontalViewSize() const override;
    double VerticalViewSize() const override;
    void SetScrollPercent(double h, double v) override;
};

class ExpanderPeer : public AutomationPeer, public IExpandCollapsePeer {
public:
    explicit ExpanderPeer(Expander& owner);
    ControlType GetControlType() const override { return ControlType::Group; }
    std::wstring GetName() const override;
    bool IsControlElement() const override { return true; }
    IExpandCollapsePeer* AsExpandCollapse() override { return this; }
    ExpandState GetExpandState() const override;
    void Expand() override;
    void Collapse() override;
};

class HyperlinkPeer : public AutomationPeer, public IInvokePeer, public IValuePeer {
public:
    explicit HyperlinkPeer(Hyperlink& owner);
    ControlType GetControlType() const override { return ControlType::Hyperlink; }
    std::wstring GetName() const override;
    bool IsControlElement() const override { return true; }
    IInvokePeer* AsInvoke() override { return this; }
    IValuePeer* AsValue() override { return this; }
    void Invoke() override;
    std::wstring GetValue() const override;  // the URI
    bool IsReadOnly() const override { return true; }
    void SetValue(const std::wstring&) override {}
};

// A labelled, non-interactive element: Name from the control's own text.
class LabelPeer : public AutomationPeer {
public:
    LabelPeer(UIElement& owner, ControlType type, std::wstring (*name)(const UIElement&));
    ControlType GetControlType() const override { return type_; }
    std::wstring GetName() const override;
    // Decorative images (no AutomationName) stay out of the control view.
    bool IsControlElement() const override;

private:
    ControlType type_;
    std::wstring (*name_)(const UIElement&);
};

}  // namespace fluent::automation
