// TextControlsCursorTests.cpp — cursor behavior for text controls.
//
// Verifies that TextArea and TextBlock show the correct cursor (I-beam vs
// arrow) based on pointer position.

#include "../framework/Test.h"
#include "../../FluentUI/controls/TextArea.h"
#include "../../FluentUI/controls/TextBlock.h"
#include "../../FluentUI/graphics/DWriteContext.h"
#include "../../FluentUI/core/UIContext.h"
#include <windows.h>

using namespace fluent;

// --- TextArea ----------------------------------------------------------------

TEST(TextAreaCursor, DefaultIBeam) {
    TextArea ta;
    HCURSOR cur = ta.Cursor();
    EXPECT_TRUE(cur == LoadCursor(nullptr, IDC_IBEAM));
}

TEST(TextAreaCursor, IBeamOverTextAfterMove) {
    TextArea ta;
    ta.Measure(200, 100);
    ta.Arrange(RectDip{0, 0, 200, 100});
    
    // Move over text area (left side, well inside).
    PointerEventArgs e;
    e.position = {50, 25};
    ta.OnPointerMoved(e);
    
    // Should be I-beam.
    HCURSOR cur = ta.Cursor();
    EXPECT_TRUE(cur == LoadCursor(nullptr, IDC_IBEAM));
}

// --- TextBlock ---------------------------------------------------------------

TEST(TextBlockCursor, NonSelectableReturnsNull) {
    TextBlock tb;
    tb.SetSelectable(false);
    
    HCURSOR cur = tb.Cursor();
    EXPECT_TRUE(cur == nullptr);
}

TEST(TextBlockCursor, SelectableDefaultsToArrow) {
    TextBlock tb;
    tb.SetSelectable(true);
    
    // Before any OnPointerMoved, pointerOverText_ is false, so cursor is arrow.
    HCURSOR cur = tb.Cursor();
    EXPECT_TRUE(cur == nullptr);
}

TEST(TextBlockCursor, IsPointOverTextOutsideBounds) {
    TextBlock tb;
    tb.SetSelectable(true);
    tb.Measure(200, 100);
    tb.Arrange(RectDip{10, 10, 200, 100});
    
    // Point outside bounds should return false immediately.
    bool outside = tb.IsPointOverText(300, 300);
    EXPECT_FALSE(outside);
}

TEST(TextBlockCursor, IsPointOverTextInsideBounds) {
    TextBlock tb;
    tb.SetText(L"Hello");
    tb.SetSelectable(true);
    tb.Measure(200, 100);
    tb.Arrange(RectDip{10, 10, 200, 100});
    
    // Point inside bounds. Without DWrite, just ensure it doesn't crash.
    bool result = tb.IsPointOverText(50, 50);
    (void)result;
}

TEST(TextBlockCursor, PointerLeftClearsState) {
    TextBlock tb;
    tb.SetSelectable(true);
    tb.Measure(200, 100);
    tb.Arrange(RectDip{0, 0, 200, 100});
    
    tb.OnPointerEnter();
    PointerEventArgs e;
    e.position = {50, 25};
    tb.OnPointerMoved(e);
    
    tb.OnPointerLeave();
    
    // State should be cleared.
    HCURSOR cur = tb.Cursor();
    EXPECT_TRUE(cur == nullptr);
}

TEST(TextBlockCursor, IBeamDuringSelectionDrag) {
    TextBlock tb;
    tb.SetText(L"Hello");
    tb.SetSelectable(true);
    tb.Measure(200, 100);
    tb.Arrange(RectDip{0, 0, 200, 100});
    
    // Start selection drag.
    PointerEventArgs press;
    press.button = PointerButton::Left;
    press.position = {50, 25};
    tb.OnPointerPressed(press);
    
    // During drag, cursor should be I-beam.
    HCURSOR cur = tb.Cursor();
    EXPECT_TRUE(cur == LoadCursor(nullptr, IDC_IBEAM));
}

// --- TextBlock Shift+click (selectable mode) --------------------------------

TEST(TextBlockCursor, ShiftClickExtendsSelectionInSelectableMode)
{
    DWriteContext dw;
    if (!dw.Valid()) dw.Initialize();
    if (!dw.Valid()) return;
    
    UIContext ctx;
    ctx.dwrite = &dw;
    ctx.dpiScale = 1.0f;
    
    TextBlock block;
    block.SetText(L"hello world");
    block.SetBounds({0, 0, 200, 30});
    block.SetSelectable(true);
    block.AttachToContext(ctx);
    
    // First click near start
    PointerEventArgs e1;
    e1.position = {10.0f, 15.0f};
    e1.button = PointerButton::Left;
    e1.clickCount = 1;
    block.OnPointerPressed(e1);
    
    // Shift+click near end
    PointerEventArgs e2;
    e2.position = {100.0f, 15.0f};
    e2.button = PointerButton::Left;
    e2.clickCount = 1;
    e2.modifiers = ModifierKeys::Shift;
    block.OnPointerPressed(e2);
    
    EXPECT_TRUE(block.HasSelection());
    block.DetachFromContext();
}

TEST(TextBlockCursor, ShiftClickIgnoredWhenNotSelectable)
{
    DWriteContext dw;
    if (!dw.Valid()) dw.Initialize();
    if (!dw.Valid()) return;
    
    UIContext ctx;
    ctx.dwrite = &dw;
    ctx.dpiScale = 1.0f;
    
    TextBlock block;
    block.SetText(L"hello world");
    block.SetBounds({0, 0, 200, 30});
    block.SetSelectable(false);  // not selectable
    block.AttachToContext(ctx);
    
    PointerEventArgs e;
    e.position = {50.0f, 15.0f};
    e.button = PointerButton::Left;
    e.clickCount = 1;
    e.modifiers = ModifierKeys::Shift;
    block.OnPointerPressed(e);
    
    EXPECT_FALSE(block.HasSelection());
    block.DetachFromContext();
}
