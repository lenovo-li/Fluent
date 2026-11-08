// NavigationControlsPage.cpp — NavRail + PageHost + OverlayHost 演示（610/611）
//
// NavigationControls 是独立于 FluentUI 核心的扩展库，演示了「继承核心控件」
// 这条扩展路径：NavRail / PageHost / OverlayHost 都以 Panel 为基类。
//
// 610 NavRail + PageHost：侧边导航栏 + 页面容器（含切换动画）
// 611 OverlayHost：全屏模态/进度覆盖层，用于阻断操作直到完成
#include "../GalleryMain.h"
#include "../../extensions/NavigationControls/NavRail.h"
#include "../../extensions/NavigationControls/PageHost.h"
#include "../../extensions/NavigationControls/OverlayHost.h"
#include "../../FluentUI/controls/Button.h"
#include "../../FluentUI/controls/TextBlock.h"
#include "../../FluentUI/controls/ProgressBar.h"
#include "../../FluentUI/layout/StackPanel.h"
#include "../../FluentUI/layout/Border.h"
#include "../../FluentUI/layout/Grid.h"
#include "../../FluentUI/layout/ScrollPanel.h"

namespace fluent {

// ===========================================================================
// NavRail + PageHost —— nav 610
// ===========================================================================

std::unique_ptr<ScrollPanel> GalleryApp::CreateNavRailPageHostPage() {
    auto [page, content] = CreatePageShell(L"NavRail + PageHost");

    auto* intro = content->Add(std::make_unique<TextBlock>());
    intro->SetText(
        L"NavRail 是侧边导航栏（继承自 Panel），PageHost 是页面容器（含切换动画）。"
        L"两者组合是应用级导航的标准用法：NavRail 选中项驱动 PageHost 切换页面。");
    intro->SetFontSize(13.0f);
    intro->SetMargin(Thickness{0, 0, 0, 20.0f});

    // --- 卡片 1：基础导航（Rail 模式）------------------------------------
    auto* card1 = CreateExampleCard(content, L"Rail 模式：图标 + 标签导航");

    auto* d1 = card1->Add(std::make_unique<TextBlock>());
    d1->SetText(L"点击左侧导航项，右侧页面用 FadeSlide 动画切换。");
    d1->SetFontSize(12.0f);
    d1->SetMargin(Thickness{0, 0, 0, 10.0f});

    // 演示容器：固定尺寸，内部是 Grid（左 NavRail 右 PageHost）
    auto* demoBox1 = card1->Add(std::make_unique<Border>());
    demoBox1->SetWidth(640.0f);
    demoBox1->SetHeight(320.0f);
    demoBox1->SetCornerRadius(8.0f);
    demoBox1->SetBorderThickness(1.0f);

    auto* demoGrid = demoBox1->SetChild(std::make_unique<Grid>());
    demoGrid->SetColumns({GridLength::Auto(), GridLength::Star()});
    demoGrid->SetRows({GridLength::Star()});

    auto* rail = demoGrid->Emplace<NavRail>();
    demoGrid->SetCell(rail, 0, 0);
    rail->SetItems({
        {L"\xE80F", L"主页"},
        {L"\xE8F1", L"消息"},
        {L"\xE7EF", L"联系人"},
        {L"\xE713", L"设置"},
    });
    rail->SetSelectedIndex(0);

    auto* host = demoGrid->Emplace<PageHost>();
    demoGrid->SetCell(host, 0, 1);
    host->SetTransition(PageTransition::FadeSlide);

    // 构建四个页面
    const struct { const wchar_t* title; const wchar_t* body; } kPages[] = {
        {L"主页", L"这是主页内容。\nNavRail 选中第 0 项。"},
        {L"消息", L"这是消息列表。\nNavRail 选中第 1 项。"},
        {L"联系人", L"这是联系人列表。\nNavRail 选中第 2 项。"},
        {L"设置", L"这是设置页面。\nNavRail 选中第 3 项。"},
    };
    for (const auto& p : kPages) {
        auto pg = std::make_unique<StackPanel>();
        pg->SetOrientation(StackPanel::Orientation::Vertical);
        pg->SetSpacing(8.0f);
        pg->SetPadding(Thickness{20, 20, 20, 20});
        auto* title = pg->Add(std::make_unique<TextBlock>());
        title->SetText(p.title);
        title->SetFontSize(20.0f);
        title->SetFontWeight(DWRITE_FONT_WEIGHT_SEMI_BOLD);
        auto* body = pg->Add(std::make_unique<TextBlock>());
        body->SetText(p.body);
        body->SetFontSize(13.0f);
        body->SetWrap(true);
        host->AddPage(std::move(pg));
    }
    host->ShowPage(0);

    // NavRail 选中事件驱动 PageHost 切换
    pageSubs_ += rail->SelectionChanged().Subscribe(host,
        [](void* owner, NavRail&, int& idx) {
            static_cast<PageHost*>(owner)->ShowPage(idx);
        });

    // --- 卡片 2：Pane 模式（展开/收起）----------------------------------
    auto* card2 = CreateExampleCard(content, L"Pane 模式：可展开的导航栏");

    auto* d2 = card2->Add(std::make_unique<TextBlock>());
    d2->SetText(
        L"SetPaneMode(true) 启用汉堡菜单样式：收起时只显示图标（48 DIP），"
        L"展开时显示图标 + 标签（默认 240 DIP）。点击切换按钮展开/收起。");
    d2->SetFontSize(12.0f);
    d2->SetMargin(Thickness{0, 0, 0, 10.0f});

    auto* demoBox2 = card2->Add(std::make_unique<Border>());
    demoBox2->SetWidth(640.0f);
    demoBox2->SetHeight(320.0f);
    demoBox2->SetCornerRadius(8.0f);
    demoBox2->SetBorderThickness(1.0f);

    auto* paneGrid = demoBox2->SetChild(std::make_unique<Grid>());
    paneGrid->SetColumns({GridLength::Auto(), GridLength::Star()});
    paneGrid->SetRows({GridLength::Star()});

    auto* paneRail = paneGrid->Emplace<NavRail>();
    paneGrid->SetCell(paneRail, 0, 0);
    paneRail->SetPaneMode(true);
    paneRail->SetItems({
        {L"\xE80F", L"主页"},
        {L"\xE8F1", L"消息"},
        {L"\xE7EF", L"联系人"},
        {L"\xE713", L"设置"},
    });
    paneRail->SetSelectedIndex(0);

    auto* paneHost = paneGrid->Emplace<PageHost>();
    paneGrid->SetCell(paneHost, 0, 1);
    paneHost->SetTransition(PageTransition::Fade);

    for (const auto& p : kPages) {
        auto pg = std::make_unique<StackPanel>();
        pg->SetOrientation(StackPanel::Orientation::Vertical);
        pg->SetSpacing(8.0f);
        pg->SetPadding(Thickness{20, 20, 20, 20});
        auto* title = pg->Add(std::make_unique<TextBlock>());
        title->SetText(p.title);
        title->SetFontSize(20.0f);
        title->SetFontWeight(DWRITE_FONT_WEIGHT_SEMI_BOLD);
        auto* body = pg->Add(std::make_unique<TextBlock>());
        body->SetText(p.body);
        body->SetFontSize(13.0f);
        body->SetWrap(true);
        paneHost->AddPage(std::move(pg));
    }
    paneHost->ShowPage(0);

    pageSubs_ += paneRail->SelectionChanged().Subscribe(paneHost,
        [](void* owner, NavRail&, int& idx) {
            static_cast<PageHost*>(owner)->ShowPage(idx);
        });

    // --- 卡片 3：代码示例 ------------------------------------------------
    auto* card3 = CreateExampleCard(content, L"接入方式");
    CreateCodeExample(card3, LR"(// Grid：左 NavRail + 右 PageHost
grid->SetColumns({GridLength::Auto(), GridLength::Star()});
auto* rail = grid->Emplace<NavRail>();
auto* host = grid->Emplace<PageHost>();
grid->SetCell(rail, 0, 0);
grid->SetCell(host, 0, 1);

rail->SetItems({
    {L"\xE80F", L"主页"},
    {L"\xE8F1", L"消息"},
});
host->AddPage(std::move(homePage));
host->AddPage(std::move(msgPage));
host->ShowPage(0);

// 选中事件驱动页面切换
navSub_ = rail->SelectionChanged().Subscribe(host,
    [](void* o, NavRail&, int& idx) {
        static_cast<PageHost*>(o)->ShowPage(idx);
    });

// Pane 模式（汉堡菜单）
rail->SetPaneMode(true);
rail->SetExpanded(false);)");

    return std::move(page);
}

// ===========================================================================
// OverlayHost —— nav 611
// ===========================================================================

std::unique_ptr<ScrollPanel> GalleryApp::CreateOverlayHostPage() {
    auto [page, content] = CreatePageShell(L"OverlayHost");

    auto* intro = content->Add(std::make_unique<TextBlock>());
    intro->SetText(
        L"OverlayHost 是全屏模态覆盖层：Show() 后它拦截全部输入（只有自身按钮可点），"
        L"同时弹出半透明遮罩和居中卡片。用于安装中、需要确认等必须等待的场景。");
    intro->SetFontSize(13.0f);
    intro->SetMargin(Thickness{0, 0, 0, 20.0f});

    // --- 卡片 1：各类 OverlayIcon 演示 -----------------------------------
    auto* card1 = CreateExampleCard(content, L"各类覆盖场景");

    auto* d1 = card1->Add(std::make_unique<TextBlock>());
    d1->SetText(L"OverlayHost 通常作为根容器的最后一个子元素，跨所有行列。"
                L"下方演示区包含一组按钮和一个 OverlayHost，点击按钮触发对应场景。");
    d1->SetFontSize(12.0f);
    d1->SetMargin(Thickness{0, 0, 0, 10.0f});

    // 演示容器：Grid，两行（内容行 + 按钮行），OverlayHost 跨两行
    auto* demoBox = card1->Add(std::make_unique<Border>());
    demoBox->SetWidth(640.0f);
    demoBox->SetHeight(360.0f);
    demoBox->SetCornerRadius(8.0f);
    demoBox->SetBorderThickness(1.0f);

    auto* demoGrid = demoBox->SetChild(std::make_unique<Grid>());
    demoGrid->SetColumns({GridLength::Star()});
    demoGrid->SetRows({GridLength::Star(), GridLength::Auto()});

    // 内容区
    auto* demoContent = demoGrid->Emplace<StackPanel>();
    demoGrid->SetCell(demoContent, 0, 0);
    demoContent->SetOrientation(StackPanel::Orientation::Vertical);
    demoContent->SetSpacing(8.0f);
    demoContent->SetPadding(Thickness{24, 24, 24, 24});

    auto* bgText = demoContent->Add(std::make_unique<TextBlock>());
    bgText->SetText(L"这是底层内容。覆盖层显示时，这里的按钮不可点击。");
    bgText->SetFontSize(13.0f);
    bgText->SetWrap(true);

    auto* bgBtn = demoContent->Add(std::make_unique<Button>());
    bgBtn->SetText(L"底层按钮（覆盖时无法点击）");

    // 触发按钮行
    auto* btnRow = demoGrid->Emplace<StackPanel>();
    demoGrid->SetCell(btnRow, 1, 0);
    btnRow->SetOrientation(StackPanel::Orientation::Horizontal);
    btnRow->SetSpacing(8.0f);
    btnRow->SetPadding(Thickness{16, 10, 16, 10});

    // OverlayHost 跨两行，topmost
    auto* overlay = demoGrid->Emplace<OverlayHost>();
    demoGrid->SetCell(overlay, 0, 0, 2, 1);  // rowSpan=2

    // 结果回显
    auto* resultLabel = card1->Add(std::make_unique<TextBlock>());
    resultLabel->SetText(L"（尚未触发）");
    resultLabel->SetFontSize(11.0f);
    resultLabel->SetMargin(Thickness{0, 8.0f, 0, 0});

    // 订阅 Closed 事件：显示用户选了哪个按钮
    pageSubs_ += overlay->Closed().Subscribe(resultLabel,
        [](void* owner, OverlayHost&, OverlayResultArgs& args) {
            auto* lbl = static_cast<TextBlock*>(owner);
            switch (args.result) {
            case OverlayResult::Primary:   lbl->SetText(L"结果：主按钮（Primary）"); break;
            case OverlayResult::Secondary: lbl->SetText(L"结果：次按钮（Secondary）"); break;
            default:                       lbl->SetText(L"结果：关闭（Dismissed / Esc）"); break;
            }
        });

    // 各场景按钮：每个按钮独立订阅，owner 是 overlay 指针，handler 内直接填入对应 spec
    {
        auto* btn = btnRow->Add(std::make_unique<Button>());
        btn->SetText(L"信息确认");
        pageSubs_ += btn->Click().Subscribe(overlay,
            [](void* o, Button&, RoutedEventArgs&) {
                static_cast<OverlayHost*>(o)->Show({
                    OverlayIcon::Info, L"确认操作",
                    L"你确定要继续吗？此操作不可撤销。",
                    L"确认", L"取消"});
            });
    }
    {
        auto* btn = btnRow->Add(std::make_unique<Button>());
        btn->SetText(L"警告");
        pageSubs_ += btn->Click().Subscribe(overlay,
            [](void* o, Button&, RoutedEventArgs&) {
                static_cast<OverlayHost*>(o)->Show({
                    OverlayIcon::Warning, L"检测到问题",
                    L"发现一个配置错误，建议修复后再继续。",
                    L"立即修复", L"忽略"});
            });
    }
    {
        auto* btn = btnRow->Add(std::make_unique<Button>());
        btn->SetText(L"错误");
        pageSubs_ += btn->Click().Subscribe(overlay,
            [](void* o, Button&, RoutedEventArgs&) {
                static_cast<OverlayHost*>(o)->Show({
                    OverlayIcon::Error, L"操作失败",
                    L"无法连接到服务器，请检查网络连接。",
                    L"重试", L"取消"});
            });
    }
    {
        auto* btn = btnRow->Add(std::make_unique<Button>());
        btn->SetText(L"成功");
        pageSubs_ += btn->Click().Subscribe(overlay,
            [](void* o, Button&, RoutedEventArgs&) {
                static_cast<OverlayHost*>(o)->Show({
                    OverlayIcon::Success, L"安装完成",
                    L"所有组件已成功安装，重启后生效。",
                    L"立即重启", L"稍后重启"});
            });
    }

    // --- 卡片 2：进度覆盖 ------------------------------------------------
    auto* card2 = CreateExampleCard(content, L"进度覆盖：showProgress = true");

    auto* d2 = card2->Add(std::make_unique<TextBlock>());
    d2->SetText(
        L"showProgress = true 时显示进度条，配合 UpdateProgress(percent, message) 更新。"
        L"进度覆盖期间 Esc 键无效，必须通过代码调用 Hide() 或切换到按钮状态来关闭。");
    d2->SetFontSize(12.0f);
    d2->SetMargin(Thickness{0, 0, 0, 10.0f});

    // 独立的演示 Grid
    auto* demoBox2 = card2->Add(std::make_unique<Border>());
    demoBox2->SetWidth(640.0f);
    demoBox2->SetHeight(260.0f);
    demoBox2->SetCornerRadius(8.0f);
    demoBox2->SetBorderThickness(1.0f);

    auto* demoGrid2 = demoBox2->SetChild(std::make_unique<Grid>());
    demoGrid2->SetColumns({GridLength::Star()});
    demoGrid2->SetRows({GridLength::Star(), GridLength::Auto()});

    auto* overlay2 = demoGrid2->Emplace<OverlayHost>();
    demoGrid2->SetCell(overlay2, 0, 0, 2, 1);

    auto* bg2 = demoGrid2->Emplace<StackPanel>();
    demoGrid2->SetCell(bg2, 0, 0);
    bg2->SetPadding(Thickness{24, 24, 24, 0});
    auto* bgTxt2 = bg2->Add(std::make_unique<TextBlock>());
    bgTxt2->SetText(L"底层内容 —— 进度覆盖时这里不可交互");
    bgTxt2->SetFontSize(13.0f);

    auto* bar2Row = demoGrid2->Emplace<StackPanel>();
    demoGrid2->SetCell(bar2Row, 1, 0);
    bar2Row->SetOrientation(StackPanel::Orientation::Horizontal);
    bar2Row->SetSpacing(8.0f);
    bar2Row->SetPadding(Thickness{16, 10, 16, 10});

    auto* startBtn = bar2Row->Add(std::make_unique<Button>());
    startBtn->SetText(L"开始模拟进度");
    pageSubs_ += startBtn->Click().Subscribe(overlay2,
        [](void* o, Button&, RoutedEventArgs&) {
            static_cast<OverlayHost*>(o)->Show({OverlayIcon::Info, L"正在安装",
                L"请稍候，正在处理文件...", L"", L"", true});
        });

    auto* prog25 = bar2Row->Add(std::make_unique<Button>());
    prog25->SetText(L"进度 25%");
    pageSubs_ += prog25->Click().Subscribe(overlay2,
        [](void* o, Button&, RoutedEventArgs&) {
            static_cast<OverlayHost*>(o)->UpdateProgress(25, L"正在复制文件 (25%)...");
        });

    auto* prog75 = bar2Row->Add(std::make_unique<Button>());
    prog75->SetText(L"进度 75%");
    pageSubs_ += prog75->Click().Subscribe(overlay2,
        [](void* o, Button&, RoutedEventArgs&) {
            static_cast<OverlayHost*>(o)->UpdateProgress(75, L"正在注册组件 (75%)...");
        });

    auto* doneBtn = bar2Row->Add(std::make_unique<Button>());
    doneBtn->SetText(L"完成（切到按钮状态）");
    pageSubs_ += doneBtn->Click().Subscribe(overlay2,
        [](void* o, Button&, RoutedEventArgs&) {
            static_cast<OverlayHost*>(o)->Show({OverlayIcon::Success, L"安装完成",
                L"所有组件已成功安装。", L"关闭"});
        });

    pageSubs_ += overlay2->Closed().Subscribe(overlay2,
        [](void* o, OverlayHost&, OverlayResultArgs&) {
            static_cast<OverlayHost*>(o)->Hide();
        });

    // --- 卡片 3：代码示例 ------------------------------------------------
    auto* card3 = CreateExampleCard(content, L"接入方式");
    CreateCodeExample(card3, LR"(// OverlayHost 作为 topmost 元素跨所有行列
auto* overlay = root->Emplace<OverlayHost>();
root->SetCell(overlay, 0, 0, totalRows, totalCols);

// 订阅关闭事件
closedSub_ = overlay->Closed().Subscribe(this,
    [](void* o, OverlayHost& ov, OverlayResultArgs& a) {
        if (a.result == OverlayResult::Primary)
            static_cast<App*>(o)->OnConfirmed();
        ov.Hide();
    });

// 显示各场景
overlay->Show({OverlayIcon::Warning, L"注意", L"确认继续？", L"确认", L"取消"});

// 进度状态
overlay->Show({OverlayIcon::Info, L"安装中", L"请稍候...", L"", L"", true});
overlay->UpdateProgress(50, L"复制文件 50%...");
// 完成后切换到按钮状态（或直接 Hide）
overlay->Show({OverlayIcon::Success, L"完成", L"安装成功", L"关闭"});)");

    return std::move(page);
}

} // namespace fluent
