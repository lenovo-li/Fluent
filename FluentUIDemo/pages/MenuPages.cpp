// MenuPages.cpp — 菜单和窗口控件页面
#include "../GalleryMain.h"
#include "../../FluentUI/controls/MenuBar.h"
#include "../../FluentUI/controls/MenuFlyout.h"
#include "../../FluentUI/controls/StatusBar.h"
#include "../../FluentUI/controls/ToolBar.h"
#include "../../FluentUI/controls/ContentDialog.h"
#include "../../FluentUI/controls/MessageDialog.h"
#include "../../FluentUI/controls/GridSplitter.h"
#include "../../FluentUI/controls/TextBlock.h"
#include "../../FluentUI/controls/TextBox.h"
#include "../../FluentUI/controls/Button.h"
#include "../../FluentUI/controls/CheckBox.h"
#include "../../FluentUI/controls/ComboBox.h"
#include "../../FluentUI/controls/ProgressBar.h"
#include "../../FluentUI/layout/StackPanel.h"
#include "../../FluentUI/layout/ScrollPanel.h"
#include "../../FluentUI/layout/Grid.h"
#include "../../FluentUI/layout/Border.h"
#include "../../FluentUI/window/NativeWindowHost.h"

namespace fluent {

std::unique_ptr<ScrollPanel> GalleryApp::CreateMenuBarPage() {
    auto [page, content] = CreatePageShell(L"MenuBar");
    auto* card = CreateExampleCard(content, L"Desktop menu navigation");
    auto* menu = card->Add(std::make_unique<MenuBar>());
    menu->SetWidth(520.0f);
    {
        std::vector<MenuItem> fileItems;
        MenuItem m1; m1.text = L"New"; fileItems.push_back(std::move(m1));
        MenuItem m2; m2.text = L"Open"; fileItems.push_back(std::move(m2));
        fileItems.push_back(MenuItem::Sep());
        MenuItem m3; m3.text = L"Exit"; fileItems.push_back(std::move(m3));
        menu->AddMenu(L"File", std::move(fileItems));
    }
    {
        std::vector<MenuItem> editItems;
        MenuItem m1; m1.text = L"Undo"; editItems.push_back(std::move(m1));
        MenuItem m2; m2.text = L"Redo"; editItems.push_back(std::move(m2));
        editItems.push_back(MenuItem::Sep());
        MenuItem m3; m3.text = L"Copy"; editItems.push_back(std::move(m3));
        MenuItem m4; m4.text = L"Paste"; editItems.push_back(std::move(m4));
        menu->AddMenu(L"Edit", std::move(editItems));
    }
    {
        std::vector<MenuItem> helpItems;
        MenuItem about; about.text = L"About FluentUI"; helpItems.push_back(std::move(about));
        menu->AddMenu(L"Help", std::move(helpItems));
    }
    // MenuItem owns an Event<> and is therefore non-copyable: menus are built by
    // move, not with an initializer_list (which always copies its elements).
    CreateCodeExample(content, LR"(std::vector<MenuItem> items;

MenuItem newFile;
newFile.text = L"New";
newFile.AddInvokeHandler([]{ OnNew(); });
items.push_back(std::move(newFile));

items.push_back(MenuItem::Sep());

menu->AddMenu(L"File", std::move(items));)");
    return std::move(page);
}

std::unique_ptr<ScrollPanel> GalleryApp::CreateMenuFlyoutPage() {
    auto [page, content] = CreatePageShell(L"MenuFlyout");
    auto* card = CreateExampleCard(content, L"Context menus and submenus");
    auto* btn = card->Add(std::make_unique<Button>());
    btn->SetText(L"Show menu");
    auto flyout = std::make_shared<MenuFlyout>();
    {
        std::vector<MenuItem> items;
        MenuItem m1; m1.text = L"New file"; items.push_back(std::move(m1));
        MenuItem m2; m2.text = L"Open..."; items.push_back(std::move(m2));
        items.push_back(MenuItem::Sep());
        MenuItem m3; m3.text = L"Exit"; items.push_back(std::move(m3));
        flyout->SetItems(std::move(items));
    }
    btn->SetFlyout(flyout.get());
    flyouts_.push_back(flyout);
    auto* hint = card->Add(std::make_unique<TextBlock>());
    hint->SetText(L"Click the button or right-click to show the context menu. MenuFlyout supports submenus and keyboard navigation.");
    hint->SetWrap(true); hint->SetFontSize(12.0f);
    CreateCodeExample(content, LR"(auto flyout = std::make_shared<MenuFlyout>();
std::vector<MenuItem> items;

MenuItem newItem;
newItem.text = L"New";
newItem.AddInvokeHandler([]{ OnNew(); });
items.push_back(std::move(newItem));

MenuItem openItem;
openItem.text = L"Open";
openItem.AddInvokeHandler([]{ OnOpen(); });
items.push_back(std::move(openItem));

items.push_back(MenuItem::Sep());

MenuItem exitItem;
exitItem.text = L"Exit";
exitItem.AddInvokeHandler([]{ OnExit(); });
items.push_back(std::move(exitItem));

flyout->SetItems(std::move(items));
button->SetFlyout(flyout.get());)");
    return std::move(page);
}

std::unique_ptr<ScrollPanel> GalleryApp::CreateStatusBarPage() {
    auto [page, content] = CreatePageShell(L"StatusBar");

    // 示例 1：基本状态栏
    auto* card1 = CreateExampleCard(content, L"底部状态条");
    auto* desc1 = card1->Add(std::make_unique<TextBlock>());
    desc1->SetText(L"StatusBar 是 32 DIP 高的底部条，显示左对齐文本 + 可选的右对齐内容（进度条、按钮等）。");
    desc1->SetWrap(true);

    // 演示容器
    auto* demoContainer = card1->Add(std::make_unique<Border>());
    demoContainer->SetWidth(520.0f);
    demoContainer->SetHeight(300.0f);
    demoContainer->SetBorderThickness(1.0f);
    demoContainer->SetCornerRadius(6.0f);
    demoContainer->SetMargin(Thickness{0, 8.0f, 0, 0});

    auto* demoGrid = demoContainer->SetChild(std::make_unique<Grid>());
    demoGrid->SetColumns({GridLength::Star()});
    demoGrid->SetRows({GridLength::Star(), GridLength::Pixels(32.0f)});

    // 主内容区
    auto* contentArea = demoGrid->Add(std::make_unique<Border>());
    demoGrid->SetCell(contentArea, 0, 0);
    auto* contentText = contentArea->SetChild(std::make_unique<TextBlock>());
    contentText->SetText(L"主内容区域");
    contentText->SetVAlign(VAlign::Center);
    contentText->SetHAlign(HAlign::Center);

    // StatusBar
    auto* statusBar = demoGrid->Add(std::make_unique<StatusBar>());
    demoGrid->SetCell(statusBar, 1, 0);
    statusBar->SetText(L"Ready");

    // 右侧进度条
    auto rightProgress = std::make_unique<ProgressBar>();
    rightProgress->SetWidth(150.0f);
    rightProgress->SetValue(0.35f);
    statusBar->SetRightContent(std::move(rightProgress));

    auto* hint1 = card1->Add(std::make_unique<TextBlock>());
    hint1->SetText(L"💡 StatusBar 通常放在 DockPanel 或 Grid 的底部行。");
    hint1->SetWrap(true);
    hint1->SetFontSize(12.0f);
    hint1->SetMargin(Thickness{0, 8.0f, 0, 0});

    // 示例 2：动态更新
    auto* card2 = CreateExampleCard(content, L"动态更新状态");
    auto* btnRow = card2->Add(std::make_unique<StackPanel>());
    btnRow->SetOrientation(StackPanel::Orientation::Horizontal);
    btnRow->SetSpacing(8.0f);

    auto* btnReady = btnRow->Add(std::make_unique<Button>());
    btnReady->SetText(L"Ready");
    auto* btnLoading = btnRow->Add(std::make_unique<Button>());
    btnLoading->SetText(L"Loading...");
    auto* btnError = btnRow->Add(std::make_unique<Button>());
    btnError->SetText(L"Error");

    pageSubs_ += btnReady->Click().Subscribe(statusBar, [](void* owner, Button&, RoutedEventArgs&) {
        static_cast<StatusBar*>(owner)->SetText(L"Ready");
    });
    pageSubs_ += btnLoading->Click().Subscribe(statusBar, [](void* owner, Button&, RoutedEventArgs&) {
        static_cast<StatusBar*>(owner)->SetText(L"Loading data...");
    });
    pageSubs_ += btnError->Click().Subscribe(statusBar, [](void* owner, Button&, RoutedEventArgs&) {
        static_cast<StatusBar*>(owner)->SetText(L"⚠️ Connection error");
    });

    CreateCodeExample(content, LR"(// StatusBar 用法
auto* grid = panel->Add(std::make_unique<Grid>());
grid->SetRows({GridLength::Star(), GridLength::Pixels(32)});

// 主内容区
auto* content = grid->Add(std::make_unique<ScrollPanel>());
Grid::SetCell(content, 0, 0);

// 底部状态栏
auto* statusBar = grid->Add(std::make_unique<StatusBar>());
Grid::SetCell(statusBar, 1, 0);
statusBar->SetText(L"Ready");

// 右侧内容（可选）
auto progress = std::make_unique<ProgressBar>();
progress->SetWidth(150.0f);
statusBar->SetRightContent(std::move(progress));)");
    return std::move(page);
}

std::unique_ptr<ScrollPanel> GalleryApp::CreateToolBarPage() {
    auto [page, content] = CreatePageShell(L"ToolBar");

    // 示例 1：基本工具栏
    auto* card1 = CreateExampleCard(content, L"命令按钮条");
    auto* desc1 = card1->Add(std::make_unique<TextBlock>());
    desc1->SetText(L"ToolBar 横向排列命令按钮，空间不足时自动将溢出按钮收入右侧 ⋯ 菜单。");
    desc1->SetWrap(true);

    auto* toolbar = card1->Add(std::make_unique<ToolBar>());
    toolbar->SetWidth(520.0f);

    // 添加命令按钮
    auto btnNew = std::make_unique<Button>();
    btnNew->SetText(L"New");
    toolbar->AddButton(std::move(btnNew), []() {});

    auto btnOpen = std::make_unique<Button>();
    btnOpen->SetText(L"Open");
    toolbar->AddButton(std::move(btnOpen), []() {});

    auto btnSave = std::make_unique<Button>();
    btnSave->SetText(L"Save");
    toolbar->AddButton(std::move(btnSave), []() {});

    toolbar->AddSeparator();

    auto btnCut = std::make_unique<Button>();
    btnCut->SetText(L"Cut");
    toolbar->AddButton(std::move(btnCut), []() {});

    auto btnCopy = std::make_unique<Button>();
    btnCopy->SetText(L"Copy");
    toolbar->AddButton(std::move(btnCopy), []() {});

    auto btnPaste = std::make_unique<Button>();
    btnPaste->SetText(L"Paste");
    toolbar->AddButton(std::move(btnPaste), []() {});

    toolbar->AddSeparator();

    auto btnUndo = std::make_unique<Button>();
    btnUndo->SetText(L"Undo");
    toolbar->AddButton(std::move(btnUndo), []() {});

    auto btnRedo = std::make_unique<Button>();
    btnRedo->SetText(L"Redo");
    toolbar->AddButton(std::move(btnRedo), []() {});

    auto* hint1 = card1->Add(std::make_unique<TextBlock>());
    hint1->SetText(L"💡 调整窗口宽度，当空间不足时按钮会自动收入右侧 ⋯ 溢出菜单。");
    hint1->SetWrap(true);
    hint1->SetFontSize(12.0f);
    hint1->SetMargin(Thickness{0, 8.0f, 0, 0});

    // 示例 2：溢出行为演示
    auto* card2 = CreateExampleCard(content, L"溢出测试");
    auto* desc2 = card2->Add(std::make_unique<TextBlock>());
    desc2->SetText(L"下方的 ToolBar 宽度固定为 300 DIP，故意让部分按钮溢出到菜单中。");
    desc2->SetWrap(true);

    auto* narrowToolbar = card2->Add(std::make_unique<ToolBar>());
    narrowToolbar->SetWidth(300.0f);
    narrowToolbar->SetMargin(Thickness{0, 8.0f, 0, 0});

    for (int i = 1; i <= 8; ++i) {
        auto btn = std::make_unique<Button>();
        btn->SetText(L"Cmd " + std::to_wstring(i));
        narrowToolbar->AddButton(std::move(btn), []() {});
    }

    auto* hint2 = card2->Add(std::make_unique<TextBlock>());
    hint2->SetText(L"点击右侧 ⋯ 按钮查看溢出菜单。溢出按钮的点击事件与直接点击按钮效果相同。");
    hint2->SetWrap(true);
    hint2->SetFontSize(12.0f);
    hint2->SetMargin(Thickness{0, 8.0f, 0, 0});

    CreateCodeExample(content, LR"(auto* toolbar = panel->Add(std::make_unique<ToolBar>());

auto btnSave = std::make_unique<Button>();
btnSave->SetText(L"Save");
toolbar->AddButton(std::move(btnSave), OnSave);

toolbar->AddSeparator();

auto btnUndo = std::make_unique<Button>();
btnUndo->SetText(L"Undo");
toolbar->AddButton(std::move(btnUndo), OnUndo);

// 溢出按钮自动出现在右侧
// 点击溢出菜单项 = 点击对应按钮)");
    return std::move(page);
}

std::unique_ptr<ScrollPanel> GalleryApp::CreateContentDialogPage() {
    auto [page, content] = CreatePageShell(L"ContentDialog");

    auto* intro = content->Add(std::make_unique<TextBlock>());
    intro->SetText(
        L"ContentDialog 是 DialogWindow 的固定模板：标题 + 一个内容槽 + 右对齐按钮行。"
        L"内容槽接受任意 FrameworkElement，所以里面可以放整棵控件树。");
    intro->SetWrap(true);
    intro->SetMargin(Thickness{0, 0, 0, 16.0f});

    // 示例 1：模态 vs 非模态 —— 真实对话框
    auto* card1 = CreateExampleCard(content, L"模态 vs 非模态（真实对话框）");
    auto* desc1 = card1->Add(std::make_unique<TextBlock>());
    desc1->SetText(
        L"同一个 ContentDialog 类，两种打开方式：ShowDialog(owner) 禁用主窗口并阻塞调用方直到关闭；"
        L"Show(owner) 立刻返回，主窗口仍可交互——可以一边开着对话框一边继续点左侧导航。");
    desc1->SetWrap(true);

    auto* dlgRow = card1->Add(std::make_unique<StackPanel>());
    dlgRow->SetOrientation(StackPanel::Orientation::Horizontal);
    dlgRow->SetSpacing(12.0f);
    dlgRow->SetMargin(Thickness{0, 8.0f, 0, 0});

    auto* btnModal = dlgRow->Add(std::make_unique<Button>());
    btnModal->SetText(L"打开模态对话框");
    btnModal->SetKind(Button::Kind::Accent);
    btnModal->SetTooltip(L"ShowDialog：禁用主窗口，阻塞到关闭");

    auto* btnModeless = dlgRow->Add(std::make_unique<Button>());
    btnModeless->SetText(L"打开非模态对话框");
    btnModeless->SetTooltip(L"Show：立刻返回，主窗口仍可操作");

    auto* dlgResult = card1->Add(std::make_unique<TextBlock>());
    dlgResult->SetText(L"结果：（尚未打开）");
    dlgResult->SetFontSize(13.0f);
    dlgResult->SetMargin(Thickness{0, 8.0f, 0, 0});
    dialogResultText_ = dlgResult;

    pageSubs_ += btnModal->Click().Subscribe(
        this, [](void* owner, Button&, RoutedEventArgs&) {
            static_cast<GalleryApp*>(owner)->ShowModalContentDialog();
        });
    pageSubs_ += btnModeless->Click().Subscribe(
        this, [](void* owner, Button&, RoutedEventArgs&) {
            static_cast<GalleryApp*>(owner)->ShowModelessContentDialog();
        });

    // 示例 2：内容槽能放什么
    auto* card2 = CreateExampleCard(content, L"内容槽接受任意元素");
    auto* desc2 = card2->Add(std::make_unique<TextBlock>());
    desc2->SetText(
        L"上面两个对话框的内容不是纯文本，而是一棵真实的控件树（TextBlock + TextBox + "
        L"CheckBox + ComboBox）。它们和主窗口里的控件走同一套 Measure/Arrange、"
        L"输入路由和主题令牌——对话框只是换了个宿主 HWND。");
    desc2->SetWrap(true);

    auto* noteList = card2->Add(std::make_unique<StackPanel>());
    noteList->SetSpacing(6.0f);
    noteList->SetMargin(Thickness{0, 8.0f, 0, 0});

    auto* note1 = noteList->Add(std::make_unique<TextBlock>());
    note1->SetText(L"• 对话框继承 owner 的完整主题配置（切主题后再打开即可看到）");
    note1->SetWrap(true);
    note1->SetFontSize(13.0f);

    auto* note2 = noteList->Add(std::make_unique<TextBlock>());
    note2->SetText(L"• 模态期间 Application 的泵仍在跑，所以主窗口背景继续正常重绘");
    note2->SetWrap(true);
    note2->SetFontSize(13.0f);

    auto* note3 = noteList->Add(std::make_unique<TextBlock>());
    note3->SetText(L"• 非模态对话框必须活得比调用点长，所以实例挂在 GalleryApp 上而不是栈上");
    note3->SetWrap(true);
    note3->SetFontSize(13.0f);

    // 示例 3：边界 —— 什么时候不该用 ContentDialog
    auto* card3 = CreateExampleCard(content, L"什么时候不用它");
    auto* desc3 = card3->Add(std::make_unique<TextBlock>());
    desc3->SetText(
        L"ContentDialog 的价值就是那个固定形状。一旦需要它描述不了的布局（多栏、"
        L"自定义标题区、非标准按钮排布），直接继承 DialogWindow 在 OnInitialize 里"
        L"搭树——那条路没有上限。MessageDialog 则是反方向的极简封装。");
    desc3->SetWrap(true);

    CreateCodeExample(content, LR"(// 模态：ShowDialog 阻塞到关闭，栈变量即可
ContentDialog dialog;
dialog.SetTitle(L"设置");
dialog.SetClientSize(420.0f, 300.0f);

auto body = std::make_unique<StackPanel>();
body->SetSpacing(12.0f);
auto* box = body->Add(std::make_unique<TextBox>());
box->SetPlaceholder(L"显示名称");
dialog.SetContent(std::move(body));

dialog.AddButton(L"保存", DialogResult::Primary);
dialog.AddButton(L"取消", DialogResult::Cancel);

DialogResult r = dialog.ShowDialog(ownerWindow);
if (r == DialogResult::Primary) Save();

// 非模态：Show 立刻返回，实例必须活得更久
modelessDialog_ = std::make_unique<ContentDialog>();
modelessDialog_->SetTitle(L"非模态");
modelessDialog_->AddButton(L"关闭", DialogResult::Cancel);
modelessDialog_->Show(ownerWindow);   // 主窗口仍可交互)");
    return std::move(page);
}

// ---------------------------------------------------------------------------
// 对话框演示的实现
//
// 放在页面工厂外面而不是内联进 Click 回调里，原因是 Event 的 handler 是无捕获
// 函数指针（见 base/Event.h）：回调只能拿到一个 void* owner，所以真正的逻辑必须
// 是 GalleryApp 的成员函数，由回调转发过来。
// ---------------------------------------------------------------------------

namespace {

// 两个对话框共用的内容树：一棵普通的控件树，证明内容槽不是只能放文本。
std::unique_ptr<FrameworkElement> BuildDialogBody(const wchar_t* headline) {
    auto body = std::make_unique<StackPanel>();
    body->SetOrientation(StackPanel::Orientation::Vertical);
    body->SetSpacing(12.0f);

    auto* head = body->Add(std::make_unique<TextBlock>());
    head->SetText(headline);
    head->SetWrap(true);

    auto* name = body->Add(std::make_unique<TextBox>());
    name->SetPlaceholder(L"显示名称");
    name->SetWidth(320.0f);

    auto* combo = body->Add(std::make_unique<ComboBox>());
    combo->SetWidth(320.0f);
    combo->SetItems({L"跟随系统", L"始终浅色", L"始终深色"});
    combo->SetSelectedIndex(0);

    auto* check = body->Add(std::make_unique<CheckBox>());
    check->SetText(L"启动时恢复上次窗口位置");
    check->SetChecked(true);

    return body;
}

const wchar_t* ResultName(DialogResult r) {
    switch (r) {
        case DialogResult::Primary:   return L"Primary（保存）";
        case DialogResult::Secondary: return L"Secondary";
        case DialogResult::Cancel:    return L"Cancel（取消）";
        case DialogResult::Custom:    return L"Custom";
        case DialogResult::None:      break;
    }
    return L"None（未打开或被拒绝）";
}

} // namespace

void GalleryApp::ShowModalContentDialog() {
    NativeWindowHost* owner = ownerWindow_ ? ownerWindow_() : nullptr;
    if (!owner) {
        if (dialogResultText_) dialogResultText_->SetText(L"结果：宿主窗口不可用");
        return;
    }

    // 模态对话框可以是栈变量：ShowDialog 直到窗口关闭才返回，所以对象在整个
    // 可见期间都活着。非模态的那条路径不能这样写 —— 见下面的 modelessDialog_。
    ContentDialog dialog;
    dialog.SetTitle(L"模态对话框 — 设置");
    dialog.SetClientSize(420.0f, 330.0f);
    dialog.SetContent(BuildDialogBody(
        L"主窗口在对话框关闭前是禁用的（点它没有反应，标题栏也不响应）。"
        L"但它的背景仍在正常重绘 —— 模态泵服务的是整个 Application。"));
    dialog.AddButton(L"保存", DialogResult::Primary);
    dialog.AddButton(L"取消", DialogResult::Cancel);
    dialog.SetDefaultResult(DialogResult::Cancel);

    const DialogResult r = dialog.ShowDialog(*owner);
    if (dialogResultText_)
        dialogResultText_->SetText(std::wstring(L"结果：模态返回 ") + ResultName(r));
}

void GalleryApp::ShowModelessContentDialog() {
    NativeWindowHost* owner = ownerWindow_ ? ownerWindow_() : nullptr;
    if (!owner) {
        if (dialogResultText_) dialogResultText_->SetText(L"结果：宿主窗口不可用");
        return;
    }

    // 非模态：Show() 立刻返回。栈上的对话框会在返回时析构、窗口随即消失，所以
    // 实例必须挂在成员上。reset 掉旧的那个，保证同时最多一个。
    modelessDialog_ = std::make_unique<ContentDialog>();
    modelessDialog_->SetTitle(L"非模态对话框");
    modelessDialog_->SetClientSize(420.0f, 320.0f);
    modelessDialog_->SetContent(BuildDialogBody(
        L"主窗口没有被禁用：现在就可以点左侧导航切页、拖窗口、切主题，"
        L"这个对话框会一直开着，并跟随主题变化。"));
    modelessDialog_->AddButton(L"关闭", DialogResult::Cancel);
    modelessDialog_->SetDefaultResult(DialogResult::Cancel);

    // 结果通过事件回来，而不是 Show() 的返回值 —— 非模态调用在用户点按钮之前
    // 早就返回了。
    pageSubs_ += modelessDialog_->DialogClosed().Subscribe(
        this, [](void* owner, DialogWindow&, DialogClosedArgs& args) {
            auto* app = static_cast<GalleryApp*>(owner);
            if (app->dialogResultText_)
                app->dialogResultText_->SetText(
                    std::wstring(L"结果：非模态关闭 ") + ResultName(args.result));
        });

    if (FAILED(modelessDialog_->Show(*owner))) {
        modelessDialog_.reset();
        if (dialogResultText_) dialogResultText_->SetText(L"结果：非模态打开失败");
        return;
    }
    if (dialogResultText_)
        dialogResultText_->SetText(L"结果：非模态已打开（主窗口仍可交互）");
}

std::unique_ptr<ScrollPanel> GalleryApp::CreateMessageDialogPage() {
    auto [page, content] = CreatePageShell(L"MessageDialog");

    auto* intro = content->Add(std::make_unique<TextBlock>());
    intro->SetText(
        L"MessageDialog 封装 ContentDialog，提供类型化的 DialogResult 和预设按钮集合。"
        L"三个按钮组：Ok（单按钮确认）、OkCancel（确认操作）、YesNo（二选一决策）。");
    intro->SetWrap(true);
    intro->SetMargin(Thickness{0, 0, 0, 16.0f});

    // 示例 1：三种按钮集合的真实演示
    auto* card1 = CreateExampleCard(content, L"三种按钮集合（真实对话框）");
    auto* desc1 = card1->Add(std::make_unique<TextBlock>());
    desc1->SetText(
        L"MessageDialog::Show 是静态方法，一行调用即可弹出模态对话框并拿到结果。"
        L"下面三个按钮打开真实的系统级对话框——点击后主窗口被禁用，选择按钮后立刻返回结果。");
    desc1->SetWrap(true);

    auto* btnRow = card1->Add(std::make_unique<StackPanel>());
    btnRow->SetOrientation(StackPanel::Orientation::Horizontal);
    btnRow->SetSpacing(12.0f);
    btnRow->SetMargin(Thickness{0, 8.0f, 0, 0});

    auto* btnOk = btnRow->Add(std::make_unique<Button>());
    btnOk->SetText(L"Ok 按钮集合");
    btnOk->SetTooltip(L"单按钮确认，返回 Primary");

    auto* btnOkCancel = btnRow->Add(std::make_unique<Button>());
    btnOkCancel->SetText(L"OkCancel 按钮集合");
    btnOkCancel->SetTooltip(L"Ok → Primary, Cancel → Cancel");

    auto* btnYesNo = btnRow->Add(std::make_unique<Button>());
    btnYesNo->SetText(L"YesNo 按钮集合");
    btnYesNo->SetTooltip(L"Yes → Primary, No → Cancel");

    auto* msgResult = card1->Add(std::make_unique<TextBlock>());
    msgResult->SetText(L"结果：（尚未打开）");
    msgResult->SetFontSize(13.0f);
    msgResult->SetMargin(Thickness{0, 8.0f, 0, 0});
    messageResultText_ = msgResult;

    pageSubs_ += btnOk->Click().Subscribe(
        this, [](void* owner, Button&, RoutedEventArgs&) {
            static_cast<GalleryApp*>(owner)->ShowMessageDialogOk();
        });
    pageSubs_ += btnOkCancel->Click().Subscribe(
        this, [](void* owner, Button&, RoutedEventArgs&) {
            static_cast<GalleryApp*>(owner)->ShowMessageDialogOkCancel();
        });
    pageSubs_ += btnYesNo->Click().Subscribe(
        this, [](void* owner, Button&, RoutedEventArgs&) {
            static_cast<GalleryApp*>(owner)->ShowMessageDialogYesNo();
        });

    // 示例 2：文本长度与自适应尺寸
    //
    // MessageDialog 不接受调用方设置尺寸，窗口高度完全由文本量决定。这一组按钮
    // 覆盖从几个字到超长多段落的跨度，用来检查三件事：短文本没有多余留白、长文本
    // 不把按钮挤出可视区、超长文本触发高度上限后由 ScrollPanel 接管滚动。
    auto* cardLen = CreateExampleCard(content, L"文本长度自适应");
    auto* lenDesc = cardLen->Add(std::make_unique<TextBlock>());
    lenDesc->SetText(
        L"对话框宽度固定 360 DIP，高度按文本实测高度增长，最高不超过工作区的 80%。"
        L"超过上限后消息区变为可滚动，按钮行始终留在可视区内。");
    lenDesc->SetWrap(true);

    auto* lenRow = cardLen->Add(std::make_unique<StackPanel>());
    lenRow->SetOrientation(StackPanel::Orientation::Horizontal);
    lenRow->SetSpacing(8.0f);
    lenRow->SetMargin(Thickness{0, 8.0f, 0, 0});

    auto* btnTiny = lenRow->Add(std::make_unique<Button>());
    btnTiny->SetText(L"极短（3 字）");
    btnTiny->SetTooltip(L"最小高度：标题栏 + 一行文字 + 按钮行");

    auto* btnOneLine = lenRow->Add(std::make_unique<Button>());
    btnOneLine->SetText(L"单行");
    btnOneLine->SetTooltip(L"恰好一行不换行");

    auto* btnParagraph = lenRow->Add(std::make_unique<Button>());
    btnParagraph->SetText(L"单段长文本");
    btnParagraph->SetTooltip(L"一段连续文字，靠自动换行撑高");

    auto* btnMultiPara = lenRow->Add(std::make_unique<Button>());
    btnMultiPara->SetText(L"多段落");
    btnMultiPara->SetTooltip(L"5 段，带空行分隔");

    auto* btnHuge = lenRow->Add(std::make_unique<Button>());
    btnHuge->SetText(L"超长（触发上限 + 滚动）");
    btnHuge->SetTooltip(L"20 段，高度被工作区上限截断，消息区可滚动");

    pageSubs_ += btnTiny->Click().Subscribe(
        this, [](void* owner, Button&, RoutedEventArgs&) {
            static_cast<GalleryApp*>(owner)->ShowMessageDialogLength(
                MessageLengthCase::Tiny);
        });
    pageSubs_ += btnOneLine->Click().Subscribe(
        this, [](void* owner, Button&, RoutedEventArgs&) {
            static_cast<GalleryApp*>(owner)->ShowMessageDialogLength(
                MessageLengthCase::OneLine);
        });
    pageSubs_ += btnParagraph->Click().Subscribe(
        this, [](void* owner, Button&, RoutedEventArgs&) {
            static_cast<GalleryApp*>(owner)->ShowMessageDialogLength(
                MessageLengthCase::Paragraph);
        });
    pageSubs_ += btnMultiPara->Click().Subscribe(
        this, [](void* owner, Button&, RoutedEventArgs&) {
            static_cast<GalleryApp*>(owner)->ShowMessageDialogLength(
                MessageLengthCase::MultiParagraph);
        });
    pageSubs_ += btnHuge->Click().Subscribe(
        this, [](void* owner, Button&, RoutedEventArgs&) {
            static_cast<GalleryApp*>(owner)->ShowMessageDialogLength(
                MessageLengthCase::Huge);
        });

    // 示例 3：典型用例
    auto* card2 = CreateExampleCard(content, L"典型用例");
    auto* useCaseList = card2->Add(std::make_unique<StackPanel>());
    useCaseList->SetSpacing(6.0f);

    auto* use1 = useCaseList->Add(std::make_unique<TextBlock>());
    use1->SetText(L"• Ok: 单按钮提示（\"操作已完成\"、\"文件未找到\" 等纯通知）");
    use1->SetWrap(true);
    use1->SetFontSize(13.0f);

    auto* use2 = useCaseList->Add(std::make_unique<TextBlock>());
    use2->SetText(L"• OkCancel: 确认破坏性操作（\"删除此文件？\" Ok=Primary 执行删除，Cancel 什么都不做）");
    use2->SetWrap(true);
    use2->SetFontSize(13.0f);

    auto* use3 = useCaseList->Add(std::make_unique<TextBlock>());
    use3->SetText(L"• YesNo: 二选一决策（\"保存更改？\" Yes=Primary 保存并关闭，No=Cancel 直接关闭）");
    use3->SetWrap(true);
    use3->SetFontSize(13.0f);

    // 示例 3：与 ContentDialog 对比
    auto* card3 = CreateExampleCard(content, L"什么时候用 MessageDialog");
    auto* desc3 = card3->Add(std::make_unique<TextBlock>());
    desc3->SetText(
        L"MessageDialog 的价值是 *简洁*：一行代码解决模态确认。一旦需要自定义控件"
        L"（输入框、下拉框、勾选项）或非标准按钮，直接用 ContentDialog——两者共享"
        L"同一个底层 DialogWindow，MessageDialog 只是最简包装。");
    desc3->SetWrap(true);

    CreateCodeExample(content, LR"(// MessageDialog::Show 是静态方法，一行调用
auto result = MessageDialog::Show(
    window,                          // NativeWindowHost& owner
    L"未保存的更改",                 // 标题
    L"关闭前保存更改吗？",           // 消息
    DialogButtons::YesNo             // 按钮集合
);

// 按钮映射（所有集合共用这套结果）：
//   - Ok / Yes 按钮 → DialogResult::Primary
//   - Cancel / No 按钮 → DialogResult::Cancel

if (result == DialogResult::Primary) {
    // Yes / Ok
    Save();
    Close();
} else {
    // No / Cancel（或 Esc 关闭）
    Close();  // 不保存
})");
    return std::move(page);
}

void GalleryApp::ShowMessageDialogLength(MessageLengthCase which) {
    NativeWindowHost* owner = ownerWindow_ ? ownerWindow_() : nullptr;
    if (!owner) {
        if (messageResultText_) messageResultText_->SetText(L"结果：宿主窗口不可用");
        return;
    }

    std::wstring title;
    std::wstring message;
    switch (which) {
        case MessageLengthCase::Tiny:
            title = L"已保存";
            message = L"完成。";
            break;

        case MessageLengthCase::OneLine:
            title = L"单行消息";
            message = L"配置已写入磁盘，重启后生效。";
            break;

        case MessageLengthCase::Paragraph:
            title = L"单段长文本";
            message =
                L"导入过程中发现若干条记录的时间戳缺少时区信息，这些记录已按本地时区"
                L"解释并写入数据库。如果原始数据来自其他时区的系统，建议重新导出一份"
                L"带时区标记的文件后再次导入，否则跨时区的时间比较可能出现最多一天的"
                L"偏差。本次导入的其余记录没有问题。";
            break;

        case MessageLengthCase::MultiParagraph: {
            title = L"多段落消息";
            message =
                L"同步已完成，但有几处需要你确认。\n\n"
                L"本地有 3 个文件比远端更新，它们已被上传覆盖远端版本。如果这不是你"
                L"想要的结果，可以从版本历史里恢复。\n\n"
                L"远端有 1 个文件在本地不存在，已下载到工作目录根部。\n\n"
                L"有 2 个文件双方都有修改，无法自动合并，已保留两份并在文件名后追加"
                L"了 .conflict 后缀。\n\n"
                L"其余 148 个文件没有变化，未做任何处理。";
            break;
        }

        case MessageLengthCase::Huge: {
            title = L"超长消息（测试高度上限）";
            // 拼接足够多的段落，必然超过工作区 80% 的高度上限，用来验证
            // ScrollPanel 接管后按钮行仍留在可视区内。
            message = L"以下是完整的迁移报告，共 20 个阶段。\n\n";
            for (int i = 1; i <= 20; ++i) {
                message += L"阶段 " + std::to_wstring(i) +
                           L"：扫描源目录并建立文件清单，校验每个条目的哈希，"
                           L"把与目标端不一致的条目加入待传队列。此阶段耗时取决于"
                           L"文件数量而非总字节数，小文件很多时会明显变慢。\n\n";
            }
            message += L"全部阶段已完成，没有发现不可恢复的错误。";
            break;
        }
    }

    const DialogResult r =
        MessageDialog::Show(*owner, title, message, DialogButtons::OkCancel);

    if (messageResultText_)
        messageResultText_->SetText(std::wstring(L"结果：长度测试返回 ") + ResultName(r));
}

void GalleryApp::ShowMessageDialogOk() {
    NativeWindowHost* owner = ownerWindow_ ? ownerWindow_() : nullptr;
    if (!owner) {
        if (messageResultText_) messageResultText_->SetText(L"结果：宿主窗口不可用");
        return;
    }

    const DialogResult r = MessageDialog::Show(
        *owner,
        L"操作完成",
        L"文件已成功保存到本地。这是一个单按钮确认对话框（Ok 按钮集合），"
        L"主窗口在你点击 Ok 之前是禁用的。",
        DialogButtons::Ok);

    if (messageResultText_)
        messageResultText_->SetText(std::wstring(L"结果：Ok 集合返回 ") + ResultName(r));
}

void GalleryApp::ShowMessageDialogOkCancel() {
    NativeWindowHost* owner = ownerWindow_ ? ownerWindow_() : nullptr;
    if (!owner) {
        if (messageResultText_) messageResultText_->SetText(L"结果：宿主窗口不可用");
        return;
    }

    const DialogResult r = MessageDialog::Show(
        *owner,
        L"删除文件",
        L"确定删除该文件吗？此操作无法撤销。\n\n"
        L"Ok → DialogResult::Primary（执行删除）\n"
        L"Cancel → DialogResult::Cancel（什么都不做）",
        DialogButtons::OkCancel);

    if (messageResultText_)
        messageResultText_->SetText(std::wstring(L"结果：OkCancel 集合返回 ") + ResultName(r) +
            (r == DialogResult::Primary ? L" → 文件已删除" : L" → 已取消"));
}

void GalleryApp::ShowMessageDialogYesNo() {
    NativeWindowHost* owner = ownerWindow_ ? ownerWindow_() : nullptr;
    if (!owner) {
        if (messageResultText_) messageResultText_->SetText(L"结果：宿主窗口不可用");
        return;
    }

    const DialogResult r = MessageDialog::Show(
        *owner,
        L"未保存的更改",
        L"文档已修改但尚未保存。关闭前保存更改吗？\n\n"
        L"Yes → DialogResult::Primary（保存并关闭）\n"
        L"No → DialogResult::Cancel（不保存，直接关闭）",
        DialogButtons::YesNo);

    if (messageResultText_)
        messageResultText_->SetText(std::wstring(L"结果：YesNo 集合返回 ") + ResultName(r) +
            (r == DialogResult::Primary ? L" → 已保存" : L" → 未保存"));
}

std::unique_ptr<ScrollPanel> GalleryApp::CreateGridSplitterPage() {
    auto [page, content] = CreatePageShell(L"GridSplitter");

    // 示例 1：垂直分割
    auto* card1 = CreateExampleCard(content, L"可调整大小的 Grid 列");
    auto* desc1 = card1->Add(std::make_unique<TextBlock>());
    desc1->SetText(L"GridSplitter 位于 Grid 的两列或两行之间，用户可拖拽调整相邻轨道的尺寸。");
    desc1->SetWrap(true);

    // 创建一个带 GridSplitter 的演示网格
    auto* demoGrid = card1->Add(std::make_unique<Grid>());
    demoGrid->SetWidth(520.0f);
    demoGrid->SetHeight(240.0f);
    demoGrid->SetColumns({GridLength::Star(), GridLength::Pixels(4.0f), GridLength::Star()});
    demoGrid->SetRows({GridLength::Star()});
    demoGrid->SetMargin(Thickness{0, 8.0f, 0, 0});

    // 左侧面板
    auto* leftPanel = demoGrid->Add(std::make_unique<Border>());
    demoGrid->SetCell(leftPanel, 0, 0);
    leftPanel->SetBorderThickness(1.0f);
    leftPanel->SetCornerRadius(6.0f);
    auto* leftText = leftPanel->SetChild(std::make_unique<TextBlock>());
    leftText->SetText(L"左侧面板\n拖拽中间的分割条调整大小");
    leftText->SetVAlign(VAlign::Center);
    leftText->SetHAlign(HAlign::Center);
    leftText->SetWrap(true);

    // GridSplitter（中间 4 DIP 列）
    auto* splitter = demoGrid->Add(std::make_unique<GridSplitter>());
    demoGrid->SetCell(splitter, 0, 1);

    // 右侧面板
    auto* rightPanel = demoGrid->Add(std::make_unique<Border>());
    demoGrid->SetCell(rightPanel, 0, 2);
    rightPanel->SetBorderThickness(1.0f);
    rightPanel->SetCornerRadius(6.0f);
    auto* rightText = rightPanel->SetChild(std::make_unique<TextBlock>());
    rightText->SetText(L"右侧面板\n星号轨道会按比例调整");
    rightText->SetVAlign(VAlign::Center);
    rightText->SetHAlign(HAlign::Center);
    rightText->SetWrap(true);

    auto* hint1 = card1->Add(std::make_unique<TextBlock>());
    hint1->SetText(L"💡 鼠标悬停在中间分割条上，光标变为双向箭头，拖拽即可调整左右面板宽度。");
    hint1->SetWrap(true);
    hint1->SetFontSize(12.0f);
    hint1->SetMargin(Thickness{0, 8.0f, 0, 0});

    // 示例 2：水平分割
    auto* card2 = CreateExampleCard(content, L"可调整大小的 Grid 行");
    auto* demoGrid2 = card2->Add(std::make_unique<Grid>());
    demoGrid2->SetWidth(520.0f);
    demoGrid2->SetHeight(240.0f);
    demoGrid2->SetRows({GridLength::Star(), GridLength::Pixels(4.0f), GridLength::Star()});
    demoGrid2->SetColumns({GridLength::Star()});
    demoGrid2->SetMargin(Thickness{0, 8.0f, 0, 0});

    // 上方面板
    auto* topPanel = demoGrid2->Add(std::make_unique<Border>());
    demoGrid2->SetCell(topPanel, 0, 0);
    topPanel->SetBorderThickness(1.0f);
    topPanel->SetCornerRadius(6.0f);
    auto* topText = topPanel->SetChild(std::make_unique<TextBlock>());
    topText->SetText(L"上方面板");
    topText->SetVAlign(VAlign::Center);
    topText->SetHAlign(HAlign::Center);

    // 水平 GridSplitter
    auto* splitter2 = demoGrid2->Add(std::make_unique<GridSplitter>());
    demoGrid2->SetCell(splitter2, 1, 0);
    splitter2->SetOrientation(GridSplitter::Orientation::Horizontal);

    // 下方面板
    auto* bottomPanel = demoGrid2->Add(std::make_unique<Border>());
    demoGrid2->SetCell(bottomPanel, 2, 0);
    bottomPanel->SetBorderThickness(1.0f);
    bottomPanel->SetCornerRadius(6.0f);
    auto* bottomText = bottomPanel->SetChild(std::make_unique<TextBlock>());
    bottomText->SetText(L"下方面板");
    bottomText->SetVAlign(VAlign::Center);
    bottomText->SetHAlign(HAlign::Center);

    CreateCodeExample(content, LR"(// 垂直分割（调整列宽）
auto* grid = panel->Add(std::make_unique<Grid>());
grid->SetColumns({
    GridLength::Star(),        // 左列（可调整）
    GridLength::Pixels(4),     // 分割条
    GridLength::Star()         // 右列（可调整）
});
grid->SetRows({GridLength::Star()});

auto* leftPanel = grid->Add(std::make_unique<Border>());
Grid::SetCell(leftPanel, 0, 0);

auto* splitter = grid->Add(std::make_unique<GridSplitter>());
Grid::SetCell(splitter, 0, 1);  // 中间列

auto* rightPanel = grid->Add(std::make_unique<Border>());
Grid::SetCell(rightPanel, 0, 2);

// 水平分割（调整行高）类似，只需改用行定义)");
    return std::move(page);
}

} // namespace fluent
