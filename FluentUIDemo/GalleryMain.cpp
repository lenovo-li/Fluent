// GalleryMain.cpp — 核心框架逻辑（导航 + 页面切换）
#include "GalleryMain.h"
#include "../FluentUI/layout/DockPanel.h"
#include "../FluentUI/layout/Grid.h"
#include "../FluentUI/layout/StackPanel.h"
#include "../FluentUI/layout/Border.h"
#include "../FluentUI/layout/ScrollPanel.h"
#include "../FluentUI/controls/TextBlock.h"
#include "../FluentUI/controls/TextArea.h"
#include "../FluentUI/controls/RichTextBox.h"
#include "../FluentUI/controls/Button.h"
#include "../FluentUI/controls/ContentDialog.h"
#include <unordered_set>
#include <cwctype>

namespace fluent {

// Both defined out-of-line so unique_ptr<ContentDialog>'s destructor is
// instantiated here, where ContentDialog is complete — the header only forward
// declares it (see GalleryMain.h).
GalleryApp::GalleryApp() = default;
GalleryApp::~GalleryApp() = default;

std::unique_ptr<Panel> GalleryApp::BuildContent() {
    // 根布局：DockPanel（标题栏 Top + Grid 填充）
    auto root = std::make_unique<DockPanel>();

    // 标题栏：48 DIP 高，左侧标题 + 右侧主题切换按钮
    auto* titleBar = root->Add(std::make_unique<StackPanel>());
    titleBar->SetOrientation(StackPanel::Orientation::Horizontal);
    titleBar->SetHeight(48.0f);
    DockPanel::SetDock(titleBar, Dock::Top);

    auto* titleText = titleBar->Add(std::make_unique<TextBlock>());
    titleText->SetText(L"  FluentUI Gallery");
    titleText->SetTypographyRole(TypographyRole::Subtitle);
    titleText->SetVAlign(VAlign::Center);
    titleText->SetHitTestVisible(false);  // 允许通过文字拖动标题栏

    // 右侧弹簧（把主题按钮推到最右）
    auto* spacer = titleBar->Add(std::make_unique<Border>());
    spacer->SetHAlign(HAlign::Stretch);

    // 主题切换按钮（最右侧）
    themeToggleBtn_ = titleBar->Add(std::make_unique<Button>());
    themeToggleBtn_->SetMargin(Thickness{ 12.0f, 12.0f, 12.0f, 12.0f });
    if (isDark_ && isDark_()) {
        themeToggleBtn_->SetText(L"☀️");
    } else {
        themeToggleBtn_->SetText(L"🌙");
    }
    themeSub_ = themeToggleBtn_->Click().Subscribe(this, [](void* owner, Button&, RoutedEventArgs&) {
        auto* app = static_cast<GalleryApp*>(owner);
        if (!app->themeToggle_ || !app->isDark_) return;

        const bool currentlyDark = app->isDark_();
        app->themeToggle_(!currentlyDark);
        const bool nowDark = app->isDark_();
        app->themeToggleBtn_->SetText(nowDark ? L"☀️" : L"🌙");
    });

    // 内容区：Grid 2 列（固定 220 DIP + 剩余空间）
    auto* contentGrid = root->Add(std::make_unique<Grid>());
    contentGrid->SetColumns({GridLength::Pixels(220.0f), GridLength::Star()});
    contentGrid->SetRows({GridLength::Star()});

    // 左侧：TreeView 导航
    auto* nav = contentGrid->Add(std::make_unique<TreeView>());
    contentGrid->SetCell(nav, 0, 0);
    nav->SetMargin(Thickness{12.0f, 12.0f, 12.0f, 12.0f});
    nav->SetRows(BuildNavigationTree());
    navSub_ = nav->SelectionChanged().Subscribe(
        this, [](void* owner, TreeView& tree, TreeSelection& sel) {
            static_cast<GalleryApp*>(owner)->OnNavigationChanged(tree, sel);
        });

    // 右侧：详情页容器
    auto* pageHost = contentGrid->Add(std::make_unique<Grid>());
    contentGrid->SetCell(pageHost, 0, 1);
    pageHost->SetColumns({GridLength::Star()});
    pageHost->SetRows({GridLength::Star()});
    detailContainer_ = pageHost;

    BuildAllPages();
    nav->SetSelectedIndex(1);  // 默认选中第一个控件页（Button）

    return root;
}

void GalleryApp::BuildAllPages() {
    static constexpr int kPageIds[] = {
        // 输入 · Input
        101, 102, 103, 104, 109, 201, 208, 202, 203, 204, 205, 206, 111, 107,
        // 显示 · Display
        301, 105, 106, 108,
        // 文本编辑 · Text Editing
        302, 409, 408,
        // 选择器 · Selectors
        401, 402,
        // 数据展示 · Data & Visualization
        404, 405,
        // 状态与反馈 · Status & Feedback
        403, 406, 407,
        // 布局容器 · Layout
        501, 502, 503, 504, 505, 506, 507, 508, 509, 510, 511, 608, 512,
        // 窗口与导航 · Window & Navigation
        601, 602, 603, 604, 605, 606, 607, 610, 611,
        // 框架功能 · Framework Features
        801, 802, 803, 804, 805, 806, 807, 808, 809,
        // 扩展库 · Extensions
        112, 113,
        // 性能与诊断 · Performance
        701, 702, 703, 704, 705, 707,
        // 真实应用 · Real Apps
        901, 902,
    };

    for (int id : kPageIds) {
        auto page = CreatePageForId(id);
        if (!page) continue;
        ScrollPanel* raw = detailContainer_->Add(std::move(page));
        detailContainer_->SetCell(raw, 0, 0);
        raw->SetVisible(false);
        pages_[id] = raw;
    }
}

std::vector<TreeViewRow> GalleryApp::BuildNavigationTree() {
    std::vector<TreeViewRow> rows;

    // 1000: 输入 · Input
    rows.push_back({1000, -1, L"输入 · Input", 0, false, true, true, TreeViewIcon::Folder});
    rows.push_back({101, 1000, L"Button", 1, false, false, true, TreeViewIcon::None});
    rows.push_back({102, 1000, L"CheckBox", 1, false, false, true, TreeViewIcon::None});
    rows.push_back({103, 1000, L"RadioButton", 1, false, false, true, TreeViewIcon::None});
    rows.push_back({104, 1000, L"ToggleSwitch", 1, false, false, true, TreeViewIcon::None});
    rows.push_back({109, 1000, L"ToggleButton", 1, false, false, true, TreeViewIcon::None});
    rows.push_back({201, 1000, L"TextBox", 1, false, false, true, TreeViewIcon::None});
    rows.push_back({208, 1000, L"PasswordBox", 1, false, false, true, TreeViewIcon::None});
    rows.push_back({202, 1000, L"Slider", 1, false, false, true, TreeViewIcon::None});
    rows.push_back({203, 1000, L"ComboBox", 1, false, false, true, TreeViewIcon::None});
    rows.push_back({204, 1000, L"DatePicker", 1, false, false, true, TreeViewIcon::None});
    rows.push_back({205, 1000, L"Calendar", 1, false, false, true, TreeViewIcon::None});
    rows.push_back({206, 1000, L"NumericUpDown", 1, false, false, true, TreeViewIcon::None});
    rows.push_back({111, 1000, L"Rating", 1, false, false, true, TreeViewIcon::None});
    rows.push_back({107, 1000, L"RepeatButton", 1, false, false, true, TreeViewIcon::None});

    // 2000: 显示 · Display
    rows.push_back({2000, -1, L"显示 · Display", 0, false, true, true, TreeViewIcon::Folder});
    rows.push_back({301, 2000, L"TextBlock", 1, false, false, true, TreeViewIcon::None});
    rows.push_back({105, 2000, L"Hyperlink", 1, false, false, true, TreeViewIcon::None});
    rows.push_back({106, 2000, L"Image", 1, false, false, true, TreeViewIcon::None});
    rows.push_back({108, 2000, L"Separator", 1, false, false, true, TreeViewIcon::None});

    // 3000: 文本编辑 · Text Editing
    rows.push_back({3000, -1, L"文本编辑 · Text Editing", 0, false, true, true, TreeViewIcon::Folder});
    rows.push_back({302, 3000, L"TextArea", 1, false, false, true, TreeViewIcon::None});
    rows.push_back({409, 3000, L"RichTextBox", 1, false, false, true, TreeViewIcon::None});
    rows.push_back({408, 3000, L"LogViewer", 1, false, false, true, TreeViewIcon::None});

    // 4000: 选择器 · Selectors
    rows.push_back({4000, -1, L"选择器 · Selectors", 0, false, true, true, TreeViewIcon::Folder});
    rows.push_back({401, 4000, L"ListBox", 1, false, false, true, TreeViewIcon::None});
    rows.push_back({402, 4000, L"TreeView", 1, false, false, true, TreeViewIcon::None});

    // 5000: 数据展示 · Data & Visualization
    rows.push_back({5000, -1, L"数据展示 · Data & Visualization", 0, false, true, true, TreeViewIcon::Folder});
    rows.push_back({404, 5000, L"DataGrid", 1, false, false, true, TreeViewIcon::None});
    rows.push_back({405, 5000, L"Chart", 1, false, false, true, TreeViewIcon::None});

    // 6000: 状态与反馈 · Status & Feedback
    rows.push_back({6000, -1, L"状态与反馈 · Status & Feedback", 0, false, true, true, TreeViewIcon::Folder});
    rows.push_back({403, 6000, L"ProgressBar", 1, false, false, true, TreeViewIcon::None});
    rows.push_back({406, 6000, L"InfoBar", 1, false, false, true, TreeViewIcon::None});
    rows.push_back({407, 6000, L"Metric", 1, false, false, true, TreeViewIcon::None});

    // 7000: 布局容器 · Layout
    rows.push_back({7000, -1, L"布局容器 · Layout", 0, false, true, true, TreeViewIcon::Folder});
    rows.push_back({501, 7000, L"Grid", 1, false, false, true, TreeViewIcon::None});
    rows.push_back({502, 7000, L"StackPanel", 1, false, false, true, TreeViewIcon::None});
    rows.push_back({503, 7000, L"WrapPanel", 1, false, false, true, TreeViewIcon::None});
    rows.push_back({504, 7000, L"DockPanel", 1, false, false, true, TreeViewIcon::None});
    rows.push_back({505, 7000, L"Border", 1, false, false, true, TreeViewIcon::None});
    rows.push_back({506, 7000, L"Canvas", 1, false, false, true, TreeViewIcon::None});
    rows.push_back({507, 7000, L"ScrollViewer", 1, false, false, true, TreeViewIcon::None});
    rows.push_back({508, 7000, L"Expander", 1, false, false, true, TreeViewIcon::None});
    rows.push_back({509, 7000, L"UniformGrid", 1, false, false, true, TreeViewIcon::None});
    rows.push_back({510, 7000, L"Viewbox", 1, false, false, true, TreeViewIcon::None});
    rows.push_back({511, 7000, L"GroupBox", 1, false, false, true, TreeViewIcon::None});
    rows.push_back({608, 7000, L"GridSplitter", 1, false, false, true, TreeViewIcon::None});
    rows.push_back({512, 7000, L"动态列表 (RemoveAt/InsertAt)", 1, false, false, true, TreeViewIcon::None});

    // 8000: 窗口与导航 · Window & Navigation
    rows.push_back({8000, -1, L"窗口与导航 · Window & Navigation", 0, false, true, true, TreeViewIcon::Folder});
    rows.push_back({601, 8000, L"TabControl", 1, false, false, true, TreeViewIcon::None});
    rows.push_back({602, 8000, L"MenuBar", 1, false, false, true, TreeViewIcon::None});
    rows.push_back({603, 8000, L"MenuFlyout", 1, false, false, true, TreeViewIcon::None});
    rows.push_back({604, 8000, L"ToolBar", 1, false, false, true, TreeViewIcon::None});
    rows.push_back({605, 8000, L"StatusBar", 1, false, false, true, TreeViewIcon::None});
    rows.push_back({606, 8000, L"ContentDialog", 1, false, false, true, TreeViewIcon::None});
    rows.push_back({607, 8000, L"MessageDialog", 1, false, false, true, TreeViewIcon::None});
    rows.push_back({610, 8000, L"NavRail + PageHost", 1, false, false, true, TreeViewIcon::None});
    rows.push_back({611, 8000, L"OverlayHost", 1, false, false, true, TreeViewIcon::None});

    // 9000: 框架功能 · Framework Features
    rows.push_back({9000, -1, L"框架功能 · Framework Features", 0, false, true, true, TreeViewIcon::Folder});
    rows.push_back({801, 9000, L"工具提示", 1, false, false, true, TreeViewIcon::None});
    rows.push_back({802, 9000, L"右键菜单", 1, false, false, true, TreeViewIcon::None});
    rows.push_back({803, 9000, L"Ctrl+Wheel 缩放", 1, false, false, true, TreeViewIcon::None});
    rows.push_back({804, 9000, L"拖放", 1, false, false, true, TreeViewIcon::None});
    rows.push_back({805, 9000, L"透明度", 1, false, false, true, TreeViewIcon::None});
    rows.push_back({806, 9000, L"文件对话框", 1, false, false, true, TreeViewIcon::None});
    rows.push_back({807, 9000, L"DPI 适配", 1, false, false, true, TreeViewIcon::None});
    rows.push_back({808, 9000, L"动画", 1, false, false, true, TreeViewIcon::None});
    rows.push_back({809, 9000, L"托盘图标", 1, false, false, true, TreeViewIcon::None});

    // 10000: 扩展库 · Extensions
    rows.push_back({10000, -1, L"扩展库 · Extensions", 0, false, true, true, TreeViewIcon::Folder});
    rows.push_back({112, 10000, L"Custom Controls", 1, false, false, true, TreeViewIcon::None});
    rows.push_back({113, 10000, L"Installer Controls", 1, false, false, true, TreeViewIcon::None});

    // 11000: 性能与诊断 · Performance
    rows.push_back({11000, -1, L"性能与诊断 · Performance", 0, false, true, true, TreeViewIcon::Warning});
    rows.push_back({701, 11000, L"虚拟化对比", 1, false, false, true, TreeViewIcon::None});
    rows.push_back({702, 11000, L"日志追尾", 1, false, false, true, TreeViewIcon::None});
    rows.push_back({703, 11000, L"主题切换", 1, false, false, true, TreeViewIcon::None});
    rows.push_back({704, 11000, L"动画流畅度", 1, false, false, true, TreeViewIcon::None});
    rows.push_back({705, 11000, L"压力测试", 1, false, false, true, TreeViewIcon::None});
    rows.push_back({707, 11000, L"主题 vs 自定义", 1, false, false, true, TreeViewIcon::None});

    // 12000: 真实应用 · Real Apps
    rows.push_back({12000, -1, L"真实应用 · Real Apps", 0, false, true, true, TreeViewIcon::Folder});
    rows.push_back({901, 12000, L"A股分析工具", 1, false, false, true, TreeViewIcon::None});
    rows.push_back({902, 12000, L"自我调试 · Inspector", 1, false, false, true, TreeViewIcon::None});

    return rows;
}

void GalleryApp::OnNavigationChanged(TreeView& tree, const TreeSelection& sel) {
    if (!sel.row || sel.row->depth == 0) return;

    const int newPageId = sel.row->id;
    if (newPageId == currentPageId_) return;

    if (currentPageId_ != -1) {
        auto it = pages_.find(currentPageId_);
        if (it != pages_.end() && it->second) {
            it->second->SetVisible(false);
        }
    }

    if (logState_ && logState_->running.exchange(false) && logAppendBtn_) {
        logAppendBtn_->SetText(L"高速追加 (10k 行/秒)");
    }

    auto it = pages_.find(newPageId);
    if (it != pages_.end() && it->second) {
        it->second->SetVisible(true);
        currentPageId_ = newPageId;
        // Label the resize traces with the page that is actually on screen. The
        // TreeView row text IS the page name, so there is nothing to map — and
        // nothing to keep in sync when a page is added.
        if (ownerWindow_) {
            if (NativeWindowHost* win = ownerWindow_()) {
                win->SetResizeTraceLabel(sel.row->text);
            }
        }
    }
}

std::unique_ptr<ScrollPanel> GalleryApp::CreatePageForId(int id) {
    switch (id) {
        case 101: return CreateButtonPage();
        case 102: return CreateCheckBoxPage();
        case 103: return CreateRadioButtonPage();
        case 104: return CreateToggleSwitchPage();
        case 105: return CreateHyperlinkPage();
        case 106: return CreateImagePage();
        case 107: return CreateRepeatButtonPage();
        case 108: return CreateSeparatorPage();
        case 109: return CreateToggleButtonPage();
        case 111: return CreateRatingPage();
        case 112: return CreateCustomControlsPage();
        case 113: return CreateInstallerControlsPage();

        case 201: return CreateTextBoxPage();
        case 208: return CreatePasswordBoxPage();
        case 202: return CreateSliderPage();
        case 203: return CreateComboBoxPage();
        case 204: return CreateDatePickerPage();
        case 205: return CreateCalendarPage();
        case 206: return CreateNumericUpDownPage();

        case 301: return CreateTextBlockPage();
        case 302: return CreateTextAreaPage();

        case 401: return CreateListBoxPage();
        case 402: return CreateTreeViewPage();
        case 403: return CreateProgressBarPage();
        case 404: return CreateDataGridPage();
        case 405: return CreateChartPage();
        case 406: return CreateInfoBarPage();
        case 407: return CreateMetricPage();
        case 408: return CreateLogViewerPage();
        case 409: return CreateRichTextBoxPage();

        case 501: return CreateGridPage();
        case 502: return CreateStackPanelPage();
        case 503: return CreateWrapPanelPage();
        case 504: return CreateDockPanelPage();
        case 505: return CreateBorderPage();
        case 506: return CreateCanvasPage();
        case 507: return CreateScrollViewerPage();
        case 508: return CreateExpanderPage();
        case 509: return CreateUniformGridPage();
        case 510: return CreateViewboxPage();
        case 511: return CreateGroupBoxPage();
        case 512: return CreateDynamicListPage();
        case 901: return CreateStockAnalyzerPage();
        case 902: return CreateInspectorPage();

        case 801: return CreateTooltipFeaturePage();
        case 802: return CreateContextMenuFeaturePage();
        case 803: return CreateZoomFeaturePage();
        case 804: return CreateDragDropFeaturePage();
        case 805: return CreateOpacityFeaturePage();
        case 806: return CreateFileDialogFeaturePage();
        case 807: return CreateDpiFeaturePage();
        case 808: return CreateAnimationFeaturePage();
        case 809: return CreateTrayFeaturePage();

        case 601: return CreateTabControlPage();
        case 602: return CreateMenuBarPage();
        case 603: return CreateMenuFlyoutPage();
        case 604: return CreateToolBarPage();
        case 605: return CreateStatusBarPage();
        case 606: return CreateContentDialogPage();
        case 607: return CreateMessageDialogPage();
        case 608: return CreateGridSplitterPage();
        case 610: return CreateNavRailPageHostPage();
        case 611: return CreateOverlayHostPage();

        case 701: return CreateVirtualizationDemoPage();
        case 702: return CreateLogDemoPage();
        case 703: return CreateThemeDemoPage();
        case 704: return CreateAnimationDemoPage();
        case 705: return CreateStressTestPage();

        case 707: return CreateThemeVsCustomPage();

        default: return nullptr;
    }
}

// ============================================================================
// 辅助方法
// ============================================================================

GalleryApp::PageShell GalleryApp::CreatePageShell(std::wstring title) {
    auto scroll = std::make_unique<ScrollPanel>();
    scroll->SetPadding(24.0f);

    auto* content = scroll->Add(std::make_unique<StackPanel>());
    content->SetOrientation(StackPanel::Orientation::Vertical);
    content->SetSpacing(0);

    auto* titleBlock = content->Add(std::make_unique<TextBlock>());
    titleBlock->SetText(std::move(title));
    titleBlock->SetFontSize(32.0f);
    titleBlock->SetMargin(Thickness{0, 0, 0, 24.0f});

    return {std::move(scroll), content};
}

StackPanel* GalleryApp::CreateExampleCard(Panel* parent, std::wstring title) {
    auto* card = parent->Add(std::make_unique<Border>());
    card->SetPadding(Thickness{16.0f});
    card->SetMargin(Thickness{0, 0, 0, 16.0f});
    card->SetBorderThickness(1.0f);
    card->SetCornerRadius(8.0f);

    auto* content = card->SetChild(std::make_unique<StackPanel>());
    content->SetOrientation(StackPanel::Orientation::Vertical);
    content->SetSpacing(8.0f);

    auto* titleBlock = content->Add(std::make_unique<TextBlock>());
    titleBlock->SetText(std::move(title));
    titleBlock->SetFontSize(16.0f);

    return content;
}

RichTextBox* GalleryApp::CreateCodeExample(Panel* parent, std::wstring code) {
    auto* codeBlock = parent->Add(std::make_unique<RichTextBox>());
    codeBlock->SetReadOnly(true);
    codeBlock->SetWordWrap(false);
    codeBlock->SetHeight(200.0f);
    codeBlock->SetMargin(Thickness{0, 16.0f, 0, 0});
    
    // Visual styling to distinguish code blocks
    codeBlock->SetBackground(D2D1::ColorF(0.96f, 0.96f, 0.96f));  // Light gray background
    codeBlock->SetBorderBrush(D2D1::ColorF(0.8f, 0.8f, 0.8f));
    codeBlock->SetBorderThickness(1.0f);
    codeBlock->SetCornerRadius(4.0f);
    
    // Apply simple C++ syntax highlighting
    RichTextDocument doc = ApplySyntaxHighlighting(code);
    codeBlock->SetDocument(std::move(doc));
    return codeBlock;
}

// --- Syntax Highlighting Helper ---

namespace {
    // C++ keywords for highlighting
    const std::unordered_set<std::wstring> kCppKeywords = {
        L"auto", L"bool", L"break", L"case", L"catch", L"char", L"class", L"const",
        L"continue", L"default", L"delete", L"do", L"double", L"else", L"enum",
        L"explicit", L"extern", L"false", L"float", L"for", L"friend", L"if",
        L"inline", L"int", L"long", L"namespace", L"new", L"nullptr", L"operator",
        L"private", L"protected", L"public", L"return", L"short", L"signed",
        L"sizeof", L"static", L"struct", L"switch", L"template", L"this", L"throw",
        L"true", L"try", L"typedef", L"typename", L"union", L"unsigned", L"using",
        L"virtual", L"void", L"volatile", L"while", L"override", L"final"
    };

    const D2D1_COLOR_F kDefaultColor = D2D1::ColorF(0.2f, 0.2f, 0.2f);
    const D2D1_COLOR_F kKeywordColor = D2D1::ColorF(0.0f, 0.0f, 0.8f);
    const D2D1_COLOR_F kCommentColor = D2D1::ColorF(0.0f, 0.5f, 0.0f);
    const D2D1_COLOR_F kStringColor = D2D1::ColorF(0.64f, 0.08f, 0.08f);
}

RichTextDocument GalleryApp::ApplySyntaxHighlighting(const std::wstring& code) {
    RichTextDocument doc;
    
    size_t pos = 0;
    while (pos < code.size()) {
        // Line comment: // ...
        if (pos + 1 < code.size() && code[pos] == L'/' && code[pos + 1] == L'/') {
            size_t end = code.find(L'\n', pos);
            if (end == std::wstring::npos) end = code.size();
            
            TextRun run;
            run.text = code.substr(pos, end - pos);
            run.fontSize = 13.0f;
            run.color = kCommentColor;
            run.weight = DWRITE_FONT_WEIGHT_NORMAL;
            doc.AppendRun(run);
            pos = end;
            continue;
        }
        
        // String literal: "..." or L"..."
        if (code[pos] == L'L' && pos + 1 < code.size() && code[pos + 1] == L'"') {
            pos++; // skip L prefix
        }
        if (code[pos] == L'"') {
            size_t end = pos + 1;
            while (end < code.size() && code[end] != L'"') {
                if (code[end] == L'\\' && end + 1 < code.size()) end += 2;
                else end++;
            }
            if (end < code.size()) end++; // include closing quote
            
            TextRun run;
            run.text = code.substr(pos, end - pos);
            run.fontSize = 13.0f;
            run.color = kStringColor;
            run.weight = DWRITE_FONT_WEIGHT_NORMAL;
            doc.AppendRun(run);
            pos = end;
            continue;
        }
        
        // Identifier or keyword
        if (std::iswalpha(code[pos]) || code[pos] == L'_') {
            size_t end = pos;
            while (end < code.size() && (std::iswalnum(code[end]) || code[end] == L'_')) {
                end++;
            }
            
            std::wstring word = code.substr(pos, end - pos);
            TextRun run;
            run.text = word;
            run.fontSize = 13.0f;
            run.color = kCppKeywords.count(word) ? kKeywordColor : kDefaultColor;
            run.weight = DWRITE_FONT_WEIGHT_NORMAL;
            doc.AppendRun(run);
            pos = end;
            continue;
        }
        
        // Default: single character (whitespace, operators, etc.)
        TextRun run;
        run.text = code.substr(pos, 1);
        run.fontSize = 13.0f;
        run.color = kDefaultColor;
        run.weight = DWRITE_FONT_WEIGHT_NORMAL;
        doc.AppendRun(run);
        pos++;
    }
    
    return doc;
}

} // namespace fluent
