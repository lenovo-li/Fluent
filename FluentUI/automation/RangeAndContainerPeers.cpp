// RangeAndContainerPeers.cpp — see RangeAndContainerPeers.h.
#include "RangeAndContainerPeers.h"

#include <algorithm>

#include "../controls/Expander.h"
#include "../controls/Hyperlink.h"
#include "../controls/Image.h"
#include "../controls/NumericUpDown.h"
#include "../controls/ProgressBar.h"
#include "../controls/Slider.h"
#include "../controls/StatusBar.h"
#include "../layout/ScrollPanel.h"

namespace fluent::automation {

template <class T>
static T& As(const AutomationPeer& p) {
    return static_cast<T&>(p.Owner());
}

// --- RangeBase ----------------------------------------------------------------
RangeBasePeer::RangeBasePeer(RangeBase& o, ControlType type, float step, bool readOnly)
    : AutomationPeer(o), type_(type), step_(step > 0 ? step : 1.0f), readOnly_(readOnly) {}
double RangeBasePeer::GetValue() const { return As<RangeBase>(*this).Value(); }
double RangeBasePeer::GetMinimum() const { return As<RangeBase>(*this).Minimum(); }
double RangeBasePeer::GetMaximum() const { return As<RangeBase>(*this).Maximum(); }
double RangeBasePeer::GetLargeChange() const {
    const double span = GetMaximum() - GetMinimum();
    return std::max(static_cast<double>(step_), span / 10.0);
}
void RangeBasePeer::SetValue(double v) {
    // RangeBase::SetValue coerces (LogSlider snaps to a decade) and raises the
    // control's ValueChanged, the same path a drag or key takes.
    if (!readOnly_) As<RangeBase>(*this).SetValue(static_cast<float>(v));
}


// --- ScrollPanel ----------------------------------------------------------------
// UIA's ScrollPattern speaks percent; -1 (UIA_ScrollPatternNoScroll) = axis cannot
// scroll. View size = viewport / (viewport + max offset).
namespace {
double Percent(float offset, float maxOffset) {
    return maxOffset > 0 ? std::clamp(100.0 * offset / maxOffset, 0.0, 100.0) : -1.0;
}
double ViewSize(float viewport, float maxOffset) {
    const double extent = static_cast<double>(viewport) + maxOffset;
    return extent > 0 ? std::clamp(100.0 * viewport / extent, 0.0, 100.0) : 100.0;
}
}  // namespace
ScrollPanelPeer::ScrollPanelPeer(ScrollPanel& o) : AutomationPeer(o) {}
double ScrollPanelPeer::HorizontalPercent() const {
    const auto& s = As<ScrollPanel>(*this);
    return Percent(s.HorizontalOffset(), s.MaxHorizontalOffset());
}
double ScrollPanelPeer::VerticalPercent() const {
    const auto& s = As<ScrollPanel>(*this);
    return Percent(s.VerticalOffset(), s.MaxVerticalOffset());
}
double ScrollPanelPeer::HorizontalViewSize() const {
    const auto& s = As<ScrollPanel>(*this);
    return ViewSize(s.Bounds().w, s.MaxHorizontalOffset());
}
double ScrollPanelPeer::VerticalViewSize() const {
    const auto& s = As<ScrollPanel>(*this);
    return ViewSize(s.Bounds().h, s.MaxVerticalOffset());
}
void ScrollPanelPeer::SetScrollPercent(double h, double v) {
    auto& s = As<ScrollPanel>(*this);
    if (h >= 0 && s.MaxHorizontalOffset() > 0)
        s.SetHorizontalOffset(static_cast<float>(h / 100.0 * s.MaxHorizontalOffset()));
    if (v >= 0 && s.MaxVerticalOffset() > 0)
        s.SetVerticalOffset(static_cast<float>(v / 100.0 * s.MaxVerticalOffset()));
}

// --- Expander -------------------------------------------------------------------
ExpanderPeer::ExpanderPeer(Expander& o) : AutomationPeer(o) {}
std::wstring ExpanderPeer::GetName() const {
    const std::wstring& n = Owner().AutomationName();
    return n.empty() ? As<Expander>(*this).Header() : n;
}
ExpandState ExpanderPeer::GetExpandState() const {
    return As<Expander>(*this).IsExpanded() ? ExpandState::Expanded : ExpandState::Collapsed;
}
// The header click's path: the app's user transition, and ExpandedChanged raised,
// so content an app loads lazily in ExpandedChanged is there for a UIA Expand too.
void ExpanderPeer::Expand() { As<Expander>(*this).PerformSetExpanded(true); }
void ExpanderPeer::Collapse() { As<Expander>(*this).PerformSetExpanded(false); }

// --- Hyperlink ------------------------------------------------------------------
HyperlinkPeer::HyperlinkPeer(Hyperlink& o) : AutomationPeer(o) {}
std::wstring HyperlinkPeer::GetName() const {
    const std::wstring& n = Owner().AutomationName();
    return n.empty() ? As<Hyperlink>(*this).Text() : n;
}
void HyperlinkPeer::Invoke() { As<Hyperlink>(*this).PerformActivate(); }
std::wstring HyperlinkPeer::GetValue() const { return As<Hyperlink>(*this).Uri(); }

// --- Labels -----------------------------------------------------------------------
LabelPeer::LabelPeer(UIElement& o, ControlType type, std::wstring (*name)(const UIElement&))
    : AutomationPeer(o), type_(type), name_(name) {}
std::wstring LabelPeer::GetName() const {
    const std::wstring& n = Owner().AutomationName();
    return !n.empty() ? n : (name_ ? name_(Owner()) : std::wstring());
}
bool LabelPeer::IsControlElement() const {
    return type_ != ControlType::Image || !Owner().AutomationName().empty();
}

}  // namespace fluent::automation

namespace fluent {
using namespace automation;

std::unique_ptr<AutomationPeer> Slider::CreateAutomationPeer() {
    // LogSlider inherits this: RangeBase::SetValue coerces to its decades.
    return std::make_unique<RangeBasePeer>(*this, ControlType::Slider, Step(), false);
}
std::unique_ptr<AutomationPeer> NumericUpDown::CreateAutomationPeer() {
    return std::make_unique<RangeBasePeer>(*this, ControlType::Spinner, Step(), false);
}
std::unique_ptr<AutomationPeer> ProgressBar::CreateAutomationPeer() {
    return std::make_unique<RangeBasePeer>(*this, ControlType::ProgressBar, 1.0f, true);
}
std::unique_ptr<AutomationPeer> ScrollPanel::CreateAutomationPeer() {
    return std::make_unique<ScrollPanelPeer>(*this);
}
std::unique_ptr<AutomationPeer> Expander::CreateAutomationPeer() {
    return std::make_unique<ExpanderPeer>(*this);
}
std::unique_ptr<AutomationPeer> Hyperlink::CreateAutomationPeer() {
    return std::make_unique<HyperlinkPeer>(*this);
}
std::unique_ptr<AutomationPeer> StatusBar::CreateAutomationPeer() {
    return std::make_unique<LabelPeer>(*this, ControlType::StatusBar, [](const UIElement& e) {
        return static_cast<const StatusBar&>(e).Text();
    });
}
std::unique_ptr<AutomationPeer> Image::CreateAutomationPeer() {
    // Name only from SetAutomationName: an image has no text of its own, and one
    // without a name is decoration (raw view only, see LabelPeer).
    return std::make_unique<LabelPeer>(*this, ControlType::Image, nullptr);
}

}  // namespace fluent
