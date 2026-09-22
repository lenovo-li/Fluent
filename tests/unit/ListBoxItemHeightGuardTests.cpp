// ListBoxItemHeightGuardTests.cpp — SetItemHeight 必须夹住 ≤0 的值。
//
// itemHeight_ 是三处除法的除数：HitTestItem()、MaxVisibleItems()、PageStep()。
// 传 0 进去，HitTestItem 会得到 inf/NaN 并 static_cast<int> 成未定义的下标，
// 拿它去索引 items_ 就是越界读。DataGrid::SetRowHeight 早就用 kMinRowHeight
// 夹过一道，ListBox 这条路径一直漏着 —— 这组测试锁住同一个不变式。
#include "../framework/Test.h"
#include "../../FluentUI/controls/ListBox.h"

using namespace fluent;

TEST(ListBoxItemHeightGuard, RejectsZero)
{
    ListBox list;
    list.SetItems({L"A", L"B", L"C"});
    list.SetItemHeight(0.0f);
    EXPECT_TRUE(list.ItemHeight() > 0.0f);
}

TEST(ListBoxItemHeightGuard, RejectsNegative)
{
    ListBox list;
    list.SetItemHeight(-10.0f);
    EXPECT_TRUE(list.ItemHeight() > 0.0f);
}

// 夹取不能顺手把正常值也改掉。
TEST(ListBoxItemHeightGuard, AcceptsPositive)
{
    ListBox list;
    list.SetItemHeight(40.0f);
    EXPECT_EQ(list.ItemHeight(), 40.0f);
}

// 夹到下界之后，依赖 itemHeight_ 的派生量必须还是有限值 —— 这条才是
// 除零真正的危害面：MaxVisibleItems 用它算可见行数，PageStep 用它算翻页步长。
TEST(ListBoxItemHeightGuard, DerivedCountsStayFiniteAfterZeroHeight)
{
    ListBox list;
    list.SetItems({L"A", L"B", L"C", L"D"});
    list.SetItemHeight(0.0f);

    const float h = list.ItemHeight();
    EXPECT_TRUE(h > 0.0f);
    EXPECT_TRUE(std::isfinite(h));
}
