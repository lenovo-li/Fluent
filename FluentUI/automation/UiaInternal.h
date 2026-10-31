// UiaInternal.h — private to the two provider translation units.
//
// UiaProvider.cpp (window root, core, teardown) and UiaElementProvider.cpp (one
// provider per peer) are the only files that include <UIAutomation.h>; this header
// is how they share the thread probe and reach each other's objects. Nothing
// outside automation/*.cpp may include it.
#pragma once

#include <UIAutomation.h>

#include <atomic>
#include <crtdbg.h>

#include "UiaProvider.h"
#include "../fl_common.h"

namespace fluent {
class UIElement;
}

namespace fluent::automation {

// The thread probe (UiaProvider.h, "THREADING"): counts, traces and, in Debug,
// asserts a provider call arriving off `uiThreadId`. One GetCurrentThreadId and a
// compare per client call; client calls only happen while a client is attached.
void CheckProviderThread(DWORD uiThreadId, const char* method);
extern std::atomic<uint32_t> g_threadViolations;
extern std::atomic<bool> g_threadAssertEnabled;
extern std::atomic<bool> g_optionsOffThreadTraced;

// The provider for `e` as a fragment, AddRef'd into *out; *out = null when the
// element has no peer (not attached to a site). Creates the peer on first use.
HRESULT FragmentForElement(UIElement* e, IRawElementProviderFragment** out);
// The provider for `peer` as a fragment, AddRef'd; null peer = null.
HRESULT FragmentForPeer(AutomationPeer* peer, IRawElementProviderFragment** out);

// IRawElementProviderSimple behind an IUnknown handed out by either provider kind
// (both put that interface first, so the IUnknown is that base).
inline IRawElementProviderSimple* AsSimple(IUnknown* p) {
    return static_cast<IRawElementProviderSimple*>(p);
}

}  // namespace fluent::automation
