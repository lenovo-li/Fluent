// TextAreaLineHeightProbe.cpp — TEMPORARY diagnostic, not a regression test.
// Prints the DWrite line metrics (height / baseline) of the TextArea demo strings,
// laid out with the SAME format TextArea uses, before and after typing 's'.

#include "../framework/Test.h"
#include "../../FluentUI/graphics/DWriteContext.h"
#include <windows.h>
#include <dwrite.h>
#include <wrl/client.h>
#include <cstdio>
#include <string>

using namespace fluent;

namespace {

void DumpLine(DWriteContext& dw, IDWriteTextFormat* fmt, const wchar_t* label,
              const std::wstring& text) {
    Microsoft::WRL::ComPtr<IDWriteTextLayout> layout;
    if (FAILED(dw.Factory()->CreateTextLayout(text.c_str(),
                                              static_cast<UINT32>(text.size()), fmt,
                                              1e5f, 1e5f, layout.GetAddressOf()))) {
        std::printf("  %-22ls CreateTextLayout FAILED\n", label);
        return;
    }
    DWRITE_LINE_METRICS lm[4]{};
    UINT32 n = 0;
    layout->GetLineMetrics(lm, 4, &n);
    DWRITE_TEXT_METRICS tm{};
    layout->GetMetrics(&tm);
    std::printf("  %-22ls height=%.4f baseline=%.4f textMetrics.height=%.4f\n",
                label, n ? lm[0].height : -1.0f, n ? lm[0].baseline : -1.0f, tm.height);
}

} // namespace

TEST(ZProbe, TextAreaLineMetrics) {
    DWriteContext dw;
    EXPECT_EQ(S_OK, dw.Initialize());

    for (float size : {12.0f, 16.0f}) {
        IDWriteTextFormat* fmt = dw.Format(size, DWRITE_FONT_WEIGHT_NORMAL,
                                           DWRITE_TEXT_ALIGNMENT_LEADING,
                                           DWRITE_PARAGRAPH_ALIGNMENT_NEAR,
                                           DWRITE_WORD_WRAPPING_WRAP);
        EXPECT_TRUE(fmt != nullptr);
        std::printf("\n=== font size %.0f DIP ===\n", size);
        DumpLine(dw, fmt, L"empty", L"");
        DumpLine(dw, fmt, L"probe Ag", L"Ag");
        DumpLine(dw, fmt, L"line1 (mixed)", L"字号 12pt 的多行文本编辑器。");
        DumpLine(dw, fmt, L"line2 (CJK only)", L"支持多行输入。");
        DumpLine(dw, fmt, L"line2 + s", L"支持多行输入。s");
        DumpLine(dw, fmt, L"line3 (mixed)", L"Ctrl+Z 撤销，Ctrl+Y 重做。");
        DumpLine(dw, fmt, L"line3 + s", L"Ctrl+Z 撤销，Ctrl+Y 重做。s");
    }
    EXPECT_TRUE(true);
}
