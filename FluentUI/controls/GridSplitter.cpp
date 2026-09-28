// GridSplitter.cpp

#include "GridSplitter.h"
#include "../layout/Grid.h"
#include "../core/UIContext.h"
#include "../input/InputManager.h"
#include "../styling/ThemeManager.h"
#include <algorithm>  // std::clamp — the drag delta is clamped so neither track underflows
#include <Windows.h>

namespace fluent {

GridSplitter::GridSplitter() {
    // Default size: 4 DIP wide/tall, stretches along the other axis.
    SetWidth(4.0f);
    SetHeight(4.0f);
}

void GridSplitter::SetOrientation(Orientation o) {
    if (orientation_ == o) return;
    orientation_ = o;
    Invalidate();
}

HCURSOR GridSplitter::Cursor() const {
    if (!CanResize()) return nullptr;
    return LoadCursor(nullptr, orientation_ == Orientation::Vertical
                                   ? IDC_SIZEWE  // left/right resize
                                   : IDC_SIZENS); // up/down resize
}

Grid* GridSplitter::ParentGrid() const {
    return dynamic_cast<Grid*>(Parent());
}

int GridSplitter::TrackIndex() const {
    Grid* grid = ParentGrid();
    if (!grid) return -1;
    return (orientation_ == Orientation::Vertical) ? grid->ColumnOf(this)
                                                   : grid->RowOf(this);
}

bool GridSplitter::CanResize() const {
    Grid* grid = ParentGrid();
    if (!grid) return false;

    // Need a track on each side, so at least 3 tracks and an interior index.
    const int count = static_cast<int>(orientation_ == Orientation::Vertical
                                           ? grid->ColumnCount()
                                           : grid->RowCount());
    if (count < 3) return false;
    const int idx = TrackIndex();
    return idx > 0 && idx < count - 1;
}

void GridSplitter::OnPointerPressed(PointerEventArgs& e) {
    if (e.button != PointerButton::Left) return;
    if (!CanResize()) return;
    Grid* grid = ParentGrid();
    if (!grid) return;

    // Capture so a drag that leaves the 4 DIP bar keeps resizing (the whole point
    // of a splitter is that the pointer outruns it).
    if (Context().input) Context().input->CapturePointer(this);
    dragging_ = true;

    // e.position is already window DIPs; record the anchor.
    dragStartX_ = e.position.x;
    dragStartY_ = e.position.y;

    // Sample the two neighbouring tracks' RESOLVED sizes. A Star or Auto track has
    // no size in its definition, so this is the only place the draggable number
    // exists. Empty before the first Arrange — then there is nothing to drag yet.
    const int idx = TrackIndex();
    const auto& resolved = (orientation_ == Orientation::Vertical)
                               ? grid->ResolvedColumnSizes()
                               : grid->ResolvedRowSizes();
    const size_t trackCount = (orientation_ == Orientation::Vertical)
                                  ? grid->ColumnCount()
                                  : grid->RowCount();
    if (idx > 0 && idx + 1 < static_cast<int>(resolved.size()) &&
        static_cast<size_t>(idx) + 1 < trackCount) {
        trackBeforeStart_ = resolved[idx - 1];
        trackAfterStart_ = resolved[idx + 1];
        // Also snapshot the two DEFINITIONS, for the same reason the sizes are
        // snapshotted: every move writes back from the drag anchor, so the unit it
        // writes has to be the unit that was there when the drag began, not one the
        // previous move left behind.
        //
        // With the current write-back rules this is belt-and-braces — every branch
        // preserves the unit, so a live read would see the same unit (verified: a
        // live-read mutation leaves all these tests green). It is kept because the
        // invariant it depends on is fragile. The Auto branch already breaks it
        // (Auto becomes Pixel on the first move, so a live read on move two would
        // see Pixel), and any future rule that changed a unit would silently
        // reintroduce the mid-drag ratchet that the shrinking-side-only fix had.
        const bool vertical = (orientation_ == Orientation::Vertical);
        defBeforeStart_ = vertical ? grid->ColumnAt(static_cast<size_t>(idx) - 1)
                                   : grid->RowAt(static_cast<size_t>(idx) - 1);
        defAfterStart_ = vertical ? grid->ColumnAt(static_cast<size_t>(idx) + 1)
                                  : grid->RowAt(static_cast<size_t>(idx) + 1);
    }

    // Fire DragStarted after capturing and setting up state.
    RoutedEventArgs startArgs;
    startArgs.source = this;
    startArgs.originalSource = this;
    dragStarted_.Raise(*this, startArgs);

    e.handled = true;
}

void GridSplitter::OnPointerMoved(PointerEventArgs& e) {
    if (!dragging_ || !trackBeforeStart_ || !trackAfterStart_) return;
    if (!defBeforeStart_ || !defAfterStart_) return;  // 两组快照总是一起写入，防御性检查
    Grid* grid = ParentGrid();
    if (!grid) return;

    const int idx = TrackIndex();
    if (idx <= 0) return;

    const bool vertical = (orientation_ == Orientation::Vertical);
    const size_t beforeIdx = static_cast<size_t>(idx) - 1;
    const size_t afterIdx = static_cast<size_t>(idx) + 1;
    const size_t trackCount = vertical ? grid->ColumnCount() : grid->RowCount();
    if (afterIdx >= trackCount) return;

    const float rawDelta = vertical ? (e.position.x - dragStartX_)
                                    : (e.position.y - dragStartY_);

    // The pair is resized as a zero-sum move: what one track gains the other loses,
    // so the grid's total content extent does not change and nothing outside the pair
    // shifts. The DELTA is clamped rather than each track's result, which is what
    // keeps it zero-sum at the extremes too — clamping the two sizes independently
    // lets their sum grow past the pair's original extent, and under Star write-back
    // that difference is not absorbed anywhere, it pushes the far edge out.
    const float pairExtent = *trackBeforeStart_ + *trackAfterStart_;
    const float deltaLo = kMinTrackDip - *trackBeforeStart_;  // 最多能往左走多少
    const float deltaHi = *trackAfterStart_ - kMinTrackDip;   // 最多能往右走多少
    // 这对轨道已经窄到放不下两个 kMinTrackDip 时 lo > hi（std::clamp 在那种参数下是 UB），
    // 此时任何移动都只能让情况更糟，直接原地不动。
    const float delta = (deltaLo > deltaHi) ? 0.0f
                                            : std::clamp(rawDelta, deltaLo, deltaHi);
    const float newBefore = *trackBeforeStart_ + delta;
    const float newAfter = *trackAfterStart_ - delta;

    // 写回时保持每条轨道**原本的单位**（快照里的，不是上一次移动留下的）。
    //
    // 为什么不能把轨道转成 Pixel（之前两个版本都这么做，都不对）：Star 是"分配剩余
    // 空间"的声明，一旦一个 Grid 里再没有 Star 轨道，窗口变大时多出来的宽度无人吸收，
    // 就会在最后一条轨道外侧留下一条永久空隙 —— 用户报告的现象正是"拖过分割条之后，
    // 右侧内容不再贴着窗口边移动"。
    //
    // 上一版只固定"被缩小"的那条，只挡住了单向拖动：鼠标越过锚点抖一下（先右再左），
    // 两个方向都发生过，两条 Star 就都被冻结，弹性照样丢失。
    //
    // 按原单位写回同时解决两件事：Star ↔ Star 之间只重新分配**星权重**（权重之和守恒，
    // 比例按新尺寸），所以用户拖出来的比例被记住了，而两条轨道仍然是 Star，窗口变化
    // 继续按新比例分配。Pixel 轨道照旧写像素值。Auto 轨道必须转成 Pixel —— Auto 的
    // 尺寸由内容决定，没有任何可写回的参数，不转就等于拖不动。
    const auto writeBack = [&](size_t i, const GridLength& original, float newSize,
                               float otherSize, const GridLength& otherOriginal) {
        GridLength out;
        if (original.isStar() && otherOriginal.isStar() && pairExtent > 0.0f) {
            // 星权重按新尺寸比例重分配，两条权重之和保持不变。守恒很重要：这对轨道
            // 相对于 Grid 里**其他** Star 轨道的总份额不能因为一次拖动而改变。
            const float weightSum = std::max(0.0f, original.value) +
                                    std::max(0.0f, otherOriginal.value);
            const float share = (weightSum > 0.0f) ? weightSum : 2.0f;
            out = GridLength::Star(std::max(0.0001f, share * (newSize / pairExtent)));
        } else if (original.isStar()) {
            // 对面是 Pixel/Auto：这条 Star 是 Grid 里唯一被拖动的弹性轨道，写回一个
            // 绝对尺寸会把它冻结，所以保留 Star，用它和对面的实际尺寸定权重。
            // 对面尺寸为 0 时退化为保持原权重（无从推导比例）。
            out = (otherSize > 0.0f)
                      ? GridLength::Star(std::max(0.0001f, newSize / otherSize))
                      : original;
        } else {
            // Pixel 和 Auto 都写绝对尺寸。Auto 在这里被"钉住"是必然的取舍，见上文。
            out = GridLength::Pixels(newSize);
        }
        if (vertical) grid->SetColumnAt(i, out);
        else          grid->SetRowAt(i, out);
    };

    writeBack(beforeIdx, *defBeforeStart_, newBefore, newAfter, *defAfterStart_);
    writeBack(afterIdx, *defAfterStart_, newAfter, newBefore, *defBeforeStart_);

    // Fire DragDelta after the tracks are resized, so a handler reading the grid
    // sees the new sizes. delta is cumulative from the anchor, matching what the
    // tracks were actually resized by.
    if (dragDelta_.HasSubscribers()) {
        GridSplitterDragDeltaArgs args;
        args.deltaDip = delta;
        dragDelta_.Raise(*this, args);
    }

    e.handled = true;
}

void GridSplitter::OnPointerReleased(PointerEventArgs& e) {
    if (!dragging_) return;
    if (Context().input && Context().input->Captured() == this)
        Context().input->ReleaseCapture(this);
    dragging_ = false;
    trackBeforeStart_ = std::nullopt;
    trackAfterStart_ = std::nullopt;
    defBeforeStart_ = std::nullopt;
    defAfterStart_ = std::nullopt;

    // Fire DragCompleted after clearing drag state: a handler that persists the
    // layout should see a settled splitter, not one still marked as dragging.
    RoutedEventArgs endArgs;
    endArgs.source = this;
    endArgs.originalSource = this;
    dragCompleted_.Raise(*this, endArgs);

    e.handled = true;
}

void GridSplitter::Render(const DrawingContext& dc) {
    const ThemeSnapshot& th = Theme();
    const float corner = EffectiveCornerRadius(2.0f);

    D2D1_ROUNDED_RECT rr = D2D1::RoundedRect(
        D2D1::RectF(bounds_.x, bounds_.y, bounds_.right(), bounds_.bottom()),
        corner, corner);

    // A quiet bar: a hairline stroke reads as a seam between two panes, and the
    // hover/drag fill is what tells the user it is grabbable. Both go through the
    // Control property layer, so an app can restyle one splitter without a theme.
    if (dragging_ || State() == VisualState::Hover) {
        const D2D1_COLOR_F fill = EffectiveBackground(
            dragging_ ? th.colors.controlFillPressed : th.colors.controlFillHover);
        dc.FillRoundedRect(rr, fill);
    } else if (HasBackground()) {
        // An explicitly set Background is drawn at rest too — otherwise setting it
        // would look like it did nothing until the pointer arrives.
        dc.FillRoundedRect(rr, EffectiveBackground());
    }

    dc.DrawRoundedRect(rr, EffectiveBorderBrush(th.colors.controlStrokeDefault),
                       EffectiveBorderThickness(1.0f));
}

} // namespace fluent
