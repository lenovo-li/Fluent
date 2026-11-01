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
#include "../../FluentUI/controls/DataGrid.h"
#include "../../FluentUI/controls/Expander.h"
#include "../../FluentUI/controls/InfoBar.h"
#include "../../FluentUI/controls/MenuFlyout.h"
#include "../../FluentUI/controls/ListBox.h"
#include "../../FluentUI/controls/Slider.h"

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
std::atomic<int> g_listSelected{-1};
std::atomic<int> g_infoClosed{0};
std::atomic<int> g_menuCopied{0};
std::atomic<int> g_gridSelected{-1};
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

        // A RangeValue, an ExpandCollapse, a list with item peers.
        auto* sld = root->Emplace<Slider>();
        sld->SetAutomationId(L"sld");
        sld->SetMinimum(0);
        sld->SetMaximum(100);
        sld->SetValue(10);
        auto* ex = root->Emplace<Expander>();
        ex->SetHeader(L"More");
        ex->SetAutomationId(L"ex");
        ex->SetContent(std::make_unique<TextBlock>())->SetText(L"Hidden text");
        auto* lst = root->Emplace<ListBox>();
        lst->SetAutomationId(L"lst");
        lst->SetHeight(70);
        lst->SetItems({L"Alpha", L"Beta", L"Gamma", L"Delta"});
        list_ = lst;

        // A menu button: Click opens a MenuFlyout in its own popup window.
        menu_ = std::make_unique<fluent::MenuFlyout>();
        {
            std::vector<fluent::MenuItem> items;
            fluent::MenuItem copy;
            copy.text = L"Copy";
            copy.accelerator = L"Ctrl+C";
            // The event lives on the heap, so the subscription survives the move.
            subs_.push_back(copy.Invoked().Subscribe(
                this, [](void*, fluent::MenuItem&, fluent::RoutedEventArgs&) { ++g_menuCopied; }));
            items.push_back(std::move(copy));
            items.push_back(fluent::MenuItem::Sep());
            fluent::MenuItem paste;
            paste.text = L"Paste";
            items.push_back(std::move(paste));
            menu_->SetItems(std::move(items));
        }
        auto* menuBtn = root->Emplace<Button>();
        menuBtn->SetText(L"Menu");
        menuBtn->SetAutomationId(L"menuBtn");
        menuBtn->SetWidth(120.0f);
        menuBtn->SetHAlign(HAlign::Left);
        menuBtn_ = menuBtn;
        menuBtn->SetContextMenuRef(menu_.get());  // hands the flyout the button's context on attach
        subs_.push_back(menuBtn->Click().Subscribe(this, [](void* o, Button&, RoutedEventArgs&) {
            auto* self = static_cast<SmokeWindow*>(o);
            RECT r{};
            GetWindowRect(self->Hwnd(), &r);
            self->menu_->ShowBelow(RECT{r.left + 40, r.top + 80, r.left + 160, r.top + 100});
        }));

        // A grid with drawn rows and a closable live status bar.
        auto* grid = root->Emplace<fluent::DataGrid>();
        grid->SetAutomationId(L"grid");
        grid->SetHeight(90);
        grid->SetColumns({{L"Name", 120.0f}, {L"Size", 80.0f}});
        grid->SetRowCount(50);
        grid->SetCellProvider([](int r, int c) {
            return c == 0 ? L"file" + std::to_wstring(r) : std::to_wstring(r * 10) + L" KB";
        });
        subs_.push_back(grid->SelectionChanged().Subscribe(
            this, [](void*, fluent::DataGrid& g, fluent::RoutedEventArgs&) { g_gridSelected = g.SelectedRow(); }));
        auto* info = root->Emplace<fluent::InfoBar>();
        info->SetAutomationId(L"info");
        info->SetTitle(L"Update ready");
        info->SetMessage(L"Restart to apply.");
        info->SetClosable(true);
        subs_.push_back(info->Closed().Subscribe(
            this, [](void*, fluent::InfoBar&, fluent::RoutedEventArgs&) { ++g_infoClosed; }));

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
    std::unique_ptr<fluent::MenuFlyout> menu_;
    fluent::Button* menuBtn_ = nullptr;
    fluent::StackPanel* panel_ = nullptr;

public:
    fluent::ListBox* list_ = nullptr;

private:
    std::vector<fluent::Subscription> subs_;
};

}  // namespace

namespace {

// Counts the events UIA core actually DELIVERS to a subscribed client: proves our
// raises reach a real client (UiaClientsAreListening true, provider resolvable).
class EventCounter final : public IUIAutomationFocusChangedEventHandler,
                           public IUIAutomationStructureChangedEventHandler,
                           public IUIAutomationEventHandler {
public:
    std::atomic<int> focus{0}, structure{0}, menuOpened{0}, invoked{0};
    IFACEMETHODIMP_(ULONG) AddRef() override { return ++refs_; }
    IFACEMETHODIMP_(ULONG) Release() override {
        const ULONG r = --refs_;
        if (!r) delete this;
        return r;
    }
    IFACEMETHODIMP QueryInterface(REFIID riid, void** out) override {
        if (riid == __uuidof(IUnknown) || riid == __uuidof(IUIAutomationFocusChangedEventHandler))
            *out = static_cast<IUIAutomationFocusChangedEventHandler*>(this);
        else if (riid == __uuidof(IUIAutomationStructureChangedEventHandler))
            *out = static_cast<IUIAutomationStructureChangedEventHandler*>(this);
        else if (riid == __uuidof(IUIAutomationEventHandler))
            *out = static_cast<IUIAutomationEventHandler*>(this);
        else {
            *out = nullptr;
            return E_NOINTERFACE;
        }
        AddRef();
        return S_OK;
    }
    IFACEMETHODIMP HandleFocusChangedEvent(IUIAutomationElement*) override {
        ++focus;
        return S_OK;
    }
    IFACEMETHODIMP HandleStructureChangedEvent(IUIAutomationElement*, StructureChangeType,
                                               SAFEARRAY*) override {
        ++structure;
        return S_OK;
    }
    IFACEMETHODIMP HandleAutomationEvent(IUIAutomationElement*, EVENTID id) override {
        if (id == UIA_MenuOpenedEventId) ++menuOpened;
        if (id == UIA_Invoke_InvokedEventId) ++invoked;
        return S_OK;
    }

private:
    std::atomic<ULONG> refs_{1};
};

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
        // The client thread starts as soon as Show() returns, before the UI thread
        // enters its pump; on a busy machine (right after a build) the first
        // WM_GETOBJECT can time out inside UIA. Wait for the window to answer
        // messages, then retry the connection instead of failing the whole run.
        WaitFor([&] {
            DWORD_PTR r = 0;
            return SendMessageTimeoutW(hwnd, WM_NULL, 0, 0, SMTO_ABORTIFHUNG, 200, &r) != 0;
        }, 5000);
        ComPtr<IUIAutomationElement> root;
        WaitFor([&] {
            root.Reset();
            if (uia) uia->ElementFromHandle(hwnd, &root);
            BSTR f = nullptr;
            const bool ours = root && SUCCEEDED(root->get_CurrentFrameworkId(&f)) && f &&
                              wcscmp(f, L"FluentUI") == 0;
            SysFreeString(f);
            return ours;
        }, 10000);
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

        // Event delivery: subscribe like Narrator/Accessibility Insights do.
        ComPtr<EventCounter> events;
        events.Attach(new EventCounter());
        if (uia) {
            uia->AddFocusChangedEventHandler(nullptr, events.Get());
            ComPtr<IUIAutomationElement> desktop;
            uia->GetRootElement(&desktop);
            if (desktop)
                uia->AddAutomationEventHandler(UIA_MenuOpenedEventId, desktop.Get(), TreeScope_Subtree,
                                               nullptr, events.Get());
            if (root)
                uia->AddStructureChangedEventHandler(root.Get(), TreeScope_Subtree, nullptr,
                                                     events.Get());
        }

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
        Check(WaitFor([&] { return events->focus.load() > 0; }), "FocusChanged event delivered");
        // Window re-activation (Alt+Tab back): WM_SETFOCUS re-announces the focus.
        const int focusBefore = events->focus.load();
        PostMessageW(hwnd, WM_SETFOCUS, 0, 0);
        Check(WaitFor([&] { return events->focus.load() > focusBefore; }),
              "WM_SETFOCUS -> FocusChanged re-announced");

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
        Check(WaitFor([&] { return events->structure.load() > 0; }),
              "removal -> StructureChanged event delivered");
        if (uia) uia->RemoveAllEventHandlers();

        // --- Range, expand, list and caption patterns through the real client ---
        auto sld = FindById(uia.Get(), root.Get(), L"sld");
        ComPtr<IUIAutomationRangeValuePattern> rv;
        if (sld) sld->GetCurrentPatternAs(UIA_RangeValuePatternId, IID_PPV_ARGS(&rv));
        Check(rv && Type(sld.Get()) == UIA_SliderControlTypeId, "Slider: RangeValue pattern");
        Check(rv && SUCCEEDED(rv->SetValue(42)), "Slider: SetValue(42)");
        Check(WaitFor([&] {
                  double v = 0;
                  return rv && SUCCEEDED(rv->get_CurrentValue(&v)) && v > 41.5 && v < 42.5;
              }),
              "Slider: value reads back 42");

        auto ex = FindById(uia.Get(), root.Get(), L"ex");
        ComPtr<IUIAutomationExpandCollapsePattern> ec;
        if (ex) ex->GetCurrentPatternAs(UIA_ExpandCollapsePatternId, IID_PPV_ARGS(&ec));
        Check(ec && SUCCEEDED(ec->Expand()), "Expander: Expand()");
        Check(WaitFor([&] {
                  ExpandCollapseState st = ExpandCollapseState_Collapsed;
                  return ec && SUCCEEDED(ec->get_CurrentExpandCollapseState(&st)) &&
                         st == ExpandCollapseState_Expanded;
              }),
              "Expander: state Expanded");
        Check(WaitFor([&] {
                  VARIANT nv;
                  nv.vt = VT_BSTR;
                  nv.bstrVal = SysAllocString(L"Hidden text");
                  ComPtr<IUIAutomationCondition> c;
                  uia->CreatePropertyCondition(UIA_NamePropertyId, nv, &c);
                  VariantClear(&nv);
                  ComPtr<IUIAutomationElement> found;
                  if (c && ex) ex->FindFirst(TreeScope_Descendants, c.Get(), &found);
                  return found != nullptr;
              }),
              "Expander: content appears in the tree after expanding");

        auto lst = FindById(uia.Get(), root.Get(), L"lst");
        Check(lst && Type(lst.Get()) == UIA_ListControlTypeId, "ListBox: List control type");
        ComPtr<IUIAutomationCondition> isItem;
        {
            VARIANT tv;
            tv.vt = VT_I4;
            tv.lVal = UIA_ListItemControlTypeId;
            uia->CreatePropertyCondition(UIA_ControlTypePropertyId, tv, &isItem);
        }
        ComPtr<IUIAutomationElementArray> items;
        if (lst && isItem) lst->FindAll(TreeScope_Children, isItem.Get(), &items);
        int itemCount = 0;
        if (items) items->get_Length(&itemCount);
        Check(itemCount == 4, "ListBox: 4 ListItem children (rows are not elements)");
        ComPtr<IUIAutomationElement> gamma;
        if (items && itemCount >= 3) items->GetElement(2, &gamma);
        Check(gamma && Name(gamma.Get()) == L"Gamma", "ListBox: third item named Gamma");
        ComPtr<IUIAutomationSelectionItemPattern> sip;
        if (gamma) gamma->GetCurrentPatternAs(UIA_SelectionItemPatternId, IID_PPV_ARGS(&sip));
        Check(sip && SUCCEEDED(sip->Select()), "ListBox: Select() on Gamma");
        Check(WaitFor([&] { return g_listSelected.load() == 2; }), "ListBox: app sees index 2 selected");

        // TextPattern: the edit's document range reads the text the client set.
        {
            ComPtr<IUIAutomationTextPattern> tp;
            if (txt) txt->GetCurrentPatternAs(UIA_TextPatternId, IID_PPV_ARGS(&tp));
            ComPtr<IUIAutomationTextRange> doc;
            if (tp) tp->get_DocumentRange(&doc);
            BSTR t = nullptr;
            if (doc) doc->GetText(-1, &t);
            const std::wstring text = t ? std::wstring(t, SysStringLen(t)) : L"<none>";
            if (t) SysFreeString(t);
            Check(tp && !text.empty() && text != L"<none>", "TextBox: TextPattern document range has text");
        }

        // DataGrid rows are DataItems named by their cells; Select reaches the app.
        auto grid = FindById(uia.Get(), root.Get(), L"grid");
        Check(grid && Type(grid.Get()) == UIA_DataGridControlTypeId, "DataGrid: DataGrid control type");
        ComPtr<IUIAutomationElement> row3;
        {
            ComPtr<IUIAutomationTreeWalker> walker;
            uia->get_RawViewWalker(&walker);
            if (walker && grid) {
                walker->GetFirstChildElement(grid.Get(), &row3);
                for (int k = 0; k < 3 && row3; ++k) {
                    ComPtr<IUIAutomationElement> next;
                    walker->GetNextSiblingElement(row3.Get(), &next);
                    row3 = next;
                }
            }
        }
        Check(row3 && Type(row3.Get()) == UIA_DataItemControlTypeId && Name(row3.Get()) == L"file3, 30 KB",
              "DataGrid: fourth row is a DataItem named by its cells");
        {
            ComPtr<IUIAutomationTreeWalker> walker;
            uia->get_RawViewWalker(&walker);
            ComPtr<IUIAutomationElement> cell0, cell1;
            if (walker && row3) walker->GetFirstChildElement(row3.Get(), &cell0);
            if (walker && cell0) walker->GetNextSiblingElement(cell0.Get(), &cell1);
            Check(cell0 && Name(cell0.Get()) == L"file3" && cell1 && Name(cell1.Get()) == L"30 KB",
                  "DataGrid: row 3 lists its cells (file3, 30 KB)");
        }
        // A row outside the exposed window, found by name and realized (scrolled in).
        {
            ComPtr<IUIAutomationItemContainerPattern> icp;
            if (grid) grid->GetCurrentPatternAs(UIA_ItemContainerPatternId, IID_PPV_ARGS(&icp));
            VARIANT nv;
            nv.vt = VT_BSTR;
            nv.bstrVal = SysAllocString(L"file40, 400 KB");
            ComPtr<IUIAutomationElement> farRow;
            if (icp) icp->FindItemByProperty(nullptr, UIA_NamePropertyId, nv, &farRow);
            VariantClear(&nv);
            Check(farRow != nullptr, "DataGrid: ItemContainer finds row 40 outside the window");
            ComPtr<IUIAutomationVirtualizedItemPattern> vip;
            if (farRow) farRow->GetCurrentPatternAs(UIA_VirtualizedItemPatternId, IID_PPV_ARGS(&vip));
            Check(vip && SUCCEEDED(vip->Realize()), "DataGrid: VirtualizedItem.Realize() on row 40");
            Check(WaitFor([&] {
                      BOOL off = TRUE;
                      return farRow && SUCCEEDED(farRow->get_CurrentIsOffscreen(&off)) && !off;
                  }),
                  "DataGrid: row 40 is on screen after Realize");
        }
        ComPtr<IUIAutomationSelectionItemPattern> rsi;
        if (row3) row3->GetCurrentPatternAs(UIA_SelectionItemPatternId, IID_PPV_ARGS(&rsi));
        Check(rsi && SUCCEEDED(rsi->Select()), "DataGrid: Select() on row 3");
        Check(WaitFor([] { return g_gridSelected.load() == 3; }), "DataGrid: app's SelectionChanged sees row 3");

        // InfoBar: a polite live region whose close button closes through the app.
        auto info = FindById(uia.Get(), root.Get(), L"info");
        VARIANT live;
        VariantInit(&live);
        if (info) info->GetCurrentPropertyValue(UIA_LiveSettingPropertyId, &live);
        Check(info && Type(info.Get()) == UIA_StatusBarControlTypeId && live.vt == VT_I4 && live.lVal == Polite,
              "InfoBar: StatusBar with LiveSetting Polite");
        ComPtr<IUIAutomationElement> infoClose;
        {
            ComPtr<IUIAutomationTreeWalker> walker;
            uia->get_RawViewWalker(&walker);
            if (walker && info) walker->GetFirstChildElement(info.Get(), &infoClose);
        }
        ComPtr<IUIAutomationInvokePattern> icInv;
        if (infoClose) infoClose->GetCurrentPatternAs(UIA_InvokePatternId, IID_PPV_ARGS(&icInv));
        Check(icInv && SUCCEEDED(icInv->Invoke()), "InfoBar: Invoke() on its close button");
        Check(WaitFor([] { return g_infoClosed.load() == 1; }), "InfoBar: app's Closed raised once");

        // MenuFlyout: the level is its own popup window; MenuOpened reaches a client
        // listening on the desktop, and Invoke on a MenuItem raises the app's Invoked.
        {
            auto menuBtn = FindById(uia.Get(), root.Get(), L"menuBtn");
            ComPtr<IUIAutomationInvokePattern> mb;
            if (menuBtn) menuBtn->GetCurrentPatternAs(UIA_InvokePatternId, IID_PPV_ARGS(&mb));
            Check(mb && SUCCEEDED(mb->Invoke()), "MenuFlyout: open via the menu button");
            HWND popup = nullptr;
            Check(WaitFor([&] {
                      popup = nullptr;
                      EnumWindows([](HWND h, LPARAM lp) -> BOOL {
                          wchar_t cls[64] = {};
                          GetClassNameW(h, cls, 64);
                          if (IsWindowVisible(h) && wcscmp(cls, L"FluentUI.PopupHost") == 0) {
                              *reinterpret_cast<HWND*>(lp) = h;
                              return FALSE;
                          }
                          return TRUE;
                      }, reinterpret_cast<LPARAM>(&popup));
                      return popup != nullptr;
                  }),
                  "MenuFlyout: popup window visible");
            // MenuOpened is raised (UiaRaiseAutomationEvent returns S_OK on the Menu
            // peer), but a desktop-scoped listener has not been seen to receive it when
            // it is raised before any client has talked to the new popup window, in
            // this process or from another one. Reported, not failed: whether Narrator
            // announces the menu is a check on real hardware.
            WaitFor([&] { return events->menuOpened.load() > 0; });
            std::printf("[ NOTE ] MenuFlyout: MenuOpened delivered to a desktop listener: %s\n",
                        events->menuOpened.load() > 0 ? "yes" : "no (see comment)");
            ComPtr<IUIAutomationElement> proot, menuEl, copyEl;
            if (popup) uia->ElementFromHandle(popup, &proot);
            if (proot) {
                VARIANT tv;
                tv.vt = VT_I4;
                tv.lVal = UIA_MenuControlTypeId;
                ComPtr<IUIAutomationCondition> c;
                uia->CreatePropertyCondition(UIA_ControlTypePropertyId, tv, &c);
                if (c) proot->FindFirst(TreeScope_Descendants, c.Get(), &menuEl);
            }
            Check(menuEl != nullptr, "MenuFlyout: Menu element inside the popup");
            if (menuEl) {
                VARIANT nv;
                nv.vt = VT_BSTR;
                nv.bstrVal = SysAllocString(L"Copy");
                ComPtr<IUIAutomationCondition> c;
                uia->CreatePropertyCondition(UIA_NamePropertyId, nv, &c);
                VariantClear(&nv);
                if (c) menuEl->FindFirst(TreeScope_Children, c.Get(), &copyEl);
            }
            Check(copyEl && Type(copyEl.Get()) == UIA_MenuItemControlTypeId, "MenuFlyout: Copy is a MenuItem");
            // An event raised by a popup-window provider reaches a client registered on
            // the element (proves the popup's provider is resolvable for event routing).
            if (copyEl)
                uia->AddAutomationEventHandler(UIA_Invoke_InvokedEventId, copyEl.Get(), TreeScope_Element,
                                               nullptr, events.Get());
            ComPtr<IUIAutomationInvokePattern> ci;
            if (copyEl) copyEl->GetCurrentPatternAs(UIA_InvokePatternId, IID_PPV_ARGS(&ci));
            Check(ci && SUCCEEDED(ci->Invoke()), "MenuFlyout: Invoke() on Copy");
            Check(WaitFor([] { return g_menuCopied.load() == 1; }), "MenuFlyout: app's Invoked raised once");
            Check(WaitFor([&] { return events->invoked.load() > 0; }), "MenuFlyout: UIA Invoked delivered from the popup");
            Check(WaitFor([&] { return popup && !IsWindowVisible(popup); }), "MenuFlyout: menu closed after invoke");
        }

        // Caption buttons: Minimize/Maximize/Close are real buttons under the window.
        ComPtr<IUIAutomationElement> closeBtn;
        {
            VARIANT idv;
            idv.vt = VT_BSTR;
            idv.bstrVal = SysAllocString(L"Close");
            ComPtr<IUIAutomationCondition> c;
            uia->CreatePropertyCondition(UIA_AutomationIdPropertyId, idv, &c);
            VariantClear(&idv);
            if (c) root->FindFirst(TreeScope_Children, c.Get(), &closeBtn);
        }
        Check(closeBtn && Type(closeBtn.Get()) == UIA_ButtonControlTypeId && !Name(closeBtn.Get()).empty(),
              "caption: Close button exposed with a localized name");

        // Close check: after the window closes, cached elements are dead, not hung.
        ComPtr<IUIAutomationInvokePattern> closeInv;
        if (closeBtn) closeBtn->GetCurrentPatternAs(UIA_InvokePatternId, IID_PPV_ARGS(&closeInv));
        const bool invoked = closeInv && SUCCEEDED(closeInv->Invoke());
        Check(invoked, "caption: Invoke() on Close");
        if (!invoked) PostMessageW(hwnd, WM_CLOSE, 0, 0);
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
    fluent::Subscription listSub;
    if (window.list_)
        listSub = window.list_->SelectionChanged().Subscribe(
            nullptr, [](void*, fluent::ListBox&, int& i) { g_listSelected = i; });
    std::thread client(RunClient, window.Hwnd());
    app.Run();
    client.join();
    std::printf("=== uia-smoke: %d failure%s ===\n", g_failures, g_failures == 1 ? "" : "s");
    return g_failures;
}
