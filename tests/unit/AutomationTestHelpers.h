// AutomationTestHelpers.h — shared fakes for the headless UI Automation tests.
//
// RecordingCore stands in for UIAutomationCore (automation/UiaCore.h): it records
// every call so tests can assert order, gating and coalescing without a window, a
// COM runtime or a UIA client. FakeWindow stands in for NativeWindowHost's
// IAutomationWindow side. The HWND is a dummy value that nothing dereferences;
// AutomationHost's window queries (DPI, ClientToScreen, GetFocus) fail on it and
// fall back to 96 DPI / origin (0,0) / no OS focus, which the tests rely on.
#pragma once

// FluentUI headers first: they define NOMINMAX before <windows.h>; the UIA SDK
// header pulled in first would leave min/max as macros for everything after it.
#include "../../FluentUI/automation/AutomationHost.h"
#include "../../FluentUI/automation/UiaProvider.h"
#include "../../FluentUI/core/UIContext.h"
#include "../../FluentUI/layout/StackPanel.h"

#include <UIAutomation.h>
#include <oleacc.h>  // OBJID_CLIENT
#include <wrl/client.h>
#include <string>
#include <utility>
#include <vector>

namespace uiatest {

using fluent::ComPtr;  // fl_common.h; tests also `using namespace fluent`

struct RecordingCore final : fluent::automation::IUiaCore {
    std::vector<std::string> calls;
    bool inSend = false;
    bool attachedAtDisconnect = false;
    bool listening = true;
    int flushPosts = 0;
    int disconnects = 0;
    IUnknown* returned = nullptr;
    std::vector<int> events;
    std::vector<std::pair<int, std::wstring>> props;  // (propertyId, new value as text)

    LRESULT ReturnProvider(HWND, WPARAM, LPARAM, IUnknown* p) override {
        calls.push_back("return");
        returned = p;
        return 42;
    }
    HRESULT DisconnectProvider(IUnknown* p) override {
        calls.push_back("disconnect");
        ++disconnects;
        attachedAtDisconnect = fluent::automation::IsProviderAttached(p);
        return S_OK;
    }
    void ReleaseEventMap(HWND) override { calls.push_back("releaseEventMap"); }
    bool InSendMessage() override { return inSend; }
    bool ClientsAreListening() override { return listening; }
    HRESULT RaiseEvent(IUnknown*, int id) override {
        events.push_back(id);
        return S_OK;
    }
    HRESULT RaiseBoolChanged(IUnknown*, int id, bool, bool v) override {
        props.push_back({id, v ? L"true" : L"false"});
        return S_OK;
    }
    HRESULT RaiseIntChanged(IUnknown*, int id, int, int v) override {
        props.push_back({id, std::to_wstring(v)});
        return S_OK;
    }
    HRESULT RaiseDoubleChanged(IUnknown*, int id, double, double v) override {
        props.push_back({id, std::to_wstring(v)});
        return S_OK;
    }
    HRESULT RaiseStringChanged(IUnknown*, int id, const wchar_t*, const wchar_t* v) override {
        props.push_back({id, v});
        return S_OK;
    }
    void PostToHost(HWND, UINT) override { ++flushPosts; }
    // StructureChanged is recorded in `events` with this sentinel id, together with
    // the provider it was raised on.
    static constexpr int kChildrenInvalidated = -1;
    std::vector<IUnknown*> structureOn;
    HRESULT RaiseChildrenInvalidated(IUnknown* p) override {
        events.push_back(kChildrenInvalidated);
        structureOn.push_back(p);
        return S_OK;
    }
};

inline HWND DummyHwnd() { return reinterpret_cast<HWND>(static_cast<uintptr_t>(0x1234)); }

struct FakeWindow final : fluent::automation::IAutomationWindow {
    std::vector<fluent::UIElement*> roots;
    fluent::UIElement* focused = nullptr;
    size_t AutomationRootCount() const override { return roots.size(); }
    fluent::UIElement* AutomationRootAt(size_t i) const override {
        return i < roots.size() ? roots[i] : nullptr;
    }
    fluent::UIElement* AutomationFocusedElement() const override { return focused; }
};

inline std::wstring PropString(IRawElementProviderFragment* f, PROPERTYID id) {
    ComPtr<IRawElementProviderSimple> s;
    if (!f || FAILED(f->QueryInterface(IID_PPV_ARGS(&s)))) return L"<no provider>";
    VARIANT v;
    if (FAILED(s->GetPropertyValue(id, &v))) return L"<error>";
    std::wstring out = v.vt == VT_BSTR ? v.bstrVal : (v.vt == VT_EMPTY ? L"<empty>" : L"<other>");
    VariantClear(&v);
    return out;
}
inline long PropInt(IRawElementProviderFragment* f, PROPERTYID id) {
    ComPtr<IRawElementProviderSimple> s;
    f->QueryInterface(IID_PPV_ARGS(&s));
    VARIANT v;
    if (FAILED(s->GetPropertyValue(id, &v)) || v.vt != VT_I4) return -1;
    return v.lVal;
}
inline int PropBool(IRawElementProviderFragment* f, PROPERTYID id) {  // -1 = not a bool
    ComPtr<IRawElementProviderSimple> s;
    f->QueryInterface(IID_PPV_ARGS(&s));
    VARIANT v;
    if (FAILED(s->GetPropertyValue(id, &v)) || v.vt != VT_BOOL) return -1;
    return v.boolVal ? 1 : 0;
}
inline ComPtr<IRawElementProviderFragment> Nav(IRawElementProviderFragment* f, NavigateDirection d) {
    ComPtr<IRawElementProviderFragment> out;
    if (f) f->Navigate(d, &out);
    return out;
}

// A window with one StackPanel root, the real AutomationHost as its site.
struct UiaFixture {
    RecordingCore core;
    FakeWindow window;
    fluent::automation::AutomationHost host{core};
    std::unique_ptr<fluent::StackPanel> root = std::make_unique<fluent::StackPanel>();

    UiaFixture() {
        host.Bind(&window);
        window.roots.push_back(root.get());
    }
    ~UiaFixture() {
        if (root && root->IsAttached()) root->DetachFromContext();
        host.OnWindowDestroyed(DummyHwnd());
    }
    void Attach() {
        fluent::UIContext ctx;
        ctx.automation = &host;
        root->AttachToContext(ctx);
        root->Arrange({0, 0, 400, 300});
    }
    // What UIA core does first: WM_GETOBJECT, then the root's provider.
    ComPtr<IRawElementProviderFragment> Connect() {
        LRESULT r = 0;
        host.OnGetObject(DummyHwnd(), 0, static_cast<LPARAM>(UiaRootObjectId), false, r);
        ComPtr<IRawElementProviderFragment> f;
        core.returned->QueryInterface(IID_PPV_ARGS(&f));
        return f;
    }
};

}  // namespace uiatest
