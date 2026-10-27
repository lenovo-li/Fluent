// ScrollPanel.cpp
#include "ScrollPanel.h"
#include "../graphics/DrawingContext.h"
#include "../graphics/PixelSnapping.h"
#include "../core/UIContext.h"
#include "../input/InputManager.h"
#include "../styling/ThemeTokens.h"
#include <d2d1_1.h>
#include <algorithm>
#include <cmath>

namespace fluent {

namespace {
// Width of the vertical rail's strip, reserved from the children while it is live.
constexpr float kRailStrip = 16.0f;
}  // namespace

float ScrollPanel::MeasureChildren(float childAvailW)
{
    // Unbounded height: children report their natural size.
    float totalH = 0.0f;
    float maxW = 0.0f;
    bool first = true;
    for (auto& child : children_) {
        if (!child || !child->IsVisible()) continue;
        const Thickness& m = child->Margin();
        child->MeasureCached(std::max(0.0f, childAvailW - m.horizontal()),
                             std::numeric_limits<float>::infinity());
        const auto& desired = child->Desired();
        maxW = std::max(maxW, desired.w + m.horizontal());
        if (!first)
            totalH += spacing_;
        totalH += desired.h + m.vertical();
        first = false;
    }
    contentHeight_ = totalH;
    // The horizontal extent includes the padding, so a wide child can scroll until
    // its right edge AND the trailing padding are both visible.
    scroll_.SetContentWidth(maxW + padding_.horizontal());
    UpdateScrollExtent();
    return maxW;
}

SizeDip ScrollPanel::MeasureOverride(float availW, float availH)
{
    // Children see the width minus horizontal padding. Infinity minus a finite
    // padding stays infinity (IEEE), so an unconstrained measure stays unconstrained.
    const float childAvailW = std::max(0.0f, availW - padding_.horizontal());
    // Remember the width constraint: Arrange needs it to tell "the child genuinely
    // wants more than the viewport" (desired > constraint — e.g. a fixed-column Grid)
    // from "the child was stretched BY the measurement" (desired == constraint).
    //
    // Content taller than the viewport means the vertical rail will show, and Arrange
    // gives the children the width minus its strip — so they are measured at that
    // width. A child whose height depends on its width (wrapped text) would otherwise
    // report the height of the wider measure, gain a line when arranged narrower,
    // and have that line clipped. Narrowing only makes content taller, so overflow
    // found at the full width still holds at the narrow one.
    //
    // Whether it overflows is only known after measuring, so start from the last
    // answer for the same offer: a steady layout then measures once, not twice.
    const bool canRail = std::isfinite(availH) && std::isfinite(childAvailW);
    const float narrowW = std::max(0.0f, childAvailW - kRailStrip);
    const bool guessRail = canRail && railLastPass_ &&
                           availW == railAvailW_ && availH == railAvailH_;
    auto overflows = [&] { return canRail && contentHeight_ + padding_.vertical() > availH; };

    measureConstraintW_ = guessRail ? narrowW : childAvailW;
    float maxW = MeasureChildren(measureConstraintW_);
    bool rail = overflows();
    if (rail && !guessRail) {
        measureConstraintW_ = narrowW;
        maxW = MeasureChildren(narrowW);
    } else if (!rail && guessRail) {
        // Fits even at the narrow width, so it fits at the full one: no rail.
        measureConstraintW_ = childAvailW;
        maxW = MeasureChildren(childAvailW);
    }
    railLastPass_ = rail;
    railAvailW_ = availW;
    railAvailH_ = availH;

    // Report our desired size: width = max of children (or availW if constrained),
    // height = min(contentHeight, availH) so we don't request more than the parent
    // offers. Padding counts toward both.
    const float totalW = maxW + padding_.horizontal();
    const float totalHPad = contentHeight_ + padding_.vertical();
    return SizeDip{
        std::isfinite(availW) ? availW : totalW,
        std::isfinite(availH) ? std::min(totalHPad, availH) : totalHPad
    };
}

void ScrollPanel::ArrangeOverride(const RectDip& content)
{
    // Reserve a strip on the right for the vertical rail so a child with its own
    // scrollbar (ListBox, TreeView) does not draw its rail directly under ours.
    // When the content fits (no scroll) the strip is not needed.
    float vRail = (scroll_.MaxOffset() > 0.0f) ? kRailStrip : 0.0f;
    float fitW = std::max(0.0f, content.w - padding_.horizontal() - vRail);

    // Arrange's width must be the one the children were measured at, or wrapped
    // text gets a height for another width. Measure normally predicts it (see
    // MeasureOverride); re-measure when it did not — the parent arranged at another
    // width than it measured, or the rail decision changed with the real bounds.
    // If the re-measured content now overflows, measure once more at the rail-inset
    // width (narrowing only adds height, so the rail then stays). An unconstrained
    // measure (infinite width) keeps the old behaviour: every child is fitted to the
    // viewport without a re-measure.
    if (std::isfinite(measureConstraintW_) && std::fabs(fitW - measureConstraintW_) > 0.5f) {
        measureConstraintW_ = fitW;
        MeasureChildren(fitW);
        if (vRail == 0.0f && scroll_.MaxOffset() > 0.0f) {
            vRail = kRailStrip;
            fitW = std::max(0.0f, fitW - kRailStrip);
            measureConstraintW_ = fitW;
            MeasureChildren(fitW);
        }
    }

    // Arrange children in a vertical stack, APPLYING both scroll offsets to their
    // bounds. This moves compositor-backed children (TextArea, TreeView) along with
    // ordinary controls — a D2D transform in Render would not affect DComp visuals.
    const float offsetY = scroll_.Offset();
    const float offsetX = scroll_.OffsetX();
    arrangedOffset_ = offsetY;    // record for SyncScrollArrange comparison
    arrangedOffsetX_ = offsetX;

    // SNAP THE OFFSET TO THE PIXEL GRID. The smooth-scroll tween produces fractional
    // offsets (123.7, 124.2, 124.6 …), and because the offset goes into the children's
    // bounds, every child — and every glyph it draws — lands on a fractional pixel.
    // DirectWrite's ClearType weights each glyph differently per subpixel position, so
    // successive frames shimmer: text visibly jitters while scrolling. Snapping here
    // rather than in Render is what makes it work for this panel: the offset is baked
    // into bounds_ (see the note above), so Render never sees it, and a composited
    // child positions its DComp visual from those same bounds.
    //
    // TextArea / RichTextBox / LogViewer snap at their own draw call for the same
    // reason (graphics/PixelSnapping.h). Sub-half-pixel shift, no visible cost.
    const float dpiScale = Context().dpiScale;
    const float offsetYSnapped = SnapToPixel(offsetY, dpiScale);
    const float offsetXSnapped = SnapToPixel(offsetX, dpiScale);

    float y = content.y + padding_.top - offsetYSnapped;
    const float childX = content.x + padding_.left - offsetXSnapped;
    for (auto& childPtr : children_) {
        FrameworkElement* child = childPtr.get();
        if (!child || !child->IsVisible()) continue;
        const auto& desired = child->Desired();
        const Thickness& m = child->Margin();
        // Width: a child measured WIDER than the constraint it was given has a
        // genuine natural width (a fixed-column Grid of Calendars in a narrow
        // window) — keep it and let the horizontal rail scroll. Everyone else was
        // merely stretched by the measurement to that constraint, which is fitW
        // (re-measured above if not), so their desired height is valid at fitW.
        const float childW = (desired.w > measureConstraintW_ + 0.5f)
                                 ? desired.w + m.horizontal()
                                 : fitW;
        // ArrangeChild honors Margin/HAlign/VAlign — the per-control docking the
        // framework already exposes. The slot spans the child's full band including
        // its margin; the child aligns inside the margin-inset remainder.
        ArrangeChild(child, RectDip{childX, y, childW, desired.h + m.vertical()});
        y += desired.h + m.vertical() + spacing_;
    }
}

void ScrollPanel::OnBoundsChanged()
{
    LayoutCostProbe::Scope probe(LayoutCostKey::ScrollPanelBoundsChanged);
    scroll_.SetBounds(bounds_);
    UpdateScrollExtent();
}

void ScrollPanel::UpdateScrollExtent()
{
    // While the horizontal rail is live it covers the bottom 16 DIP of content;
    // inflate the vertical extent by that strip so the last child can scroll fully
    // above the rail instead of resting half-covered beneath it. Vertical padding
    // also counts, so the trailing gap scrolls into view too.
    const float hRail = (scroll_.MaxOffsetX() > 0.0f) ? 16.0f : 0.0f;
    scroll_.SetContentHeight(contentHeight_ + padding_.vertical() + hRail);
}

void ScrollPanel::OnPointerWheelChanged(PointerEventArgs& e)
{
    // Shift+Wheel scrolls horizontally (the OS-wide convention); plain wheel is
    // vertical. The horizontal axis has no tween (see ScrollViewer's design note),
    // so it applies immediately.
    const bool shift = (e.modifiers & ModifierKeys::Shift) != ModifierKeys::None;
    if (shift) {
        if (scroll_.MaxOffsetX() <= 0.0f) return;  // nothing to scroll sideways
        float lines = e.WheelScrollLines();
        if (lines == 0.0f)
            lines = bounds_.h / wheelStep_;
        scroll_.SetOffsetX(scroll_.OffsetX() - lines * wheelStep_);
        SyncScrollArrange();
        e.handled = true;
        return;
    }

    if (scroll_.MaxOffset() <= 0.0f) return;  // nothing to scroll

    // One wheel notch scrolls WheelScrollLines() lines, read from the system setting
    // (Settings → Bluetooth & devices → Mouse → scroll lines; 3 by default), the same
    // way TreeView / ListBox / TextArea do it. 0 means the user picked "one screen at
    // a time", so estimate a page's worth of lines from the viewport.
    //
    // Multiplied by wheelStep_ (default 32 DIP, settable via SetWheelStep): a
    // ScrollPanel holds heterogeneous content, so unlike ListBox (itemHeight_) or
    // TreeView (rowHeight_) there is no intrinsic row height to scale by. 32 matches
    // ListBox's default item height, which makes one notch travel the same distance
    // as scrolling a list.
    float lines = e.WheelScrollLines();
    if (lines == 0.0f)
        lines = bounds_.h / wheelStep_;

    scroll_.AnimateBy(lines * wheelStep_);
    SyncScrollArrange();
    e.handled = true;
}

void ScrollPanel::OnKeyDownRouted(KeyEventArgs& e)
{
    // Either axis alone must keep working: a panel that overflows only horizontally
    // (wide Grid, short content) still needs Left/Right.
    if (scroll_.MaxOffset() <= 0.0f && scroll_.MaxOffsetX() <= 0.0f) return;

    float delta = 0.0f;
    constexpr float kLineHeight = 16.0f;

    switch (e.vk) {
    case VK_UP:
        delta = -kLineHeight;
        break;
    case VK_DOWN:
        delta = kLineHeight;
        break;
    case VK_PRIOR:  // PageUp
        delta = -PageStep();
        break;
    case VK_NEXT:   // PageDown
        delta = PageStep();
        break;
    case VK_LEFT:
        if (scroll_.MaxOffsetX() > 0.0f) {
            scroll_.SetOffsetX(scroll_.OffsetX() - kLineHeight);
            SyncScrollArrange();
            e.handled = true;
        }
        return;
    case VK_RIGHT:
        if (scroll_.MaxOffsetX() > 0.0f) {
            scroll_.SetOffsetX(scroll_.OffsetX() + kLineHeight);
            SyncScrollArrange();
            e.handled = true;
        }
        return;
    case VK_HOME:
        scroll_.SetOffset(0.0f);
        SyncScrollArrange();
        e.handled = true;
        return;
    case VK_END:
        scroll_.SetOffset(scroll_.MaxOffset());
        SyncScrollArrange();
        e.handled = true;
        return;
    default:
        return;
    }

    scroll_.AnimateBy(delta);
    SyncScrollArrange();
    e.handled = true;
}

float ScrollPanel::PageStep() const
{
    // Page up/down scrolls (viewport height - one line), so there's overlap for context.
    constexpr float kLineHeight = 16.0f;
    return std::max(bounds_.h - kLineHeight, kLineHeight);
}

void ScrollPanel::OnPointerMoved(PointerEventArgs& e)
{
    float dipX = e.position.x;
    float dipY = e.position.y;

    if (scroll_.IsDragging()) {
        scroll_.DragTo(dipY);
        SyncScrollArrange();  // the drag moved the offset
        e.handled = true;
        return;
    }
    if (scroll_.IsHDragging()) {
        scroll_.HDragTo(dipX);
        SyncScrollArrange();
        e.handled = true;
        return;
    }

    // Wake the scrollbars on any movement near them.
    scroll_.Wake();
    scroll_.SetBarHover(scroll_.HitBarRegion(dipX, dipY));
    scroll_.SetHBarHover(scroll_.HitHBarRegion(dipX, dipY));
}

void ScrollPanel::OnPointerPressed(PointerEventArgs& e)
{
    if (e.button != PointerButton::Left) return;

    float dipX = e.position.x;
    float dipY = e.position.y;

    // A press anywhere in either hover strip (16 DIP) starts a drag, not just one on
    // the visible thumb (3 DIP idle / 7 DIP expanded). The press target must match
    // the region that gives hover feedback: a rail that lights up under the pointer
    // but ignores the click reads as broken, and asking the user to land inside
    // 3 DIP is not a real target. Vertical rail first: it owns the corner.
    if (scroll_.HitBarRegion(dipX, dipY)) {
        // Off-thumb presses first center the thumb on the pointer, then drag from
        // there. Without the jump the thumb would stay put and then leap on the first
        // move, because DragTo is relative to where the press landed.
        if (!scroll_.HitThumb(dipX, dipY)) {
            const RectDip thumb = scroll_.ThumbRect();
            const float trackRange = std::max(1.0f, bounds_.h - thumb.h);
            const float wantTop = (dipY - thumb.h * 0.5f) - bounds_.y;
            scroll_.SetOffset(wantTop / trackRange * scroll_.MaxOffset());
            SyncScrollArrange();
        }
        scroll_.BeginDrag(dipY);
        if (auto* input = Context().input)
            input->CapturePointer(this);
        e.handled = true;
        return;
    }

    if (scroll_.HitHBarRegion(dipX, dipY)) {
        if (!scroll_.HitHThumb(dipX, dipY)) {
            const RectDip thumb = scroll_.HThumbRect();
            const float trackRange = std::max(1.0f, bounds_.w - thumb.w);
            const float wantLeft = (dipX - thumb.w * 0.5f) - bounds_.x;
            scroll_.SetOffsetX(wantLeft / trackRange * scroll_.MaxOffsetX());
            SyncScrollArrange();
        }
        scroll_.BeginHDrag(dipX);
        if (auto* input = Context().input)
            input->CapturePointer(this);
        e.handled = true;
        return;
    }
}

void ScrollPanel::OnPointerReleased(PointerEventArgs& e)
{
    if (e.button != PointerButton::Left) return;

    bool wasDragging = scroll_.IsDragging() || scroll_.IsHDragging();
    if (wasDragging) {
        scroll_.EndDrag();
        scroll_.EndHDrag();
        if (auto* input = Context().input) {
            if (input->Captured() == this)
                input->ReleaseCapture(this);
        }
        e.handled = true;
    }
}

void ScrollPanel::OnPointerLeft()
{
    // Stop hovering the scrollbars when the pointer leaves the panel.
    scroll_.SetBarHover(false);
    scroll_.SetHBarHover(false);
}

void ScrollPanel::Render(const DrawingContext& dc)
{
    if (!dc.Dc()) return;  // headless

    // Clip to the panel's bounds so children and scrollbar don't draw outside.
    ClipGuard clip = dc.PushClip(
        D2D1::RectF(bounds_.x, bounds_.y, bounds_.right(), bounds_.bottom()));

    // NO scroll transform. The offset lives in the children's bounds (see
    // ArrangeOverride), which is what makes compositor-backed children scroll too:
    // a TextArea's pixels come from a DComp child visual positioned from its bounds,
    // and a D2D transform on this device context cannot move it. The previous
    // revision translated here instead, so ordinary controls scrolled while
    // composited ones stayed nailed in place.
    for (auto& child : children_) {
        if (!child || !child->IsVisible()) continue;
        // Cull against the viewport using on-screen bounds.
        const RectDip cb = child->VisualBounds();
        if (cb.bottom() < bounds_.y || cb.y > bounds_.bottom()) continue;
        child->RenderWithOpacity(dc);
    }

    // Scrollbar on top of the content.
    scroll_.Render(dc);
}

} // namespace fluent
