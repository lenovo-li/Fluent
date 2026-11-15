// InfoBarMetricPages.cpp — standalone showcase page for InfoBar (the Metric control it once shared this file with has been removed).
//
// Each page follows the same three-part shape, because a control gallery has three
// different readers:
//   1. STATES — every visual variant side by side, so a designer can compare them.
//   2. BEHAVIOUR — the interactive part actually working, so a developer can see what an
//      event does rather than read that it exists.
//   3. IN CONTEXT — the control combined with others the way a real page uses it. A
//      control shown alone hides the things that only go wrong in combination
//      (alignment against neighbours, height in a shared row, colour against a card).
//
// Part 3 is the one usually missing from galleries and the one that catches real bugs:
// the clipped Metric line that started this work was invisible until four of them sat in
// one row.

#include "../GalleryMain.h"
#include "../../FluentUI/window/NativeWindowHost.h"

#include "../../FluentUI/controls/InfoBar.h"
#include "../../FluentUI/controls/Button.h"
#include "../../FluentUI/controls/TextBlock.h"
#include "../../FluentUI/controls/TextBox.h"
#include "../../FluentUI/controls/CheckBox.h"
#include "../../FluentUI/controls/Slider.h"
#include "../../FluentUI/controls/ProgressBar.h"
#include "../../FluentUI/layout/StackPanel.h"
#include "../../FluentUI/layout/Grid.h"
#include "../../FluentUI/layout/Border.h"
#include "../../FluentUI/layout/ScrollPanel.h"
#include "../../FluentUI/layout/GroupBox.h"

#include <cstdio>
#include <string>

namespace fluent {

namespace {

// Four severities in the order a reader scans them: neutral -> good -> caution -> bad.
struct SeverityRow {
    InfoBar::Severity severity;
    const wchar_t* title;
    const wchar_t* message;
};

const SeverityRow kSeverities[] = {
    {InfoBar::Severity::Informational, L"Informational",
     L"缓存已建立，本地占用 128 MB。这一级用于陈述事实，不要求用户做任何事。"},
    {InfoBar::Severity::Success, L"Success",
     L"回测完成，共 240 个交易日、37 笔交易。用于确认一个操作真的做成了。"},
    {InfoBar::Severity::Warning,
     L"Warning", L"同行分类只覆盖约 2980 只股票，科创板与北交所查不到行业，"
                 L"这部分标的的相对估值会缺失。用于「能继续，但结果有折扣」。"},
    {InfoBar::Severity::Error, L"Error",
     L"自由现金流为负，两阶段 DCF 无法给出有意义的结果，已拒绝估值。"
     L"用于「这条路走不通」，而不是用于普通的校验失败。"},
};

std::wstring Fmt2(double v) {
    wchar_t buf[48];
    std::swprintf(buf, 48, L"%.2f", v);
    return buf;
}

}  // namespace

// ---------------------------------------------------------------------------
// InfoBar
// ---------------------------------------------------------------------------

std::unique_ptr<ScrollPanel> GalleryApp::CreateInfoBarPage() {
    auto [page, content] = CreatePageShell(L"InfoBar");

    // --- 1. States -------------------------------------------------------
    {
        auto* card = CreateExampleCard(content, L"四个严重级别");
        auto* intro = card->Add(std::make_unique<TextBlock>());
        intro->SetText(L"级别决定颜色，而颜色来自主题的 severity* token —— 控件里没有任何"
                       L"硬编码色值。这一点是它存在的理由：警告底色写死 0xFFF4CE 在深色"
                       L"模式下会看不见。切换主题（顶栏按钮）可以验证四种级别都跟着变。");
        intro->SetWrap(true);
        intro->SetDimmed(true);
        intro->SetMargin(Thickness{0, 0, 0, 8.0f});

        for (const SeverityRow& row : kSeverities) {
            auto* bar = card->Add(std::make_unique<InfoBar>());
            bar->SetSeverity(row.severity);
            bar->SetTitle(row.title);
            bar->SetMessage(row.message);
        }
    }

    // --- 2. Shapes -------------------------------------------------------
    {
        auto* card = CreateExampleCard(content, L"三种形态：仅消息 / 带标题 / 可关闭");

        auto* messageOnly = card->Add(std::make_unique<InfoBar>());
        messageOnly->SetSeverity(InfoBar::Severity::Informational);
        messageOnly->SetMessage(L"只有消息，没有标题 —— 一行通知的常见形态，"
                                L"此时不会为标题预留任何纵向空间。");

        auto* titled = card->Add(std::make_unique<InfoBar>());
        titled->SetSeverity(InfoBar::Severity::Informational);
        titled->SetTitle(L"有标题");
        titled->SetMessage(L"标题加粗，占一行；正文在下面换行。");

        auto* closable = card->Add(std::make_unique<InfoBar>());
        closable->SetSeverity(InfoBar::Severity::Warning);
        closable->SetTitle(L"可关闭");
        closable->SetMessage(L"右上角有关闭按钮。点它只会触发 Closed 事件 —— "
                             L"控件不会自己隐藏，因为「关掉之后怎么办」（移除、折叠、"
                             L"记住选择）每个应用不一样。下面那行文字会记录点击。");
        closable->SetClosable(true);

        auto* log = card->Add(std::make_unique<TextBlock>());
        log->SetText(L"（还没点关闭）");
        log->SetDimmed(true);
        infoBarCloseLog_ = log;

        // The handler proves the point made in the message above: the bar stays.
        infoBarSub_ = closable->Closed().Subscribe(this,
            [](void* owner, InfoBar& bar, RoutedEventArgs&) {
                auto* self = static_cast<GalleryApp*>(owner);
                ++self->infoBarCloseCount_;
                if (self->infoBarCloseLog_) {
                    self->infoBarCloseLog_->SetText(
                        L"Closed 已触发 " + std::to_wstring(self->infoBarCloseCount_) +
                        L" 次，而 InfoBar 仍在原位 —— 隐藏它是调用者的决定。");
                }
                (void)bar;
            });
    }

    // --- 3. Wrapping -----------------------------------------------------
    {
        auto* card = CreateExampleCard(content, L"高度随内容走（拖动滑块改变宽度）");
        auto* hint = card->Add(std::make_unique<TextBlock>());
        hint->SetText(L"消息在可用宽度处换行，控件上报换行后需要的高度，"
                      L"所以窄列里会变高而不是被裁掉。调用者不需要猜高度。");
        hint->SetWrap(true);
        hint->SetDimmed(true);

        auto* holder = card->Add(std::make_unique<Border>());
        holder->SetBorderThickness(1.0f);
        holder->SetCornerRadius(4.0f);
        holder->SetPadding(Thickness{8.0f});
        holder->SetHAlign(HAlign::Left);
        holder->SetWidth(560.0f);

        auto* bar = holder->SetChild(std::make_unique<InfoBar>());
        bar->SetSeverity(InfoBar::Severity::Informational);
        bar->SetTitle(L"宽度决定高度");
        bar->SetMessage(L"本页所有数字都是进程内生成的合成数据，用于验证 FluentUI "
                        L"能否表达这类数据分析界面。把宽度调窄，这段文字会占更多行，"
                        L"InfoBar 会跟着变高。");

        auto* slider = card->Add(std::make_unique<Slider>());
        slider->SetMin(220.0f);
        slider->SetMax(760.0f);
        slider->SetValue(560.0f);
        infoBarWidthSub_ = slider->ValueChanged().Subscribe(holder,
            [](void* owner, Slider& s, float& v) {
                static_cast<Border*>(owner)->SetWidth(v);
                (void)s;
            });
    }

    // --- 4. In context ---------------------------------------------------
    {
        auto* card = CreateExampleCard(content, L"混合使用：表单校验");
        auto* hint = card->Add(std::make_unique<TextBlock>());
        hint->SetText(L"InfoBar 的实际位置几乎总是在别的控件旁边。这里它给一个表单做"
                      L"内联反馈 —— 留空提交是 Error，填了内容提交是 Success。");
        hint->SetWrap(true);
        hint->SetDimmed(true);

        auto* form = card->Add(std::make_unique<Grid>());
        form->AddRow(GridLength::Auto());
        form->AddColumn(GridLength::Pixels(96.0f));
        form->AddColumn(GridLength::Star(1.0f));
        form->AddColumn(GridLength::Auto());

        auto* label = form->Add(std::make_unique<TextBlock>());
        form->SetCell(label, 0, 0);
        label->SetText(L"股票代码");
        label->SetVAlign(VAlign::Center);

        auto* input = form->Add(std::make_unique<TextBox>());
        form->SetCell(input, 0, 1);
        input->SetPlaceholder(L"例如 600519");
        input->SetMargin(Thickness{0, 0, 8.0f, 0});

        auto* submit = form->Add(std::make_unique<Button>());
        form->SetCell(submit, 0, 2);
        submit->SetText(L"查询");
        submit->SetKind(Button::Kind::Accent);

        auto* feedback = card->Add(std::make_unique<InfoBar>());
        feedback->SetSeverity(InfoBar::Severity::Informational);
        feedback->SetMessage(L"填入代码后点「查询」，这条会变成 Success 或 Error。");

        formInput_ = input;
        formFeedback_ = feedback;
        formSubmitSub_ = submit->Click().Subscribe(this,
            [](void* owner, Button&, RoutedEventArgs&) {
                auto* self = static_cast<GalleryApp*>(owner);
                if (!self->formInput_ || !self->formFeedback_) return;
                const std::wstring code = self->formInput_->Text();
                if (code.empty()) {
                    self->formFeedback_->SetSeverity(InfoBar::Severity::Error);
                    self->formFeedback_->SetTitle(L"代码不能为空");
                    self->formFeedback_->SetMessage(L"请输入 6 位股票代码。");
                } else {
                    self->formFeedback_->SetSeverity(InfoBar::Severity::Success);
                    self->formFeedback_->SetTitle(L"已提交");
                    self->formFeedback_->SetMessage(
                        L"查询 " + code + L"（演示，不会真的联网取数）。");
                }
            });
    }

    CreateCodeExample(content, LR"(auto* bar = panel->Add(std::make_unique<InfoBar>());
bar->SetSeverity(InfoBar::Severity::Warning);
bar->SetTitle(L"数据覆盖不全");
bar->SetMessage(L"科创板与北交所查不到行业分类。");
bar->SetClosable(true);

// Closed 只是通知；隐藏/移除由调用者决定
sub_ = bar->Closed().Subscribe(this, [](void* o, InfoBar& b, RoutedEventArgs&) {
    b.SetVisible(false);          // 这一行是应用的选择，控件不会自己做
});)");

    return std::move(page);
}

}  // namespace fluent
