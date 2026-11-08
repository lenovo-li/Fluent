// FrameworkFeaturesPage.cpp — 框架功能演示页（800 组）
//
// 每页演示一个跨控件的框架能力，而不是某个具体控件的 API。
// 这类能力挂在 UIElement 上，任何控件都能用。
#include "../GalleryMain.h"
#include "../../FluentUI/controls/Button.h"
#include "../../FluentUI/controls/TextBlock.h"
#include "../../FluentUI/controls/TextArea.h"
#include "../../FluentUI/controls/RichTextBox.h"
#include "../../FluentUI/controls/LogViewer.h"
#include "../../FluentUI/controls/Slider.h"
#include "../../FluentUI/controls/CheckBox.h"
#include "../../FluentUI/layout/Border.h"
#include "../../FluentUI/controls/MenuFlyout.h"
#include "../../FluentUI/controls/RichTextDocument.h"
#include "../../FluentUI/layout/StackPanel.h"
#include "../../FluentUI/layout/Grid.h"
#include "../../FluentUI/layout/ScrollPanel.h"

namespace fluent {

// ===========================================================================
// 工具提示（Tooltip）—— nav 801
// ===========================================================================
//
// SetTooltip 挂在 UIElement 上，所有派生控件（Button、Border、Image……）
// 都能用，不限于"有文字"的控件。TooltipService 在鼠标悬停约 500 ms 后弹出
// 一个浮动提示气泡，移走或点击后关闭。

std::unique_ptr<ScrollPanel> GalleryApp::CreateTooltipFeaturePage() {
    auto [page, content] = CreatePageShell(L"工具提示 · Tooltip");

    auto* intro = content->Add(std::make_unique<TextBlock>());
    intro->SetText(
        L"SetTooltip 是 UIElement 的方法，所有控件都能调用。"
        L"TooltipService 在鼠标悬停约 500 ms 后弹出气泡，移走或点击后关闭。");
    intro->SetFontSize(13.0f);
    intro->SetMargin(Thickness{0, 0, 0, 20.0f});

    // --- 卡片 1：按钮提示 -------------------------------------------------
    auto* card1 = CreateExampleCard(content, L"控件提示：悬停查看说明");

    auto* d1 = card1->Add(std::make_unique<TextBlock>());
    d1->SetText(L"把鼠标悬停在下方控件上，约半秒后出现工具提示气泡。");
    d1->SetFontSize(12.0f);
    d1->SetMargin(Thickness{0, 0, 0, 12.0f});

    auto* btnRow = card1->Add(std::make_unique<StackPanel>());
    btnRow->SetOrientation(StackPanel::Orientation::Horizontal);
    btnRow->SetSpacing(12.0f);

    auto* btnSave = btnRow->Add(std::make_unique<Button>());
    btnSave->SetText(L"保存");
    btnSave->SetTooltip(L"保存当前文档（Ctrl+S）");

    auto* btnDelete = btnRow->Add(std::make_unique<Button>());
    btnDelete->SetText(L"删除");
    btnDelete->SetTooltip(L"将选中项移入回收站，此操作不可撤销");

    auto* btnDisabled = btnRow->Add(std::make_unique<Button>());
    btnDisabled->SetText(L"已禁用");
    btnDisabled->SetEnabled(false);
    // 禁用元素因为 HitTest 检查 enabled_，所以不会被命中，工具提示无法显示。
    // 这是框架的设计：禁用元素不响应任何交互，包括悬停。

    // --- 卡片 2：任意元素都可用 -------------------------------------------
    auto* card2 = CreateExampleCard(content, L"任意 UIElement 都可挂工具提示");

    auto* d2 = card2->Add(std::make_unique<TextBlock>());
    d2->SetText(
        L"SetTooltip 是 UIElement 的方法，不限于 Button。"
        L"Border、Image、StackPanel 等都能用。悬停在下方色块上查看效果。");
    d2->SetFontSize(12.0f);
    d2->SetMargin(Thickness{0, 0, 0, 12.0f});

    auto* colorRow = card2->Add(std::make_unique<StackPanel>());
    colorRow->SetOrientation(StackPanel::Orientation::Horizontal);
    colorRow->SetSpacing(10.0f);

    struct Swatch { const wchar_t* tip; D2D1_COLOR_F col; };
    const Swatch swatches[] = {
        {L"主色调：accent\n用于主按钮、选中高亮", {0.0f, 0.47f, 0.90f, 1.0f}},
        {L"成功：severitySuccessFill\n用于 InfoBar 成功状态", {0.27f, 0.72f, 0.33f, 1.0f}},
        {L"警告：severityWarningFill\n用于 InfoBar 警告状态", {0.90f, 0.65f, 0.0f, 1.0f}},
        {L"错误：severityErrorFill\n用于 InfoBar 错误状态",   {0.88f, 0.20f, 0.20f, 1.0f}},
    };
    for (const auto& s : swatches) {
        auto* b = colorRow->Add(std::make_unique<Border>());
        b->SetWidth(64.0f);
        b->SetHeight(32.0f);
        b->SetCornerRadius(4.0f);
        b->SetBackground(s.col);
        b->SetTooltip(s.tip);
    }

    // --- 卡片 3：代码示例 -------------------------------------------------
    auto* card3 = CreateExampleCard(content, L"接入方式");
    CreateCodeExample(card3, LR"(// 任何控件都能直接调用，一行代码
button->SetTooltip(L"保存当前文档（Ctrl+S）");

// Border、Image 等非交互元素也行
border->SetTooltip(L"蓝色代表主题色 accent");

// 清除
button->SetTooltip(L"");)");

    return std::move(page);
}

// ===========================================================================
// 右键菜单（Context Menu）—— nav 802
// ===========================================================================
//
// SetContextMenu(unique_ptr<IContextMenu>) 给任意 UIElement 挂右键菜单。
// MenuFlyout 实现了 IContextMenu，支持多级子菜单、快捷键提示、分隔线和
// checked 状态。右键单击时框架自动弹出菜单，无需控件自己处理鼠标消息。

std::unique_ptr<ScrollPanel> GalleryApp::CreateContextMenuFeaturePage() {
    auto [page, content] = CreatePageShell(L"右键菜单 · Context Menu");

    auto* intro = content->Add(std::make_unique<TextBlock>());
    intro->SetText(
        L"SetContextMenu 是 UIElement 的方法，所有控件都能挂右键菜单。"
        L"MenuFlyout 实现了 IContextMenu 接口，支持子菜单、分隔线和 checked 状态。");
    intro->SetFontSize(13.0f);
    intro->SetMargin(Thickness{0, 0, 0, 20.0f});

    // --- 卡片 1：文本区域右键菜单 -----------------------------------------
    auto* card1 = CreateExampleCard(content, L"TextArea 右键菜单：编辑操作");

    auto* d1 = card1->Add(std::make_unique<TextBlock>());
    d1->SetText(L"在 TextArea 上右键，出现带快捷键提示的编辑菜单。");
    d1->SetFontSize(12.0f);
    d1->SetMargin(Thickness{0, 0, 0, 10.0f});

    auto* ta = card1->Add(std::make_unique<TextArea>());
    ta->SetWidth(520.0f);
    ta->SetHeight(100.0f);
    ta->SetText(L"在这里右键试试看。\n选中一段文字再右键，可以复制。");
    ta->SetWrapMode(TextWrapMode::Wrap);

    // 编辑菜单 —— 使用 unique_ptr 交给控件所有权
    auto editMenu = std::make_unique<MenuFlyout>();
    {
        std::vector<MenuItem> items;
        MenuItem mCut;  mCut.text = L"剪切";  mCut.accelerator = L"Ctrl+X";
        items.push_back(std::move(mCut));
        MenuItem mCopy; mCopy.text = L"复制"; mCopy.accelerator = L"Ctrl+C";
        items.push_back(std::move(mCopy));
        MenuItem mPaste; mPaste.text = L"粘贴"; mPaste.accelerator = L"Ctrl+V";
        items.push_back(std::move(mPaste));
        items.push_back(MenuItem::Sep());
        MenuItem mAll; mAll.text = L"全选"; mAll.accelerator = L"Ctrl+A";
        items.push_back(std::move(mAll));
        editMenu->SetItems(std::move(items));
    }
    ta->SetContextMenu(std::move(editMenu));

    // --- 卡片 2：子菜单和 checked 状态 ------------------------------------
    auto* card2 = CreateExampleCard(content, L"子菜单与 checked 状态");

    auto* d2 = card2->Add(std::make_unique<TextBlock>());
    d2->SetText(L"在下方 Border 上右键，演示三级子菜单和 checked 菜单项。");
    d2->SetFontSize(12.0f);
    d2->SetMargin(Thickness{0, 0, 0, 10.0f});

    // 用一个有点分量的 Border 当目标
    auto* demoBox = card2->Add(std::make_unique<Border>());
    demoBox->SetWidth(320.0f);
    demoBox->SetHeight(72.0f);
    demoBox->SetCornerRadius(6.0f);
    demoBox->SetBorderThickness(1.0f);

    auto* demoLabel = demoBox->SetChild(std::make_unique<TextBlock>());
    demoLabel->SetText(L"在这里右键");
    demoLabel->SetHAlign(HAlign::Center);
    demoLabel->SetVAlign(VAlign::Center);
    demoLabel->SetFontSize(13.0f);

    auto submenuFlyout = std::make_shared<MenuFlyout>();
    {
        std::vector<MenuItem> items;

        // 带 checked 的选项
        MenuItem mA; mA.text = L"选项 A"; mA.checked = true;
        items.push_back(std::move(mA));
        MenuItem mB; mB.text = L"选项 B"; mB.checked = false;
        items.push_back(std::move(mB));
        items.push_back(MenuItem::Sep());

        // 子菜单
        MenuItem mSub; mSub.text = L"视图";
        {
            MenuItem s1; s1.text = L"紧凑";
            MenuItem s2; s2.text = L"标准";
            MenuItem s3; s3.text = L"宽松";
            mSub.submenu.push_back(std::move(s1));
            mSub.submenu.push_back(std::move(s2));
            mSub.submenu.push_back(std::move(s3));
        }
        items.push_back(std::move(mSub));
        items.push_back(MenuItem::Sep());
        MenuItem mExit; mExit.text = L"关闭"; items.push_back(std::move(mExit));
        submenuFlyout->SetItems(std::move(items));
    }
    demoBox->SetContextMenuRef(submenuFlyout.get());
    flyouts_.push_back(submenuFlyout);

    // --- 卡片 3：代码示例 -------------------------------------------------
    auto* card3 = CreateExampleCard(content, L"接入方式");
    CreateCodeExample(card3, LR"(// SetContextMenu：控件获得所有权
auto menu = std::make_unique<MenuFlyout>();
std::vector<MenuItem> items;
MenuItem cut; cut.text = L"剪切"; cut.accelerator = L"Ctrl+X";
items.push_back(std::move(cut));
menu->SetItems(std::move(items));
element->SetContextMenu(std::move(menu));

// SetContextMenuRef：多个控件共享同一个菜单
auto shared = std::make_shared<MenuFlyout>();
// ... 填充 items ...
elementA->SetContextMenuRef(shared.get());
elementB->SetContextMenuRef(shared.get());
flyouts_.push_back(shared);  // Gallery 用 flyouts_ 延长生命期)");

    return std::move(page);
}

// ===========================================================================
// Ctrl+Wheel 缩放（Zoom）—— nav 803
// ===========================================================================
//
// TextArea、RichTextBox、LogViewer 三个控件都实现了同一套缩放契约：
//   · SetZoom(float)  范围 [kMinZoom, kMaxZoom]
//   · Ctrl+Wheel 步进 10%；Ctrl+0 复位到 100%
//   · 缩放是视图放大，不改变文档、应用字号设置或控件的 Measure 尺寸
//   · ZoomChanged() 事件报告新系数
// 这一页把三个控件并排，展示统一的交互和 API。

std::unique_ptr<ScrollPanel> GalleryApp::CreateZoomFeaturePage() {
    auto [page, content] = CreatePageShell(L"Ctrl+Wheel 缩放 · Zoom");

    auto* intro = content->Add(std::make_unique<TextBlock>());
    intro->SetText(
        L"TextArea、RichTextBox、LogViewer 共享同一套缩放契约："
        L"Ctrl+滚轮 ±10%，Ctrl+0 复位。"
        L"缩放是视图放大，不改变文档内容、应用字号设置或控件布局尺寸。");
    intro->SetFontSize(13.0f);
    intro->SetMargin(Thickness{0, 0, 0, 20.0f});

    // --- 卡片 1：三控件并排，统一交互 ------------------------------------
    auto* card1 = CreateExampleCard(content, L"三控件并排：Ctrl+滚轮缩放");

    auto* d1 = card1->Add(std::make_unique<TextBlock>());
    d1->SetText(L"在任意控件上按住 Ctrl 并滚动鼠标滚轮，文字放大或缩小。Ctrl+0 重置。");
    d1->SetFontSize(12.0f);
    d1->SetMargin(Thickness{0, 0, 0, 12.0f});

    auto* triRow = card1->Add(std::make_unique<StackPanel>());
    triRow->SetOrientation(StackPanel::Orientation::Horizontal);
    triRow->SetSpacing(12.0f);

    // -- TextArea --
    auto* taCol = triRow->Add(std::make_unique<StackPanel>());
    taCol->SetOrientation(StackPanel::Orientation::Vertical);
    taCol->SetSpacing(6.0f);

    auto* taLabel = taCol->Add(std::make_unique<TextBlock>());
    taLabel->SetText(L"TextArea");
    taLabel->SetFontSize(12.0f);
    taLabel->SetFontWeight(DWRITE_FONT_WEIGHT_SEMI_BOLD);

    auto* ta = taCol->Add(std::make_unique<TextArea>());
    ta->SetWidth(220.0f);
    ta->SetHeight(140.0f);
    ta->SetWrapMode(TextWrapMode::Wrap);
    ta->SetText(L"Ctrl+滚轮：放大 / 缩小\n"
                L"Ctrl+0：重置为 100%\n\n"
                L"这是一段示例文字，用来演示 TextArea 的 Ctrl+Wheel 缩放效果。"
                L"缩放只影响显示，不改变文档内容。");

    auto* taZoomLabel = taCol->Add(std::make_unique<TextBlock>());
    taZoomLabel->SetText(L"缩放：100%");
    taZoomLabel->SetFontSize(11.0f);

    pageSubs_ += ta->ZoomChanged().Subscribe(taZoomLabel,
        [](void* owner, TextArea&, float& z) {
            auto* lbl = static_cast<TextBlock*>(owner);
            int pct = static_cast<int>(z * 100.0f + 0.5f);
            lbl->SetText(L"缩放：" + std::to_wstring(pct) + L"%");
        });

    // -- RichTextBox --
    auto* rtbCol = triRow->Add(std::make_unique<StackPanel>());
    rtbCol->SetOrientation(StackPanel::Orientation::Vertical);
    rtbCol->SetSpacing(6.0f);

    auto* rtbLabel = rtbCol->Add(std::make_unique<TextBlock>());
    rtbLabel->SetText(L"RichTextBox");
    rtbLabel->SetFontSize(12.0f);
    rtbLabel->SetFontWeight(DWRITE_FONT_WEIGHT_SEMI_BOLD);

    auto* rtb = rtbCol->Add(std::make_unique<RichTextBox>());
    rtb->SetWidth(220.0f);
    rtb->SetHeight(140.0f);
    rtb->SetReadOnly(true);
    {
        RichTextDocument doc;
        doc.AppendRun({L"Ctrl+滚轮放大 / 缩小\n", {0.1f, 0.1f, 0.12f, 1.0f}, 14.0f,
                       DWRITE_FONT_WEIGHT_SEMI_BOLD});
        doc.AppendRun({L"这是一段 RichTextBox 示例文字，支持富文本格式。\n",
                       {0.1f, 0.1f, 0.12f, 1.0f}, 12.0f});
        doc.AppendRun({L"缩放是视图放大，不影响文档字号。",
                       {0.3f, 0.5f, 0.9f, 1.0f}, 12.0f});
        rtb->SetDocument(std::move(doc));
    }

    auto* rtbZoomLabel = rtbCol->Add(std::make_unique<TextBlock>());
    rtbZoomLabel->SetText(L"缩放：100%");
    rtbZoomLabel->SetFontSize(11.0f);

    pageSubs_ += rtb->ZoomChanged().Subscribe(rtbZoomLabel,
        [](void* owner, RichTextBox&, float& z) {
            auto* lbl = static_cast<TextBlock*>(owner);
            int pct = static_cast<int>(z * 100.0f + 0.5f);
            lbl->SetText(L"缩放：" + std::to_wstring(pct) + L"%");
        });

    // -- LogViewer --
    auto* lvCol = triRow->Add(std::make_unique<StackPanel>());
    lvCol->SetOrientation(StackPanel::Orientation::Vertical);
    lvCol->SetSpacing(6.0f);

    auto* lvLabel = lvCol->Add(std::make_unique<TextBlock>());
    lvLabel->SetText(L"LogViewer");
    lvLabel->SetFontSize(12.0f);
    lvLabel->SetFontWeight(DWRITE_FONT_WEIGHT_SEMI_BOLD);

    auto* lv = lvCol->Add(std::make_unique<LogViewer>());
    lv->SetWidth(220.0f);
    lv->SetHeight(140.0f);
    lv->SetAutoScrollToTail(false);
    constexpr D2D1_COLOR_F kGreen{0.40f, 0.85f, 0.45f, 1.0f};
    constexpr D2D1_COLOR_F kAmber{0.98f, 0.75f, 0.20f, 1.0f};
    constexpr D2D1_COLOR_F kRed{0.94f, 0.35f, 0.35f, 1.0f};
    lv->AppendLine({{L"[INFO]  ", kGreen}, {L"服务启动，端口 8080"}});
    lv->AppendLine({{L"[WARN]  ", kAmber}, {L"内存使用率 78%"}});
    lv->AppendLine({{L"[ERROR] ", kRed},   {L"数据库连接超时"}});
    lv->AppendLine({{L"[INFO]  ", kGreen}, {L"重连成功"}});
    lv->AppendLine({{L"[INFO]  ", kGreen}, {L"Ctrl+0 重置缩放"}});

    auto* lvZoomLabel = lvCol->Add(std::make_unique<TextBlock>());
    lvZoomLabel->SetText(L"缩放：100%");
    lvZoomLabel->SetFontSize(11.0f);

    pageSubs_ += lv->ZoomChanged().Subscribe(lvZoomLabel,
        [](void* owner, LogViewer&, float& z) {
            auto* lbl = static_cast<TextBlock*>(owner);
            int pct = static_cast<int>(z * 100.0f + 0.5f);
            lbl->SetText(L"缩放：" + std::to_wstring(pct) + L"%");
        });

    // --- 卡片 2：禁用缩放 ------------------------------------------------
    auto* card2 = CreateExampleCard(content, L"SetZoomEnabled(false)：禁用缩放手势");

    auto* d2 = card2->Add(std::make_unique<TextBlock>());
    d2->SetText(
        L"用 SetZoomEnabled(false) 关掉手势，Ctrl+Wheel 退化为普通滚动，"
        L"SetZoom 仍然可以程序化控制缩放。");
    d2->SetFontSize(12.0f);
    d2->SetMargin(Thickness{0, 0, 0, 10.0f});

    auto* disRow = card2->Add(std::make_unique<StackPanel>());
    disRow->SetOrientation(StackPanel::Orientation::Horizontal);
    disRow->SetSpacing(12.0f);

    auto* ta2 = disRow->Add(std::make_unique<TextArea>());
    ta2->SetWidth(280.0f);
    ta2->SetHeight(100.0f);
    ta2->SetWrapMode(TextWrapMode::Wrap);
    ta2->SetText(L"此控件的 ZoomEnabled = false，\nCtrl+滚轮不会缩放，只会滚动。");
    ta2->SetZoomEnabled(false);

    auto* toggleZoomBtn = disRow->Add(std::make_unique<Button>());
    toggleZoomBtn->SetText(L"启用缩放");
    toggleZoomBtn->SetVAlign(VAlign::Center);
    toggleZoomBtn->SetTooltip(L"切换此 TextArea 的 ZoomEnabled");

    pageSubs_ += toggleZoomBtn->Click().Subscribe(ta2,
        [](void* owner, Button& btn, RoutedEventArgs&) {
            auto* area = static_cast<TextArea*>(owner);
            bool nowOn = !area->ZoomEnabled();
            area->SetZoomEnabled(nowOn);
            btn.SetText(nowOn ? L"禁用缩放" : L"启用缩放");
        });

    // --- 卡片 3：代码示例 ------------------------------------------------
    auto* card3 = CreateExampleCard(content, L"接入方式");
    CreateCodeExample(card3, LR"(// TextArea / RichTextBox / LogViewer 都有同一套接口
ta->SetZoom(1.5f);               // 程序化设为 150%
float z = ta->Zoom();            // 读取当前缩放系数
ta->SetZoomEnabled(false);       // 关闭手势（SetZoom 仍然有效）

// ZoomChanged 事件（payload = 新系数）
sub_ = ta->ZoomChanged().Subscribe(this,
    [](void* o, TextArea&, float& z) {
        static_cast<MyView*>(o)->OnZoomChanged(z);
    });

// 键盘绑定（框架内置，无需手动处理）
//   Ctrl+滚轮上  → SetZoom(zoom * 1.1)
//   Ctrl+滚轮下  → SetZoom(zoom / 1.1)
//   Ctrl+0       → SetZoom(1.0)  （仅在非 100% 时才 handle，让应用快捷键生效）)");

    return std::move(page);
}

} // namespace fluent
