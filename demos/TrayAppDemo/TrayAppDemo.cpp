// TrayAppDemo.cpp — 关闭时最小化到托盘。
//
// 这个 demo 的历史值得一提：它原先创建**两个**窗口——一个 HWND_MESSAGE 隐藏窗口
// 专门接收托盘回调，加上 FluentUI 主窗口——并手写 WndProc、全局变量和托盘数据。
// 托盘支持进入框架后（FluentUI/window/TrayIcon.h），那些都不再需要：托盘图标绑定
// 在主窗口自己的 HWND 上，回调消息由 Window::OnAppMessage 拦截。
//
// 隐藏窗口不只是多余，它还是错的：shell 在 Explorer 重启后广播 "TaskbarCreated"，
// 所有托盘图标必须重新添加，而广播消息**到不了** HWND_MESSAGE 窗口。旧设计里
// Explorer 一崩，图标就永久消失了。
#include "../../FluentUI/Api.h"
#include "../../FluentUI/Application.h"
#include "../../FluentUI/window/Window.h"
#include "../../FluentUI/layout/StackPanel.h"
#include "../../FluentUI/controls/TextBlock.h"
#include "../../FluentUI/controls/Button.h"

#include <memory>

using namespace fluent;

namespace {
constexpr UINT IDM_SHOW = 1001;
constexpr UINT IDM_EXIT = 1002;
}  // namespace

class TrayDemoWindow : public Window {
public:
    void OnInitialize() override {
        auto* root = new StackPanel();
        root->SetPadding(Thickness{20.0f});
        root->SetSpacing(16.0f);

        auto* title = root->Emplace<TextBlock>();
        title->SetText(L"FluentUI 托盘应用演示");
        title->SetFontSize(24.0f);
        title->SetWeight(static_cast<DWRITE_FONT_WEIGHT>(600));

        auto* info = root->Emplace<TextBlock>();
        info->SetText(
            L"这是一个托盘应用演示。\n\n"
            L"• 点击窗口的关闭按钮，窗口会最小化到托盘区（右下角）\n"
            L"• 双击托盘图标或右键选择\"显示窗口\"可以恢复窗口\n"
            L"• 右键托盘图标选择\"退出\"才真正关闭程序\n\n"
            L"试试看：点击右上角的 ✕ 按钮！");
        info->SetWrap(true);
        info->SetLineSpacing(1.6f);

        auto* hideBtn = root->Emplace<Button>();
        hideBtn->SetText(L"手动隐藏到托盘");
        subs_ += hideBtn->Click().Subscribe<&TrayDemoWindow::OnHideClicked>(this);

        auto* exitBtn = root->Emplace<Button>();
        exitBtn->SetText(L"退出程序（不隐藏）");
        subs_ += exitBtn->Click().Subscribe<&TrayDemoWindow::OnExitClicked>(this);

        SetRoot(std::unique_ptr<UIElement>(root));
    }

    // 托盘图标必须在 HWND 存在之后才能创建，所以放在 OnContentCreated 而不是
    // OnInitialize——后者跑在原生窗口创建之前，SetTrayIcon 会返回
    // ERROR_INVALID_WINDOW_HANDLE。
    void OnContentCreated() override {
        // 用 LoadIconW 拿到的系统图标：shell 和框架都不接管它的所有权，所以它必须
        // 比托盘图标活得久。系统图标或模块资源图标满足这一点，临时创建再销毁的不行。
        SetTrayIcon(LoadIconW(nullptr, IDI_APPLICATION),
                    L"FluentUI 托盘演示 - 右键打开菜单");
    }

protected:
    // 双击恢复窗口。框架已经处理了"隐藏时还被最小化"这种情况。
    void OnTrayIconDoubleClick(TrayIconEventArgs&) override { RestoreFromTray(); }

    void OnTrayIconRightClick(TrayIconEventArgs& args) override {
        HMENU menu = CreatePopupMenu();
        AppendMenuW(menu, MF_STRING, IDM_SHOW, L"显示窗口(&S)");
        AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
        AppendMenuW(menu, MF_STRING, IDM_EXIT, L"退出(&X)");

        // 托盘菜单必须先 SetForegroundWindow，否则点击菜单外部时它不会关闭——
        // 这是 Windows 对 owner 窗口的要求，不是可选的礼貌。
        SetForegroundWindow(Hwnd());
        const UINT cmd = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_NONOTIFY,
                                        args.screenX, args.screenY, 0, Hwnd(), nullptr);
        DestroyMenu(menu);

        if (cmd == IDM_SHOW) RestoreFromTray();
        else if (cmd == IDM_EXIT) RequestExit();
    }

    void OnClosing(WindowClosingArgs& args) override {
        // 真正退出走 RequestExit，它先撤掉托盘图标再关窗口，所以这里不会再拦。
        if (exiting_) return;

        // 关键：取消销毁，只隐藏。HWND 继续存活，因此托盘回调仍然送达——
        // 一个 NativeWindowHost 的 HWND 一旦销毁就无法再 Show。
        args.cancel = true;
        HideToTray();
        ShowTrayBalloon(L"FluentUI Demo", L"程序已最小化到托盘");
    }

private:
    void OnHideClicked(Button&, RoutedEventArgs&) { HideToTray(); }
    void OnExitClicked(Button&, RoutedEventArgs&) { RequestExit(); }

    void RequestExit() {
        exiting_ = true;
        // 先撤图标。shell 不会可靠地回收已死窗口的图标——没有 NIM_DELETE 就退出，
        // 图标会留在托盘里当幽灵，直到用户把鼠标移过去。
        RemoveTrayIcon();
        if (auto* app = OwningApplication()) app->Shutdown(0);
    }

    SubscriptionBag subs_;
    bool exiting_ = false;
};

int APIENTRY wWinMain(_In_ HINSTANCE hInstance, _In_opt_ HINSTANCE hPrevInstance,
                      _In_ LPWSTR lpCmdLine, _In_ int nCmdShow) {
    UNREFERENCED_PARAMETER(hPrevInstance);
    UNREFERENCED_PARAMETER(lpCmdLine);
    UNREFERENCED_PARAMETER(nCmdShow);

    Application app(hInstance);
    // 托盘应用必须用这个模式：默认的 OnMainWindowClose 会在窗口隐藏后仍然把进程
    // 结束掉，因为窗口计数是按"关闭"算的。只有显式 Shutdown 才退出。
    app.SetShutdownMode(ShutdownMode::OnExplicitShutdown);

    TrayDemoWindow window;
    window.SetTitle(L"FluentUI 托盘应用演示");
    window.SetClientSize(500, 320);
    window.SetTitleBarHeight(24);
    if (FAILED(window.Show(app))) return 1;

    app.TryRun();
    return app.ExitCode();
}
