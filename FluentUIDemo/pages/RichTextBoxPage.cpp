// RichTextBoxPage.cpp — RichTextBox 展示页（nav 409）
//
// RichTextBox 基础编辑的 demo：证明样式套用生效、光标打字正常、删除和剪贴板基本可用。
// 三个按钮分别修改当前样式（粗体、红色、大字号），后续打字用新样式；三个按钮
// 对选区套用样式；复制粘贴和撤销/重做按钮证明这些功能走通。视觉细节（光标闪烁
// 流畅度、选区高亮颜色）只能在真机检查，headless 测试覆盖不到。
#include "../GalleryMain.h"
#include "../../FluentUI/controls/RichTextBox.h"
#include "../../FluentUI/controls/TextBlock.h"
#include "../../FluentUI/controls/Button.h"
#include "../../FluentUI/layout/StackPanel.h"
#include "../../FluentUI/layout/ScrollPanel.h"
#include <string>

namespace fluent {

std::unique_ptr<ScrollPanel> GalleryApp::CreateRichTextBoxPage() {
    auto [page, content] = CreatePageShell(L"RichTextBox");

    auto* intro = content->Add(std::make_unique<TextBlock>());
    intro->SetText(L"可编辑的富文本框。支持行内混合样式（颜色、字号、粗细、下划线），"
                   L"光标、选区、剪贴板、撤销/重做。每段文本都是 DirectWrite 排版，"
                   L"所以字号和样式可以任意混合，基线自动对齐。");
    intro->SetFontSize(13.0f);
    intro->SetMargin(Thickness{0, 0, 0, 20.0f});

    // --- 卡片 1：基础编辑与样式套用 -------------------------------------
    auto* card1 = CreateExampleCard(content, L"基础编辑：打字、样式、剪贴板");

    auto* d1 = card1->Add(std::make_unique<TextBlock>());
    d1->SetText(L"点击文本框获得焦点，开始打字。前三个按钮修改「当前样式」，后续打的字"
                L"用新样式；后三个按钮对选中文字套用样式。Ctrl+C/V 复制粘贴，Ctrl+Z/Y "
                L"撤销/重做。");
    d1->SetFontSize(12.0f);
    d1->SetMargin(Thickness{0, 0, 0, 10.0f});

    auto* richBox = card1->Add(std::make_unique<RichTextBox>());
    richBox->SetWidth(720.0f);
    richBox->SetHeight(180.0f);
    richBox->SetMargin(Thickness{0, 0, 0, 10.0f});

    // 初始内容：混合样式的欢迎语。
    //
    // 正文用近黑色而不是浅灰：RichTextBox 画在 controlFillDefault 上（浅色主题下
    // 接近白），浅灰文字在上面几乎看不见。这里刻意不跟主题令牌走，因为 demo 要的
    // 是「几段不同样式并排」的可见对比，而不是主题正确性。
    constexpr D2D1_COLOR_F kInk  {0.10f, 0.10f, 0.12f, 1.0f};
    constexpr D2D1_COLOR_F kBlue {0.05f, 0.45f, 0.85f, 1.0f};

    RichTextDocument doc;
    TextRun welcome;
    welcome.text = L"欢迎使用 ";
    welcome.color = kInk;
    welcome.fontSize = 14.0f;
    welcome.weight = DWRITE_FONT_WEIGHT_NORMAL;
    doc.AppendRun(welcome);

    TextRun brand;
    brand.text = L"FluentUI";
    brand.color = kBlue;
    brand.fontSize = 16.0f;
    brand.weight = DWRITE_FONT_WEIGHT_BOLD;
    doc.AppendRun(brand);

    TextRun rest;
    rest.text = L" RichTextBox！在下方打字、选中、修改样式试试。";
    rest.color = kInk;
    rest.fontSize = 14.0f;
    rest.weight = DWRITE_FONT_WEIGHT_NORMAL;
    doc.AppendRun(rest);

    richBox->SetDocument(std::move(doc));

    // 两排按钮：第一排修改 currentStyle_，第二排对选区套用
    auto* styleRow = card1->Add(std::make_unique<StackPanel>());
    styleRow->SetOrientation(StackPanel::Orientation::Horizontal);
    styleRow->SetSpacing(10.0f);
    styleRow->SetMargin(Thickness{0, 10.0f, 0, 0});

    auto* boldBtn = styleRow->Add(std::make_unique<Button>());
    boldBtn->SetText(L"粗体（后续打字）");

    auto* redBtn = styleRow->Add(std::make_unique<Button>());
    redBtn->SetText(L"红色（后续打字）");

    auto* bigBtn = styleRow->Add(std::make_unique<Button>());
    bigBtn->SetText(L"大字号（后续打字）");

    auto* actionRow = card1->Add(std::make_unique<StackPanel>());
    actionRow->SetOrientation(StackPanel::Orientation::Horizontal);
    actionRow->SetSpacing(10.0f);
    actionRow->SetMargin(Thickness{0, 10.0f, 0, 0});

    auto* boldSelBtn = actionRow->Add(std::make_unique<Button>());
    boldSelBtn->SetText(L"选区加粗");

    auto* redSelBtn = actionRow->Add(std::make_unique<Button>());
    redSelBtn->SetText(L"选区变红");

    auto* bigSelBtn = actionRow->Add(std::make_unique<Button>());
    bigSelBtn->SetText(L"选区放大");

    // 订阅按钮事件
    pageSubs_ += boldBtn->Click().Subscribe(richBox,
        [](void* owner, Button&, RoutedEventArgs&) {
            auto* box = static_cast<RichTextBox*>(owner);
            TextRun style = box->GetCurrentStyle();
            style.weight = DWRITE_FONT_WEIGHT_BOLD;
            box->SetCurrentStyle(style);
        });

    pageSubs_ += redBtn->Click().Subscribe(richBox,
        [](void* owner, Button&, RoutedEventArgs&) {
            auto* box = static_cast<RichTextBox*>(owner);
            TextRun style = box->GetCurrentStyle();
            style.color = D2D1::ColorF(0.94f, 0.35f, 0.35f);
            box->SetCurrentStyle(style);
        });

    pageSubs_ += bigBtn->Click().Subscribe(richBox,
        [](void* owner, Button&, RoutedEventArgs&) {
            auto* box = static_cast<RichTextBox*>(owner);
            TextRun style = box->GetCurrentStyle();
            style.fontSize = 18.0f;
            box->SetCurrentStyle(style);
        });

    pageSubs_ += boldSelBtn->Click().Subscribe(richBox,
        [](void* owner, Button&, RoutedEventArgs&) {
            auto* box = static_cast<RichTextBox*>(owner);
            box->SetSelectionBold(true);
        });

    pageSubs_ += redSelBtn->Click().Subscribe(richBox,
        [](void* owner, Button&, RoutedEventArgs&) {
            auto* box = static_cast<RichTextBox*>(owner);
            constexpr D2D1_COLOR_F red{0.94f, 0.35f, 0.35f, 1.0f};
            box->SetSelectionColor(red);
        });

    pageSubs_ += bigSelBtn->Click().Subscribe(richBox,
        [](void* owner, Button&, RoutedEventArgs&) {
            auto* box = static_cast<RichTextBox*>(owner);
            box->SetSelectionFontSize(18.0f);
        });

    auto* note = card1->Add(std::make_unique<TextBlock>());
    note->SetText(L"选区套用按钮在有选区时修改选中文字的样式，无选区时修改 currentStyle_。"
                  L"试试：全选（Ctrl+A）→ 选区加粗 → 文字全部变粗；或者选中某几个字 → "
                  L"选区变红 → 只有选中部分变红。");
    note->SetFontSize(11.0f);
    note->SetMargin(Thickness{0, 10.0f, 0, 0});

    // --- 卡片 2：只改一个属性，其余保留 ---------------------------------
    //
    // 这张卡片存在的理由是区分两套 API：SetSelectionXxx 只动一个属性，
    // ApplyStyle(TextRun) 会把整段压成同一个样式。用一段本身就多色多字号的文字
    // 才能看出差别 —— 单色文字上两者看起来一样。
    auto* card2 = CreateExampleCard(content, L"逐属性改样式：加粗不会丢掉颜色和字号");

    auto* d2 = card2->Add(std::make_unique<TextBlock>());
    d2->SetText(L"下面这行本身就有三种颜色和三种字号。全选后点「只加粗」，颜色和字号"
                L"应当全部保留；点「整段压平」则全部变成同一个样式 —— 这就是 "
                L"SetSelectionBold() 与 ApplyStyle(TextRun) 的区别。");
    d2->SetFontSize(12.0f);
    d2->SetMargin(Thickness{0, 0, 0, 10.0f});

    auto* mixBox = card2->Add(std::make_unique<RichTextBox>());
    mixBox->SetWidth(720.0f);
    mixBox->SetHeight(120.0f);
    mixBox->SetMargin(Thickness{0, 0, 0, 10.0f});

    {
        constexpr D2D1_COLOR_F kRed  {0.80f, 0.20f, 0.20f, 1.0f};
        constexpr D2D1_COLOR_F kGreen{0.10f, 0.55f, 0.25f, 1.0f};
        constexpr D2D1_COLOR_F kNavy {0.15f, 0.25f, 0.65f, 1.0f};

        RichTextDocument mix;
        mix.AppendRun(TextRun(L"红色 12pt ", kRed, 12.0f));
        mix.AppendRun(TextRun(L"绿色 18pt ", kGreen, 18.0f));
        mix.AppendRun(TextRun(L"深蓝 14pt 带下划线", kNavy, 14.0f,
                              DWRITE_FONT_WEIGHT_NORMAL, /*underline=*/true));
        mixBox->SetDocument(std::move(mix));
    }

    auto* mixRow = card2->Add(std::make_unique<StackPanel>());
    mixRow->SetOrientation(StackPanel::Orientation::Horizontal);
    mixRow->SetSpacing(10.0f);
    mixRow->SetMargin(Thickness{0, 10.0f, 0, 0});

    auto* selectAllBtn = mixRow->Add(std::make_unique<Button>());
    selectAllBtn->SetText(L"全选");

    auto* onlyBoldBtn = mixRow->Add(std::make_unique<Button>());
    onlyBoldBtn->SetText(L"只加粗");

    auto* underlineBtn = mixRow->Add(std::make_unique<Button>());
    underlineBtn->SetText(L"切下划线");

    auto* flattenBtn = mixRow->Add(std::make_unique<Button>());
    flattenBtn->SetText(L"整段压平");

    pageSubs_ += selectAllBtn->Click().Subscribe(mixBox,
        [](void* owner, Button&, RoutedEventArgs&) {
            static_cast<RichTextBox*>(owner)->SelectAll();
        });

    pageSubs_ += onlyBoldBtn->Click().Subscribe(mixBox,
        [](void* owner, Button&, RoutedEventArgs&) {
            static_cast<RichTextBox*>(owner)->SetSelectionBold(true);
        });

    pageSubs_ += underlineBtn->Click().Subscribe(mixBox,
        [](void* owner, Button&, RoutedEventArgs&) {
            auto* box = static_cast<RichTextBox*>(owner);
            // 以选区起点的当前状态取反，这样按钮是个开关而不是单向设置。
            const bool on = box->GetCurrentStyle().underline;
            box->SetSelectionUnderline(!on);
        });

    pageSubs_ += flattenBtn->Click().Subscribe(mixBox,
        [](void* owner, Button&, RoutedEventArgs&) {
            auto* box = static_cast<RichTextBox*>(owner);
            TextRun uniform;
            uniform.color = D2D1_COLOR_F{0.10f, 0.10f, 0.12f, 1.0f};
            uniform.fontSize = 14.0f;
            uniform.weight = DWRITE_FONT_WEIGHT_NORMAL;
            uniform.underline = false;
            box->ApplyStyle(uniform);
        });

    auto* n2 = card2->Add(std::make_unique<TextBlock>());
    n2->SetText(L"键盘导航也在这里试：Home/End 到行首行尾，Ctrl+Home/End 到全文首尾，"
                L"上下方向键跨行且保持水平位置，Shift+方向键扩选。Ctrl+Z 可以撤销"
                L"上面任何一次样式修改。");
    n2->SetFontSize(11.0f);
    n2->SetMargin(Thickness{0, 8.0f, 0, 0});

    // --- 卡片 3：换行开关与大文本性能 -----------------------------------
    auto* card3 = CreateExampleCard(content, L"换行与滚动");

    auto* d3 = card3->Add(std::make_unique<TextBlock>());
    d3->SetText(L"Wrap 模式（默认）：文字到右边界自动换行，只有竖直滚动条。"
                L"NoWrap 模式：文字不换行，出现水平滚动条。生成 10k 字文本测试性能。");
    d3->SetFontSize(12.0f);
    d3->SetMargin(Thickness{0, 0, 0, 10.0f});

    auto* richBox3 = card3->Add(std::make_unique<RichTextBox>());
    richBox3->SetWidth(720.0f);
    richBox3->SetHeight(240.0f);
    richBox3->SetMargin(Thickness{0, 0, 0, 10.0f});

    {
        RichTextDocument doc3;
        doc3.AppendRun(TextRun(L"切换换行模式，观察滚动条变化。按「生成 10k 字」后，"
                               L"Wrap 下只有竖直滚动条，NoWrap 下每段变成一整行，"
                               L"底部出现水平滚动条。",
                               kInk, 14.0f));
        richBox3->SetDocument(std::move(doc3));
    }

    auto* btnRow3 = card3->Add(std::make_unique<StackPanel>());
    btnRow3->SetOrientation(StackPanel::Orientation::Horizontal);
    btnRow3->SetSpacing(8.0f);
    btnRow3->SetMargin(Thickness{0, 0, 0, 10.0f});

    auto* btnWrap = btnRow3->Add(std::make_unique<Button>());
    btnWrap->SetText(L"Wrap（换行）");

    auto* btnNoWrap = btnRow3->Add(std::make_unique<Button>());
    btnNoWrap->SetText(L"NoWrap（不换行）");

    auto* btn10k = btnRow3->Add(std::make_unique<Button>());
    btn10k->SetText(L"生成 10k 字");

    pageSubs_ += btnWrap->Click().Subscribe(richBox3,
        [](void* owner, Button&, RoutedEventArgs&) {
            static_cast<RichTextBox*>(owner)->SetWordWrap(true);
        });

    pageSubs_ += btnNoWrap->Click().Subscribe(richBox3,
        [](void* owner, Button&, RoutedEventArgs&) {
            static_cast<RichTextBox*>(owner)->SetWordWrap(false);
        });

    pageSubs_ += btn10k->Click().Subscribe(richBox3,
        [](void* owner, Button&, RoutedEventArgs&) {
            // 10k 字，50 段 × 约 200 字。两种颜色交替，让每段都是独立的 run ——
            // 单色文本会被 MergeAdjacentRuns 合成一个 run，那样测不出多 run 的成本。
            constexpr D2D1_COLOR_F c1{0.10f, 0.10f, 0.12f, 1.0f};
            constexpr D2D1_COLOR_F c2{0.70f, 0.20f, 0.10f, 1.0f};

            RichTextDocument big;
            for (int i = 0; i < 50; ++i) {
                std::wstring chunk = L"第" + std::to_wstring(i + 1) + L"段：";
                chunk.append(190, L'字');
                chunk += L"\n";
                big.AppendRun(TextRun(std::move(chunk), (i % 2 == 0) ? c1 : c2, 14.0f));
            }
            static_cast<RichTextBox*>(owner)->SetDocument(std::move(big));
        });

    // --- 卡片 4：只读模式 ------------------------------------------------
    auto* card4 = CreateExampleCard(content, L"只读模式：能选能复制，不能改");

    auto* d4 = card4->Add(std::make_unique<TextBlock>());
    d4->SetText(L"只读模式下打字、Backspace/Delete、粘贴、撤销/重做、对选区套用样式全部"
                L"无效；选择、Ctrl+C 复制、滚动、双击选词、三击选行照常可用。Ctrl+X 退化"
                L"成 Ctrl+C（复制但不删），因为静默什么都不做会被误读成剪贴板坏了。");
    d4->SetFontSize(12.0f);
    d4->SetMargin(Thickness{0, 0, 0, 10.0f});

    auto* richBox4 = card4->Add(std::make_unique<RichTextBox>());
    richBox4->SetWidth(720.0f);
    richBox4->SetHeight(150.0f);
    richBox4->SetReadOnly(true);
    richBox4->SetMargin(Thickness{0, 0, 0, 10.0f});

    {
        RichTextDocument doc4;
        doc4.AppendRun(TextRun(L"这段文本是只读的。", kInk, 15.0f, DWRITE_FONT_WEIGHT_SEMI_BOLD));
        doc4.AppendRun(TextRun(L"试着打字或按 Delete，内容不会变；"
                               L"但可以双击选中一个词、三击选中整行，再按 Ctrl+C 复制出去。\n\n",
                               kInk, 14.0f));
        doc4.AppendRun(TextRun(L"中文分词验证：", kInk, 14.0f, DWRITE_FONT_WEIGHT_SEMI_BOLD));
        doc4.AppendRun(TextRun(L"你好，世界。双击「你好」只会选中这两个字，"
                               L"不会跨过标点连到「世界」——中日韩标点现在是词边界。",
                               kInk, 14.0f));
        richBox4->SetDocument(std::move(doc4));
    }

    auto* btnRow4 = card4->Add(std::make_unique<StackPanel>());
    btnRow4->SetOrientation(StackPanel::Orientation::Horizontal);
    btnRow4->SetSpacing(8.0f);

    auto* btnToggleRO = btnRow4->Add(std::make_unique<Button>());
    btnToggleRO->SetText(L"切换只读（当前：只读）");

    pageSubs_ += btnToggleRO->Click().Subscribe(richBox4,
        [](void* owner, Button& btn, RoutedEventArgs&) {
            auto* box = static_cast<RichTextBox*>(owner);
            const bool next = !box->ReadOnly();
            box->SetReadOnly(next);
            btn.SetText(next ? L"切换只读（当前：只读）" : L"切换只读（当前：可编辑）");
        });

    // --- 卡片 5：段落格式 ------------------------------------------------
    auto* card5 = CreateExampleCard(content, L"段落格式：对齐、行距、缩进");

    auto* d5 = card5->Add(std::make_unique<TextBlock>());
    d5->SetText(L"对齐、行距、段间距、缩进都是段落级属性。光标在某段时，按钮读写那段的格式；"
                L"选区跨段时，操作覆盖所有涉及的段落。缩进按钮演示首行缩进（第一行向右偏移）"
                L"和悬挂缩进（第一行向左、其余行向右，用于编号列表）。");
    d5->SetFontSize(12.0f);
    d5->SetMargin(Thickness{0, 0, 0, 10.0f});

    auto* richBox5 = card5->Add(std::make_unique<RichTextBox>());
    richBox5->SetWidth(720.0f);
    richBox5->SetHeight(280.0f);
    richBox5->SetMargin(Thickness{0, 0, 0, 10.0f});

    {
        RichTextDocument doc5;
        doc5.AppendRun(TextRun(L"第一段：默认左对齐、单倍行高、无缩进。这段故意写得长一点，"
                               L"让它自动换行成多行，这样才能看出行距和缩进的效果。"
                               L"试试点击下面的按钮修改对齐方式和缩进。\n\n",
                               kInk, 14.0f));
        doc5.AppendRun(TextRun(L"第二段：这段稍短一些。每段的格式是独立的，光标在哪段，按钮"
                               L"就读写哪段的格式。\n\n",
                               kInk, 14.0f));
        doc5.AppendRun(TextRun(L"第三段：用这段试「居中」和「右对齐」，能清楚看出整段向中间或"
                               L"右侧偏移。悬挂缩进在这里特别明显：第一行向左突出，后续行向右缩进。",
                               kInk, 14.0f));
        richBox5->SetDocument(std::move(doc5));
    }

    // 第一排：对齐
    auto* alignRow = card5->Add(std::make_unique<StackPanel>());
    alignRow->SetOrientation(StackPanel::Orientation::Horizontal);
    alignRow->SetSpacing(8.0f);
    alignRow->SetMargin(Thickness{0, 0, 0, 8.0f});

    auto* labelAlign = alignRow->Add(std::make_unique<TextBlock>());
    labelAlign->SetText(L"对齐：");
    labelAlign->SetFontSize(12.0f);

    auto* btnAlignLeft = alignRow->Add(std::make_unique<Button>());
    btnAlignLeft->SetText(L"左对齐");

    auto* btnAlignCenter = alignRow->Add(std::make_unique<Button>());
    btnAlignCenter->SetText(L"居中");

    auto* btnAlignRight = alignRow->Add(std::make_unique<Button>());
    btnAlignRight->SetText(L"右对齐");

    pageSubs_ += btnAlignLeft->Click().Subscribe(richBox5,
        [](void* owner, Button&, RoutedEventArgs&) {
            static_cast<RichTextBox*>(owner)->SetParagraphAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);
        });

    pageSubs_ += btnAlignCenter->Click().Subscribe(richBox5,
        [](void* owner, Button&, RoutedEventArgs&) {
            static_cast<RichTextBox*>(owner)->SetParagraphAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
        });

    pageSubs_ += btnAlignRight->Click().Subscribe(richBox5,
        [](void* owner, Button&, RoutedEventArgs&) {
            static_cast<RichTextBox*>(owner)->SetParagraphAlignment(DWRITE_TEXT_ALIGNMENT_TRAILING);
        });

    // 第二排：行距
    auto* lineSpacingRow = card5->Add(std::make_unique<StackPanel>());
    lineSpacingRow->SetOrientation(StackPanel::Orientation::Horizontal);
    lineSpacingRow->SetSpacing(8.0f);
    lineSpacingRow->SetMargin(Thickness{0, 0, 0, 8.0f});

    auto* labelLS = lineSpacingRow->Add(std::make_unique<TextBlock>());
    labelLS->SetText(L"行距：");
    labelLS->SetFontSize(12.0f);

    auto* btnLS1 = lineSpacingRow->Add(std::make_unique<Button>());
    btnLS1->SetText(L"单倍");

    auto* btnLS15 = lineSpacingRow->Add(std::make_unique<Button>());
    btnLS15->SetText(L"1.5 倍");

    auto* btnLS2 = lineSpacingRow->Add(std::make_unique<Button>());
    btnLS2->SetText(L"2 倍");

    auto* btnLSFixed = lineSpacingRow->Add(std::make_unique<Button>());
    btnLSFixed->SetText(L"固定 30pt");

    pageSubs_ += btnLS1->Click().Subscribe(richBox5,
        [](void* owner, Button&, RoutedEventArgs&) {
            static_cast<RichTextBox*>(owner)->SetParagraphLineSpacing(1.0f);
        });

    pageSubs_ += btnLS15->Click().Subscribe(richBox5,
        [](void* owner, Button&, RoutedEventArgs&) {
            static_cast<RichTextBox*>(owner)->SetParagraphLineSpacing(1.5f);
        });

    pageSubs_ += btnLS2->Click().Subscribe(richBox5,
        [](void* owner, Button&, RoutedEventArgs&) {
            static_cast<RichTextBox*>(owner)->SetParagraphLineSpacing(2.0f);
        });

    pageSubs_ += btnLSFixed->Click().Subscribe(richBox5,
        [](void* owner, Button&, RoutedEventArgs&) {
            static_cast<RichTextBox*>(owner)->SetParagraphLineHeight(30.0f);
        });

    // 第三排：段间距
    auto* spacingRow = card5->Add(std::make_unique<StackPanel>());
    spacingRow->SetOrientation(StackPanel::Orientation::Horizontal);
    spacingRow->SetSpacing(8.0f);
    spacingRow->SetMargin(Thickness{0, 0, 0, 8.0f});

    auto* labelSpacing = spacingRow->Add(std::make_unique<TextBlock>());
    labelSpacing->SetText(L"段间距：");
    labelSpacing->SetFontSize(12.0f);

    auto* btnSpacingNone = spacingRow->Add(std::make_unique<Button>());
    btnSpacingNone->SetText(L"无间距");

    auto* btnSpacingBefore = spacingRow->Add(std::make_unique<Button>());
    btnSpacingBefore->SetText(L"段前 15pt");

    auto* btnSpacingAfter = spacingRow->Add(std::make_unique<Button>());
    btnSpacingAfter->SetText(L"段后 15pt");

    pageSubs_ += btnSpacingNone->Click().Subscribe(richBox5,
        [](void* owner, Button&, RoutedEventArgs&) {
            auto* box = static_cast<RichTextBox*>(owner);
            box->SetParagraphSpaceBefore(0.0f);
            box->SetParagraphSpaceAfter(0.0f);
        });

    pageSubs_ += btnSpacingBefore->Click().Subscribe(richBox5,
        [](void* owner, Button&, RoutedEventArgs&) {
            static_cast<RichTextBox*>(owner)->SetParagraphSpaceBefore(15.0f);
        });

    pageSubs_ += btnSpacingAfter->Click().Subscribe(richBox5,
        [](void* owner, Button&, RoutedEventArgs&) {
            static_cast<RichTextBox*>(owner)->SetParagraphSpaceAfter(15.0f);
        });

    // 第四排：缩进
    auto* indentRow = card5->Add(std::make_unique<StackPanel>());
    indentRow->SetOrientation(StackPanel::Orientation::Horizontal);
    indentRow->SetSpacing(8.0f);

    auto* labelIndent = indentRow->Add(std::make_unique<TextBlock>());
    labelIndent->SetText(L"缩进：");
    labelIndent->SetFontSize(12.0f);

    auto* btnIndentNone = indentRow->Add(std::make_unique<Button>());
    btnIndentNone->SetText(L"无缩进");

    auto* btnFirstLine = indentRow->Add(std::make_unique<Button>());
    btnFirstLine->SetText(L"首行缩进 2 字符");

    auto* btnHanging = indentRow->Add(std::make_unique<Button>());
    btnHanging->SetText(L"悬挂缩进");

    auto* btnLeftRight = indentRow->Add(std::make_unique<Button>());
    btnLeftRight->SetText(L"左右各缩进 40pt");

    pageSubs_ += btnIndentNone->Click().Subscribe(richBox5,
        [](void* owner, Button&, RoutedEventArgs&) {
            auto* box = static_cast<RichTextBox*>(owner);
            box->SetParagraphFirstLineIndent(0.0f);
            box->SetParagraphLeftIndent(0.0f);
            box->SetParagraphRightIndent(0.0f);
        });

    pageSubs_ += btnFirstLine->Click().Subscribe(richBox5,
        [](void* owner, Button&, RoutedEventArgs&) {
            auto* box = static_cast<RichTextBox*>(owner);
            // 中文 14pt 字符宽度约 14 DIP，2 字符 ≈ 28 DIP
            box->SetParagraphFirstLineIndent(28.0f);
            box->SetParagraphLeftIndent(0.0f);
            box->SetParagraphRightIndent(0.0f);
        });

    pageSubs_ += btnHanging->Click().Subscribe(richBox5,
        [](void* owner, Button&, RoutedEventArgs&) {
            auto* box = static_cast<RichTextBox*>(owner);
            // 悬挂缩进：leftIndent 40，firstLineIndent -20 → 第一行向左突出 20，其余行右移 40
            box->SetParagraphLeftIndent(40.0f);
            box->SetParagraphFirstLineIndent(-20.0f);
            box->SetParagraphRightIndent(0.0f);
        });

    pageSubs_ += btnLeftRight->Click().Subscribe(richBox5,
        [](void* owner, Button&, RoutedEventArgs&) {
            auto* box = static_cast<RichTextBox*>(owner);
            box->SetParagraphFirstLineIndent(0.0f);
            box->SetParagraphLeftIndent(40.0f);
            box->SetParagraphRightIndent(40.0f);
        });

    return std::move(page);
}

}  // namespace fluent
