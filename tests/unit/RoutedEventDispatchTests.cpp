// RoutedEventDispatchTests.cpp — verify InputManager raises routed events
// PointerMoved, PointerWheelChanged, PreviewKeyDown, KeyDown, TextInput.
// These events were declared on UIElement but never fired before the dispatch
// entry points (DispatchXxx methods) were added.

#include "../framework/Test.h"
#include "../../FluentUI/input/InputManager.h"
#include "../../FluentUI/input/FocusManager.h"
#include "../../FluentUI/core/UIElement.h"
#include "../../FluentUI/layout/StackPanel.h"

using namespace fluent;

namespace {

// A leaf that subscribes to the 5 events and counts how many times each fires.
struct EventCounter : FrameworkElement {
    int movedCount = 0;
    int wheelCount = 0;
    int previewKeyCount = 0;
    int keyCount = 0;
    int textCount = 0;

    Subscription moveSub, wheelSub, previewSub, keySub, textSub;

    EventCounter() {
        SetFocusable(true);
        moveSub = PointerMoved().Subscribe(this, [](void* owner, UIElement&, PointerEventArgs&) {
            static_cast<EventCounter*>(owner)->movedCount++;
        });
        wheelSub = PointerWheelChanged().Subscribe(this, [](void* owner, UIElement&, PointerEventArgs&) {
            static_cast<EventCounter*>(owner)->wheelCount++;
        });
        previewSub = PreviewKeyDown().Subscribe(this, [](void* owner, UIElement&, KeyEventArgs&) {
            static_cast<EventCounter*>(owner)->previewKeyCount++;
        });
        keySub = KeyDown().Subscribe(this, [](void* owner, UIElement&, KeyEventArgs&) {
            static_cast<EventCounter*>(owner)->keyCount++;
        });
        textSub = TextInput().Subscribe(this, [](void* owner, UIElement&, wchar_t&) {
            static_cast<EventCounter*>(owner)->textCount++;
        });
    }
    void Render(const DrawingContext&) override {}
};

void WireInput(UIElement& e, InputManager* im, FocusManager* fm) {
    UIContext ctx;
    ctx.input = im;
    ctx.focus = fm;
    e.AttachToContext(ctx);
}

}  // namespace

TEST(RoutedEventDispatch, PointerMovedEventFires) {
    InputManager im;
    FocusManager fm;
    EventCounter el;
    el.SetBounds({0, 0, 50, 50});
    WireInput(el, &im, &fm);
    std::vector<UIElement*> roots{&el};
    im.SetRoots(&roots);

    im.PointerMoved(Point{10, 10}, ModifierKeys::None);
    EXPECT_EQ(el.movedCount, 1);

    im.PointerMoved(Point{20, 20}, ModifierKeys::None);
    EXPECT_EQ(el.movedCount, 2);

    el.DetachFromContext();
}

TEST(RoutedEventDispatch, PointerWheelChangedEventFires) {
    InputManager im;
    FocusManager fm;
    EventCounter el;
    el.SetBounds({0, 0, 50, 50});
    WireInput(el, &im, &fm);
    std::vector<UIElement*> roots{&el};
    im.SetRoots(&roots);

    im.PointerWheel(Point{10, 10}, 120, ModifierKeys::None, 3);
    EXPECT_EQ(el.wheelCount, 1);

    el.DetachFromContext();
}

TEST(RoutedEventDispatch, PreviewKeyDownEventFires) {
    InputManager im;
    FocusManager fm;
    im.SetFocusManager(&fm);  // InputManager needs this to dispatch key events
    EventCounter el;
    el.SetBounds({0, 0, 50, 50});
    WireInput(el, &im, &fm);
    std::vector<UIElement*> roots{&el};
    im.SetRoots(&roots);
    fm.SetRoots(&roots);
    fm.SetFocus(&el);

    im.KeyDown('A', ModifierKeys::None);
    EXPECT_EQ(el.previewKeyCount, 1);
    EXPECT_EQ(el.keyCount, 1);

    el.DetachFromContext();
}

TEST(RoutedEventDispatch, TextInputEventFires) {
    InputManager im;
    FocusManager fm;
    im.SetFocusManager(&fm);  // InputManager needs this to dispatch text input
    EventCounter el;
    WireInput(el, &im, &fm);
    std::vector<UIElement*> roots{&el};
    im.SetRoots(&roots);
    fm.SetRoots(&roots);
    fm.SetFocus(&el);

    im.TextInput(L'X');
    EXPECT_EQ(el.textCount, 1);

    im.TextInput(L'Y');
    EXPECT_EQ(el.textCount, 2);

    el.DetachFromContext();
}

// A subscriber marking KeyDown as handled should stop the virtual from running.
TEST(RoutedEventDispatch, HandledStopsPropagation) {
    InputManager im;
    FocusManager fm;
    im.SetFocusManager(&fm);

    struct TestLeaf : FrameworkElement {
        bool virtualRan = false;
        Subscription sub;
        TestLeaf() {
            SetFocusable(true);
            sub = KeyDown().Subscribe(this, [](void*, UIElement&, KeyEventArgs& e) {
                e.handled = true;  // stop before virtual
            });
        }
        void Render(const DrawingContext&) override {}
        void OnKeyDownRouted(KeyEventArgs&) override { virtualRan = true; }
    };

    TestLeaf el;
    WireInput(el, &im, &fm);
    std::vector<UIElement*> roots{&el};
    im.SetRoots(&roots);
    fm.SetRoots(&roots);
    fm.SetFocus(&el);

    im.KeyDown('A', ModifierKeys::None);
    EXPECT_FALSE(el.virtualRan);

    el.DetachFromContext();
}
