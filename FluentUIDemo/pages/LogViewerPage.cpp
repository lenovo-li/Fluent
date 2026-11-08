// LogViewerPage.cpp — LogViewer 展示页（nav 408）
//
// 这一页要回答的问题是「行内混合样式到底长什么样」——这是 LogViewer 存在的理由，
// 也是唯一无法在 headless 测试里验证的部分（每个 span 都要走真实的 DWrite）。
// 四张卡片依次覆盖：行内多样式、动态行高、实时追尾、以及规模。
#include "../GalleryMain.h"
#include "../../FluentUI/controls/LogViewer.h"
#include "../../FluentUI/controls/TextBlock.h"
#include "../../FluentUI/controls/Button.h"
#include "../../FluentUI/layout/StackPanel.h"
#include "../../FluentUI/layout/ScrollPanel.h"
#include <string>

namespace fluent {

namespace {

// 一组固定的日志配色。刻意不走主题令牌：日志的语义颜色（红=错、黄=警）在浅色和
// 深色下含义相同，跟着主题反转会把 ERROR 变成看不出是 ERROR 的颜色。
constexpr D2D1_COLOR_F kDim   {0.55f, 0.57f, 0.60f, 1.0f};  // 时间戳、括号
constexpr D2D1_COLOR_F kText  {0.85f, 0.87f, 0.90f, 1.0f};  // 正文
constexpr D2D1_COLOR_F kRed   {0.94f, 0.35f, 0.35f, 1.0f};  // ERROR
constexpr D2D1_COLOR_F kAmber {0.98f, 0.75f, 0.20f, 1.0f};  // WARN
constexpr D2D1_COLOR_F kGreen {0.40f, 0.85f, 0.45f, 1.0f};  // OK / INFO
constexpr D2D1_COLOR_F kCyan  {0.35f, 0.80f, 0.92f, 1.0f};  // 地址、数值

// 终端底色。LogViewer 用 EffectiveBackground()，所以 SetBackground 直接生效。
constexpr D2D1_COLOR_F kConsole{0.08f, 0.09f, 0.11f, 1.0f};

// 每行一条固定格式的记录：时间戳 + 级别 + 正文 + 尾部数值。级别一段单独着色并加粗，
// 尾部数值另一种颜色，正文第三种 —— 这就是「一行内任意分段」的实际样子。
void AddRecord(LogViewer* log, const wchar_t* time, const wchar_t* level,
               D2D1_COLOR_F levelColor, const wchar_t* body,
               const wchar_t* tail = nullptr) {
    std::vector<TextSpan> spans;
    spans.emplace_back(time, kDim, 12.0f);
    spans.emplace_back(L"  ", kDim, 12.0f);
    spans.emplace_back(level, levelColor, 12.0f, DWRITE_FONT_WEIGHT_BOLD);
    spans.emplace_back(L"  ", kDim, 12.0f);
    spans.emplace_back(body, kText, 12.0f);
    if (tail)
        spans.emplace_back(tail, kCyan, 12.0f);
    log->AppendLine(std::move(spans));
}

} // namespace

std::unique_ptr<ScrollPanel> GalleryApp::CreateLogViewerPage() {
    auto [page, content] = CreatePageShell(L"LogViewer");

    auto* intro = content->Add(std::make_unique<TextBlock>());
    intro->SetText(L"只读的虚拟化日志视图。每行由若干 TextSpan 组成，每个 span 独立设置"
                   L"颜色、字号、粗细和下划线；行高取该行最大字号，所以大字会把行撑高。"
                   L"绘制与命中测试都只覆盖可见行，行数可以到百万级。");
    intro->SetFontSize(13.0f);
    intro->SetMargin(Thickness{0, 0, 0, 20.0f});

    // --- 卡片 0：主题自适应 ----------------------------------------------
    auto* card0 = CreateExampleCard(content, L"主题自适应（不设置背景）");

    auto* d0 = card0->Add(std::make_unique<TextBlock>());
    d0->SetText(L"不调用 SetBackground，背景和滚动条自动跟随主题的 layerFill 和 textPrimary。"
                L"点击右上角主题切换按钮（月亮图标），观察背景、滚动条和文本颜色一起变化。");
    d0->SetFontSize(12.0f);
    d0->SetMargin(Thickness{0, 0, 0, 10.0f});

    auto* autoLog = card0->Add(std::make_unique<LogViewer>());
    autoLog->SetWidth(720.0f);
    autoLog->SetHeight(180.0f);
    // 不调用 SetBackground(kConsole)，让它跟随主题
    autoLog->SetAutoScrollToTail(false);

    // 使用默认颜色（sentinel）的 span，跟随主题的 textPrimary
    autoLog->AppendLine({{L"[INFO] ", kGreen}, {L"背景跟随主题 layerFill"}});
    autoLog->AppendLine({{L"[INFO] ", kGreen}, {L"滚动条根据背景亮度自动选色"}});
    autoLog->AppendLine({{L"点击右上角月亮图标切换主题，观察背景和滚动条变化"}});
    autoLog->AppendLine({{L"[OK] ", kGreen}, {L"显式颜色（绿色）不受主题影响"}});
    autoLog->AppendLine({{L"默认颜色的文本在亮色主题下接近黑色，暗色主题下接近白色"}});

    auto* cacheBtn = card0->Add(std::make_unique<Button>());
    cacheBtn->SetText(L"追加几条演示行");
    cacheBtn->SetMargin(Thickness{0, 10.0f, 0, 0});

    pageSubs_ += cacheBtn->Click().Subscribe(autoLog,
        [](void* owner, Button&, RoutedEventArgs&) {
            auto* log = static_cast<LogViewer*>(owner);
            log->AppendLine({{L"[DEBUG] ", kCyan}, {L"这行用默认颜色（跟随主题）"}});
            log->AppendLine({{L"[WARN] ", kAmber}, {L"警告级别用固定黄色"}});
            log->AppendLine({{L"[ERROR] ", kRed}, {L"错误级别用固定红色"}});
        });

    // --- 卡片 1：行内多样式 ---------------------------------------------
    auto* card1 = CreateExampleCard(content, L"行内多样式：一行中分段着色与加粗");

    auto* d1 = card1->Add(std::make_unique<TextBlock>());
    d1->SetText(L"每条记录被切成 5-6 个 span：灰色时间戳、彩色加粗级别、正文、青色数值。"
                L"这些都在同一行里，不是每行一种样式。");
    d1->SetFontSize(12.0f);
    d1->SetMargin(Thickness{0, 0, 0, 10.0f});

    auto* log1 = card1->Add(std::make_unique<LogViewer>());
    log1->SetWidth(720.0f);
    log1->SetHeight(200.0f);
    log1->SetBackground(kConsole);
    log1->SetAutoScrollToTail(false);

    AddRecord(log1, L"14:23:01.204", L"INFO ", kGreen, L"服务启动，监听端口 ", L"8080");
    AddRecord(log1, L"14:23:01.318", L"INFO ", kGreen, L"已加载插件 ", L"ReportGenerator 2.3.1");
    AddRecord(log1, L"14:23:04.771", L"WARN ", kAmber, L"缓存命中率偏低 ", L"38.2%");
    AddRecord(log1, L"14:23:07.005", L"ERROR", kRed,   L"数据库连接超时 ", L"10.0.0.17:3306");
    AddRecord(log1, L"14:23:07.006", L"ERROR", kRed,   L"重试 1/3，退避 ", L"250ms");
    AddRecord(log1, L"14:23:07.262", L"INFO ", kGreen, L"连接恢复，耗时 ", L"256ms");
    AddRecord(log1, L"14:23:11.480", L"DEBUG", kCyan,  L"GC 暂停 ", L"15ms / 回收 256MB");
    AddRecord(log1, L"14:23:19.933", L"WARN ", kAmber, L"内存使用率 ", L"78%");

    auto* n1 = card1->Add(std::make_unique<TextBlock>());
    n1->SetText(L"这台 viewer 可以拖选文字：按住左键横跨几行拖动，Ctrl+C 复制，Esc 取消，"
                L"Ctrl+A 全选。跨行复制的结果用 \\n 连接，段与段之间不插入任何分隔符 —— "
                L"span 只是着色单位，不是文本单位。");
    n1->SetFontSize(11.0f);
    n1->SetMargin(Thickness{0, 8.0f, 0, 0});

    auto* selButtons = card1->Add(std::make_unique<StackPanel>());
    selButtons->SetOrientation(StackPanel::Orientation::Horizontal);
    selButtons->SetSpacing(10.0f);
    selButtons->SetMargin(Thickness{0, 10.0f, 0, 0});

    auto* selectAllBtn = selButtons->Add(std::make_unique<Button>());
    selectAllBtn->SetText(L"全选");

    auto* copyBtn = selButtons->Add(std::make_unique<Button>());
    copyBtn->SetText(L"复制选中");

    auto* clearSelBtn = selButtons->Add(std::make_unique<Button>());
    clearSelBtn->SetText(L"取消选择");

    // 复制结果回显：证明剪贴板里到底是什么，而不是只相信「已复制」的提示。
    auto* copyEcho = card1->Add(std::make_unique<TextBlock>());
    copyEcho->SetText(L"（尚未复制）");
    copyEcho->SetFontSize(11.0f);
    copyEcho->SetMargin(Thickness{0, 10.0f, 0, 0});

    pageSubs_ += selectAllBtn->Click().Subscribe(log1,
        [](void* owner, Button&, RoutedEventArgs&) {
            static_cast<LogViewer*>(owner)->SelectAll();
        });

    pageSubs_ += clearSelBtn->Click().Subscribe(log1,
        [](void* owner, Button&, RoutedEventArgs&) {
            static_cast<LogViewer*>(owner)->ClearSelection();
        });

    // 这个订阅要同时用到 viewer 和回显 TextBlock，单个 owner 指针装不下两个对象，
    // 所以把配对存成页面成员再传它的地址。
    logSelectionEcho_ = {log1, copyEcho};
    pageSubs_ += copyBtn->Click().Subscribe(&logSelectionEcho_,
        [](void* owner, Button&, RoutedEventArgs&) {
            auto* pair = static_cast<LogSelectionEcho*>(owner);
            const std::wstring sel = pair->viewer->SelectedText();
            if (sel.empty()) {
                pair->echo->SetText(L"没有选中任何文字。");
                return;
            }
            const bool ok = pair->viewer->CopySelectionToClipboard();
            // 把换行显示成可见的 \n，否则回显本身会换行，看不出复制到几行。
            std::wstring shown;
            for (wchar_t c : sel)
                if (c == L'\n') shown += L"\\n"; else shown += c;
            if (shown.size() > 120) shown = shown.substr(0, 120) + L"…";
            pair->echo->SetText((ok ? L"已复制 " : L"复制失败（剪贴板被占用）：")
                                + std::to_wstring(sel.size()) + L" 个字符：" + shown);
        });

    // --- 卡片 2：动态行高 ------------------------------------------------
    auto* card2 = CreateExampleCard(content, L"动态行高：大字号撑高所在行");

    auto* d2 = card2->Add(std::make_unique<TextBlock>());
    d2->SetText(L"行高 = 该行最大字号 × 1.2。10pt 的行紧凑，24pt 的标题行自动变高，"
                L"下面的行随之下移 —— 不是所有行共用一个固定高度。");
    d2->SetFontSize(12.0f);
    d2->SetMargin(Thickness{0, 0, 0, 10.0f});

    auto* log2 = card2->Add(std::make_unique<LogViewer>());
    log2->SetWidth(720.0f);
    log2->SetHeight(230.0f);
    log2->SetBackground(kConsole);
    log2->SetAutoScrollToTail(false);

    log2->AppendLine({{L"小字 10pt：", kDim, 10.0f}, {L"routine heartbeat ok", kText, 10.0f}});
    log2->AppendLine({{L"正文 12pt：", kDim, 12.0f}, {L"handled request /api/v1/users", kText, 12.0f}});
    log2->AppendLine({{L"BUILD FAILED", kRed, 24.0f, DWRITE_FONT_WEIGHT_BOLD}});
    log2->AppendLine({{L"小字 10pt：", kDim, 10.0f}, {L"上面那行 24pt，本行回到 10pt", kText, 10.0f}});
    log2->AppendLine({{L"混合一行：", kDim, 11.0f},
                      {L"小 ", kText, 10.0f},
                      {L"中 ", kText, 14.0f},
                      {L"大", kAmber, 20.0f, DWRITE_FONT_WEIGHT_BOLD}});
    log2->AppendLine({{L"下划线：", kDim, 12.0f},
                      {L"https://example.com/build/4471", kCyan, 12.0f,
                       DWRITE_FONT_WEIGHT_NORMAL, /*underline=*/true}});
    log2->AppendLine({{L"粗细对比：", kDim, 12.0f},
                      {L"NORMAL ", kText, 12.0f, DWRITE_FONT_WEIGHT_NORMAL},
                      {L"SEMIBOLD ", kText, 12.0f, DWRITE_FONT_WEIGHT_SEMI_BOLD},
                      {L"BOLD", kText, 12.0f, DWRITE_FONT_WEIGHT_BOLD}});

    auto* n2 = card2->Add(std::make_unique<TextBlock>());
    n2->SetText(L"注意「BUILD FAILED」那一行明显比邻行高，而同一行内的小/中/大三段共享一条基线。");
    n2->SetFontSize(11.0f);
    n2->SetMargin(Thickness{0, 8.0f, 0, 0});

    // --- 卡片 3：追尾 + 环形缓冲 -----------------------------------------
    auto* card3 = CreateExampleCard(content, L"实时追尾与环形缓冲");

    auto* d3 = card3->Add(std::make_unique<TextBlock>());
    d3->SetText(L"追尾开启时新行到达即滚到底部；向上滚开会自动关闭追尾（滚回底部再打开）。"
                L"这台 viewer 的 MaxLines 设为 200，超出后最老的行被丢弃，视野内文字不跳。");
    d3->SetFontSize(12.0f);
    d3->SetMargin(Thickness{0, 0, 0, 10.0f});

    auto* log3 = card3->Add(std::make_unique<LogViewer>());
    log3->SetWidth(720.0f);
    log3->SetHeight(180.0f);
    log3->SetBackground(kConsole);
    log3->SetMaxLines(200);
    log3->SetAutoScrollToTail(true);

    AddRecord(log3, L"14:30:00.000", L"INFO ", kGreen, L"等待连接…");

    auto* buttons = card3->Add(std::make_unique<StackPanel>());
    buttons->SetOrientation(StackPanel::Orientation::Horizontal);
    buttons->SetSpacing(10.0f);
    buttons->SetMargin(Thickness{0, 12.0f, 0, 0});

    auto* addOne = buttons->Add(std::make_unique<Button>());
    addOne->SetText(L"追加 1 行");

    auto* addBurst = buttons->Add(std::make_unique<Button>());
    addBurst->SetText(L"追加 500 行");

    auto* clearBtn = buttons->Add(std::make_unique<Button>());
    clearBtn->SetText(L"清空");

    // Subscribe 返回 Subscription 且是 [[nodiscard]] —— 丢掉返回值等于立刻退订，
    // 按钮会变成死的。pageSubs_ 是 GalleryApp 的 SubscriptionBag，生命期覆盖整页。
    pageSubs_ += addOne->Click().Subscribe(log3,
        [](void* owner, Button&, RoutedEventArgs&) {
            auto* log = static_cast<LogViewer*>(owner);
            const size_t n = log->LineCount();
            AddRecord(log, L"14:30:0?.???", L"INFO ", kGreen,
                      L"手动追加的记录 #", std::to_wstring(n).c_str());
        });

    pageSubs_ += addBurst->Click().Subscribe(log3,
        [](void* owner, Button&, RoutedEventArgs&) {
            auto* log = static_cast<LogViewer*>(owner);
            for (int i = 0; i < 500; ++i) {
                const bool warn = (i % 50) == 0;
                log->AppendLine({
                    {L"burst  ", kDim, 12.0f},
                    {warn ? L"WARN " : L"INFO ", warn ? kAmber : kGreen, 12.0f,
                     DWRITE_FONT_WEIGHT_BOLD},
                    {L"  batch record ", kText, 12.0f},
                    {std::to_wstring(i), kCyan, 12.0f},
                });
            }
        });

    pageSubs_ += clearBtn->Click().Subscribe(log3,
        [](void* owner, Button&, RoutedEventArgs&) {
            static_cast<LogViewer*>(owner)->Clear();
        });

    auto* n3 = card3->Add(std::make_unique<TextBlock>());
    n3->SetText(L"「追加 500 行」按下后行数应稳定在 200（环形缓冲上限），且视图停在底部。"
                L"先向上滚几行再按，追尾应保持关闭、视野不被拽走。");
    n3->SetFontSize(11.0f);
    n3->SetMargin(Thickness{0, 8.0f, 0, 0});

    // --- 卡片 4：规模 ----------------------------------------------------
    auto* card4 = CreateExampleCard(content, L"规模：5 万行");

    auto* d4 = card4->Add(std::make_unique<TextBlock>());
    d4->SetText(L"5 万行、每行 4 个 span。滚动时只有可见的十几行被绘制，"
                L"拖动滚动条到任意位置都应即时响应。按 F12 看 CPU Max。");
    d4->SetFontSize(12.0f);
    d4->SetMargin(Thickness{0, 0, 0, 10.0f});

    auto* log4 = card4->Add(std::make_unique<LogViewer>());
    log4->SetWidth(720.0f);
    log4->SetHeight(260.0f);
    log4->SetBackground(kConsole);
    log4->SetAutoScrollToTail(false);

    static const wchar_t* kBodies[] = {
        L"handled GET /api/v1/users  ",
        L"cache miss, falling back to db  ",
        L"session refreshed  ",
        L"background job tick  ",
        L"websocket frame received  ",
    };
    static const wchar_t* kLevels[] = {L"INFO ", L"DEBUG", L"WARN ", L"ERROR"};
    static const D2D1_COLOR_F kLevelColors[] = {kGreen, kCyan, kAmber, kRed};

    for (int i = 0; i < 50000; ++i) {
        const int lv = (i % 97 == 0) ? 3 : (i % 13 == 0) ? 2 : (i % 3 == 0) ? 1 : 0;
        log4->AppendLine({
            {std::to_wstring(i + 1), kDim, 11.0f},
            {L"  ", kDim, 11.0f},
            {kLevels[lv], kLevelColors[lv], 12.0f, DWRITE_FONT_WEIGHT_BOLD},
            {L"  ", kDim, 12.0f},
            {kBodies[i % 5], kText, 12.0f},
            {std::to_wstring(i % 400) + L"ms", kCyan, 12.0f},
        });
    }

    auto* n4 = card4->Add(std::make_unique<TextBlock>());
    n4->SetText(L"已知取舍：滚动进从未绘制过的区域时，滚动条会轻微调整后稳定 —— "
                L"未测量的行用估算高度，首次绘制才换成实测值。与 TextArea 的 Wrap 模式同源。");
    n4->SetFontSize(11.0f);
    n4->SetMargin(Thickness{0, 8.0f, 0, 0});

    // --- 卡片 5：滚动条可见性（不同背景色） --------------------------------
    auto* card5 = CreateExampleCard(content, L"滚动条可见性测试");

    auto* d5 = card5->Add(std::make_unique<TextBlock>());
    d5->SetText(L"验证白色滚动条在各种深色背景上都可见。每个 LogViewer 1000 行。");
    d5->SetFontSize(12.0f);
    d5->SetMargin(Thickness{0, 0, 0, 10.0f});

    // 绿色背景
    auto* logGreen = card5->Add(std::make_unique<LogViewer>());
    logGreen->SetWidth(720.0f);
    logGreen->SetHeight(120.0f);
    logGreen->SetBackground(D2D1::ColorF(0.05f, 0.2f, 0.08f));  // 深绿
    for (int i = 0; i < 1000; ++i) {
        logGreen->AppendLine({
            {L"Line ", kText, 12.0f},
            {std::to_wstring(i + 1), kGreen, 12.0f},
            {L" on green background", kText, 12.0f},
        });
    }

    auto* spacer1 = card5->Add(std::make_unique<TextBlock>());
    spacer1->SetMargin(Thickness{0, 8.0f, 0, 0});

    // 深蓝背景
    auto* logBlue = card5->Add(std::make_unique<LogViewer>());
    logBlue->SetWidth(720.0f);
    logBlue->SetHeight(120.0f);
    logBlue->SetBackground(D2D1::ColorF(0.02f, 0.05f, 0.15f));  // 深蓝
    for (int i = 0; i < 1000; ++i) {
        logBlue->AppendLine({
            {L"Line ", kText, 12.0f},
            {std::to_wstring(i + 1), kCyan, 12.0f},
            {L" on blue background", kText, 12.0f},
        });
    }

    return std::move(page);
}

} // namespace fluent
