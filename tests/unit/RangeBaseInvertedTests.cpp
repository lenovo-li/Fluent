// RangeBaseInvertedTests.cpp — min > max 时 CoerceValue 的行为
//
// `RangeBase::CoerceValue` 是 `std::clamp(v, min_, max_)`，而 std::clamp 在
// `min > max` 时是**未定义行为**（C++17 [alg.clamp]/2 要求 !(hi < lo)）。
// SetMinimum/SetMaximum 各自独立赋值，都不检查另一端，所以两行合法调用就能
// 把区间做倒：
//
//     Slider s;                 // 默认 [0, 100]
//     s.SetMaximum(10.0f);      // [0, 10]
//     s.SetMinimum(50.0f);      // [50, 10]  ← 倒了
//     s.SetValue(30.0f);        // clamp(30, 50, 10) → UB
//
// libstdc++/MSVC 的实现实际返回 hi，但那是实现细节，不是契约。这组测试锁住
// 「不管怎么设，区间都不倒」这个不变式。
#include "../framework/Test.h"
#include "../../FluentUI/controls/Slider.h"

using namespace fluent;

// 先缩小 max 再抬高 min → 区间不能倒
TEST(RangeBaseInverted, MinimumCannotExceedMaximum)
{
    Slider s;
    s.SetMaximum(10.0f);
    s.SetMinimum(50.0f);

    EXPECT_TRUE(s.Minimum() <= s.Maximum());
}

// 反方向：先抬高 min 再压低 max
TEST(RangeBaseInverted, MaximumCannotFallBelowMinimum)
{
    Slider s;
    s.SetMinimum(50.0f);
    s.SetMaximum(10.0f);

    EXPECT_TRUE(s.Minimum() <= s.Maximum());
}

// 区间被做倒之后 SetValue 仍须落在 [min, max] 里 —— 这条才是 UB 的实际危害面
TEST(RangeBaseInverted, ValueStaysInsideRangeAfterInvertedSet)
{
    Slider s;
    s.SetMaximum(10.0f);
    s.SetMinimum(50.0f);
    s.SetValue(30.0f);

    EXPECT_TRUE(s.Value() >= s.Minimum());
    EXPECT_TRUE(s.Value() <= s.Maximum());
}

// 退化区间（min == max）是合法的，value 只能是那一个点
TEST(RangeBaseInverted, DegenerateRangeIsAllowed)
{
    Slider s;
    s.SetMinimum(42.0f);
    s.SetMaximum(42.0f);
    s.SetValue(100.0f);

    EXPECT_NEAR(s.Value(), 42.0f, 0.001f);
}
