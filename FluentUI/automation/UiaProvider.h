// UiaProvider.h — the COM side of UI Automation, behind a header with no UIA types.
//
// WHY THIS HEADER EXPOSES ONLY IUnknown. <UIAutomation.h> drags in the whole UIA
// client/provider surface and several macro collisions with the rest of the SDK.
// Only UiaProvider.cpp includes it; everything else (AutomationHost, the window,
// tests that do not need UIA types) talks to the provider through these free
// functions and an opaque IUnknown. When the peer layer lands, it plugs in here too.
//
// THREADING: ProviderOptions_UseComThreading, on an STA UI thread.
// Without that flag UIA core calls the provider from its own worker threads. That
// was measured, not assumed: under Inspect.exe, get_HostRawElementProvider arrived on
// the UI thread but get_ProviderOptions and get_FragmentRoot arrived on a
// UIAutomationCore worker thread. The peer model is single-threaded, so the provider
// sets UseComThreading. Learn: "STA-based provider implementations will be called
// back on their own STA thread, and therefore do not need extra synchronization"
// (learn.microsoft.com/windows/win32/api/uiautomationcore/ne-uiautomationcore-provideroptions).
// The UI thread is an STA (Application's ComApartment, COINIT_APARTMENTTHREADED), and
// the pump waits with QS_ALLINPUT, so COM's posted calls wake it and are dispatched
// there. Cost is paid only while a client is querying.
//
// get_ProviderOptions itself is exempt from the thread check: UIA core has to ask it
// BEFORE it knows to route through COM, so it may come from any thread. It returns a
// constant.
//
// The check stays on every other method permanently. A mismatch is counted, traced as
//   [FluentUI.Uia] WRONG THREAD <method> tid=<n> ui=<n>
// and, in Debug builds, raises an _ASSERTE. Creation also traces the creating
// thread's apartment; a non-STA thread there means COM cannot route calls back to the
// UI thread and the check will fire.
#pragma once

#include "../fl_common.h"  // NOMINMAX / WIN32_LEAN_AND_MEAN before <windows.h>
#include <unknwn.h>
#include <cstdint>

#include <string>

#include "AutomationSite.h"
#include "UiaCore.h"

namespace fluent::automation {

// Creates the fragment-root provider for a top-level window. `uiThreadId` is the
// thread every call is expected on. Returns the provider AddRef'd once in *out.
// `site` (may be null in tests) supplies the root's children, focus and hit-testing.
HRESULT CreateWindowRootProvider(HWND hwnd, DWORD uiThreadId, IAutomationSite* site,
                                 IUnknown** out);

// Severs the provider from the window: afterwards every method except
// get_ProviderOptions returns UIA_E_ELEMENTNOTAVAILABLE. Touches no framework object
// and never calls into UIA core, so it is safe from any message, including one that
// arrived via SendMessage. Null-safe.
void DetachProvider(IUnknown* provider);

// True while the provider is still attached (not yet detached). For tests.
bool IsProviderAttached(IUnknown* provider);

// WM_DESTROY teardown for a window that has handed out a provider:
// UiaDisconnectProvider (skipped, and traced, when we are inside a SendMessage,
// where the API fails with RPC_E_CANTCALLOUT_ININPUTSYNCCALL), THEN detach, then
// UiaReturnRawElementProvider(hwnd, 0, 0, nullptr) so UIA drops the provider
// references it kept for raised events. The provider must still be attached on
// entry: UIA core calls back into it during the disconnect (detaching first made
// the disconnect fail with UIA_E_ELEMENTNOTAVAILABLE, measured under Inspect.exe).
// All UIA calls go through `core` (UiaCore.h) so tests can check the order.
void ReleaseWindowProvider(IUiaCore& core, HWND hwnd, IUnknown* provider);

// --- Element providers (one per AutomationPeer; UiaElementProvider.cpp) ---------
// Creates the provider for `peer`, AddRef'd once in *out. The provider keeps a raw
// pointer to the peer until FreezeProvider.
HRESULT CreateElementProvider(AutomationPeer& peer, IUnknown** out);
// Stops the provider from reaching its peer. A frozen provider still answers what
// UIA core asks while disconnecting it (ProviderOptions, RuntimeId, FragmentRoot)
// and returns UIA_E_ELEMENTNOTAVAILABLE for everything else. Null-safe.
void FreezeProvider(IUnknown* provider);
// The peer behind an unfrozen element provider, else null.
AutomationPeer* PeerFromProvider(IUnknown* provider);
// Runs a posted action on the provider's peer, if it is still unfrozen.
void RunPeerAction(IUnknown* provider, PeerAction action, const std::wstring& value,
                   double a = 0, double b = 0);
// Re-reads the reported properties and raises UIA property events for the ones that
// changed since the last raise. Null `core` only records the baseline.
void RaiseChangedProperties(IUiaCore* core, IUnknown* provider);

// WM_GETOBJECT carries the object id in lParam as a 32-bit value. Pure, for tests:
// true only for UiaRootObjectId and only while the window is not being destroyed
// (Learn: do not hand out a provider that is about to be disconnected).
bool ShouldServeUiaRoot(LPARAM lp, bool destroying);

// Thread-check diagnostics. Violations() counts calls that arrived on a thread other
// than the provider's UI thread. Tests drive a provider from a worker thread on
// purpose, so they switch the Debug _ASSERTE off; counting and tracing stay on.
uint32_t ThreadViolations();
void SetThreadAssertEnabled(bool enabled);

}  // namespace fluent::automation
