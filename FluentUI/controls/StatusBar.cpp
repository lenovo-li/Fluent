// StatusBar.cpp
#include "StatusBar.h"
#include "../controls/TextBlock.h"
#include "../styling/ThemeTokens.h"
#include "../graphics/DrawingContext.h"
#include <algorithm>

namespace fluent {

namespace {
constexpr float kHeight = 32.0f;
constexpr float kPadX = 12.0f;
}  // namespace

StatusBar::StatusBar() {
    SetHeight(kHeight);
    SetVAlign(VAlign::Bottom);

    // Left text block.
    // Caption size, secondary color: status text is ambient information, and at body
    // size in primary color it competed with the page content above it.
    auto* text = Emplace<TextBlock>();
    text->SetVAlign(VAlign::Center);
    text->SetHAlign(HAlign::Left);
    text->SetTypographyRole(TypographyRole::Caption);
    text->SetDimmed(true);
    text_ = text;
}

void StatusBar::SetText(std::wstring text) {
    textStr_ = text;
    if (text_) text_->SetText(std::move(text));
}

void StatusBar::SetRightContent(std::unique_ptr<FrameworkElement> content) {
    if (rightContent_) {
        // Find and remove the old content from the panel's child list so it is
        // destroyed (not just detached). Panel has no Remove API, so we scan
        // children_ directly.
        for (auto it = children_.begin(); it != children_.end(); ++it) {
            if (it->get() == rightContent_) {
                if ((*it)->IsAttached()) (*it)->DetachFromContext();
                (*it)->SetParent(nullptr);
                children_.erase(it);
                break;
            }
        }
        rightContent_ = nullptr;
    }
    if (content) {
        rightContent_ = content.get();
        Add(std::move(content));
    }
    InvalidateMeasure();
}

SizeDip StatusBar::MeasureOverride(float availW, float availH) {
    UNREFERENCED_PARAMETER(availH);
    float textW = 0.0f;
    if (text_) {
        text_->MeasureCached(availW, kHeight);
        textW = text_->Desired().w;
    }
    float rightW = 0.0f;
    if (rightContent_) {
        rightContent_->MeasureCached(availW, kHeight);
        rightW = rightContent_->Desired().w;
    }
    return SizeDip{std::max(textW + rightW + kPadX * 3.0f, availW), kHeight};
}

void StatusBar::ArrangeOverride(const RectDip& content) {
    // Left text: takes the remaining space after the right content.
    float rightW = 0.0f;
    if (rightContent_) rightW = rightContent_->Desired().w;

    if (text_) {
        // Inset by kPadX. This used to be a Margin on the TextBlock, but this panel
        // arranges its children itself and never read margins, so the text sat flush
        // against the left edge.
        const float textW = std::max(0.0f, content.w - rightW - kPadX * 3.0f);
        // Center using the text's OWN desired height, not the full strip height:
        // TextBlock draws its glyphs top-aligned within its bounds, so handing it
        // the full kHeight left the single line of text sitting at the top of the
        // strip instead of centered in it.
        const float th = text_->Desired().h;
        const float ty = content.y + (content.h - th) * 0.5f;
        text_->Arrange(RectDip{content.x + kPadX, ty, textW, th});
    }

    if (rightContent_) {
        const float x = content.x + content.w - rightW - kPadX;
        const float rh = rightContent_->Desired().h;
        const float ry = content.y + (content.h - rh) * 0.5f;
        rightContent_->Arrange(RectDip{x, ry, rightW, rh});
    }
}

void StatusBar::OnRenderBackground(const DrawingContext& dc) {
    // No fill of its own: the strip belongs to the surface it sits on (window Mica,
    // a card), separated from the content above by one divider line — the Win11
    // status-strip look. It used to paint an opaque controlFillDefault slab with
    // rounded corners, which read as a stray button-like block, and inside a
    // rounded container its square bottom corners poked out past the container's
    // curve. A host that wants a distinct band puts the StatusBar in a Border with
    // that background.
    dc.FillRect(D2D1::RectF(bounds_.x, bounds_.y, bounds_.right(), bounds_.y + 1.0f),
                Theme().colors.controlStrokeDefault);
}

} // namespace fluent
