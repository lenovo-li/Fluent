// TrayAppDemo.cpp - 托盘应用最小化演示
// 演示：点击关闭按钮时最小化到托盘，而不是退出程序

// 先包含 FluentUI 头文件（它们会正确处理 Windows.h）
#include "../../FluentUI/Api.h"
#include "../../FluentUI/Application.h"
#include "../../FluentUI/window/Window.h"
#include "../../FluentUI/layout/StackPanel.h"
#include "../../FluentUI/controls/TextBlock.h"
#include "../../FluentUI/controls/Button.h"

// 然后包含 Shell API（用于托盘图标）
#include <shellapi.h>

#include <memory>
#include <string>

using namespace fluent;

// 托盘图标消息 ID
#define WM_TRAYICON (WM_USER + 1)
#define IDM_SHOW    1001
#define IDM_EXIT    1002

// 全局变量
static NOTIFYICONDATAW g_nid = {};
static HWND g_hiddenWindow = nullptr;
static Window* g_mainWindow = nullptr;
static Application* g_app = nullptr;

// 托盘图标辅助函数
void CreateTrayIcon(HWND hwnd, HICON icon) {
    g_nid.cbSize = sizeof(g_nid);
    g_nid.hWnd = hwnd;
    g_nid.uID = 1;
    g_nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
    g_nid.uCallbackMessage = WM_TRAYICON;
    g_nid.hIcon = icon;
    wcscpy_s(g_nid.szTip, L"FluentUI 托盘演示 - 右键打开菜单");
    
    Shell_NotifyIconW(NIM_ADD, &g_nid);
}

void RemoveTrayIcon() {
    Shell_NotifyIconW(NIM_DELETE, &g_nid);
}

void ShowTrayMenu(HWND hwnd) {
    HMENU hMenu = CreatePopupMenu();
    AppendMenuW(hMenu, MF_STRING, IDM_SHOW, L"显示窗口(&S)");
    AppendMenuW(hMenu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(hMenu, MF_STRING, IDM_EXIT, L"退出(&X)");
    
    POINT pt;
    GetCursorPos(&pt);
    
    // 托盘菜单需要 SetForegroundWindow 才能正确关闭
    SetForegroundWindow(hwnd);
    
    UINT cmd = TrackPopupMenu(hMenu, TPM_RETURNCMD | TPM_NONOTIFY,
                              pt.x, pt.y, 0, hwnd, nullptr);
    
    if (cmd == IDM_SHOW) {
        if (g_mainWindow && g_mainWindow->IsOpen()) {
            ShowWindow(g_mainWindow->Hwnd(), SW_SHOW);
            SetForegroundWindow(g_mainWindow->Hwnd());
        }
    } else if (cmd == IDM_EXIT) {
        if (g_app) {
            g_app->Shutdown(0);
        }
    }
    
    DestroyMenu(hMenu);
}

// 隐藏的消息窗口（接收托盘消息）
LRESULT CALLBACK HiddenWndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    if (msg == WM_TRAYICON) {
        if (lp == WM_RBUTTONUP) {
            ShowTrayMenu(hwnd);
        } else if (lp == WM_LBUTTONDBLCLK) {
            // 双击托盘图标 = 显示窗口
            if (g_mainWindow && g_mainWindow->IsOpen()) {
                ShowWindow(g_mainWindow->Hwnd(), SW_SHOW);
                SetForegroundWindow(g_mainWindow->Hwnd());
            }
        }
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

// 主窗口类
class TrayDemoWindow : public Window {
public:
    void OnInitialize() override {
        auto* root = new StackPanel();
        root->SetPadding(Thickness{20.0f});
        root->SetSpacing(16.0f);
        
        // 标题
        auto* title = root->Emplace<TextBlock>();
        title->SetText(L"FluentUI 托盘应用演示");
        title->SetFontSize(24.0f);
        title->SetWeight(static_cast<DWRITE_FONT_WEIGHT>(600));
        
        // 说明文字
        auto* info = root->Emplace<TextBlock>();
        info->SetText(
            L"这是一个托盘应用演示。\n\n"
            L"• 点击窗口的关闭按钮，窗口会最小化到托盘区（右下角）\n"
            L"• 双击托盘图标或右键选择\"显示窗口\"可以恢复窗口\n"
            L"• 右键托盘图标选择\"退出\"才真正关闭程序\n\n"
            L"试试看：点击右上角的 ✕ 按钮！"
        );
        info->SetWrap(true);
        info->SetLineSpacing(1.6f);
        
        // 按钮：隐藏到托盘
        auto* hideBtn = root->Emplace<Button>();
        hideBtn->SetText(L"手动隐藏到托盘");
        subs_.push_back(hideBtn->Click().Subscribe(this, 
            [](void* owner, Button&, RoutedEventArgs&) {
                auto* self = static_cast<TrayDemoWindow*>(owner);
                ShowWindow(self->Hwnd(), SW_HIDE);
            }
        ));
        
        // 按钮：真正退出
        auto* exitBtn = root->Emplace<Button>();
        exitBtn->SetText(L"退出程序（不隐藏）");
        subs_.push_back(exitBtn->Click().Subscribe(this,
            [](void* owner, Button&, RoutedEventArgs&) {
                auto* self = static_cast<TrayDemoWindow*>(owner);
                if (g_app) {
                    g_app->Shutdown(0);
                }
            }
        ));
        
        SetRoot(std::unique_ptr<UIElement>(root));
    }
    
    void OnClosing(WindowClosingArgs& args) override {
        // 关键：拦截关闭事件，隐藏而非销毁
        args.cancel = true;
        ShowWindow(Hwnd(), SW_HIDE);
        
        // 可以弹个气泡提示（可选）
        g_nid.uFlags = NIF_INFO;
        wcscpy_s(g_nid.szInfo, L"程序已最小化到托盘");
        wcscpy_s(g_nid.szInfoTitle, L"FluentUI Demo");
        g_nid.dwInfoFlags = NIIF_INFO;
        Shell_NotifyIconW(NIM_MODIFY, &g_nid);
    }
    
private:
    std::vector<Subscription> subs_;
};

int APIENTRY wWinMain(
    _In_ HINSTANCE hInstance,
    _In_opt_ HINSTANCE hPrevInstance,
    _In_ LPWSTR lpCmdLine,
    _In_ int nCmdShow)
{
    UNREFERENCED_PARAMETER(hPrevInstance);
    UNREFERENCED_PARAMETER(lpCmdLine);
    UNREFERENCED_PARAMETER(nCmdShow);
    
    // 1. 创建隐藏的消息窗口（用于接收托盘消息）
    WNDCLASSEXW wcex = {};
    wcex.cbSize = sizeof(wcex);
    wcex.lpfnWndProc = HiddenWndProc;
    wcex.hInstance = hInstance;
    wcex.lpszClassName = L"TrayDemoHiddenWindow";
    RegisterClassExW(&wcex);
    
    g_hiddenWindow = CreateWindowExW(
        0, L"TrayDemoHiddenWindow", L"",
        0, 0, 0, 0, 0,
        HWND_MESSAGE, nullptr, hInstance, nullptr
    );
    
    if (!g_hiddenWindow) {
        return 1;
    }
    
    // 2. 创建托盘图标（使用应用程序图标）
    HICON hIcon = LoadIconW(nullptr, IDI_APPLICATION);
    CreateTrayIcon(g_hiddenWindow, hIcon);
    
    // 3. 创建 FluentUI Application
    Application app(hInstance);
    g_app = &app;
    
    // 关键：设置为显式退出模式（窗口关闭不退出程序）
    app.SetShutdownMode(ShutdownMode::OnExplicitShutdown);
    
    // 4. 创建并显示主窗口
    TrayDemoWindow mainWindow;
    g_mainWindow = &mainWindow;
    
    mainWindow.SetTitle(L"FluentUI 托盘应用演示");
    mainWindow.SetClientSize(500, 320);
    mainWindow.SetTitleBarHeight(24);
    HRESULT hr = mainWindow.Show(app);
    if (FAILED(hr)) {
        RemoveTrayIcon();
        return 1;
    }
    
    // 5. 运行消息循环（只有显式 Shutdown 才退出）
    if (!app.TryRun()) {
        RemoveTrayIcon();
        return app.ExitCode();
    }
    
    // 6. 清理托盘图标
    RemoveTrayIcon();
    
    return app.ExitCode();
}
