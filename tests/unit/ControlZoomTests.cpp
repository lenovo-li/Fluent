// ControlZoomTests.cpp — Ctrl+Wheel zoom for TextArea, LogViewer, RichTextBox.
//
// Headless: no device, so the actual glyph size and layout metrics are not
// testable. What IS testable is the API contract (SetZoom clamping, event firing),
// the wheel-gesture binding (Ctrl+Wheel / Ctrl+0), and that zoom changes trigger
// the right invalidation paths (cache clears, layout dirty flags).

#include "../framework/Test.h"
#include "../../FluentUI/controls/TextArea.h"
#include "../../FluentUI/controls/LogViewer.h"
#include "../../FluentUI/controls/RichTextBox.h"
#include "../../FluentUI/controls/DataGrid.h"
#include "../../FluentUI/controls/RichTextDocument.h"
#include "../../FluentUI/core/UIContext.h"
#include "../../FluentUI/window/WindowServices.h"
#include "../../FluentUI/graphics/D2DContext.h"
#include "../../FluentUI/graphics/DWriteContext.h"
#include "../../FluentUI/input/InputTypes.h"
#include <windows.h>

using namespace fluent;

namespace {
constexpr D2D1_COLOR_F kWhite{1.0f, 1.0f, 1.0f, 1.0f};
constexpr D2D1_COLOR_F kInk{0.1f, 0.1f, 0.12f, 1.0f};

class MockHost : public WindowServices {
public:
    MockHost() { dwrite_.Initialize(); }
    HINSTANCE Instance() const override { return nullptr; }
    HWND Hwnd() const override { return nullptr; }
    float DpiScale() const override { return 1.0f; }
    D2DContext& D2D() override { return d2d_; }
    DWriteContext& DWrite() override { return dwrite_; }
    ICompositionBackend* Composition() override { return nullptr; }
    Subscription RegisterActivePopupDismiss(
        std::function<bool(PopupDismissReason, HWND, int, int)>) override { return {}; }
    Subscription RegisterActivePopupKeyHandler(
        std::function<bool(UINT)>) override { return {}; }
    Subscription RegisterActivePopupWheelHandler(
        std::function<bool(int, int, int)>) override { return {}; }
private:
    D2DContext d2d_;
    DWriteContext dwrite_;
};

UIContext MakeCtx(MockHost& host) {
    UIContext c;
    c.window = &host;
    c.dwrite = &host.DWrite();
    c.dpiScale = 1.0f;
    return c;
}
}

// ---------------------------------------------------------------------------
// TextArea zoom
// ---------------------------------------------------------------------------

TEST(TextAreaZoom, DefaultZoomIs100Percent)
{
    TextArea t;
    EXPECT_EQ(t.Zoom(), 1.0f);
}

TEST(TextAreaZoom, SetZoomChangesValue)
{
    TextArea t;
    t.SetZoom(1.5f);
    EXPECT_EQ(t.Zoom(), 1.5f);
}

TEST(TextAreaZoom, SetZoomClampsToBounds)
{
    TextArea t;
    t.SetZoom(0.1f);
    EXPECT_EQ(t.Zoom(), TextArea::kMinZoom);
    t.SetZoom(10.0f);
    EXPECT_EQ(t.Zoom(), TextArea::kMaxZoom);
}

TEST(TextAreaZoom, ZoomEventFires)
{
    struct Rec {
        float captured = 0.0f;
        static void OnZoom(void* ctx, TextArea&, float& z) {
            static_cast<Rec*>(ctx)->captured = z;
        }
    } rec;
    TextArea t;
    auto sub = t.ZoomChanged().Subscribe(&rec, &Rec::OnZoom);
    t.SetZoom(1.2f);
    EXPECT_EQ(rec.captured, 1.2f);
}

TEST(TextAreaZoom, ZoomEventDoesNotFireWhenUnchanged)
{
    struct Rec {
        int fireCount = 0;
        static void OnZoom(void* ctx, TextArea&, float&) {
            ++static_cast<Rec*>(ctx)->fireCount;
        }
    } rec;
    TextArea t;
    t.SetZoom(1.5f);
    auto sub = t.ZoomChanged().Subscribe(&rec, &Rec::OnZoom);
    t.SetZoom(1.5f);  // same value
    EXPECT_EQ(rec.fireCount, 0);
}

TEST(TextAreaZoom, CtrlWheelUpZoomsIn)
{
    TextArea t;
    t.SetText(L"test");
    PointerEventArgs e;
    e.wheelDelta = WHEEL_DELTA;
    e.modifiers = ModifierKeys::Ctrl;
    t.OnPointerWheelChanged(e);
    EXPECT_TRUE(t.Zoom() > 1.0f);
    EXPECT_TRUE(e.handled);
}

TEST(TextAreaZoom, CtrlWheelDownZoomsOut)
{
    TextArea t;
    t.SetText(L"test");
    PointerEventArgs e;
    e.wheelDelta = -WHEEL_DELTA;
    e.modifiers = ModifierKeys::Ctrl;
    t.OnPointerWheelChanged(e);
    EXPECT_TRUE(t.Zoom() < 1.0f);
    EXPECT_TRUE(e.handled);
}

TEST(TextAreaZoom, Ctrl0ResetsZoom)
{
    TextArea t;
    t.SetZoom(1.5f);
    KeyEventArgs e;
    e.vk = '0';
    e.modifiers = ModifierKeys::Ctrl;
    t.OnKeyDownRouted(e);
    EXPECT_EQ(t.Zoom(), 1.0f);
    EXPECT_TRUE(e.handled);
}

TEST(TextAreaZoom, CtrlNumpad0ResetsZoom)
{
    TextArea t;
    t.SetZoom(1.5f);
    KeyEventArgs e;
    e.vk = VK_NUMPAD0;
    e.modifiers = ModifierKeys::Ctrl;
    t.OnKeyDownRouted(e);
    EXPECT_EQ(t.Zoom(), 1.0f);
    EXPECT_TRUE(e.handled);
}

TEST(TextAreaZoom, Ctrl0AtDefaultDoesNotHandle)
{
    // An app shortcut bound to Ctrl+0 should still fire when zoom is already 100%.
    TextArea t;
    EXPECT_EQ(t.Zoom(), 1.0f);
    KeyEventArgs e;
    e.vk = '0';
    e.modifiers = ModifierKeys::Ctrl;
    t.OnKeyDownRouted(e);
    EXPECT_FALSE(e.handled);
}

TEST(TextAreaZoom, ZoomDisabledIgnoresGestures)
{
    TextArea t;
    t.SetZoomEnabled(false);
    PointerEventArgs wheel;
    wheel.wheelDelta = WHEEL_DELTA;
    wheel.modifiers = ModifierKeys::Ctrl;
    t.OnPointerWheelChanged(wheel);
    EXPECT_EQ(t.Zoom(), 1.0f);
    EXPECT_FALSE(wheel.handled);
}

// ---------------------------------------------------------------------------
// LogViewer zoom
// ---------------------------------------------------------------------------

TEST(LogViewerZoom, DefaultZoomIs100Percent)
{
    LogViewer v;
    EXPECT_EQ(v.Zoom(), 1.0f);
}

TEST(LogViewerZoom, SetZoomChangesValue)
{
    LogViewer v;
    v.SetZoom(2.0f);
    EXPECT_EQ(v.Zoom(), 2.0f);
}

TEST(LogViewerZoom, SetZoomClampsToBounds)
{
    LogViewer v;
    v.SetZoom(0.1f);
    EXPECT_EQ(v.Zoom(), LogViewer::kMinZoom);
    v.SetZoom(10.0f);
    EXPECT_EQ(v.Zoom(), LogViewer::kMaxZoom);
}

TEST(LogViewerZoom, ZoomEventFires)
{
    struct Rec {
        float captured = 0.0f;
        static void OnZoom(void* ctx, LogViewer&, float& z) {
            static_cast<Rec*>(ctx)->captured = z;
        }
    } rec;
    LogViewer v;
    auto sub = v.ZoomChanged().Subscribe(&rec, &Rec::OnZoom);
    v.SetZoom(0.8f);
    EXPECT_EQ(rec.captured, 0.8f);
}

TEST(LogViewerZoom, CtrlWheelUpZoomsIn)
{
    LogViewer v;
    v.AppendLine({{L"log", kWhite}});
    PointerEventArgs e;
    e.wheelDelta = WHEEL_DELTA;
    e.modifiers = ModifierKeys::Ctrl;
    v.OnPointerWheelChanged(e);
    EXPECT_TRUE(v.Zoom() > 1.0f);
    EXPECT_TRUE(e.handled);
}

TEST(LogViewerZoom, Ctrl0ResetsZoom)
{
    LogViewer v;
    v.SetZoom(1.5f);
    KeyEventArgs e;
    e.vk = '0';
    e.modifiers = ModifierKeys::Ctrl;
    v.OnKeyDownRouted(e);
    EXPECT_EQ(v.Zoom(), 1.0f);
    EXPECT_TRUE(e.handled);
}

TEST(LogViewerZoom, ZoomDisabledIgnoresGestures)
{
    LogViewer v;
    v.SetZoomEnabled(false);
    PointerEventArgs wheel;
    wheel.wheelDelta = WHEEL_DELTA;
    wheel.modifiers = ModifierKeys::Ctrl;
    v.OnPointerWheelChanged(wheel);
    EXPECT_EQ(v.Zoom(), 1.0f);
    EXPECT_FALSE(wheel.handled);
}

// ---------------------------------------------------------------------------
// RichTextBox zoom
// ---------------------------------------------------------------------------

TEST(RichTextBoxZoom, DefaultZoomIs100Percent)
{
    RichTextBox r;
    EXPECT_EQ(r.Zoom(), 1.0f);
}

TEST(RichTextBoxZoom, SetZoomChangesValue)
{
    RichTextBox r;
    r.SetZoom(1.8f);
    EXPECT_EQ(r.Zoom(), 1.8f);
}

TEST(RichTextBoxZoom, SetZoomClampsToBounds)
{
    RichTextBox r;
    r.SetZoom(0.2f);
    EXPECT_EQ(r.Zoom(), RichTextBox::kMinZoom);
    r.SetZoom(8.0f);
    EXPECT_EQ(r.Zoom(), RichTextBox::kMaxZoom);
}

TEST(RichTextBoxZoom, ZoomEventFires)
{
    struct Rec {
        float captured = 0.0f;
        static void OnZoom(void* ctx, RichTextBox&, float& z) {
            static_cast<Rec*>(ctx)->captured = z;
        }
    } rec;
    RichTextBox r;
    auto sub = r.ZoomChanged().Subscribe(&rec, &Rec::OnZoom);
    r.SetZoom(1.3f);
    EXPECT_EQ(rec.captured, 1.3f);
}


TEST(RichTextBoxZoom, CtrlWheelUpZoomsIn)
{
    MockHost host;
    RichTextBox r;
    r.AttachToContext(MakeCtx(host));
    RichTextDocument doc;
    doc.AppendRun({L"text", kInk, 14.0f});
    r.SetDocument(std::move(doc));
    r.Measure(400.0f, 200.0f);
    r.Arrange(RectDip{0, 0, 400, 200});

    PointerEventArgs e;
    e.wheelDelta = WHEEL_DELTA;
    e.modifiers = ModifierKeys::Ctrl;
    e.position = Point{10.0f, 10.0f};
    r.OnPointerWheelChanged(e);
    EXPECT_TRUE(r.Zoom() > 1.0f);
    EXPECT_TRUE(e.handled);
}

TEST(RichTextBoxZoom, Ctrl0ResetsZoom)
{
    RichTextBox r;
    r.SetZoom(1.5f);
    KeyEventArgs e;
    e.vk = '0';
    e.modifiers = ModifierKeys::Ctrl;
    r.OnKeyDownRouted(e);
    EXPECT_EQ(r.Zoom(), 1.0f);
    EXPECT_TRUE(e.handled);
}

TEST(RichTextBoxZoom, ZoomDisabledIgnoresGestures)
{
    MockHost host;
    RichTextBox r;
    r.AttachToContext(MakeCtx(host));
    r.SetZoomEnabled(false);
    RichTextDocument doc;
    doc.AppendRun({L"text", kInk, 14.0f});
    r.SetDocument(std::move(doc));
    r.Measure(400.0f, 200.0f);
    r.Arrange(RectDip{0, 0, 400, 200});

    PointerEventArgs wheel;
    wheel.wheelDelta = WHEEL_DELTA;
    wheel.modifiers = ModifierKeys::Ctrl;
    wheel.position = Point{10.0f, 10.0f};
    r.OnPointerWheelChanged(wheel);
    EXPECT_EQ(r.Zoom(), 1.0f);  // zoom unchanged
    EXPECT_TRUE(wheel.handled);  // but Ctrl+Wheel falls through to plain scroll, which RichTextBox always handles
}

// ---------------------------------------------------------------------------
// Shift+Wheel horizontal scroll
// ---------------------------------------------------------------------------

TEST(DataGridShiftWheel, ShiftWheelScrollsHorizontally)
{
    // DataGrid has columns that scroll horizontally; Shift+Wheel should move them.
    // This is a smoke test (the real horizontal scroll logic is in ScrollViewer).
    // We just confirm the gesture is wired and e.handled flips.
    MockHost host;
    DataGrid g;
    g.AttachToContext(MakeCtx(host));
    g.SetRowCount(3);
    g.SetColumns({{L"C1", 100.0f}, {L"C2", 100.0f}, {L"C3", 100.0f}});
    g.SetCellProvider([](int, int) { return std::wstring(L"cell"); });
    g.Measure(150.0f, 200.0f);
    g.Arrange(RectDip{0, 0, 150, 200});  // narrower than 300, so horizontal scroll exists

    PointerEventArgs e;
    e.wheelDelta = WHEEL_DELTA;
    e.modifiers = ModifierKeys::Shift;
    g.OnPointerWheelChanged(e);
    EXPECT_TRUE(e.handled);
}

TEST(RichTextBoxShiftWheel, ShiftWheelScrollsHorizontallyInNoWrap)
{
    MockHost host;
    RichTextBox r;
    r.AttachToContext(MakeCtx(host));
    r.SetWordWrap(false);
    RichTextDocument doc;
    doc.AppendRun({L"A very long line that exceeds the control width to create horizontal scroll", kInk, 14.0f});
    r.SetDocument(std::move(doc));
    r.Measure(100.0f, 200.0f);
    r.Arrange(RectDip{0, 0, 100, 200});

    PointerEventArgs e;
    e.wheelDelta = WHEEL_DELTA;
    e.modifiers = ModifierKeys::Shift;
    e.position = Point{50.0f, 50.0f};
    r.OnPointerWheelChanged(e);
    EXPECT_TRUE(e.handled);
}
