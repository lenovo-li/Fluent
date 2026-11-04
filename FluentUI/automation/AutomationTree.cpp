// AutomationTree.cpp — see AutomationTree.h for the one rule that defines the tree.
#include "AutomationTree.h"

#include "AutomationSite.h"
#include "../core/UIElement.h"

namespace fluent::automation {
namespace {

// A uniform view over "the children of X": an element's logical children or, for
// the window root (parent == null), the site's root elements.
struct ChildList {
    const UIElement* parent;
    const IAutomationSite* site;
    size_t Count() const {
        return parent ? parent->LogicalChildCount() : (site ? site->RootElementCount() : 0);
    }
    UIElement* At(size_t i) const {
        return parent ? parent->LogicalChildAt(i) : site->RootElementAt(i);
    }
};

UIElement* FirstIn(const ChildList& l) {
    for (size_t i = 0, n = l.Count(); i < n; ++i)
        if (UIElement* c = l.At(i); c && IsExposed(*c)) return c;
    return nullptr;
}
UIElement* LastIn(const ChildList& l) {
    for (size_t i = l.Count(); i-- > 0;)
        if (UIElement* c = l.At(i); c && IsExposed(*c)) return c;
    return nullptr;
}

}  // namespace

bool IsExposed(const UIElement& e) { return e.IsAttached() && e.IsVisible(); }

UIElement* FirstExposedChild(const UIElement& p) { return FirstIn({&p, nullptr}); }
UIElement* LastExposedChild(const UIElement& p) { return LastIn({&p, nullptr}); }
UIElement* FirstExposedRoot(const IAutomationSite& s) { return FirstIn({nullptr, &s}); }
UIElement* LastExposedRoot(const IAutomationSite& s) { return LastIn({nullptr, &s}); }

UIElement* ExposedParent(const UIElement& e) {
    // Every Visual is a UIElement (UIElement is Visual's only subclass).
    return static_cast<UIElement*>(static_cast<Visual*>(e.Parent()));
}

UIElement* ExposedSibling(const IAutomationSite& site, const UIElement& e, int dir,
                          size_t& hint, size_t* foundAt) {
    const ChildList list{ExposedParent(e), &site};
    const size_t n = list.Count();
    // Locate e: the hint first (O(1) when walking in order), else a full scan.
    size_t at = n;
    if (hint < n && list.At(hint) == &e) {
        at = hint;
    } else {
        for (size_t i = 0; i < n; ++i)
            if (list.At(i) == &e) { at = i; break; }
    }
    if (at == n) return nullptr;  // not listed by its parent: no siblings to report
    hint = at;
    if (dir > 0) {
        for (size_t i = at + 1; i < n; ++i)
            if (UIElement* c = list.At(i); c && IsExposed(*c)) {
                if (foundAt) *foundAt = i;
                return c;
            }
    } else {
        for (size_t i = at; i-- > 0;)
            if (UIElement* c = list.At(i); c && IsExposed(*c)) {
                if (foundAt) *foundAt = i;
                return c;
            }
    }
    return nullptr;
}

namespace {
bool IsListedBy(const ChildList& l, const UIElement* e) {
    for (size_t i = 0, n = l.Count(); i < n; ++i)
        if (l.At(i) == e) return true;
    return false;
}
}  // namespace

UIElement* NearestListed(const IAutomationSite& site, UIElement* e) {
    // Walk up until every link from e to a window root is a listed, exposed child.
    // O(depth x siblings); runs only for a focus report while a client listens.
    UIElement* candidate = e;
    for (UIElement* cur = e; cur; cur = ExposedParent(*cur)) {
        if (!IsExposed(*cur) || !IsListedBy({ExposedParent(*cur), &site}, cur))
            candidate = ExposedParent(*cur);
    }
    return candidate;
}

namespace {
UIElement* DeepestAt(UIElement& e, float x, float y) {
    for (size_t i = e.LogicalChildCount(); i-- > 0;) {
        UIElement* c = e.LogicalChildAt(i);
        if (!c || !IsExposed(*c) || !c->BoundsInHostDip().contains(x, y)) continue;
        return DeepestAt(*c, x, y);
    }
    return &e;
}
}  // namespace

UIElement* ExposedElementAt(const IAutomationSite& site, float x, float y) {
    for (size_t i = site.RootElementCount(); i-- > 0;) {
        UIElement* r = site.RootElementAt(i);
        if (!r || !IsExposed(*r) || !r->BoundsInHostDip().contains(x, y)) continue;
        return DeepestAt(*r, x, y);
    }
    return nullptr;
}

}  // namespace fluent::automation
