// BigButton.cpp — Simplified implementation
#include "CustomControls.h"

namespace fluent::custom {

void BigButton::Measure(float availW, float availH) {
    (void)availH;
    float w = IsAuto(Width()) ? std::min(availW, 200.0f) : Width();
    SetDesired(SizeDip(w, 64.0f));
}

void BigButton::RenderContent(const DrawingContext& dc) {
    // Just draw the main text for now - simplified version
    DWriteContext* dw = Dwrite();
    if (!dw) return;
    
    RectDip bounds = Bounds();
    D2D1_COLOR_F textColor = ButtonTextColor(CurrentKind(), State(),
                                             AppearanceForRender(),
                                             Theme().colors);
    
    std::wstring mainText = Text();
    if (!mainText.empty()) {
        if (auto* fmt = dw->Format(14.0f, DWRITE_FONT_WEIGHT_SEMI_BOLD,
                                   DWRITE_TEXT_ALIGNMENT_CENTER,
                                   DWRITE_PARAGRAPH_ALIGNMENT_CENTER)) {
            D2D1_RECT_F rect = D2D1::RectF(bounds.x, bounds.y, 
                                           bounds.right(), bounds.bottom());
            dc.DrawText(mainText.c_str(), static_cast<UINT32>(mainText.size()),
                       fmt, rect, textColor);
        }
    }
}

} // namespace fluent::custom
