// CustomControlsPage.cpp — 用户扩展控件演示
#include "../GalleryMain.h"
#include "../../extensions/CustomControls/CustomControls.h"
#include "../../extensions/CustomControls/SpinnerControl.h"
#include "../../FluentUI/layout/StackPanel.h"
#include "../../FluentUI/layout/WrapPanel.h"
#include "../../FluentUI/layout/ScrollPanel.h"
#include "../../FluentUI/controls/TextBlock.h"
#include "../../FluentUI/controls/Button.h"

namespace fluent {

using namespace fluent::custom;

std::unique_ptr<ScrollPanel> GalleryApp::CreateCustomControlsPage() {
    auto [page, content] = CreatePageShell(L"Custom Controls");

    // SpinnerControl
    auto title1 = content->Add(std::make_unique<TextBlock>());
    title1->SetText(L"SpinnerControl (Fully Custom)");
    title1->SetFontSize(20.0f);
    title1->SetMargin(Thickness{0, 0, 0, 8.0f});

    auto desc1 = content->Add(std::make_unique<TextBlock>());
    desc1->SetText(L"Built from Control base. AnimatedValue + WantsAnimationTick for smooth rotation.");
    desc1->SetMargin(Thickness{0, 0, 0, 12.0f});

    auto* card1 = CreateExampleCard(content, L"Rotating Spinners");
    auto wrap1 = card1->Add(std::make_unique<WrapPanel>());
    // WrapPanel uses fixed spacing, no SetSpacing method

    for (int i = 0; i < 3; ++i) {
        auto spinner = std::make_unique<SpinnerControl>();
        spinner->SetDiameter(32.0f + i * 16.0f);
        spinner->SetSpeed(0.8f + i * 0.4f);
        spinner->SetMargin(Thickness{8.0f});
        wrap1->Add(std::move(spinner));
    }

    CreateCodeExample(content, LR"(auto spinner = std::make_unique<SpinnerControl>();
spinner->SetDiameter(48.0f);
spinner->SetSpeed(1.0f);

bool WantsAnimationTick() const override { return running_; }
void OnAnimationTick(float dtSec) override {
    rotation_ += speed_ * 2*pi * dtSec;
    pulse_.Approach(target, dtSec, 0.15f);
    Invalidate();
})");

    // BigButton
    auto title3 = content->Add(std::make_unique<TextBlock>());
    title3->SetText(L"BigButton (Inherits Button)");
    title3->SetFontSize(20.0f);
    title3->SetMargin(Thickness{0, 24.0f, 0, 8.0f});

    auto desc3 = content->Add(std::make_unique<TextBlock>());
    desc3->SetText(L"64dp tall: icon, title and subtitle rows; reads CurrentKind() to sync text colour.");
    desc3->SetMargin(Thickness{0, 0, 0, 12.0f});

    auto* card3 = CreateExampleCard(content, L"Big Button");
    auto bigBtn = card3->Add(std::make_unique<BigButton>());
    bigBtn->SetText(L"Download");
    bigBtn->SetSubtitle(L"1.2 GB available");
    bigBtn->SetIcon(L"⬇");  // downwards arrow
    bigBtn->SetWidth(200.0f);

    // LogSlider
    auto title4 = content->Add(std::make_unique<TextBlock>());
    title4->SetText(L"LogSlider (Inherits Slider)");
    title4->SetFontSize(20.0f);
    title4->SetMargin(Thickness{0, 24.0f, 0, 8.0f});

    auto desc4 = content->Add(std::make_unique<TextBlock>());
    desc4->SetText(L"Snaps to 1, 10, 100, 1000, 10000 (CoerceValue); arrow keys step one decade.");
    desc4->SetMargin(Thickness{0, 0, 0, 12.0f});

    auto* card4 = CreateExampleCard(content, L"Logarithmic Slider");
    auto logSlider = card4->Add(std::make_unique<LogSlider>());
    logSlider->SetWidth(300.0f);
    logSlider->SetValue(100.0f);

    return std::move(page);
}

}  // namespace fluent
