// AutomationProviderTests.cpp — headless checks for the UI Automation window root.
//
// What IS testable without a window or a UIA client:
//   * the WM_GETOBJECT gate (only UiaRootObjectId, never while destroying),
//   * laziness: AutomationHost creates nothing until the first served request,
//     and nothing at all for MSAA object ids,
//   * detach makes the provider inert (UIA_E_ELEMENTNOTAVAILABLE everywhere except
//     get_ProviderOptions), which is what keeps a provider UIA core still holds
//     after the window is gone from touching freed state,
//   * the thread probe counts a call from a non-UI thread.
// The provider is created directly with a null HWND, so no method that would reach
// UIA core (get_HostRawElementProvider, ReturnRootProvider, ReleaseWindowProvider)
// is called here.
//
// What is NOT covered: whether UIA core really calls us on the UI thread. That is
// the premise the thread probe exists to check, and only a real client
// (Inspect.exe / Narrator against FluentUIDemo) can exercise it.
#include "../framework/Test.h"
#include "../../FluentUI/automation/AutomationHost.h"
#include "../../FluentUI/automation/UiaProvider.h"

#include "AutomationTestHelpers.h"
#include <wrl/client.h>
#include <string>
#include <thread>
#include <vector>

using namespace fluent;

namespace {

ComPtr<IUnknown> MakeRoot() {
    ComPtr<IUnknown> root;
    automation::CreateWindowRootProvider(nullptr, GetCurrentThreadId(), nullptr, root.GetAddressOf());
    return root;
}

}  // namespace

TEST(AutomationProvider, ServesOnlyUiaRootObjectId) {
    EXPECT_TRUE(automation::ShouldServeUiaRoot(static_cast<LPARAM>(UiaRootObjectId), false));
    // lParam may arrive sign-extended or not; only the low 32 bits are the object id.
    EXPECT_TRUE(automation::ShouldServeUiaRoot(
        static_cast<LPARAM>(static_cast<DWORD>(UiaRootObjectId)), false));
    EXPECT_FALSE(automation::ShouldServeUiaRoot(static_cast<LPARAM>(OBJID_CLIENT), false));
    EXPECT_FALSE(automation::ShouldServeUiaRoot(static_cast<LPARAM>(OBJID_WINDOW), false));
}

TEST(AutomationProvider, NeverServesWhileDestroying) {
    // Learn: a re-entrant WM_GETOBJECT during teardown must not return the provider
    // that is being disconnected.
    EXPECT_FALSE(automation::ShouldServeUiaRoot(static_cast<LPARAM>(UiaRootObjectId), true));
}

TEST(AutomationProvider, HostCreatesNothingForMsaaRequests) {
    automation::AutomationHost host;
    LRESULT r = 123;
    EXPECT_FALSE(host.OnGetObject(nullptr, 0, static_cast<LPARAM>(OBJID_CLIENT), false, r));
    EXPECT_FALSE(host.OnGetObject(nullptr, 0, static_cast<LPARAM>(UiaRootObjectId), true, r));
    EXPECT_FALSE(host.HasRootProvider());
    EXPECT_EQ(r, static_cast<LRESULT>(123));
}

TEST(AutomationProvider, TeardownWithoutClientIsANoOpAndFinal) {
    automation::AutomationHost host;
    host.OnWindowDestroyed(nullptr);  // no client ever came: must not touch UIA
    EXPECT_FALSE(host.HasRootProvider());
    LRESULT r = 0;
    // After teardown even a valid root request is refused (the window is going away).
    EXPECT_FALSE(host.OnGetObject(nullptr, 0, static_cast<LPARAM>(UiaRootObjectId), false, r));
    EXPECT_FALSE(host.HasRootProvider());
}

TEST(AutomationProvider, AttachedRootReportsFrameworkId) {
    ComPtr<IUnknown> root = MakeRoot();
    ComPtr<IRawElementProviderSimple> simple;
    EXPECT_TRUE(SUCCEEDED(root.As(&simple)));
    VARIANT v;
    EXPECT_EQ(simple->GetPropertyValue(UIA_FrameworkIdPropertyId, &v), S_OK);
    EXPECT_EQ(v.vt, static_cast<VARTYPE>(VT_BSTR));
    EXPECT_TRUE(v.vt == VT_BSTR && wcscmp(v.bstrVal, L"FluentUI") == 0);
    VariantClear(&v);
    // Properties we do not own stay empty so the HWND host provider answers them.
    EXPECT_EQ(simple->GetPropertyValue(UIA_NamePropertyId, &v), S_OK);
    EXPECT_EQ(v.vt, static_cast<VARTYPE>(VT_EMPTY));
    automation::DetachProvider(root.Get());
}

TEST(AutomationProvider, DetachedRootIsInert) {
    ComPtr<IUnknown> root = MakeRoot();
    EXPECT_TRUE(automation::IsProviderAttached(root.Get()));
    automation::DetachProvider(root.Get());
    EXPECT_FALSE(automation::IsProviderAttached(root.Get()));

    ComPtr<IRawElementProviderSimple> simple;
    ComPtr<IRawElementProviderFragment> frag;
    ComPtr<IRawElementProviderFragmentRoot> fragRoot;
    EXPECT_TRUE(SUCCEEDED(root.As(&simple)));
    EXPECT_TRUE(SUCCEEDED(root.As(&frag)));
    EXPECT_TRUE(SUCCEEDED(root.As(&fragRoot)));

    VARIANT v;
    EXPECT_EQ(simple->GetPropertyValue(UIA_FrameworkIdPropertyId, &v), UIA_E_ELEMENTNOTAVAILABLE);
    EXPECT_EQ(v.vt, static_cast<VARTYPE>(VT_EMPTY));
    ComPtr<IUnknown> pattern;
    EXPECT_EQ(simple->GetPatternProvider(UIA_InvokePatternId, &pattern), UIA_E_ELEMENTNOTAVAILABLE);
    ComPtr<IRawElementProviderSimple> hostProv;
    EXPECT_EQ(simple->get_HostRawElementProvider(&hostProv), UIA_E_ELEMENTNOTAVAILABLE);
    ComPtr<IRawElementProviderFragment> nav;
    EXPECT_EQ(frag->Navigate(NavigateDirection_FirstChild, &nav), UIA_E_ELEMENTNOTAVAILABLE);
    ComPtr<IRawElementProviderFragmentRoot> fr;
    EXPECT_EQ(frag->get_FragmentRoot(&fr), UIA_E_ELEMENTNOTAVAILABLE);
    EXPECT_TRUE(fr == nullptr);
    UiaRect rect;
    EXPECT_EQ(frag->get_BoundingRectangle(&rect), UIA_E_ELEMENTNOTAVAILABLE);
    EXPECT_EQ(frag->SetFocus(), UIA_E_ELEMENTNOTAVAILABLE);
    ComPtr<IRawElementProviderFragment> hit;
    EXPECT_EQ(fragRoot->ElementProviderFromPoint(0, 0, &hit), UIA_E_ELEMENTNOTAVAILABLE);

    // UIA core asks this during its own teardown; it must keep answering.
    ProviderOptions opts{};
    EXPECT_EQ(simple->get_ProviderOptions(&opts), S_OK);
    EXPECT_TRUE((opts & ProviderOptions_ServerSideProvider) != 0);
}

TEST(AutomationProvider, FragmentRootIsTheSameObject) {
    // UIA compares providers by identity; the root must hand back itself.
    ComPtr<IUnknown> root = MakeRoot();
    ComPtr<IRawElementProviderFragment> frag;
    EXPECT_TRUE(SUCCEEDED(root.As(&frag)));
    ComPtr<IRawElementProviderFragmentRoot> fr;
    EXPECT_EQ(frag->get_FragmentRoot(&fr), S_OK);
    ComPtr<IUnknown> back;
    EXPECT_TRUE(fr && SUCCEEDED(fr.As(&back)));
    EXPECT_TRUE(back.Get() == root.Get());
    automation::DetachProvider(root.Get());
}

TEST(AutomationProvider, ThreadProbeCountsOffThreadCalls) {
    ComPtr<IUnknown> root = MakeRoot();
    ComPtr<IRawElementProviderSimple> simple;
    EXPECT_TRUE(SUCCEEDED(root.As(&simple)));

    const uint32_t before = automation::ThreadViolations();
    VARIANT v;
    simple->GetPropertyValue(UIA_FrameworkIdPropertyId, &v);  // own thread: not counted
    VariantClear(&v);
    EXPECT_EQ(automation::ThreadViolations(), before);

    automation::SetThreadAssertEnabled(false);  // the off-thread call below is deliberate
    std::thread worker([&] {
        VARIANT w;
        simple->GetPropertyValue(UIA_FrameworkIdPropertyId, &w);
        VariantClear(&w);
    });
    worker.join();
    automation::SetThreadAssertEnabled(true);
    EXPECT_EQ(automation::ThreadViolations(), before + 1);
    automation::DetachProvider(root.Get());
}

TEST(AutomationProvider, ProviderOptionsIsExemptFromThreadProbe) {
    // Inspect.exe showed UIA core calling get_ProviderOptions on its own worker
    // thread right after WM_GETOBJECT. That must not count as a violation (in Debug
    // it raised the assert dialog), and the answer must still be ServerSideProvider.
    ComPtr<IUnknown> root = MakeRoot();
    ComPtr<IRawElementProviderSimple> simple;
    EXPECT_TRUE(SUCCEEDED(root.As(&simple)));
    const uint32_t before = automation::ThreadViolations();
    ProviderOptions got{};
    HRESULT hr = E_FAIL;
    std::thread worker([&] { hr = simple->get_ProviderOptions(&got); });
    worker.join();
    EXPECT_EQ(hr, S_OK);
    EXPECT_TRUE((got & ProviderOptions_ServerSideProvider) != 0);
    EXPECT_EQ(automation::ThreadViolations(), before);
    automation::DetachProvider(root.Get());
}

TEST(AutomationProvider, RequestsComThreading) {
    // Measured under Inspect.exe: without UseComThreading UIA core called
    // get_FragmentRoot on its own worker thread. The single-threaded peer model
    // depends on this flag; it must survive detach too (asked during teardown).
    ComPtr<IUnknown> root = MakeRoot();
    ComPtr<IRawElementProviderSimple> simple;
    EXPECT_TRUE(SUCCEEDED(root.As(&simple)));
    ProviderOptions opts{};
    EXPECT_EQ(simple->get_ProviderOptions(&opts), S_OK);
    EXPECT_TRUE((opts & ProviderOptions_UseComThreading) != 0);
    EXPECT_TRUE((opts & ProviderOptions_ServerSideProvider) != 0);
    EXPECT_TRUE((opts & ProviderOptions_ClientSideProvider) == 0);
    automation::DetachProvider(root.Get());
    EXPECT_EQ(simple->get_ProviderOptions(&opts), S_OK);
    EXPECT_TRUE((opts & ProviderOptions_UseComThreading) != 0);
}

// ---------------------------------------------------------------------------
// Teardown order, through an injected UIA core (automation/UiaCore.h).
//
// The order was established under Inspect.exe: UiaDisconnectProvider calls back
// into the provider, so detaching first made it fail with UIA_E_ELEMENTNOTAVAILABLE.
// These tests pin that order headlessly. The HWND is a dummy value: the recording
// core never dereferences it.
// ---------------------------------------------------------------------------
using uiatest::DummyHwnd;
using uiatest::RecordingCore;

TEST(AutomationProvider, HostServesTheSameProviderEveryTime) {
    // UIA compares providers by identity; a second WM_GETOBJECT must not mint a new one.
    RecordingCore core;
    automation::AutomationHost host(core);
    LRESULT r = 0;
    EXPECT_TRUE(host.OnGetObject(DummyHwnd(), 0, static_cast<LPARAM>(UiaRootObjectId), false, r));
    EXPECT_EQ(r, static_cast<LRESULT>(42));
    IUnknown* first = core.returned;
    EXPECT_TRUE(first != nullptr);
    EXPECT_TRUE(host.OnGetObject(DummyHwnd(), 0, static_cast<LPARAM>(UiaRootObjectId), false, r));
    EXPECT_TRUE(core.returned == first);
    host.OnWindowDestroyed(DummyHwnd());
}

TEST(AutomationProvider, TeardownDisconnectsBeforeDetaching) {
    RecordingCore core;
    automation::AutomationHost host(core);
    LRESULT r = 0;
    host.OnGetObject(DummyHwnd(), 0, static_cast<LPARAM>(UiaRootObjectId), false, r);
    ComPtr<IUnknown> provider(core.returned);  // keep it alive past the host's release
    core.calls.clear();

    host.OnWindowDestroyed(DummyHwnd());
    const std::vector<std::string> expected{"disconnect", "releaseEventMap"};
    EXPECT_TRUE(core.calls == expected);
    EXPECT_TRUE(core.attachedAtDisconnect);                   // still answering during it
    EXPECT_FALSE(automation::IsProviderAttached(provider.Get()));  // inert afterwards
    EXPECT_FALSE(host.HasRootProvider());
}

TEST(AutomationProvider, TeardownInsideSendMessageSkipsDisconnectButStillDetaches) {
    // Learn: UiaDisconnectProvider fails with RPC_E_CANTCALLOUT_ININPUTSYNCCALL while
    // responding to SendMessage. Skipping it is the documented trade-off; the provider
    // must still end inert and the event map must still be released.
    RecordingCore core;
    automation::AutomationHost host(core);
    LRESULT r = 0;
    host.OnGetObject(DummyHwnd(), 0, static_cast<LPARAM>(UiaRootObjectId), false, r);
    ComPtr<IUnknown> provider(core.returned);
    core.calls.clear();
    core.inSend = true;

    host.OnWindowDestroyed(DummyHwnd());
    const std::vector<std::string> expected{"releaseEventMap"};
    EXPECT_TRUE(core.calls == expected);
    EXPECT_FALSE(automation::IsProviderAttached(provider.Get()));
}

TEST(AutomationProvider, TeardownWithoutClientMakesNoUiaCalls) {
    // Idle cost: a window no client ever asked about must not touch UIA core at all,
    // not even on destroy.
    RecordingCore core;
    automation::AutomationHost host(core);
    LRESULT r = 0;
    host.OnGetObject(DummyHwnd(), 0, static_cast<LPARAM>(OBJID_CLIENT), false, r);
    host.OnWindowDestroyed(DummyHwnd());
    host.OnWindowDestroyed(DummyHwnd());  // idempotent
    EXPECT_TRUE(core.calls.empty());
}

TEST(AutomationProvider, NoProviderHandedOutAfterTeardown) {
    // A re-entrant WM_GETOBJECT during or after teardown must not hand the provider
    // out again (Learn: it would leave the disconnect incomplete).
    RecordingCore core;
    automation::AutomationHost host(core);
    LRESULT r = 0;
    host.OnGetObject(DummyHwnd(), 0, static_cast<LPARAM>(UiaRootObjectId), false, r);
    host.OnWindowDestroyed(DummyHwnd());
    core.calls.clear();
    EXPECT_FALSE(host.OnGetObject(DummyHwnd(), 0, static_cast<LPARAM>(UiaRootObjectId), false, r));
    EXPECT_TRUE(core.calls.empty());
}
