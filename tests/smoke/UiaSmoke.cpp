// UiaSmoke.cpp — `FluentUITests.exe --uia-smoke`: a REAL UI Automation client
// against a REAL FluentUI window, in one process.
//
// WHY. The headless tests drive our providers through a recording fake of UIA core;
// they cannot show what UIA core itself does with our answers (merging with the
// HWND proxy, COM routing to the UI thread, the disconnect handshake). Those were
// Inspect.exe checks done by hand. This replaces them: the UI thread runs an
// Application with a window; a separate MTA thread is the client (IUIAutomation),
// exactly like Inspect or Narrator but scripted. The client must not run on the UI
// thread: an in-process client calling its own window's provider from the thread
// that has to answer it would deadlock.
//
// Needs a desktop session and a GPU (or WARP), so it is not in the default run.
// Exit code = number of failed checks.
#include "../../FluentUI/FluentUI.h"

#include <UIAutomation.h>

#include <atomic>
#include <chrono>
#include <cstdio>
#include <functional>
#include <thread>

namespace {

using fluent::ComPtr;

std::atomic<int> g_clicks{0};
std::atomic<int> g_textChanges{0};
std::atomic<bool> g_doomedGone{false};
int g_failures = 0;

void Check(bool ok, const char* what) {
    std::printf("[%s] %s\n", ok ? "  OK  " : " FAIL ", what);
    if (!ok) ++g_failures;
}

// Polls `cond` from the client thread while the UI thread pumps.
bool WaitFor(const std::function<bool()>& cond, int ms = 3000) {
    const auto end = std::chrono::steady_clock::now() + std::chrono::milliseconds(ms);
    while (std::chrono::steady_clock::now() < end) {
        if (cond()) return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    return cond();
}

class SmokeWindow final : public fluent::Window {
public:
    SmokeWindow() {
        SetTitle(L"FluentUI UIA smoke");
        SetClientSize(480.0f, 360.0f);
    }

protected:
    void OnInitialize() override {
        using namespace fluent;
        auto root = std::make_unique<StackPanel>();
        root->SetSpacing(8.0f);
        root->SetMargin(Thickness(16.0f));
        panel_ = root.get();

        auto* btn = root->Emplace<Button>();
        btn->SetText(L"Press");
        btn->SetAutomationId(L"btn");
        btn->SetWidth(120.0f);
        btn->SetHAlign(HAlign::Left);
        subs_.push_back(btn->Click().Subscribe(this, [](void*, Button&, RoutedEventArgs&) { ++g_clicks; }));

        auto* chk = root->Emplace<CheckBox>();
        chk->SetText(L"Option");
        chk->SetAutomationId(L"chk");

        auto* txt = root->Emplace<TextBox>();
        txt->SetAutomationId(L"txt");
        txt->SetAutomationName(L"Name field");
        txt->SetMaxLength(8);
        subs_.push_back(txt->TextChanged().Subscribe(
            this, [](void*, TextEditBase&, std::wstring&) { ++g_textChanges; }));

        auto* lbl = root->Emplace<TextBlock>();
        lbl->SetText(L"Hello label");
        lbl->SetAutomationId(L"lbl");

        // Removes itself from its own Click handler: the posted-action and freeze
        // path under a real client.
        auto* doomed = root->Emplace<Button>();
        doomed->SetText(L"Remove me");
        doomed->SetAutomationId(L"doomed");
        // The removal is deferred one pump turn: destroying the sender inside its own
        // Event::Raise is a framework-level hazard unrelated to UIA (Raise reads its
        // slots after each handler).
        subs_.push_back(doomed->Click().Subscribe(this, [](void* o, Button& b, RoutedEventArgs&) {
            auto* self = static_cast<SmokeWindow*>(o);
            fluent::Button* target = &b;
            self->OwningApplication()->Post([self, target] {
                for (size_t i = 0; i < self->panel_->ChildCount(); ++i)
                    if (self->panel_->ChildAt(i) == target) {
                        self->panel_->RemoveAt(i);
                        g_doomedGone = true;
                        return;
                    }
            });
        }));
        SetRoot(std::move(root));
    }

private:
    fluent::StackPanel* panel_ = nullptr;
    std::vector<fluent::Subscription> subs_;
};

}  // namespace

namespace {

ComPtr<IUIAutomationElement> FindById(IUIAutomation* uia, IUIAutomationElement* root,
                                      const wchar_t* id) {
    VARIANT v;
    v.vt = VT_BSTR;
    v.bstrVal = SysAllocString(id);
    ComPtr<IUIAutomationCondition> cond;
    uia->CreatePropertyCondition(UIA_AutomationIdPropertyId, v, &cond);
    VariantClear(&v);
    ComPtr<IUIAutomationElement> out;
    if (cond) root->FindFirst(TreeScope_Descendants, cond.Get(), &out);
    return out;
}

std::wstring Name(IUIAutomationElement* e) {
    BSTR b = nullptr;
    if (!e || FAILED(e->get_CurrentName(&b)) || !b) return L"";
    std::wstring s(b);
    SysFreeString(b);
    return s;
}
CONTROLTYPEID Type(IUIAutomationElement* e) {
    CONTROLTYPEID t = 0;
    if (e) e->get_CurrentControlType(&t);
    return t;
}

// The client half, on its own MTA thread. Every check names the hand check it
// replaces (Inspect tree / hover / Action menu / Watch Focus / close log).
void RunClient(HWND hwnd) {
    CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    {
        ComPtr<IUIAutomation> uia;
        Check(SUCCEEDED(CoCreateInstance(__uuidof(CUIAutomation), nullptr, CLSCTX_INPROC_SERVER,
                                         IID_PPV_ARGS(&uia))), "CoCreateInstance(CUIAutomation)");
        ComPtr<IUIAutomationElement> root;
        if (uia) uia->ElementFromHandle(hwnd, &root);
        Check(root != nullptr, "ElementFromHandle(window)");
        BSTR fw = nullptr;
        if (root) root->get_CurrentFrameworkId(&fw);
        Check(fw && wcscmp(fw, L"FluentUI") == 0, "window FrameworkId == FluentUI");
        SysFreeString(fw);

        auto btn = root ? FindById(uia.Get(), root.Get(), L"btn") : nullptr;
        auto chk = root ? FindById(uia.Get(), root.Get(), L"chk") : nullptr;
        auto txt = root ? FindById(uia.Get(), root.Get(), L"txt") : nullptr;
        auto lbl = root ? FindById(uia.Get(), root.Get(), L"lbl") : nullptr;
        Check(btn && Name(btn.Get()) == L"Press" && Type(btn.Get()) == UIA_ButtonControlTypeId,
              "tree: Button found by AutomationId, Name/ControlType");
        Check(chk && Name(chk.Get()) == L"Option" && Type(chk.Get()) == UIA_CheckBoxControlTypeId,
              "tree: CheckBox Name/ControlType");
        Check(txt && Name(txt.Get()) == L"Name field" && Type(txt.Get()) == UIA_EditControlTypeId,
              "tree: TextBox AutomationName/ControlType");
        Check(lbl && Name(lbl.Get()) == L"Hello label" && Type(lbl.Get()) == UIA_TextControlTypeId,
              "tree: TextBlock Name/ControlType");

        // Hover check: the button's rect is inside the window, and hit-testing its
        // centre returns the button itself.
        RECT wr{};
        GetWindowRect(hwnd, &wr);
        RECT br{};
        if (btn) btn->get_CurrentBoundingRectangle(&br);
        Check(br.right > br.left && br.left >= wr.left && br.right <= wr.right &&
                  br.top >= wr.top && br.bottom <= wr.bottom,
              "BoundingRectangle non-empty and inside the window");
        ComPtr<IUIAutomationElement> hit;
        if (uia) uia->ElementFromPoint(POINT{(br.left + br.right) / 2, (br.top + br.bottom) / 2}, &hit);
        BOOL same = FALSE;
        if (uia && hit && btn) uia->CompareElements(hit.Get(), btn.Get(), &same);
        Check(same == TRUE, "ElementFromPoint(button centre) == button");

        // Action-menu checks: Invoke, Toggle, SetValue through the real patterns.
        ComPtr<IUIAutomationInvokePattern> inv;
        if (btn) btn->GetCurrentPatternAs(UIA_InvokePatternId, IID_PPV_ARGS(&inv));
        Check(inv && SUCCEEDED(inv->Invoke()), "Invoke pattern available and Invoke() succeeds");
        Check(WaitFor([] { return g_clicks.load() == 1; }), "Invoke -> Click raised exactly once");

        ComPtr<IUIAutomationTogglePattern> tog;
        if (chk) chk->GetCurrentPatternAs(UIA_TogglePatternId, IID_PPV_ARGS(&tog));
        Check(tog && SUCCEEDED(tog->Toggle()), "Toggle pattern available and Toggle() succeeds");
        Check(WaitFor([&] {
                  ToggleState s = ToggleState_Off;
                  return tog && SUCCEEDED(tog->get_CurrentToggleState(&s)) && s == ToggleState_On;
              }),
              "Toggle -> ToggleState On");

        ComPtr<IUIAutomationValuePattern> val;
        if (txt) txt->GetCurrentPatternAs(UIA_ValuePatternId, IID_PPV_ARGS(&val));
        BSTR v = SysAllocString(L"hello world");
        Check(val && SUCCEEDED(val->SetValue(v)), "Value pattern SetValue succeeds");
        SysFreeString(v);
        Check(WaitFor([&] {
                  BSTR cur = nullptr;
                  bool ok = val && SUCCEEDED(val->get_CurrentValue(&cur)) && cur &&
                            wcscmp(cur, L"hello wo") == 0;  // MaxLength 8 applied
                  SysFreeString(cur);
                  return ok;
              }),
              "SetValue -> value with MaxLength applied");
        Check(g_textChanges.load() >= 1, "SetValue raised TextChanged");

        // Watch-Focus check: SetFocus moves keyboard focus to the element.
        Check(chk && SUCCEEDED(chk->SetFocus()), "SetFocus() succeeds");
        Check(WaitFor([&] {
                  BOOL f = FALSE;
                  return chk && SUCCEEDED(chk->get_CurrentHasKeyboardFocus(&f)) && f;
              }),
              "SetFocus -> HasKeyboardFocus");

        // Removal under a real client: invoking a button that removes itself; the
        // element the client still holds must then report not-available.
        auto doomed = root ? FindById(uia.Get(), root.Get(), L"doomed") : nullptr;
        ComPtr<IUIAutomationInvokePattern> dinv;
        if (doomed) doomed->GetCurrentPatternAs(UIA_InvokePatternId, IID_PPV_ARGS(&dinv));
        Check(dinv && SUCCEEDED(dinv->Invoke()), "Invoke on self-removing button");
        Check(WaitFor([] { return g_doomedGone.load(); }), "button removed itself");
        Check(WaitFor([&] {
                  if (!doomed) return false;
                  BSTR n = nullptr;
                  const HRESULT hr = doomed->get_CurrentName(&n);
                  SysFreeString(n);
                  return hr == UIA_E_ELEMENTNOTAVAILABLE;
              }),
              "removed element -> UIA_E_ELEMENTNOTAVAILABLE");
        Check(root && FindById(uia.Get(), root.Get(), L"doomed") == nullptr, "removed element gone from tree");

        // Close check: after the window closes, cached elements are dead, not hung.
        PostMessageW(hwnd, WM_CLOSE, 0, 0);
        Check(WaitFor([&] { return !IsWindow(hwnd); }), "window closed");
        BSTR n = nullptr;
        const HRESULT hr = btn ? btn->get_CurrentName(&n) : E_FAIL;
        SysFreeString(n);
        Check(FAILED(hr), "element of a closed window fails instead of hanging");
    }
    CoUninitialize();
}

}  // namespace

// Called from TestMain for `--uia-smoke`.
int RunUiaSmoke() {
    // Unbuffered: a crash must not swallow the [ FAIL ] lines that explain it.
    setvbuf(stdout, nullptr, _IONBF, 0);
    // Match the demo's manifest (Per-Monitor V2): the test exe is a console app
    // without one, and a DPI-unaware window would be bitmap-scaled with virtualized
    // coordinates, which is not what an app built on FluentUI runs as.
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    fluent::Application app(GetModuleHandleW(nullptr));
    SmokeWindow window;
    if (FAILED(window.Show(app))) {
        std::printf("[ FAIL ] window.Show failed (no desktop session / GPU?)\n");
        return 1;
    }
    std::thread client(RunClient, window.Hwnd());
    app.Run();
    client.join();
    std::printf("=== uia-smoke: %d failure%s ===\n", g_failures, g_failures == 1 ? "" : "s");
    return g_failures;
}
