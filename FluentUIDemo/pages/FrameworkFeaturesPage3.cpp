// FrameworkFeaturesPage3.cpp — 框架功能演示页（800 组）第三部分
//
// 806 文件对话框、807 DPI 适配、808 动画、809 托盘图标
#include "../GalleryMain.h"
#include "../../FluentUI/controls/Button.h"
#include "../../FluentUI/controls/TextBlock.h"
#include "../../FluentUI/controls/TextArea.h"
#include "../../FluentUI/controls/Slider.h"
#include "../../FluentUI/layout/Border.h"
#include "../../FluentUI/layout/StackPanel.h"
#include "../../FluentUI/layout/Grid.h"
#include "../../FluentUI/layout/ScrollPanel.h"
#include "../../FluentUI/dialogs/FileDialog.h"
#include "../../FluentUI/animation/AnimatedValue.h"
#include "../../FluentUI/window/NativeWindowHost.h"
#include "../../FluentUI/window/Window.h"

namespace fluent {

// ===========================================================================
// 806 文件对话框（File Dialog）
// ===========================================================================

// 成员函数处理器：Event handler 是无捕获函数指针，this 通过 owner 参数传入。
void GalleryApp::ShowOpenFileDialog(Button&, RoutedEventArgs&) {
    NativeWindowHost* host = ownerWindow_ ? ownerWindow_() : nullptr;
    if (!host || !openFileResultText_) return;

    OpenFileDialog dlg;
    dlg.SetTitle(L"选择文件");
    dlg.AddFilter(L"所有文件", L"*.*");
    dlg.AddFilter(L"文本文件", L"*.txt;*.log;*.md");
    dlg.AddFilter(L"图片", L"*.png;*.jpg;*.bmp");

    auto path = dlg.ShowDialog(host->Hwnd());
    openFileResultText_->SetText(path.empty() ? L"已取消" : L"已选择：" + path);
}

void GalleryApp::ShowOpenMultiFileDialog(Button&, RoutedEventArgs&) {
    NativeWindowHost* host = ownerWindow_ ? ownerWindow_() : nullptr;
    if (!host || !openFileResultText_) return;

    OpenFileDialog dlg;
    dlg.SetTitle(L"多选文件");
    dlg.SetMultiSelect(true);
    dlg.AddFilter(L"所有文件", L"*.*");

    auto paths = dlg.ShowMulti(host->Hwnd());
    if (paths.empty()) {
        openFileResultText_->SetText(L"已取消");
    } else {
        std::wstring result = L"已选择 " + std::to_wstring(paths.size()) + L" 个文件：\n";
        for (auto& p : paths) result += p + L"\n";
        openFileResultText_->SetText(result);
    }
}

void GalleryApp::ShowSaveFileDialog(Button&, RoutedEventArgs&) {
    NativeWindowHost* host = ownerWindow_ ? ownerWindow_() : nullptr;
    if (!host || !saveFileResultText_) return;

    SaveFileDialog dlg;
    dlg.SetTitle(L"保存文件");
    dlg.SetFileName(L"document");
    dlg.SetDefaultExtension(L"txt");
    dlg.AddFilter(L"文本文件", L"*.txt");
    dlg.AddFilter(L"所有文件", L"*.*");

    auto path = dlg.ShowDialog(host->Hwnd());
    saveFileResultText_->SetText(path.empty() ? L"已取消" : L"将保存到：" + path);
}

std::unique_ptr<ScrollPanel> GalleryApp::CreateFileDialogFeaturePage() {
    auto [page, content] = CreatePageShell(L"文件对话框 · File Dialog");

    AddNote(content,
        L"OpenFileDialog 和 SaveFileDialog 是阻塞式模态对话框（IFileOpenDialog / IFileSaveDialog）。"
        L"ShowDialog(hwnd) 阻塞到用户关闭，返回选中的路径，取消时返回空字符串。");

    // 打开文件
    {
        auto* card = CreateExampleCard(content, L"打开文件");

        auto* row = AddRow(card, 8.0f);
        auto* btnOpen = row->Add(std::make_unique<Button>());
        btnOpen->SetText(L"打开文件……");
        btnOpen->SetTooltip(L"单选；支持文件类型过滤");

        auto* btnMulti = row->Add(std::make_unique<Button>());
        btnMulti->SetText(L"多选文件……");
        btnMulti->SetTooltip(L"SetMultiSelect(true)，返回所有路径");

        auto* result = card->Add(std::make_unique<TextBlock>());
        result->SetText(L"结果：（点按钮打开对话框）");
        result->SetMargin(Thickness{0, 10.0f, 0, 0});
        openFileResultText_ = result;

        pageSubs_ += btnOpen->Click().Subscribe<&GalleryApp::ShowOpenFileDialog>(this);
        pageSubs_ += btnMulti->Click().Subscribe<&GalleryApp::ShowOpenMultiFileDialog>(this);
    }

    // 保存文件
    {
        auto* card = CreateExampleCard(content, L"保存文件");

        auto* btn = card->Add(std::make_unique<Button>());
        btn->SetText(L"保存文件……");

        auto* result = card->Add(std::make_unique<TextBlock>());
        result->SetText(L"结果：（点按钮打开对话框）");
        result->SetMargin(Thickness{0, 10.0f, 0, 0});
        saveFileResultText_ = result;

        pageSubs_ += btn->Click().Subscribe<&GalleryApp::ShowSaveFileDialog>(this);
    }

    CreateCodeExample(content, LR"(// OpenFileDialog
OpenFileDialog dlg;
dlg.SetTitle(L"打开文件");
dlg.AddFilter(L"文本文件", L"*.txt");
dlg.AddFilter(L"所有文件", L"*.*");
auto path = dlg.ShowDialog(hwnd);   // 阻塞到关闭

// 多选
dlg.SetMultiSelect(true);
auto paths = dlg.ShowMulti(hwnd);   // 返回 vector<wstring>

// SaveFileDialog
SaveFileDialog save;
save.SetFileName(L"document");
save.SetDefaultExtension(L"txt");
auto savePath = save.ShowDialog(hwnd);)");

    return std::move(page);
}

// ===========================================================================
// 807 DPI 适配（DPI Awareness）
// ===========================================================================

void GalleryApp::RefreshDpiReadout(Button&, RoutedEventArgs&) {
    NativeWindowHost* host = ownerWindow_ ? ownerWindow_() : nullptr;
    if (!host || !dpiReadoutText_) return;

    UINT dpi = host->Dpi();
    float scale = dpi / 96.0f;
    // 用 to_wstring + 手动拼接，避免引入 sstream
    std::wstring text = L"当前 DPI：" + std::to_wstring(dpi)
        + L"\nDpiScale：" + std::to_wstring(static_cast<int>(scale * 100)) + L"%"
        + L"\n物理像素 = DIP × " + std::to_wstring(static_cast<int>(scale * 100)) + L"%";
    dpiReadoutText_->SetText(text);
}

std::unique_ptr<ScrollPanel> GalleryApp::CreateDpiFeaturePage() {
    auto [page, content] = CreatePageShell(L"DPI 适配 · DPI Awareness");

    AddNote(content,
        L"框架声明 Per-Monitor V2 DPI 感知（app.manifest）。"
        L"所有控件尺寸用 DIPs 表示（96 DPI = 1× 基准），框架在 Arrange 时乘以 DpiScale 转为物理像素。"
        L"拖动窗口到不同 DPI 的显示器时，框架自动响应 WM_DPICHANGED，不需要应用层处理。");

    // 当前 DPI 读数
    {
        auto* card = CreateExampleCard(content, L"当前窗口 DPI");
        AddNote(card, L"把窗口拖到另一个 DPI 的显示器后，点刷新查看 DPI 变化。");

        auto* btn = card->Add(std::make_unique<Button>());
        btn->SetText(L"刷新 DPI 读数");

        auto* readout = card->Add(std::make_unique<TextBlock>());
        readout->SetText(L"（点上方按钮读取）");
        readout->SetMargin(Thickness{0, 10.0f, 0, 0});
        readout->SetFontSize(16.0f);
        dpiReadoutText_ = readout;

        pageSubs_ += btn->Click().Subscribe<&GalleryApp::RefreshDpiReadout>(this);
    }

    // 同一 DIP 尺寸在不同 DPI 下的直观展示
    {
        auto* card = CreateExampleCard(content, L"DIP 尺寸对比");
        AddNote(card,
            L"下面三个方块都指定了相同的 DIP 尺寸。"
            L"在 100 DPI 显示器上，80 DIP ≈ 83 物理像素。"
            L"在 200 DPI 显示器上，80 DIP = 167 物理像素。"
            L"但在 Demo 里看起来「一样大」——这才是 DIP 的意义。");

        auto* row = AddRow(card, 16.0f);
        const struct { float dip; D2D1_COLOR_F col; const wchar_t* label; } items[] = {
            {40.0f,  {0.0f, 0.47f, 0.90f, 1.0f}, L"40 DIP"},
            {80.0f,  {0.27f, 0.72f, 0.33f, 1.0f}, L"80 DIP"},
            {120.0f, {0.88f, 0.20f, 0.20f, 1.0f}, L"120 DIP"},
        };
        for (const auto& item : items) {
            auto* col = row->Add(std::make_unique<StackPanel>());
            col->SetSpacing(4.0f);
            auto* b = col->Add(std::make_unique<Border>());
            b->SetWidth(item.dip);
            b->SetHeight(item.dip);
            b->SetCornerRadius(4.0f);
            b->SetBackground(item.col);
            auto* lbl = col->Add(std::make_unique<TextBlock>());
            lbl->SetText(item.label);
            lbl->SetFontSize(12.0f);
        }
    }

    CreateCodeExample(content, LR"(// DPI 读取
UINT dpi = host->Dpi();              // 当前窗口 DPI（如 96 / 144 / 192）
float scale = host->DpiScale();      // dpi / 96.0f

// 控件用 DIPs 指定尺寸——框架自动转换
border->SetWidth(100.0f);  // 100 DIP = 100px @ 96DPI, 200px @ 192DPI

// DPI 变化通知（框架自动处理，无需手动实现）
// NativeWindowHost 收到 WM_DPICHANGED 后调用：
//   UpdateContextDpi(newDpiScale)  → 逐元素传递新 DPI
//   OnDpiChanged(newDpiScale)      → 虚钩子，控件可自定义
)");

    return std::move(page);
}

// ===========================================================================
// 808 动画（Animation）
// ===========================================================================

// 用一个显示 0-100 进度的 Border 宽度演示 AnimatedValue 缓动。
// 实际缓动要在自定义控件内完成；这里用 Slider 的 ValueChanged 直接设置宽度，
// 说明当不需要平滑动画时不必用 AnimatedValue（直接 SetWidth 就够），
// 再用代码示例解释控件内用法。
std::unique_ptr<ScrollPanel> GalleryApp::CreateAnimationFeaturePage() {
    auto [page, content] = CreatePageShell(L"动画 · Animation");

    AddNote(content,
        L"AnimatedValue 是 UI 线程的标量缓动工具，值通过指数 Approach 逐帧接近目标。"
        L"WantsAnimationTick() 返回 true 时框架持续触发 OnAnimationTick(dtSec)，到达目标后自动停止。"
        L"ICompositionBackend 提供在合成器线程运行的动画（Animation.h），可在 UI 线程繁忙时保持流畅。");

    // 说明两种动画类型
    {
        auto* card = CreateExampleCard(content, L"两种动画路径");
        auto* grid = card->Add(std::make_unique<Grid>());
        grid->SetColumns({GridLength::Star(), GridLength::Star()});
        grid->AddRow(GridLength::Auto());

        auto* colLeft = grid->Add(std::make_unique<StackPanel>());
        grid->SetCell(colLeft, 0, 0);
        colLeft->SetSpacing(6.0f);

        auto* h1 = colLeft->Add(std::make_unique<TextBlock>());
        h1->SetText(L"UI 线程缓动（AnimatedValue）");
        h1->SetFontSize(13.0f);
        auto* b1 = colLeft->Add(std::make_unique<TextBlock>());
        b1->SetText(
            L"• 在 OnAnimationTick 里调用 Approach\n"
            L"• 适合简单状态过渡（按钮 tint、"
            L"CheckBox 勾选、滑块填充）\n"
            L"• UI 线程繁忙时可能掉帧");

        auto* colRight = grid->Add(std::make_unique<StackPanel>());
        grid->SetCell(colRight, 0, 1);
        colRight->SetSpacing(6.0f);

        auto* h2 = colRight->Add(std::make_unique<TextBlock>());
        h2->SetText(L"合成器线程（DComp）");
        h2->SetFontSize(13.0f);
        auto* b2 = colRight->Add(std::make_unique<TextBlock>());
        b2->SetText(
            L"• 通过 ICompositionBackend 提交\n"
            L"• ProgressBar 不确定扫描就用这条路\n"
            L"• UI 线程阻塞也保持流畅");
    }

    // AnimatedValue 的典型用法（代码展示）
    {
        auto* card = CreateExampleCard(content, L"AnimatedValue 典型用法");

        auto* ta = card->Add(std::make_unique<TextArea>());
        ta->SetWidth(640.0f);
        ta->SetHeight(200.0f);
        ta->SetReadOnly(true);
        ta->SetText(LR"(class MyButton : public Control {
    AnimatedValue tint_{0.0f};  // 0 = rest, 1 = hover

    bool WantsAnimationTick() const override {
        float target = (State() == VisualState::Hover) ? 1.0f : 0.0f;
        return tint_.Animating(target, 0.01f);
    }
    void OnAnimationTick(float dtSec) override {
        float target = (State() == VisualState::Hover) ? 1.0f : 0.0f;
        tint_.Approach(target, dtSec, /*tau=*/0.12f);
        Invalidate();   // 触发重绘，下帧继续采样
    }
    void Render(const DrawingContext& dc) override {
        float t = tint_;          // 转 float
        D2D1_COLOR_F fill = Lerp(restColor, hoverColor, t);
        dc.FillRoundedRect(rr, fill);
    }
};)");
    }

    // 内置动画控件展示
    {
        auto* card = CreateExampleCard(content, L"已使用 AnimatedValue 的内置控件");
        AddNote(card,
            L"Button（hover tint）、CheckBox（勾选入场）、"
            L"RadioButton（圆点缩放）、ToggleSwitch（旋钮滑动）、"
            L"Slider（填充条）、ScrollPanel（滚动惯性）——"
            L"这些都在内部用 AnimatedValue，拖动/点击即可观察缓动效果。");
    }

    CreateCodeExample(content, LR"(// AnimatedValue 用法
AnimatedValue progress_{0.0f};

bool WantsAnimationTick() const override {
    return progress_.Animating(targetValue_, 0.01f);
}
void OnAnimationTick(float dtSec) override {
    progress_.Approach(targetValue_, dtSec, /*tau=*/0.20f);
    Invalidate();
}

// 读值（转 float）
void Render(const DrawingContext& dc) override {
    float p = progress_;       // operator float()
    // 按 p 绘制进度条
}

// 时间常数从主题读，不要硬编码（MotionTokens）
// Theme().motion.tintTau   Button 的 hover tint
// Theme().motion.fadeTau   CheckBox 的勾选淡入)");

    return std::move(page);
}

// ===========================================================================
// 809 托盘图标（Tray Icon）
// ===========================================================================

void GalleryApp::AddDemoTrayIcon(Button&, RoutedEventArgs&) {
    NativeWindowHost* host = ownerWindow_ ? ownerWindow_() : nullptr;
    auto* win = host ? dynamic_cast<Window*>(host) : nullptr;
    if (!win || !trayStatusText_) return;

    if (win->HasTrayIcon()) {
        trayStatusText_->SetText(L"状态：托盘图标已存在（先移除再添加）");
        return;
    }
    HRESULT hr = win->SetTrayIcon(LoadIconW(nullptr, IDI_APPLICATION), L"FluentUI Gallery");
    if (SUCCEEDED(hr)) {
        trayStatusText_->SetText(L"状态：托盘图标已添加（查看任务栏右侧通知区域）");
    } else {
        trayStatusText_->SetText(L"状态：添加失败，HRESULT = " + std::to_wstring(hr));
    }
}

void GalleryApp::ShowDemoTrayBalloon(Button&, RoutedEventArgs&) {
    NativeWindowHost* host = ownerWindow_ ? ownerWindow_() : nullptr;
    auto* win = host ? dynamic_cast<Window*>(host) : nullptr;
    if (!win || !trayStatusText_) return;

    if (!win->HasTrayIcon()) {
        trayStatusText_->SetText(L"状态：请先添加托盘图标");
        return;
    }
    HRESULT hr = win->ShowTrayBalloon(L"FluentUI Gallery", L"这是一条托盘通知气泡");
    trayStatusText_->SetText(SUCCEEDED(hr) ? L"状态：通知气泡已弹出" : L"状态：弹出失败");
}

void GalleryApp::RemoveDemoTrayIcon(Button&, RoutedEventArgs&) {
    NativeWindowHost* host = ownerWindow_ ? ownerWindow_() : nullptr;
    auto* win = host ? dynamic_cast<Window*>(host) : nullptr;
    if (!win || !trayStatusText_) return;

    win->RemoveTrayIcon();
    trayStatusText_->SetText(L"状态：托盘图标已移除");
}

std::unique_ptr<ScrollPanel> GalleryApp::CreateTrayFeaturePage() {
    auto [page, content] = CreatePageShell(L"托盘图标 · Tray Icon");

    AddNote(content,
        L"Window 提供托盘图标支持，绑定在窗口自己的 HWND 上（无需额外隐藏窗口）。"
        L"SetTrayIcon 必须在窗口 HWND 存在后调用（OnContentCreated / OnLoaded 之后），"
        L"不能在 OnInitialize 里调用（那时 HWND 还未创建）。");

    // 实际演示
    {
        auto* card = CreateExampleCard(content, L"实际演示");
        AddNote(card, L"GalleryWindow 是普通 Window，托盘 API 直接在它身上。");

        auto* row = AddRow(card, 8.0f);

        auto* btnAdd = row->Add(std::make_unique<Button>());
        btnAdd->SetText(L"添加托盘图标");
        btnAdd->SetTooltip(L"向通知区域添加一个 Gallery 图标");

        auto* btnBalloon = row->Add(std::make_unique<Button>());
        btnBalloon->SetText(L"弹出通知气泡");
        btnBalloon->SetTooltip(L"ShowTrayBalloon：弹出一条系统通知");

        auto* btnRemove = row->Add(std::make_unique<Button>());
        btnRemove->SetText(L"移除托盘图标");
        btnRemove->SetTooltip(L"RemoveTrayIcon");

        auto* status = card->Add(std::make_unique<TextBlock>());
        status->SetText(L"状态：未添加");
        status->SetMargin(Thickness{0, 10.0f, 0, 0});
        trayStatusText_ = status;

        pageSubs_ += btnAdd->Click().Subscribe<&GalleryApp::AddDemoTrayIcon>(this);
        pageSubs_ += btnBalloon->Click().Subscribe<&GalleryApp::ShowDemoTrayBalloon>(this);
        pageSubs_ += btnRemove->Click().Subscribe<&GalleryApp::RemoveDemoTrayIcon>(this);
    }

    // 托盘应用的完整模式
    {
        auto* card = CreateExampleCard(content, L"托盘应用的典型写法");
        AddNote(card,
            L"托盘应用需要：\n"
            L"• app.SetShutdownMode(ShutdownMode::OnExplicitShutdown)——关闭窗口不退出进程\n"
            L"• OnClosing 拦截关闭事件，改为 HideToTray\n"
            L"• OnTrayIconDoubleClick 还原窗口\n"
            L"• OnTrayIconRightClick 弹出退出菜单\n"
            L"完整示例见 demos/TrayAppDemo。");
    }

    CreateCodeExample(content, LR"(// 托盘应用（继承 Window）
class MyWindow : public Window {
    void OnContentCreated() override {
        // HWND 已存在，可以添加托盘图标
        SetTrayIcon(LoadIconW(nullptr, IDI_APPLICATION), L"My App");
    }
    void OnClosing(WindowClosingArgs& e) override {
        e.cancel = true;  // 拦截关闭
        HideToTray();   // 隐藏到托盘
        ShowTrayBalloon(L"My App", L"程序在后台运行中");
    }
    void OnTrayIconDoubleClick(TrayIconEventArgs&) override {
        RestoreFromTray();  // 双击还原
    }
};

// main
Application app;
app.SetShutdownMode(ShutdownMode::OnExplicitShutdown);
MyWindow win;
return app.Run(win);)");

    return std::move(page);
}

} // namespace fluent
