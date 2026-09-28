// MessageDialogTests.cpp — headless checks for the one-line message box.
//
// The whole class is construction plus a fixed button-to-result mapping, and both
// are reachable without a host: MessageDialog's constructor runs BuildContent
// directly. The static Show() is not covered — it needs a real owner window.
#include "../framework/Test.h"
#include "../../FluentUI/controls/MessageDialog.h"
#include <string>

using namespace fluent;

TEST(MessageDialog, OkHasOneAffirmativeButton) {
    MessageDialog d(L"Saved", L"Your changes were written.", DialogButtons::Ok);
    EXPECT_EQ(d.Title(), std::wstring(L"Saved"));
    EXPECT_EQ(d.ButtonCount(), static_cast<size_t>(1));
}

TEST(MessageDialog, OkCancelAndYesNoBothProduceTwoButtons) {
    MessageDialog okCancel(L"Confirm", L"Proceed?", DialogButtons::OkCancel);
    EXPECT_EQ(okCancel.ButtonCount(), static_cast<size_t>(2));

    MessageDialog yesNo(L"Delete", L"This cannot be undone.", DialogButtons::YesNo);
    EXPECT_EQ(yesNo.ButtonCount(), static_cast<size_t>(2));
}

TEST(MessageDialog, DefaultsToOkWhenButtonsOmitted) {
    MessageDialog d(L"Note", L"Just so you know.");
    EXPECT_EQ(d.ButtonCount(), static_cast<size_t>(1));
}

TEST(MessageDialog, SizesItselfToItsContent) {
    MessageDialog d(L"Note", L"One paragraph.");
    // MessageDialog no longer hard-codes a client size. It leaves AutoSize on, and
    // DialogWindow::Start measures the built tree between OnInitialize and Create,
    // so the window ends up exactly as tall as the message needs. That is the whole
    // contract: a caller never picks a size for a message box.
    //
    // Before the HWND exists the size is still DialogWindow's fallback, so what is
    // asserted here is the INTENT (auto-size armed), not a pixel count — measuring
    // requires the tree, which OnInitialize builds during Start.
    EXPECT_TRUE(d.AutoSize());
    EXPECT_NEAR(d.TitleBarHeight(), 32.0f, 0.001f);
}

TEST(MessageDialog, AnExplicitSizeFromTheCallerDisarmsAutoSize) {
    MessageDialog d(L"Saved", L"Output directory:\nC:\\Users\\Example\\Long-path-name");
    EXPECT_TRUE(d.AutoSize());
    // SetClientSize is the opt-out: whoever states a size owns it, and the measure
    // pass in Start must not overwrite it.
    d.SetClientSize(500.0f, 400.0f);
    EXPECT_FALSE(d.AutoSize());
    EXPECT_NEAR(d.ClientWidth(), 500.0f, 0.001f);
    EXPECT_NEAR(d.ClientHeight(), 400.0f, 0.001f);
}

TEST(MessageDialog, StartsWithNoResolvedResult) {
    MessageDialog d(L"Confirm", L"Proceed?", DialogButtons::YesNo);
    EXPECT_EQ(d.Result(), DialogResult::None);
    EXPECT_FALSE(d.IsDialogOpen());
}
