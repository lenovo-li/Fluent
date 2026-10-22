// InstallerControlsPage.cpp — extensions/InstallerControls demo (nav id 113).
//
// A live four-page installer wizard exercising every WizardControl API, plus the
// FolderPicker. The wizard sits in a fixed-height Border: it is a fill-the-window
// control, and inside a ScrollPanel an Auto height would collapse the page area.
#include "../GalleryMain.h"
#include "../../extensions/InstallerControls/InstallerControls.h"
#include "../../FluentUI/layout/StackPanel.h"
#include "../../FluentUI/layout/Border.h"
#include "../../FluentUI/layout/ScrollPanel.h"
#include "../../FluentUI/controls/TextBlock.h"
#include "../../FluentUI/controls/Button.h"
#include "../../FluentUI/controls/CheckBox.h"
#include "../../FluentUI/controls/ProgressBar.h"
#include "../../FluentUI/window/NativeWindowHost.h"
#include "../../FluentUI/base/Event.h"
#include <string>

namespace fluent {

using namespace fluent::wizard;

namespace {

// Page state lives on one heap object owned by the page's lifetime (pages are
// built once and never destroyed — see GalleryApp::pages_). Handlers are
// non-capturing, so everything they touch hangs off this struct.
struct InstallerDemoState {
    WizardControl* wizard = nullptr;
    CheckBox* acceptLicense = nullptr;
    TextBlock* pathText = nullptr;
    ProgressBar* progress = nullptr;
    TextBlock* progressText = nullptr;
    TextBlock* eventLog = nullptr;
    std::function<NativeWindowHost*()> owner;
    std::wstring installPath = L"C:\\Program Files\\FluentApp";
    bool chinese = false;
    bool customFormat = false;
    bool cancelHidden = false;
    int step = 0;          // simulated install progress, 0..10
    SubscriptionBag subs;

    void Log(const std::wstring& line) {
        if (eventLog) eventLog->SetText(line);
    }
};

// Validation hook via subclass: the license page (index 1) cannot be left with
// Next until the box is ticked. Demonstrates the protected virtual.
class DemoWizard : public WizardControl {
public:
    InstallerDemoState* state = nullptr;
protected:
    bool ValidateCurrentPage() override {
        if (CurrentPage() == 1 && state && state->acceptLicense &&
            !state->acceptLicense->IsChecked()) {
            state->Log(L"ValidateCurrentPage() → false：请先勾选「我接受许可协议」");
            return false;
        }
        return true;
    }
};

TextBlock* AddText(Panel* p, const wchar_t* text, float size = 0.0f) {
    auto* t = p->Add(std::make_unique<TextBlock>());
    t->SetText(text);
    t->SetWrap(true);
    if (size > 0.0f) t->SetFontSize(size);
    return t;
}

Button* AddButton(Panel* p, const wchar_t* text) {
    auto* b = p->Add(std::make_unique<Button>());
    b->SetText(text);
    return b;
}

void RefreshProgress(InstallerDemoState* s) {
    s->progress->SetValue(static_cast<float>(s->step) / 10.0f);
    s->progressText->SetText(L"已完成 " + std::to_wstring(s->step * 10) + L"%" +
                             (s->step < 10 ? L" —— 安装中导航已锁定（SetNavigationLocked）"
                                           : L""));
}

}  // namespace

std::unique_ptr<ScrollPanel> GalleryApp::CreateInstallerControlsPage() {
    auto [page, content] = CreatePageShell(L"Installer Controls");
    auto* s = new InstallerDemoState();  // lives as long as the page (never destroyed)
    s->owner = ownerWindow_;

    AddNote(content,
            L"extensions/InstallerControls 是独立扩展库（命名空间 fluent::wizard），链接 FluentUI.lib。"
            L"WizardControl = 多页向导 + Back/Next(Finish)/Cancel 按钮栏；FolderPicker = 文件夹选择对话框。");

    // --- The live wizard -----------------------------------------------------
    auto* card = CreateExampleCard(content, L"五页安装向导（可交互）");
    auto* frame = card->Add(std::make_unique<Border>());
    frame->SetHeight(360.0f);
    frame->SetBorderThickness(1.0f);
    frame->SetCornerRadius(4.0f);
    auto* wiz = frame->SetChild(std::make_unique<DemoWizard>());
    wiz->state = s;
    s->wizard = wiz;
    wiz->SetShowStepIndicator(true);

    // Page 0: welcome.
    auto welcome = std::make_unique<StackPanel>();
    welcome->SetPadding(Thickness{16.0f});
    welcome->SetSpacing(8.0f);
    AddText(welcome.get(), L"欢迎使用 FluentApp 安装程序", 20.0f);
    AddText(welcome.get(), L"点 Next 继续。第一页不淡入（窗口打开时不能从透明开始）。");
    wiz->AddPage(L"欢迎", std::move(welcome));

    // Page 1: license — gated by ValidateCurrentPage.
    auto license = std::make_unique<StackPanel>();
    license->SetPadding(Thickness{16.0f});
    license->SetSpacing(8.0f);
    AddText(license.get(), L"许可协议", 20.0f);
    AddText(license.get(), L"不勾选时点 Next 会被 ValidateCurrentPage() 拦下；Back 永远可用。");
    s->acceptLicense = license->Add(std::make_unique<CheckBox>());
    s->acceptLicense->SetText(L"我接受许可协议");
    wiz->AddPage(L"许可", std::move(license));

    // Page 2: path + FolderPicker. Leaving it starts the (simulated) install.
    auto path = std::make_unique<StackPanel>();
    path->SetPadding(Thickness{16.0f});
    path->SetSpacing(8.0f);
    AddText(path.get(), L"安装位置", 20.0f);
    s->pathText = AddText(path.get(), s->installPath.c_str());
    auto* browse = AddButton(path.get(), L"浏览…（FolderPicker::ShowDialog）");
    s->subs.Keep(browse->Click().Subscribe(s, [](void* o, Button&, RoutedEventArgs&) {
        auto* st = static_cast<InstallerDemoState*>(o);
        NativeWindowHost* host = st->owner ? st->owner() : nullptr;
        std::wstring picked = FolderPicker::ShowDialog(host ? host->Hwnd() : nullptr,
                                                       L"选择安装文件夹", st->installPath);
        if (picked.empty()) { st->Log(L"FolderPicker：用户取消（返回空串）"); return; }
        st->installPath = picked;
        st->pathText->SetText(picked);
        st->Log(L"FolderPicker 返回：" + picked);
    }));
    wiz->AddPage(L"位置", std::move(path));

    // Page 3: install progress. Locked while "installing"; the step button stands
    // in for a worker thread (no timer — idle must stay zero-cost).
    auto install = std::make_unique<StackPanel>();
    install->SetPadding(Thickness{16.0f});
    install->SetSpacing(8.0f);
    AddText(install.get(), L"正在安装", 20.0f);
    s->progress = install->Add(std::make_unique<ProgressBar>());
    s->progressText = AddText(install.get(), L"");
    auto* stepBtn = AddButton(install.get(), L"模拟安装进度 +10%");
    s->subs.Keep(stepBtn->Click().Subscribe(s, [](void* o, Button&, RoutedEventArgs&) {
        auto* st = static_cast<InstallerDemoState*>(o);
        if (st->step >= 10) return;
        ++st->step;
        RefreshProgress(st);
        if (st->step == 10) {
            st->wizard->SetNavigationLocked(false);
            st->wizard->GoToPage(4);  // programmatic: works regardless of the lock
        }
    }));
    wiz->AddPage(L"安装", std::move(install));

    // Page 4: done.
    auto done = std::make_unique<StackPanel>();
    done->SetPadding(Thickness{16.0f});
    done->SetSpacing(8.0f);
    AddText(done.get(), L"安装完成", 20.0f);
    AddText(done.get(), L"最后一页 Next 显示为 Finish，点击触发 Finishing 事件。");
    wiz->AddPage(L"完成", std::move(done));

    // --- Events --------------------------------------------------------------
    s->eventLog = AddText(card, L"事件日志：（操作向导后显示最近一条）");
    s->subs.Keep(wiz->PageChanged().Subscribe(s, [](void* o, WizardControl& w, int& idx) {
        auto* st = static_cast<InstallerDemoState*>(o);
        st->Log(L"PageChanged → " + std::to_wstring(idx) + L"（" + w.PageTitle(idx) + L"）");
        if (idx == 3 && st->step < 10) {  // entering the install page
            w.SetNavigationLocked(true);
            RefreshProgress(st);
        }
    }));
    s->subs.Keep(wiz->Navigating().Subscribe(s,
        [](void* o, WizardControl&, WizardNavigatingArgs& a) {
            auto* st = static_cast<InstallerDemoState*>(o);
            st->Log(L"Navigating from " + std::to_wstring(a.from) + L" to " +
                    (a.to == WizardNavigatingArgs::kFinish ? std::wstring(L"Finish")
                                                           : std::to_wstring(a.to)));
        }));
    s->subs.Keep(wiz->Finishing().Subscribe(s, [](void* o, WizardControl&, RoutedEventArgs&) {
        static_cast<InstallerDemoState*>(o)->Log(L"Finishing：安装向导完成（真实应用在这里关窗口）");
    }));
    s->subs.Keep(wiz->Cancelled().Subscribe(s, [](void* o, WizardControl&, RoutedEventArgs&) {
        static_cast<InstallerDemoState*>(o)->Log(L"Cancelled：用户点了取消（真实应用在这里确认退出）");
    }));

    // --- Options: every remaining setter ------------------------------------
    auto* opts = CreateExampleCard(content, L"选项（作用于上面的向导）");
    auto* row = AddRow(opts);
    auto* btnLang = AddButton(row, L"切换中文按钮文字（SetButtonLabels）");
    s->subs.Keep(btnLang->Click().Subscribe(s, [](void* o, Button&, RoutedEventArgs&) {
        auto* st = static_cast<InstallerDemoState*>(o);
        st->chinese = !st->chinese;
        if (st->chinese) st->wizard->SetButtonLabels(L"上一步", L"下一步", L"取消", L"完成");
        else st->wizard->SetButtonLabels(L"Back", L"Next", L"Cancel", L"Finish");
    }));
    auto* btnFmt = AddButton(row, L"切换步骤格式（SetStepIndicatorFormat）");
    s->subs.Keep(btnFmt->Click().Subscribe(s, [](void* o, Button&, RoutedEventArgs&) {
        auto* st = static_cast<InstallerDemoState*>(o);
        st->customFormat = !st->customFormat;
        st->wizard->SetStepIndicatorFormat(st->customFormat ? L"第 {current}/{total} 步 · {title}"
                                                            : L"Step {current} of {total}: {title}");
    }));
    auto* btnInd = AddButton(row, L"显示/隐藏步骤指示（SetShowStepIndicator）");
    s->subs.Keep(btnInd->Click().Subscribe(s, [](void* o, Button&, RoutedEventArgs&) {
        auto* st = static_cast<InstallerDemoState*>(o);
        st->wizard->SetShowStepIndicator(!st->wizard->ShowStepIndicator());
    }));

    auto* row2 = AddRow(opts);
    auto* btnCancel = AddButton(row2, L"显示/隐藏取消按钮（SetCancelVisible）");
    s->subs.Keep(btnCancel->Click().Subscribe(s, [](void* o, Button&, RoutedEventArgs&) {
        auto* st = static_cast<InstallerDemoState*>(o);
        st->cancelHidden = !st->cancelHidden;
        st->wizard->SetCancelVisible(!st->cancelHidden);
    }));
    auto* btnFade = AddButton(row2, L"切换淡入/无过渡（SetTransitionStyle）");
    s->subs.Keep(btnFade->Click().Subscribe(s, [](void* o, Button&, RoutedEventArgs&) {
        auto* st = static_cast<InstallerDemoState*>(o);
        const bool fade = st->wizard->Transition() == WizardControl::TransitionStyle::Fade;
        st->wizard->SetTransitionStyle(fade ? WizardControl::TransitionStyle::None
                                            : WizardControl::TransitionStyle::Fade);
        st->Log(fade ? L"过渡：None（瞬切）" : L"过渡：Fade（系统关闭动画时自动跳过）");
    }));
    auto* btnReset = AddButton(row2, L"重来（GoToPage(0)）");
    s->subs.Keep(btnReset->Click().Subscribe(s, [](void* o, Button&, RoutedEventArgs&) {
        auto* st = static_cast<InstallerDemoState*>(o);
        st->step = 0;
        st->acceptLicense->SetChecked(false);
        st->wizard->SetNavigationLocked(false);
        st->wizard->GoToPage(0);
    }));

    // --- Code -----------------------------------------------------------------
    CreateCodeExample(content, LR"(#include "extensions/InstallerControls/InstallerControls.h"
using namespace fluent::wizard;

auto* wizard = root->Emplace<WizardControl>();
wizard->AddPage(L"Welcome", BuildWelcomePage());
wizard->AddPage(L"Install", BuildProgressPage());
wizard->SetButtonLabels(L"上一步", L"下一步", L"取消", L"完成");
wizard->SetShowStepIndicator(true);

// Handler 参数是 Args&（int&），必须保存 Subscription
pageSub_ = wizard->PageChanged().Subscribe(this,
    [](void* owner, WizardControl& w, int& page) {
        if (page == 1) {
            w.SetNavigationLocked(true);   // 安装中禁止翻页
            static_cast<MyWindow*>(owner)->StartInstall();
        }
    });
// 安装线程完成后回到 UI 线程：
//   wizard->SetNavigationLocked(false); wizard->GoToPage(2);

std::wstring dir = FolderPicker::ShowDialog(Hwnd(), L"选择安装文件夹");)");

    return std::move(page);
}

}  // namespace fluent
