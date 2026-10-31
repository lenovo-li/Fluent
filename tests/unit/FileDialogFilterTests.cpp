// FileDialogFilterTests.cpp — COMDLG_FILTERSPEC pointers stay valid across AddFilter.
//
// FileDialogBase keeps the filter strings in two vectors and hands SetFileTypes an
// array of raw pointers into them. A test subclass reads that array without
// creating any COM dialog (dialog_ stays null), so this runs headless.

#include "../framework/Test.h"
#include "../../FluentUI/dialogs/FileDialog.h"

#include <cwchar>

using namespace fluent;

namespace {
struct FilterProbe : FileDialogBase {
    const std::vector<COMDLG_FILTERSPEC>& Specs() const { return filters_; }
    const std::vector<std::wstring>& Names() const { return filterNames_; }
    const std::vector<std::wstring>& Patterns() const { return filterSpecs_; }
};
}  // namespace

// Every entry points at the string currently stored for it, after enough short
// (small-string-buffer) filters that the vectors certainly reallocate. Comparing the
// pointer itself, not just the text: freed memory often still holds the old text.
TEST(FileDialog, FilterPointersSurviveReallocation) {
    FilterProbe p;
    for (int i = 0; i < 20; ++i)
        p.AddFilter(L"F" + std::to_wstring(i), L"*." + std::to_wstring(i));

    EXPECT_EQ(p.Specs().size(), 20u);
    for (size_t i = 0; i < p.Specs().size(); ++i) {
        EXPECT_TRUE(p.Specs()[i].pszName == p.Names()[i].c_str());
        EXPECT_TRUE(p.Specs()[i].pszSpec == p.Patterns()[i].c_str());
        EXPECT_TRUE(std::wcscmp(p.Specs()[i].pszName, (L"F" + std::to_wstring(i)).c_str()) == 0);
    }
}
