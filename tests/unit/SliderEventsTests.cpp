// SliderEventsTests.cpp — Slider 的 ValueChanged / ValueChanging 事件。
//
// ValueChanged（已有）：值最终确定时触发 —— SetValue、键盘步进、拖动松手。
// ValueChanging（新增）：拖动预览 —— 每次 OnPointerMoved 都触发，松手时才触发 Changed。
//
// WPF 的 Slider 有同名的两个事件，分工完全一致。某些场景需要这个拆分：一个显示
// 当前拖动位置的 label，只订阅 Changing；一个保存配置的回调，只订阅 Changed（否
// 则拖动中每帧都写盘）。
//
// Headless 驱动方式：Measure + Arrange 填充实际几何（Slider 的归一化用 bounds_），
// 然后直接调路由方法 OnPointerPressed/Moved/Released。input 子系统虽然也能测，
// 但会引入 InputManager 的依赖；这里只测控件逻辑。

#include "../framework/Test.h"
#include "../../FluentUI/controls/Slider.h"
#include "../../FluentUI/input/RoutedEvent.h"

using namespace fluent;

namespace {
struct ValRec {
    float last = -999.0f;
    int count = 0;
    static void On(void* owner, Slider&, float& val) {
        auto* r = static_cast<ValRec*>(owner);
        r->last = val;
        r->count++;
    }
};
}  // namespace

// SetValue 直接触发 ValueChanged，不触发 ValueChanging（不是预览，是确定值）。
TEST(SliderEvents, SetValueFiresChanged) {
    Slider s;
    s.SetMinimum(0.0f);
    s.SetMaximum(100.0f);
    ValRec changed;
    auto sub = s.ValueChanged().Subscribe(&changed, &ValRec::On);

    s.SetValue(42.0f);
    EXPECT_EQ(changed.count, 1);
    EXPECT_EQ(changed.last, 42.0f);
}

// 键盘步进（上下左右箭头）是确定操作，只触发 Changed。
TEST(SliderEvents, KeyStepFiresChangedOnly) {
    Slider s;
    s.SetMinimum(0.0f);
    s.SetMaximum(100.0f);
    s.SetStep(10.0f);
    s.SetValue(50.0f);

    ValRec changing, changed;
    auto s1 = s.ValueChanging().Subscribe(&changing, &ValRec::On);
    auto s2 = s.ValueChanged().Subscribe(&changed, &ValRec::On);

    KeyEventArgs key;
    key.vk = VK_RIGHT;
    s.OnKeyDownRouted(key);

    EXPECT_EQ(changed.last, 60.0f);    // 步进 +10
    EXPECT_EQ(changing.count, 0);      // 键盘不触发 Changing
}

// 拖动中：Press 跳到点击位置（触发 Changing），Move 持续更新（触发 Changing），
// Release 最终确定（触发 Changed）。整个拖动期间 Changed 只在松手时触发一次。
TEST(SliderEvents, DragFiresChangingThenChanged) {
    Slider s;
    s.SetMinimum(0.0f);
    s.SetMaximum(100.0f);
    s.SetWidth(200.0f);
    s.SetHeight(32.0f);
    s.Measure(200.0f, 32.0f);
    s.Arrange(RectDip{0, 0, 200.0f, 32.0f});

    ValRec changing, changed;
    auto s1 = s.ValueChanging().Subscribe(&changing, &ValRec::On);
    auto s2 = s.ValueChanged().Subscribe(&changed, &ValRec::On);

    // Press：跳到 50%（水平中点）
    PointerEventArgs press;
    press.position = Point{100.0f, 16.0f};
    press.button = PointerButton::Left;
    s.OnPointerPressed(press);

    // Press 应触发 Changing（跳转是预览性质），不触发 Changed
    EXPECT_EQ(changing.count, 1);
    EXPECT_TRUE(changing.last > 45.0f && changing.last < 55.0f);
    EXPECT_EQ(changed.count, 0);

    // Move：拖到 75%
    PointerEventArgs move;
    move.position = Point{150.0f, 16.0f};
    s.OnPointerMoved(move);

    EXPECT_EQ(changing.count, 2);      // 再次触发 Changing
    EXPECT_TRUE(changing.last > 70.0f && changing.last < 80.0f);
    EXPECT_EQ(changed.count, 0);       // 仍未触发 Changed

    // Release：确定最终值
    PointerEventArgs release;
    release.position = Point{150.0f, 16.0f};
    release.button = PointerButton::Left;
    s.OnPointerReleased(release);

    EXPECT_EQ(changing.count, 2);      // Changing 不再增长
    EXPECT_EQ(changed.count, 1);       // 首次触发 Changed
    EXPECT_TRUE(changed.last > 70.0f && changed.last < 80.0f);
}

// 零订阅者不崩溃：两个事件都是可选的（demo 可以只显示不订阅）。
TEST(SliderEvents, NoSubscribersDoNotCrash) {
    Slider s;
    s.SetMinimum(0.0f);
    s.SetMaximum(100.0f);
    s.SetValue(50.0f);  // 只触发 ValueChanged，但无人订阅
    // 到这里不崩溃即通过
    EXPECT_TRUE(true);
}

// 多订阅者都触发。
TEST(SliderEvents, ValueChangedMultipleSubscribers) {
    Slider s;
    s.SetMinimum(0.0f);
    s.SetMaximum(100.0f);

    ValRec a, b;
    auto s1 = s.ValueChanged().Subscribe(&a, &ValRec::On);
    auto s2 = s.ValueChanged().Subscribe(&b, &ValRec::On);

    s.SetValue(77.0f);
    EXPECT_EQ(a.count, 1);
    EXPECT_EQ(b.count, 1);
    EXPECT_EQ(a.last, 77.0f);
    EXPECT_EQ(b.last, 77.0f);
}

// Regression: Slider too narrow (bounds.w <= 2*kPadX ~27 DIP) caused division by zero
// in ValueFromX, producing inf/NaN.
TEST(SliderEvents, NarrowSliderDoesNotCauseDivisionByZero) {
    Slider s;
    s.SetMinimum(0.0f);
    s.SetMaximum(100.0f);
    s.SetWidth(20.0f);  // Narrower than 2*kPadX (27.2 DIP)
    s.SetHeight(32.0f);
    s.Measure(20.0f, 32.0f);
    s.Arrange(RectDip{0, 0, 20.0f, 32.0f});
    
    // Click center: should return min (not inf/NaN)
    PointerEventArgs e;
    e.position = {10.0f, 16.0f};
    e.button = PointerButton::Left;
    s.OnPointerPressed(e);
    
    EXPECT_TRUE(std::isfinite(s.Value()));
    EXPECT_EQ(s.Value(), 0.0f);  // Returns min when track width <= 0
}

namespace {
// DisplayedValue() 是 protected 扩展点；测试用一个最薄的子类把它露出来，
// 与 LogSlider 这类下游子类看到的是同一个值。
struct ProbeSlider : Slider {
    float Displayed() const { return DisplayedValue(); }
};
}  // namespace

// 拖动时旋钮必须 1:1 跟手，不能缓动。缓动（tau 0.04s）用在拖动上会变成橡皮筋：
// 旋钮落后光标若干帧再追上。Press 和 Move 各自都必须把显示值直接落到新值上，
// 所以这里在两步之后都检查 —— 只删掉其中一处 SetImmediate 也会被抓到。
// 只看 Value() 测不出这个问题：逻辑值始终正确，错的是画出来的位置。
TEST(SliderEvents, DragSnapsDisplayedValueWithoutEasing) {
    ProbeSlider s;
    s.SetMinimum(0.0f);
    s.SetMaximum(100.0f);
    s.SetWidth(200.0f);
    s.SetHeight(32.0f);
    s.Measure(200.0f, 32.0f);
    s.Arrange(RectDip{0, 0, 200.0f, 32.0f});   // 首次 Arrange：显示值落到 0
    EXPECT_EQ(s.Displayed(), 0.0f);

    PointerEventArgs press;
    press.position = Point{150.0f, 16.0f};
    press.button = PointerButton::Left;
    s.OnPointerPressed(press);
    EXPECT_TRUE(s.Value() > 70.0f);              // 确实跳到了远处
    EXPECT_EQ(s.Displayed(), s.Value());         // 且没有从 0 缓动过去

    PointerEventArgs move;
    move.position = Point{50.0f, 16.0f};
    s.OnPointerMoved(move);
    EXPECT_TRUE(s.Value() < 30.0f);
    EXPECT_EQ(s.Displayed(), s.Value());
    EXPECT_FALSE(s.WantsAnimationTick());        // 没有残留的缓动要追
}

// 按下点正好落在当前值上、不动就松手：没有新值，两个事件都不能触发。
// 订阅 ValueChanged 去写配置的代码，否则每次单击旋钮都会白写一次。
TEST(SliderEvents, PressReleaseWithoutChangeFiresNothing) {
    Slider s;
    s.SetMinimum(0.0f);
    s.SetMaximum(100.0f);
    s.SetValue(50.0f);
    s.SetWidth(200.0f);
    s.SetHeight(32.0f);
    s.Measure(200.0f, 32.0f);
    s.Arrange(RectDip{0, 0, 200.0f, 32.0f});

    ValRec changing, changed;
    auto s1 = s.ValueChanging().Subscribe(&changing, &ValRec::On);
    auto s2 = s.ValueChanged().Subscribe(&changed, &ValRec::On);

    // 轨道两端内缩相同距离，几何中点就是值 50（step 1 吸附掉浮点误差）。
    PointerEventArgs press;
    press.position = Point{100.0f, 16.0f};
    press.button = PointerButton::Left;
    s.OnPointerPressed(press);

    PointerEventArgs release;
    release.position = Point{100.0f, 16.0f};
    release.button = PointerButton::Left;
    s.OnPointerReleased(release);

    EXPECT_EQ(s.Value(), 50.0f);
    EXPECT_EQ(changing.count, 0);
    EXPECT_EQ(changed.count, 0);
}

// 键盘步进触发的缓动，每一帧 tick 都必须把自己标成 Render 脏。AnimatedValue 不会
// 自己标脏：漏掉 Invalidate 时，单独缓动的帧脏区为空，宿主退化为整窗重绘；和别的
// 脏元素（光标闪烁、按钮悬停淡入）同帧时，局部重绘只覆盖对方，Slider 被裁掉，缓动
// 表现为卡住再跳。
TEST(SliderEvents, EaseTickMarksRenderDirty) {
    ProbeSlider s;
    s.SetMinimum(0.0f);
    s.SetMaximum(100.0f);
    s.SetStep(10.0f);
    s.SetWidth(200.0f);
    s.SetHeight(32.0f);
    s.Measure(200.0f, 32.0f);
    s.Arrange(RectDip{0, 0, 200.0f, 32.0f});

    KeyEventArgs key;
    key.vk = VK_RIGHT;
    s.OnKeyDownRouted(key);
    EXPECT_TRUE(s.WantsAnimationTick());      // 键盘步进走缓动

    s.ClearDirty();
    s.OnAnimationTick(1.0f / 60.0f);
    EXPECT_TRUE(Has(s.Dirty(), DirtyFlags::Render));
    EXPECT_FALSE(Has(s.Dirty(), DirtyFlags::Measure));  // 缓动从不改布局
}
