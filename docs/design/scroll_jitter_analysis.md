# Scroll Jitter Analysis and Fix

**Date**: 2026-09-20  
**Issue**: Text rendering jitter during scrolling due to subpixel positioning  
**Root Cause**: Floating-point scroll offsets not aligned to physical pixel boundaries

---

## Problem Description

When scrolling text-based controls (RichTextBox, LogViewer), users can observe subtle vertical jitter/shimmer of the text. This is caused by:

1. **Floating-point scroll offsets**: `scroll_.Offset()` returns values like `15.7 DIP`
2. **Subpixel rendering**: DirectWrite's ClearType renders text at fractional pixel coordinates differently each frame
3. **No pixel alignment**: Text origin is calculated as `bounds_.y - scrollY` without rounding

### Visual Effect
- Text appears to "vibrate" or "shimmer" vertically during smooth scrolling
- More noticeable on high-DPI displays where 0.3 DIP = multiple physical subpixels
- Not visible in screenshots, only during live scrolling

---

## Controls Affected

### ✅ Already Fixed
- **TextArea** (`FluentUI/controls/TextArea.cpp:1719-1723`)
  - Has `SnapToPixel()` helper function
  - Applies to both vertical (`snappedOffset`) and horizontal scrolling
  - Well-documented in comments

### ❌ Needs Fixing
1. **RichTextBox** (`FluentUI/controls/RichTextBox.cpp:454-459`)
   - Uses raw `scrollViewer_->Offset()` and `scrollViewer_->OffsetX()`
   - Text origin calculated without pixel snapping
   
2. **LogViewer** (`FluentUI/controls/LogViewer.cpp:311-333`)
   - Uses raw `scroll_.Offset()` for line Y positioning
   - Affects all log lines during scrolling

### ✅ Not Affected (no text scrolling)
- **TreeView**: Draws text at row boundaries (already integer multiples of `rowHeight_`)
- **ListBox**: Similar to TreeView, items are snapped to row grid
- **DataGrid**: Cell positions are grid-aligned
- **ScrollPanel**: Generic container, doesn't draw text itself

---

## Solution: Pixel Snapping

### Reference Implementation (from TextArea.cpp)

```cpp
// Snap a DIP offset to the nearest physical pixel boundary to avoid subpixel jitter
// when scrolling. Without snapping, floating-point scroll offsets (e.g., 15.7 DIPs)
// cause text to render at fractional pixel coordinates, producing visible blur/shimmer.
inline float SnapToPixel(float dip, float dpiScale) {
    const float px = dip * dpiScale;
    return std::roundf(px) / dpiScale;
}

// Usage in Render():
const float dpiScale = dc.DpiScale();
const float snappedOffset = SnapToPixel(scroll_.Offset(), dpiScale);
const float originY = bounds_.y + ContentTop() - snappedOffset;
```

### Why This Works

1. **Convert DIP → Physical Pixels**: `px = dip * dpiScale`
   - Example: 15.7 DIP × 1.5 scale = 23.55 px
   
2. **Round to Integer Pixel**: `std::roundf(px)` → `24 px`

3. **Convert Back to DIP**: `24 / 1.5 = 16.0 DIP`

4. **Result**: Text always renders at integer pixel boundaries, ClearType produces consistent output

### Performance Impact
- **Negligible**: Two floating-point operations per frame
- **Zero visual impact**: Rounding error is < 0.5 physical pixels (imperceptible)
- **Huge quality improvement**: Eliminates visible jitter

---

## Implementation Plan

### Step 1: Add SnapToPixel Helper to UIElement.h or a Shared Header

Option A: Add to `FluentUI/core/UIElement.h` as inline utility  
Option B: Add to new `FluentUI/graphics/PixelSnapping.h`  
**Recommendation**: Option A (already used by TextArea, RichTextBox, LogViewer)

### Step 2: Fix RichTextBox

**File**: `FluentUI/controls/RichTextBox.cpp:454-459`

**Before**:
```cpp
const float scrollY = scrollViewer_->Offset();
const float scrollX = scrollViewer_->OffsetX();
const D2D1_POINT_2F origin = D2D1::Point2F(
    bounds_.x + kPadding - scrollX,
    bounds_.y + kPadding - scrollY);
```

**After**:
```cpp
const float dpiScale = dc.DpiScale();
const float scrollY = SnapToPixel(scrollViewer_->Offset(), dpiScale);
const float scrollX = SnapToPixel(scrollViewer_->OffsetX(), dpiScale);
const D2D1_POINT_2F origin = D2D1::Point2F(
    bounds_.x + kPadding - scrollX,
    bounds_.y + kPadding - scrollY);
```

### Step 3: Fix LogViewer

**File**: `FluentUI/controls/LogViewer.cpp:311`

**Before**:
```cpp
const float scrollTop = scroll_.Offset();
// ...
const float y = bounds_.y + lineYPositions_[idx] - scrollTop;
```

**After**:
```cpp
const float dpiScale = dc.DpiScale();
const float scrollTop = SnapToPixel(scroll_.Offset(), dpiScale);
// ...
const float y = bounds_.y + lineYPositions_[idx] - scrollTop;
```

### Step 4: Verify with Tests
- Build Debug + Release
- Run all tests (should remain 1681/1681)
- **Manual verification required**: Open DeviceCommunicateX, scroll RichTextBox with mouse wheel/drag

---

## Why Not Fix ScrollViewer Itself?

**Question**: Why snap in each control's `Render()` instead of in `ScrollViewer::Offset()`?

**Answer**:
1. **Smooth animation**: `ScrollViewer` uses floating-point animation (`AnimateTo`, `AnimateBy`)
   - Snapping in `Offset()` would produce jerky 1-pixel jumps
   - Better to animate smoothly, snap only at render time
   
2. **Multiple consumers**: Some controls use offset for non-rendering (e.g., hit-testing)
   - Hit tests need precise offset, not snapped
   
3. **DPI-awareness**: Snapping requires DPI scale, which varies per monitor
   - `ScrollViewer` doesn't have access to `DrawingContext` (render-time only)
   - Each control knows its own DPI from `dc.DpiScale()`

---

## Edge Cases Considered

### Q: Does snapping affect scrollbar thumb position?
**A**: No. Scrollbar thumb is drawn separately in `scroll_.Render(dc)`, uses its own logic.

### Q: Does snapping affect scroll animation smoothness?
**A**: No. Animation still runs at full floating-point precision, only the final render snaps.

### Q: What about horizontal scrolling (scrollX)?
**A**: Same fix applies. TextArea already snaps both axes, RichTextBox should too.

### Q: Does this affect content height calculations?
**A**: No. `scroll_.SetContentHeight()` uses unsnapped values. Only render origin is snapped.

---

## Testing Strategy

### Automated Tests (Expected: No Change)
```bash
./x64/Debug/FluentUITests.exe
# Expected: 1681/1681 passing (no regressions)
```

Pixel snapping is a render-only change, doesn't affect:
- Layout (Measure/Arrange)
- Hit testing (uses unsnapped offsets)
- Scrollbar behavior
- Content measurements

### Manual Verification (Required)

1. **RichTextBox jitter test**:
   - Open DeviceCommunicateX
   - Scroll the RichTextBox with mouse wheel (smooth scrolling)
   - **Before**: Text jiggles/shimmers vertically
   - **After**: Text scrolls smoothly without jitter

2. **LogViewer jitter test** (if used in DeviceCommunicateX or Demo):
   - Scroll a long log
   - **Before**: Log lines shimmer during scroll
   - **After**: Smooth scrolling

3. **Multi-monitor test** (if available):
   - Drag window between 100% DPI and 150% DPI monitors
   - Verify snapping adapts to new DPI (no jitter on either monitor)

---

## Related Issues

### Issue: TextArea Already Has the Fix
**Status**: ✅ No action needed  
**Commit**: Already implemented in TextArea.cpp (date unknown)  
**Reference**: Lines 1719-1723

### Issue: TreeView Hierarchy Clarity
**Status**: ⏳ Separate issue (not jitter-related)  
**Root Cause**: Insufficient indent (14 DIP), no connector lines  
**Fix**: Increase indent to 20-24 DIP, draw vertical/horizontal lines

### Issue: ScrollPanel Generic Container
**Status**: ✅ No action needed  
**Reason**: ScrollPanel doesn't draw text, children are responsible for their own rendering

---

## Conclusion

**Scope**: 2 files to modify (RichTextBox.cpp, LogViewer.cpp)  
**Complexity**: Low (copy existing SnapToPixel pattern from TextArea)  
**Risk**: Very low (render-only change, no layout impact)  
**Benefit**: Eliminates user-visible text jitter during scrolling

**Recommendation**: Proceed with fix immediately after verifying event model commit.
