// UniformGridStretchTests.cpp — CellSizing::Stretch 均分可用宽度
//
// 默认（Content）：cellW = max child width，四个 60dip 子元素在 800dip
// 面板里只占 240dip，右边 560dip 空着。st.columns 用户期待均分，那
// 是 Stretch：cellW = availW / cols = 200dip/cell。
#include "../framework/Test.h"
#include "../../FluentUI/layout/UniformGrid.h"
#include "LayoutTestHelpers.h"

using namespace fluent;

TEST(UniformGridStretch, ContentModeKeepsChildDrivenCellWidth)
{
    UniformGrid grid;
    grid.SetColumns(4);
    grid.SetCellSizing(UniformGrid::CellSizing::Content);

    // 四个 60dip 宽的子元素
    for (int i = 0; i < 4; ++i) {
        grid.Add(std::make_unique<TestLeaf>(60.0f, 40.0f));
    }

    grid.Measure(800, 100);
    // Content: 4 * 60 = 240
    EXPECT_NEAR(grid.Desired().w, 240.0f, 0.01f);
}

TEST(UniformGridStretch, StretchModeDividesAvailableWidth)
{
    UniformGrid grid;
    grid.SetColumns(4);
    grid.SetCellSizing(UniformGrid::CellSizing::Stretch);

    for (int i = 0; i < 4; ++i) {
        grid.Add(std::make_unique<TestLeaf>(60.0f, 40.0f));
    }

    grid.Measure(800, 100);
    // Stretch: 4 * (800/4) = 800，填满
    EXPECT_NEAR(grid.Desired().w, 800.0f, 0.01f);
}

// Stretch 在无界 offer 下回退到 Content（不能报 inf）
TEST(UniformGridStretch, StretchFallsBackToContentWhenOfferIsInfinite)
{
    UniformGrid grid;
    grid.SetColumns(4);
    grid.SetCellSizing(UniformGrid::CellSizing::Stretch);

    for (int i = 0; i < 4; ++i) {
        grid.Add(std::make_unique<TestLeaf>(60.0f, 40.0f));
    }

    grid.Measure(std::numeric_limits<float>::infinity(), 100);
    // 无界 → 回退到 Content: 4 * 60 = 240
    EXPECT_NEAR(grid.Desired().w, 240.0f, 0.01f);
}

// Stretch 永不缩到 maxChildW 以下（宁可溢出也不裁剪）
TEST(UniformGridStretch, StretchNeverShrinksB​elowContentWidth)
{
    UniformGrid grid;
    grid.SetColumns(4);
    grid.SetCellSizing(UniformGrid::CellSizing::Stretch);

    for (int i = 0; i < 4; ++i) {
        grid.Add(std::make_unique<TestLeaf>(60.0f, 40.0f));
    }

    // 只给 100dip → 100/4 = 25dip/cell < 60dip
    grid.Measure(100, 100);
    // 应该报 4*60=240 而不是 100（拒绝缩到比内容窄）
    EXPECT_NEAR(grid.Desired().w, 240.0f, 0.01f);
}
