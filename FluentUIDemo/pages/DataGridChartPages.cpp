// DataGridChartPages.cpp — standalone showcase page for DataGrid.
//
// Same three-part shape as the InfoBar pages (states, behaviour, in context), with
// one addition specific to this control: a VISIBLE demonstration of virtualization.
//
// Virtualization is the reason this control exists, and it is invisible by construction --
// a grid that draws 100,000 rows looks exactly like one that draws 20 and clips the rest.
// So the page prints its live visible range on screen. That turns an invisible property
// into something a reviewer can watch change while scrolling, and it is the same number
// the headless tests assert.

#include "../GalleryMain.h"
#include "../../FluentUI/window/NativeWindowHost.h"

#include "../../FluentUI/controls/DataGrid.h"
#include "../../FluentUI/controls/Button.h"
#include "../../FluentUI/controls/TextBlock.h"
#include "../../FluentUI/controls/CheckBox.h"
#include "../../FluentUI/controls/ComboBox.h"
#include "../../FluentUI/controls/Slider.h"
#include "../../FluentUI/controls/InfoBar.h"
#include "../../FluentUI/layout/StackPanel.h"
#include "../../FluentUI/layout/Grid.h"
#include "../../FluentUI/layout/Border.h"
#include "../../FluentUI/layout/ScrollPanel.h"
#include "../../FluentUI/layout/GroupBox.h"

#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

namespace fluent {

namespace {

// --- Deterministic sample data ---------------------------------------------
// A fixed-seed LCG rather than <random>: reproducible across standard library versions,
// so the page shows the same numbers on every run and a screenshot stays comparable.
struct Lcg {
    uint32_t s;
    explicit Lcg(uint32_t seed) : s(seed) {}
    double Next() {
        s = s * 1664525u + 1013904223u;
        return static_cast<double>(s >> 8) / 16777216.0;
    }
};

struct Row {
    std::wstring code, name, sector;
    double price, changePct, volume, pe;
};

// 50,000 rows. Deliberately large: the point of the page is that the row COUNT does not
// affect the frame cost, and a number a reviewer can see is more convincing than a claim.
const std::vector<Row>& BigTable() {
    static std::vector<Row> rows = [] {
        static const wchar_t* names[] = {
            L"贵州茅台", L"宁德时代", L"比亚迪", L"招商银行", L"中国平安",
            L"隆基绿能", L"美的集团", L"五粮液", L"东方财富", L"万科A"};
        static const wchar_t* sectors[] = {
            L"食品饮料", L"电池", L"汽车", L"银行", L"保险",
            L"光伏", L"家电", L"食品饮料", L"证券", L"房地产"};
        std::vector<Row> out;
        out.reserve(50000);
        Lcg rng(20260823u);
        for (int i = 0; i < 50000; ++i) {
            Row r;
            wchar_t code[12];
            std::swprintf(code, 12, L"%06d", 600000 + i);
            r.code = code;
            const size_t k = static_cast<size_t>(i) % 10;
            r.name = std::wstring(names[k]) + L"-" + std::to_wstring(i / 10 + 1);
            r.sector = sectors[k];
            r.price = 5.0 + rng.Next() * 300.0;
            r.changePct = (rng.Next() - 0.45) * 10.0;
            r.volume = 1e5 + rng.Next() * 9e6;
            r.pe = 5.0 + rng.Next() * 80.0;
            out.push_back(std::move(r));
        }
        return out;
    }();
    return rows;
}

std::wstring Fmt(double v, int dec) {
    wchar_t buf[48];
    std::swprintf(buf, 48, L"%.*f", dec, v);
    return buf;
}

std::wstring FmtSigned(double v) {
    wchar_t buf[48];
    std::swprintf(buf, 48, L"%+.2f%%", v);
    return buf;
}

}  // namespace

// ---------------------------------------------------------------------------
// DataGrid
// ---------------------------------------------------------------------------

std::unique_ptr<ScrollPanel> GalleryApp::CreateDataGridPage() {
    auto [page, content] = CreatePageShell(L"DataGrid");

    // --- 1. Virtualization, made visible ---------------------------------
    {
        auto* card = CreateExampleCard(content, L"5 万行表格 —— 虚拟化实况");
        auto* intro = card->Add(std::make_unique<TextBlock>());
        intro->SetText(L"下面这张表有 50,000 行 × 7 列 = 35 万个单元格，但每帧只有可见的"
                       L"十几行被取值和绘制。滚动时看下面那行数字：可见范围会移动，"
                       L"但数量不变 —— 这正是 headless 测试断言的同一个数。");
        intro->SetWrap(true);
        intro->SetDimmed(true);

        auto* grid = card->Add(std::make_unique<DataGrid>());
        // Explicit height is CORRECT here: a virtualized table has no natural height (its
        // content is 50000 * 28 = 1.4 million DIP), so the caller must pick a viewport.
        // Contrast a metric strip, where typing a height clipped the text.
        grid->SetHeight(320.0f);
        grid->SetColumns({
            {L"代码",    90.0f, DataGrid::Align::Left},
            {L"名称",   150.0f, DataGrid::Align::Left},
            {L"行业",   110.0f, DataGrid::Align::Left},
            {L"最新价",  90.0f, DataGrid::Align::Right},
            {L"涨跌幅",  90.0f, DataGrid::Align::Right},
            {L"成交量", 120.0f, DataGrid::Align::Right},
            {L"市盈率",  90.0f, DataGrid::Align::Right},
        });
        grid->SetRowCount(static_cast<int>(BigTable().size()));
        grid->SetAlternatingRowFill(true);
        grid->SetCellProvider([](int r, int c) -> std::wstring {
            const Row& row = BigTable()[static_cast<size_t>(r)];
            switch (c) {
                case 0: return row.code;
                case 1: return row.name;
                case 2: return row.sector;
                case 3: return Fmt(row.price, 2);
                case 4: return FmtSigned(row.changePct);
                case 5: return Fmt(row.volume, 0);
                default: return Fmt(row.pe, 1);
            }
        });

        auto* status = card->Add(std::make_unique<TextBlock>());
        status->SetDimmed(true);
        status->SetText(L"（滚动或点击以更新）");
        gridDemo_ = grid;
        gridStatus_ = status;

        auto refresh = [](GalleryApp* self) {
            if (!self->gridDemo_ || !self->gridStatus_) return;
            int fr = 0, lr = -1, fc = 0, lc = -1;
            self->gridDemo_->VisibleRowRange(fr, lr);
            self->gridDemo_->VisibleColumnRange(fc, lc);
            const int rows = lr - fr + 1;
            const int cols = lc - fc + 1;
            wchar_t buf[256];
            std::swprintf(buf, 256,
                L"可见行 %d..%d（%d 行）× 可见列 %d..%d（%d 列）= 每帧 %d 个单元格；"
                L"总计 %d 行。选中第 %d 行。",
                fr, lr, rows, fc, lc, cols, rows * cols,
                self->gridDemo_->RowCount(), self->gridDemo_->SelectedRow());
            self->gridStatus_->SetText(buf);
        };
        // Selection change is the only event that fires on scroll-by-keyboard as well as
        // on click, so it is enough to keep the readout live for this demo.
        gridSubs_ += grid->SelectionChanged().Subscribe(this,
            [](void* o, DataGrid&, RoutedEventArgs&) {
                auto* self = static_cast<GalleryApp*>(o);
                if (!self->gridDemo_ || !self->gridStatus_) return;
                int fr = 0, lr = -1, fc = 0, lc = -1;
                self->gridDemo_->VisibleRowRange(fr, lr);
                self->gridDemo_->VisibleColumnRange(fc, lc);
                wchar_t buf[256];
                std::swprintf(buf, 256,
                    L"可见行 %d..%d（%d 行）× 可见列 %d..%d（%d 列）= 每帧 %d 个单元格；"
                    L"总计 %d 行。选中第 %d 行。",
                    fr, lr, lr - fr + 1, fc, lc, lc - fc + 1,
                    (lr - fr + 1) * (lc - fc + 1),
                    self->gridDemo_->RowCount(), self->gridDemo_->SelectedRow());
                self->gridStatus_->SetText(buf);
            });
        (void)refresh;

        auto* jump = card->Add(std::make_unique<Grid>());
        jump->AddRow(GridLength::Auto());
        for (int i = 0; i < 4; ++i) jump->AddColumn(GridLength::Star(1.0f));
        // One captureless handler per button. Event::Subscribe deliberately refuses
        // capturing lambdas (that would need a heap allocation and break its
        // zero-allocation property), so the target row cannot be captured -- it is
        // baked into a distinct function per button via a template parameter instead.
        struct Jump { const wchar_t* label; int row; };
        static constexpr Jump kJumps[] = {
            {L"跳到第 1 行", 0}, {L"第 1 万行", 9999},
            {L"第 4 万行", 39999}, {L"最后一行", 49999}};
        auto addJump = [&](int slot, auto handler) {
            auto* b = jump->Add(std::make_unique<Button>());
            jump->SetCell(b, 0, slot);
            b->SetText(kJumps[slot].label);
            b->SetMargin(Thickness{0, 0, 4.0f, 0});
            gridSubs_ += b->Click().Subscribe(this, handler);
        };
        addJump(0, [](void* o, Button&, RoutedEventArgs&) {
            auto* s = static_cast<GalleryApp*>(o);
            if (s->gridDemo_) { s->gridDemo_->SetSelectedRow(kJumps[0].row);
                                s->gridDemo_->ScrollRowIntoView(kJumps[0].row); }
        });
        addJump(1, [](void* o, Button&, RoutedEventArgs&) {
            auto* s = static_cast<GalleryApp*>(o);
            if (s->gridDemo_) { s->gridDemo_->SetSelectedRow(kJumps[1].row);
                                s->gridDemo_->ScrollRowIntoView(kJumps[1].row); }
        });
        addJump(2, [](void* o, Button&, RoutedEventArgs&) {
            auto* s = static_cast<GalleryApp*>(o);
            if (s->gridDemo_) { s->gridDemo_->SetSelectedRow(kJumps[2].row);
                                s->gridDemo_->ScrollRowIntoView(kJumps[2].row); }
        });
        addJump(3, [](void* o, Button&, RoutedEventArgs&) {
            auto* s = static_cast<GalleryApp*>(o);
            if (s->gridDemo_) { s->gridDemo_->SetSelectedRow(kJumps[3].row);
                                s->gridDemo_->ScrollRowIntoView(kJumps[3].row); }
        });
    }

    // --- 2. Options ------------------------------------------------------
    {
        auto* card = CreateExampleCard(content, L"外观开关");
        auto* hint = card->Add(std::make_unique<TextBlock>());
        hint->SetText(L"斑马纹默认关闭：它帮助宽表、干扰窄表，所以由调用者决定。");
        hint->SetWrap(true);
        hint->SetDimmed(true);

        auto* mini = card->Add(std::make_unique<DataGrid>());
        mini->SetHeight(180.0f);
        mini->SetColumns({
            {L"科目",   140.0f, DataGrid::Align::Left},
            {L"本期",   100.0f, DataGrid::Align::Right},
            {L"上期",   100.0f, DataGrid::Align::Right},
            {L"同比",   100.0f, DataGrid::Align::Right},
        });
        mini->SetRowCount(8);
        mini->SetCellProvider([](int r, int c) -> std::wstring {
            static const wchar_t* items[8] = {
                L"营业收入", L"营业成本", L"毛利润", L"净利润",
                L"经营现金流", L"总资产", L"总负债", L"股东权益"};
            static const double cur[8] = {68.4, 41.2, 27.2, 9.1, 11.3, 210.5, 88.0, 122.5};
            static const double prv[8] = {60.9, 37.8, 23.1, 9.6, 8.7, 198.2, 84.1, 114.1};
            switch (c) {
                case 0: return items[static_cast<size_t>(r)];
                case 1: return Fmt(cur[static_cast<size_t>(r)], 1);
                case 2: return Fmt(prv[static_cast<size_t>(r)], 1);
                default: {
                    const double a = cur[static_cast<size_t>(r)];
                    const double b = prv[static_cast<size_t>(r)];
                    return FmtSigned((a - b) / b * 100.0);
                }
            }
        });

        auto* opts = card->Add(std::make_unique<Grid>());
        opts->AddRow(GridLength::Auto());
        opts->AddColumn(GridLength::Star(1.0f));
        opts->AddColumn(GridLength::Star(1.0f));

        auto* zebra = opts->Add(std::make_unique<CheckBox>());
        opts->SetCell(zebra, 0, 0);
        zebra->SetText(L"斑马纹 AlternatingRowFill");
        gridSubs_ += zebra->Checked().Subscribe(mini,
            [](void* o, CheckBox& cb, bool&) {
                static_cast<DataGrid*>(o)->SetAlternatingRowFill(cb.IsChecked());
            });

        auto* lines = opts->Add(std::make_unique<CheckBox>());
        opts->SetCell(lines, 0, 1);
        lines->SetText(L"网格线 ShowGridLines");
        lines->SetChecked(true);
        gridSubs_ += lines->Checked().Subscribe(mini,
            [](void* o, CheckBox& cb, bool&) {
                static_cast<DataGrid*>(o)->SetShowGridLines(cb.IsChecked());
            });
    }


    // --- 4. Dynamic add/remove/refresh -----------------------------------
    {
        auto* card = CreateExampleCard(content, L"动态增删与刷新 —— SetRowCount + RefreshCells");
        auto* hint = card->Add(std::make_unique<TextBlock>());
        hint->SetText(L"SetRowCount 改行数。行数不变但内容变（同位置的值被修改）时必须调 "
                      L"RefreshCells()，否则界面停在旧内容上（本页第一张表的 bug 就是这个）。");
        hint->SetWrap(true);
        hint->SetDimmed(true);

        // Store 5 stock rows, manipulate them with buttons.
        struct DemoRow { std::wstring code; std::wstring name; double price; double chg; };
        static std::vector<DemoRow> demoData = {
            {L"600519", L"贵州茅台", 1680.50,  2.35},
            {L"000858", L"五粮液",   185.20, -1.12},
            {L"600036", L"招商银行",  38.40,  0.58},
        };

        auto* demoGrid = card->Add(std::make_unique<DataGrid>());
        demoGrid->SetHeight(200.0f);
        demoGrid->SetColumns({
            {L"代码",    90.0f, DataGrid::Align::Left},
            {L"名称",   150.0f, DataGrid::Align::Left},
            {L"最新价",  90.0f, DataGrid::Align::Right},
            {L"涨跌幅",  90.0f, DataGrid::Align::Right},
        });
        demoGrid->SetRowCount(static_cast<int>(demoData.size()));
        demoGrid->SetCellProvider([](int r, int c) -> std::wstring {
            if (r < 0 || static_cast<size_t>(r) >= demoData.size()) return L"";
            const auto& row = demoData[static_cast<size_t>(r)];
            switch (c) {
                case 0: return row.code;
                case 1: return row.name;
                case 2: return Fmt(row.price, 2);
                default: return FmtSigned(row.chg);
            }
        });
        NativeWindowHost* owner = ownerWindow_ ? ownerWindow_() : nullptr;
        demoGrid->SetRowColorProvider([owner](int r, D2D1_COLOR_F& out) {
            if (!owner || r < 0 || static_cast<size_t>(r) >= demoData.size()) return false;
            const double chg = demoData[static_cast<size_t>(r)].chg;
            const ColorTokens& c = owner->Theme().colors;
            out = chg >= 0.0 ? c.dataPositive : c.dataNegative;
            return true;
        });

        auto* toolbar = card->Add(std::make_unique<Grid>());
        toolbar->AddRow(GridLength::Auto());
        for (int i = 0; i < 5; ++i) toolbar->AddColumn(GridLength::Star(1.0f));

        auto* btnAddTop = toolbar->Add(std::make_unique<Button>());
        toolbar->SetCell(btnAddTop, 0, 0);
        btnAddTop->SetText(L"顶部插入");
        btnAddTop->SetMargin(Thickness{0, 0, 4.0f, 0});
        gridSubs_ += btnAddTop->Click().Subscribe(demoGrid,
            [](void* o, Button&, RoutedEventArgs&) {
                auto* grid = static_cast<DataGrid*>(o);
                demoData.insert(demoData.begin(), {L"688981", L"中芯国际", 45.60, 3.21});
                grid->SetRowCount(static_cast<int>(demoData.size()));
            });

        auto* btnAddBottom = toolbar->Add(std::make_unique<Button>());
        toolbar->SetCell(btnAddBottom, 0, 1);
        btnAddBottom->SetText(L"末尾追加");
        btnAddBottom->SetMargin(Thickness{0, 0, 4.0f, 0});
        gridSubs_ += btnAddBottom->Click().Subscribe(demoGrid,
            [](void* o, Button&, RoutedEventArgs&) {
                auto* grid = static_cast<DataGrid*>(o);
                demoData.push_back({L"002475", L"立讯精密", 32.80, -0.45});
                grid->SetRowCount(static_cast<int>(demoData.size()));
            });

        auto* btnRemove = toolbar->Add(std::make_unique<Button>());
        toolbar->SetCell(btnRemove, 0, 2);
        btnRemove->SetText(L"删除选中行");
        btnRemove->SetMargin(Thickness{0, 0, 4.0f, 0});
        gridSubs_ += btnRemove->Click().Subscribe(demoGrid,
            [](void* o, Button&, RoutedEventArgs&) {
                auto* grid = static_cast<DataGrid*>(o);
                const int sel = grid->SelectedRow();
                if (sel < 0 || static_cast<size_t>(sel) >= demoData.size()) return;
                demoData.erase(demoData.begin() + sel);
                grid->SetRowCount(static_cast<int>(demoData.size()));
            });

        auto* btnModify = toolbar->Add(std::make_unique<Button>());
        toolbar->SetCell(btnModify, 0, 3);
        btnModify->SetText(L"修改选中行价格");
        btnModify->SetMargin(Thickness{0, 0, 4.0f, 0});
        gridSubs_ += btnModify->Click().Subscribe(demoGrid,
            [](void* o, Button&, RoutedEventArgs&) {
                auto* grid = static_cast<DataGrid*>(o);
                const int sel = grid->SelectedRow();
                if (sel < 0 || static_cast<size_t>(sel) >= demoData.size()) return;
                // Modify data in-place: change price ±5%, flip sign on changePct
                auto& row = demoData[static_cast<size_t>(sel)];
                row.price *= (row.chg >= 0 ? 1.05 : 0.95);
                row.chg = -row.chg;
                // Row count unchanged, so SetRowCount would no-op. Must call RefreshCells.
                grid->RefreshCells();
            });

        auto* btnClear = toolbar->Add(std::make_unique<Button>());
        toolbar->SetCell(btnClear, 0, 4);
        btnClear->SetText(L"清空");
        gridSubs_ += btnClear->Click().Subscribe(demoGrid,
            [](void* o, Button&, RoutedEventArgs&) {
                auto* grid = static_cast<DataGrid*>(o);
                demoData.clear();
                grid->SetRowCount(0);
            });
    }

    // --- 5. Interactive features: multi-select, sort, autosize, clear ------
    {
        auto* card = CreateExampleCard(content, L"交互功能 —— 多选 / 排序 / 自动调整列宽");
        auto* hint = card->Add(std::make_unique<TextBlock>());
        hint->SetText(L"SetSelectionMode(Multiple) 开启 Ctrl/Shift 多选；SetSortColumn 显示排序箭头 ▲▼；"
                      L"AutosizeColumn 把列宽调整为刚好容纳最宽的单元格（表头 + 可见行）；"
                      L"ColumnHeaderClicked 事件可以监听表头点击。");
        hint->SetWrap(true);
        hint->SetDimmed(true);

        struct Employee { const wchar_t* name; const wchar_t* dept; const wchar_t* role; int years; };
        static const Employee employees[] = {
            {L"张伟", L"技术部", L"高级工程师", 8},
            {L"李娜", L"市场部", L"市场经理", 5},
            {L"王强", L"技术部", L"架构师", 12},
            {L"刘芳", L"人力资源", L"招聘专员", 3},
            {L"陈杰", L"财务部", L"财务分析师", 7},
            {L"杨雪", L"技术部", L"前端工程师", 4},
            {L"赵敏", L"市场部", L"品牌总监", 10},
            {L"孙浩", L"技术部", L"测试工程师", 6},
        };

        auto* empGrid = card->Add(std::make_unique<DataGrid>());
        empGrid->SetHeight(280.0f);
        empGrid->SetColumns({
            {L"姓名",  100.0f, DataGrid::Align::Left},
            {L"部门",  120.0f, DataGrid::Align::Left},
            {L"职位",  150.0f, DataGrid::Align::Left},
            {L"工龄",   80.0f, DataGrid::Align::Right},
        });
        empGrid->SetRowCount(8);
        empGrid->SetSelectionMode(DataGrid::SelectionMode::Multiple);
        empGrid->SetCellProvider([](int r, int c) -> std::wstring {
            const auto& e = employees[r];
            switch (c) {
                case 0: return e.name;
                case 1: return e.dept;
                case 2: return e.role;
                default: return std::to_wstring(e.years);
            }
        });

        multiGridDemo_ = empGrid;

        auto* empStatus = card->Add(std::make_unique<TextBlock>());
        empStatus->SetDimmed(true);
        empStatus->SetText(L"（未选中）");
        multiGridStatus_ = empStatus;

        gridSubs_ += empGrid->SelectionChanged().Subscribe(this,
            [](void* o, DataGrid& g, RoutedEventArgs&) {
                auto* self = static_cast<GalleryApp*>(o);
                if (!self->multiGridStatus_) return;
                auto rows = g.SelectedRows();
                if (rows.empty()) {
                    self->multiGridStatus_->SetText(L"（未选中）");
                } else {
                    std::wstring txt = L"已选中 " + std::to_wstring(rows.size()) + L" 行：";
                    for (size_t i = 0; i < rows.size() && i < 5; ++i) {
                        if (i > 0) txt += L", ";
                        txt += employees[rows[i]].name;
                    }
                    if (rows.size() > 5) txt += L" ...";
                    self->multiGridStatus_->SetText(txt);
                }
            });

        gridSubs_ += empGrid->ColumnHeaderClicked().Subscribe(empGrid,
            [](void* o, DataGrid& g, RoutedEventArgs&) {
                const int col = g.ColumnClickedIndex();
                // Click a header: cycle None → Ascending → Descending → None
                if (g.SortColumn() == col) {
                    auto dir = g.GetSortDirection();
                    if (dir == DataGrid::SortDirection::Ascending)
                        g.SetSortColumn(col, DataGrid::SortDirection::Descending);
                    else
                        g.ClearSort();
                } else {
                    g.SetSortColumn(col, DataGrid::SortDirection::Ascending);
                }
            });

        auto* toolbar = card->Add(std::make_unique<Grid>());
        toolbar->AddRow(GridLength::Auto());
        for (int i = 0; i < 4; ++i) toolbar->AddColumn(GridLength::Star(1.0f));

        auto* cbMulti = toolbar->Add(std::make_unique<CheckBox>());
        toolbar->SetCell(cbMulti, 0, 0);
        cbMulti->SetText(L"多选模式");
        cbMulti->SetChecked(true);
        gridSubs_ += cbMulti->Checked().Subscribe(empGrid,
            [](void* o, CheckBox& cb, bool&) {
                auto* g = static_cast<DataGrid*>(o);
                g->SetSelectionMode(cb.IsChecked() ? DataGrid::SelectionMode::Multiple
                                                   : DataGrid::SelectionMode::Single);
            });

        auto* btnSort = toolbar->Add(std::make_unique<Button>());
        toolbar->SetCell(btnSort, 0, 1);
        btnSort->SetText(L"按姓名排序");
        btnSort->SetMargin(Thickness{4.0f, 0, 0, 0});
        gridSubs_ += btnSort->Click().Subscribe(empGrid,
            [](void* o, Button&, RoutedEventArgs&) {
                auto* g = static_cast<DataGrid*>(o);
                if (g->SortColumn() == 0) {
                    auto dir = g->GetSortDirection();
                    if (dir == DataGrid::SortDirection::Ascending)
                        g->SetSortColumn(0, DataGrid::SortDirection::Descending);
                    else
                        g->ClearSort();
                } else {
                    g->SetSortColumn(0, DataGrid::SortDirection::Ascending);
                }
            });

        auto* btnAutosize = toolbar->Add(std::make_unique<Button>());
        toolbar->SetCell(btnAutosize, 0, 2);
        btnAutosize->SetText(L"自动调整【部门】列宽");
        btnAutosize->SetMargin(Thickness{4.0f, 0, 0, 0});
        gridSubs_ += btnAutosize->Click().Subscribe(empGrid,
            [](void* o, Button&, RoutedEventArgs&) {
                static_cast<DataGrid*>(o)->AutosizeColumn(1);
            });

        auto* btnClear = toolbar->Add(std::make_unique<Button>());
        toolbar->SetCell(btnClear, 0, 3);
        btnClear->SetText(L"清空选择");
        btnClear->SetMargin(Thickness{4.0f, 0, 0, 0});
        gridSubs_ += btnClear->Click().Subscribe(empGrid,
            [](void* o, Button&, RoutedEventArgs&) {
                static_cast<DataGrid*>(o)->ClearSelection();
            });
    }

    // --- 6. Edit and reorder ----------------------------------------------
    {
        auto* card = CreateExampleCard(content, L"编辑与重排 —— F2 编辑 / 长按拖动");
        auto* hint = card->Add(std::make_unique<TextBlock>());
        hint->SetText(L"SetEditingEnabled 开启单元格编辑：双击或 F2 开始编辑，Enter 提交，Escape 取消。"
                      L"SetRowReorderEnabled 开启行拖拽：长按 300ms 后拖动，释放时触发 RowsReordered 事件。");
        hint->SetWrap(true);
        hint->SetDimmed(true);

        struct Task { std::wstring name; std::wstring status; std::wstring priority; std::wstring deadline; };
        static std::vector<Task> tasks = {
            {L"完成设计稿", L"进行中", L"高", L"2026-09-10"},
            {L"代码审查", L"待开始", L"中", L"2026-09-12"},
            {L"客户会议", L"已完成", L"高", L"2026-09-08"},
            {L"更新文档", L"进行中", L"低", L"2026-09-15"},
        };

        auto* taskGrid = card->Add(std::make_unique<DataGrid>());
        taskGrid->SetHeight(200.0f);
        taskGrid->SetColumns({
            {L"任务",    200.0f, DataGrid::Align::Left, true},
            {L"状态",    100.0f, DataGrid::Align::Left, true},
            {L"优先级",   80.0f, DataGrid::Align::Left, true},
            {L"截止日期", 120.0f, DataGrid::Align::Left, false},
        });
        taskGrid->SetRowCount(static_cast<int>(tasks.size()));
        taskGrid->SetCellProvider([](int r, int c) -> std::wstring {
            if (r < 0 || static_cast<size_t>(r) >= tasks.size()) return L"";
            const auto& t = tasks[static_cast<size_t>(r)];
            switch (c) {
                case 0: return t.name;
                case 1: return t.status;
                case 2: return t.priority;
                default: return t.deadline;
            }
        });

        gridSubs_ += taskGrid->CellEdited().Subscribe(taskGrid,
            [](void* o, DataGrid& g, RoutedEventArgs&) {
                int row = g.EditedRow();
                int col = g.EditedColumn();
                const std::wstring& newVal = g.EditedText();
                if (row < 0 || static_cast<size_t>(row) >= tasks.size()) return;
                auto& t = tasks[static_cast<size_t>(row)];
                switch (col) {
                    case 0: t.name = newVal; break;
                    case 1: t.status = newVal; break;
                    case 2: t.priority = newVal; break;
                    case 3: t.deadline = newVal; break;
                }
                g.RefreshCells();
            });

        taskGrid->SetCellValidator([](int row, int col, const std::wstring& val) {
            return !val.empty();
        });

        gridSubs_ += taskGrid->RowsReordered().Subscribe(taskGrid,
            [](void* o, DataGrid& g, RoutedEventArgs&) {
                const std::vector<int>& oldIndices = g.ReorderedOldIndices();
                const int newIndex = g.ReorderedNewIndex();
                if (oldIndices.empty()) return;
                std::vector<Task> selected;
                for (int idx : oldIndices)
                    if (idx >= 0 && static_cast<size_t>(idx) < tasks.size())
                        selected.push_back(tasks[static_cast<size_t>(idx)]);
                for (int i = static_cast<int>(oldIndices.size()) - 1; i >= 0; --i) {
                    int idx = oldIndices[static_cast<size_t>(i)];
                    if (idx >= 0 && static_cast<size_t>(idx) < tasks.size())
                        tasks.erase(tasks.begin() + idx);
                }
                int insertPos = newIndex;
                for (size_t i = 0; i < oldIndices.size(); ++i) {
                    if (oldIndices[i] < newIndex) --insertPos;
                }
                tasks.insert(tasks.begin() + insertPos, selected.begin(), selected.end());
                g.SetRowCount(static_cast<int>(tasks.size()));
            });

        auto* toolbar = card->Add(std::make_unique<Grid>());
        toolbar->AddRow(GridLength::Auto());
        toolbar->AddColumn(GridLength::Star(1.0f));
        toolbar->AddColumn(GridLength::Star(1.0f));

        auto* cbEdit = toolbar->Add(std::make_unique<CheckBox>());
        toolbar->SetCell(cbEdit, 0, 0);
        cbEdit->SetText(L"可编辑（F2 开始编辑）");
        gridSubs_ += cbEdit->Checked().Subscribe(taskGrid,
            [](void* o, CheckBox& cb, bool&) {
                static_cast<DataGrid*>(o)->SetEditingEnabled(cb.IsChecked());
            });

        auto* cbReorder = toolbar->Add(std::make_unique<CheckBox>());
        toolbar->SetCell(cbReorder, 0, 1);
        cbReorder->SetText(L"行拖拽（长按拖动）");
        gridSubs_ += cbReorder->Checked().Subscribe(taskGrid,
            [](void* o, CheckBox& cb, bool&) {
                static_cast<DataGrid*>(o)->SetRowReorderEnabled(cb.IsChecked());
            });
    }

    // --- 7. Frozen columns and global switches ----------------------------
    {
        auto* card = CreateExampleCard(content, L"固定列与开关 —— frozen / resizable / 全局禁用");
        auto* hint = card->Add(std::make_unique<TextBlock>());
        hint->SetText(L"这张表比视口宽，需要横向滚动。前两列 frozen=true 被钉在左边不随横滚移动，"
                      L"「季度」列 resizable=false 单独禁止拖宽。下面两个开关是全局的：关掉列宽调整后"
                      L"所有分隔线都不再响应；关掉复制后 Ctrl+C 不再拦截（事件留给上层处理）。");
        hint->SetWrap(true);
        hint->SetDimmed(true);

        struct Store {
            const wchar_t* region; const wchar_t* name; const wchar_t* quarter;
            double sales; double cost; double profit; double growth; int employees;
        };
        static const Store stores[] = {
            {L"华东", L"上海南京路店", L"2026Q1", 1284.5, 902.1, 382.4, 12.3, 45},
            {L"华东", L"杭州武林店",   L"2026Q1",  876.2, 640.8, 235.4,  8.7, 32},
            {L"华北", L"北京王府井店", L"2026Q1", 1520.9,1044.3, 476.6, 15.2, 58},
            {L"华北", L"天津滨海店",   L"2026Q1",  642.0, 501.7, 140.3,  5.1, 28},
            {L"华南", L"广州天河店",   L"2026Q1", 1105.4, 780.2, 325.2, 10.8, 41},
            {L"华南", L"深圳福田店",   L"2026Q1", 1398.7, 951.6, 447.1, 18.9, 52},
            {L"西南", L"成都春熙路店", L"2026Q1",  789.3, 590.4, 198.9,  6.4, 30},
        };

        auto* wideGrid = card->Add(std::make_unique<DataGrid>());
        wideGrid->SetHeight(260.0f);
        wideGrid->SetColumns({
            {L"地区",   80.0f, DataGrid::Align::Left, true,  true},   // resizable, frozen
            {L"门店",  140.0f, DataGrid::Align::Left, true,  true},
            {L"季度",   80.0f, DataGrid::Align::Left, false, false},  // NOT resizable, not frozen
            {L"销售额", 100.0f, DataGrid::Align::Right},
            {L"成本",   100.0f, DataGrid::Align::Right},
            {L"利润",   100.0f, DataGrid::Align::Right},
            {L"增长率", 100.0f, DataGrid::Align::Right},
            {L"员工数", 100.0f, DataGrid::Align::Right},
        });
        wideGrid->SetRowCount(7);
        wideGrid->SetAlternatingRowFill(true);
        wideGrid->SetCellProvider([](int r, int c) -> std::wstring {
            const auto& s = stores[r];
            switch (c) {
                case 0: return s.region;
                case 1: return s.name;
                case 2: return s.quarter;
                case 3: return Fmt(s.sales, 1);
                case 4: return Fmt(s.cost, 1);
                case 5: return Fmt(s.profit, 1);
                case 6: return FmtSigned(s.growth);
                default: return std::to_wstring(s.employees);
            }
        });

        wideGridDemo_ = wideGrid;

        auto* measureStatus = card->Add(std::make_unique<TextBlock>());
        measureStatus->SetDimmed(true);
        measureStatus->SetText(L"（点下面的按钮量一次「门店」列）");
        wideGridMeasure_ = measureStatus;

        auto* toolbar = card->Add(std::make_unique<Grid>());
        toolbar->AddRow(GridLength::Auto());
        for (int i = 0; i < 3; ++i) toolbar->AddColumn(GridLength::Star(1.0f));

        auto* cbResize = toolbar->Add(std::make_unique<CheckBox>());
        toolbar->SetCell(cbResize, 0, 0);
        cbResize->SetText(L"允许列宽调整（全局）");
        cbResize->SetChecked(true);
        gridSubs_ += cbResize->Checked().Subscribe(wideGrid,
            [](void* o, CheckBox& cb, bool&) {
                static_cast<DataGrid*>(o)->SetColumnResizeEnabled(cb.IsChecked());
            });

        auto* cbCopy = toolbar->Add(std::make_unique<CheckBox>());
        toolbar->SetCell(cbCopy, 0, 1);
        cbCopy->SetText(L"允许 Ctrl+C 复制");
        cbCopy->SetChecked(true);
        gridSubs_ += cbCopy->Checked().Subscribe(wideGrid,
            [](void* o, CheckBox& cb, bool&) {
                static_cast<DataGrid*>(o)->SetCopyEnabled(cb.IsChecked());
            });

        auto* btnMeasure = toolbar->Add(std::make_unique<Button>());
        toolbar->SetCell(btnMeasure, 0, 2);
        btnMeasure->SetText(L"量「门店」列但不改");
        btnMeasure->SetMargin(Thickness{4.0f, 0, 0, 0});
        gridSubs_ += btnMeasure->Click().Subscribe(this,
            [](void* o, Button&, RoutedEventArgs&) {
                auto* self = static_cast<GalleryApp*>(o);
                if (!self->wideGridDemo_ || !self->wideGridMeasure_) return;
                float measured = self->wideGridDemo_->MeasureColumnContent(1);
                float current = self->wideGridDemo_->Columns()[1].width;
                wchar_t buf[128];
                std::swprintf(buf, 128, L"测量结果：%.1f DIP（当前 %.1f，差 %.1f）",
                              measured, current, measured - current);
                self->wideGridMeasure_->SetText(buf);
            });
    }

    CreateCodeExample(content, LR"(auto* grid = panel->Add(std::make_unique<DataGrid>());
grid->SetHeight(320.0f);          // 视口高度必须由调用者定：表格没有自然高度
grid->SetColumns({
    {L"代码",   90.0f, DataGrid::Align::Left},
    {L"最新价", 90.0f, DataGrid::Align::Right},   // 数字列右对齐才能比较位数
});
grid->SetRowCount(50000);         // 只是行数，不是数据

// 拉取式：只有可见单元格会被调用
grid->SetCellProvider([](int row, int col) -> std::wstring {
    return Format(MyData()[row], col);
});

// 整行着色（可选）
grid->SetRowColorProvider([win](int row, D2D1_COLOR_F& out) {
    out = Data()[row].up ? win->Theme().colors.dataPositive
                         : win->Theme().colors.dataNegative;
    return true;
});

// 行数不变但内容变时，必须显式刷新
data[2].price = 1750.0;    // 修改了第 2 行的价格
grid->RefreshCells();      // 告诉 DataGrid 重新拉取所有单元格
)");

    return std::move(page);
}

}  // namespace fluent
