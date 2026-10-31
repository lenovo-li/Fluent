// AutomationTree.h — which elements the UIA tree exposes, and how to walk them.
//
// Pure tree logic over UIElement (no COM), shared by the window-root and element
// providers and tested headless. One rule defines the tree: an element's exposed
// children are its LogicalChildAt() entries that are attached and visible, in
// order. Navigation, the root's children and hit-testing all apply that rule, so a
// client can never reach a node from one direction that is missing from another.
//
// An element's exposed parent is its Visual parent; null means "a window root",
// i.e. the window-root provider. Elements a control owns but does not list (a
// control's internal parts) are therefore still a dead end going down, which is
// what we want: a Button's internals are not separate UIA nodes.
//
// COST. Only runs while a client navigates. Sibling steps scan the parent's child
// list from a remembered position (`hint`), so walking a container front to back
// is linear overall rather than quadratic.
#pragma once

#include <cstddef>

namespace fluent {
class UIElement;
}

namespace fluent::automation {

class IAutomationSite;

bool IsExposed(const UIElement& e);

UIElement* FirstExposedChild(const UIElement& parent);
UIElement* LastExposedChild(const UIElement& parent);
// Exposed root elements of the window (site roots filtered by IsExposed).
UIElement* FirstExposedRoot(const IAutomationSite& site);
UIElement* LastExposedRoot(const IAutomationSite& site);

// Exposed parent; null = the element is a window root (parent is the root provider).
UIElement* ExposedParent(const UIElement& e);

// Next (dir > 0) or previous (dir < 0) exposed sibling, among the parent's logical
// children or, for a root element, among the site's roots. `hint` is the index the
// element was last found at; it is read and updated.
// `foundAt` (optional) receives the returned sibling's index, so the caller can seed
// THAT element's hint: a front-to-back walk is then O(1) per step.
UIElement* ExposedSibling(const IAutomationSite& site, const UIElement& e, int dir,
                          size_t& hint, size_t* foundAt = nullptr);

// `e`, or its nearest ancestor that its parent actually lists. Focus can sit on a
// control's internal part (DataGrid's cell editor) that no LogicalChildAt names;
// reporting that node would hand the client an element it can reach going up but
// never going down. Null = no listed node (focus is on the root).
UIElement* NearestListed(const IAutomationSite& site, UIElement* e);

// The deepest exposed element containing the host-client DIP point, front-most
// first (later children paint on top). Ignores enabled/hit-test-visible on purpose:
// Inspect must be able to point at a disabled button. Null = no element (the root).
UIElement* ExposedElementAt(const IAutomationSite& site, float x, float y);

}  // namespace fluent::automation
