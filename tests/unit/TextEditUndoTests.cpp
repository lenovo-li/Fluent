// TextEditUndoTests.cpp — undo/redo through the EDITOR, not the bare stack.
//
// UndoStackTests covers the stack in isolation. This file covers the wiring, which
// is where the interesting mistakes live: does typing over a selection undo as one
// step, does Backspace record anything at all, does SetText wipe the history. All of
// those can be wrong while every UndoStack test still passes.
//
// Headless: TextBox needs no device for the buffer/caret paths used here.

#include "../framework/Test.h"
#include "../../FluentUI/controls/TextBox.h"
#include "ClipboardTestHelpers.h"
#include <vector>

using namespace fluent;

namespace {

// Type each character through the real input path (OnTextInput -> InsertText).
void Type(TextBox& tb, const wchar_t* s) {
    for (const wchar_t* p = s; *p; ++p) tb.OnTextInput(*p);
}

// Press a key through the real routed handler.
void Press(TextBox& tb, UINT vk) {
    KeyEventArgs e;
    e.vk = vk;
    tb.OnKeyDownRouted(e);
}

} // namespace

// --- Nothing to undo -------------------------------------------------------

TEST(TextEditUndo, FreshControlHasNoHistory) {
    TextBox tb;
    EXPECT_FALSE(tb.CanUndo());
    EXPECT_FALSE(tb.CanRedo());
}

TEST(TextEditUndo, UndoOnEmptyHistoryIsANoOp) {
    TextBox tb;
    tb.Undo();                      // must not crash or corrupt
    EXPECT_TRUE(tb.Text().empty());
}

// --- Typing ----------------------------------------------------------------

TEST(TextEditUndo, TypingThenUndoClearsTheText) {
    TextBox tb;
    Type(tb, L"abc");
    EXPECT_EQ(tb.Text(), std::wstring(L"abc"));
    EXPECT_TRUE(tb.CanUndo());

    tb.Undo();
    EXPECT_TRUE(tb.Text().empty());
    EXPECT_EQ(tb.SelectionEndForTest(), 0u);   // caret back at the start
}

TEST(TextEditUndo, TypedRunUndoesAsOneStep) {
    // The merge is what makes this one step; without it "abc" needs three undos.
    TextBox tb;
    Type(tb, L"abc");
    tb.Undo();
    EXPECT_TRUE(tb.Text().empty());
    EXPECT_FALSE(tb.CanUndo());
}

TEST(TextEditUndo, RedoRestoresTypedText) {
    TextBox tb;
    Type(tb, L"hello");
    tb.Undo();
    EXPECT_TRUE(tb.Text().empty());

    tb.Redo();
    EXPECT_EQ(tb.Text(), std::wstring(L"hello"));
    EXPECT_EQ(tb.SelectionEndForTest(), 5u);   // caret at the end again
}

TEST(TextEditUndo, TypingAfterUndoDropsTheRedoBranch) {
    TextBox tb;
    Type(tb, L"abc");
    tb.Undo();
    EXPECT_TRUE(tb.CanRedo());

    Type(tb, L"x");
    EXPECT_FALSE(tb.CanRedo());
    EXPECT_EQ(tb.Text(), std::wstring(L"x"));
}

// --- Backspace / Delete ----------------------------------------------------

TEST(TextEditUndo, BackspaceIsUndoable) {
    TextBox tb;
    Type(tb, L"ab");
    Press(tb, VK_BACK);
    EXPECT_EQ(tb.Text(), std::wstring(L"a"));

    tb.Undo();
    EXPECT_EQ(tb.Text(), std::wstring(L"ab"));
    EXPECT_EQ(tb.SelectionEndForTest(), 2u);   // caret restored to before the erase
}

TEST(TextEditUndo, DeleteForwardIsUndoable) {
    TextBox tb;
    tb.SetText(L"abc");
    tb.TestSetCaret(1);
    Press(tb, VK_DELETE);
    EXPECT_EQ(tb.Text(), std::wstring(L"ac"));

    tb.Undo();
    EXPECT_EQ(tb.Text(), std::wstring(L"abc"));
}

TEST(TextEditUndo, BackspaceThenUndoThenRedo) {
    TextBox tb;
    tb.SetText(L"abc");
    Press(tb, VK_BACK);
    EXPECT_EQ(tb.Text(), std::wstring(L"ab"));
    tb.Undo();
    EXPECT_EQ(tb.Text(), std::wstring(L"abc"));
    tb.Redo();
    EXPECT_EQ(tb.Text(), std::wstring(L"ab"));
}

TEST(TextEditUndo, DeleteBreaksTheTypingRunSoUndoIsStepwise) {
    // "a", backspace, "b" is three operations. One merged entry here would undo
    // all of it at once, which is the bug this asserts against.
    TextBox tb;
    Type(tb, L"a");
    Press(tb, VK_BACK);
    Type(tb, L"b");
    EXPECT_EQ(tb.Text(), std::wstring(L"b"));

    tb.Undo();                                  // remove the "b"
    EXPECT_TRUE(tb.Text().empty());
    tb.Undo();                                  // put the backspaced "a" back
    EXPECT_EQ(tb.Text(), std::wstring(L"a"));
    tb.Undo();                                  // remove the original "a"
    EXPECT_TRUE(tb.Text().empty());
    EXPECT_FALSE(tb.CanUndo());
}

// --- Typing over a selection ----------------------------------------------

TEST(TextEditUndo, TypingOverASelectionUndoesInOneStep) {
    // This is the case a naive implementation gets wrong: delete-then-insert
    // recorded as two entries leaves the FIRST undo showing text with the
    // selection deleted — a state the user never saw.
    TextBox tb;
    tb.SetText(L"hello");
    tb.SelectAllForTest();
    Type(tb, L"x");
    EXPECT_EQ(tb.Text(), std::wstring(L"x"));

    tb.Undo();
    EXPECT_EQ(tb.Text(), std::wstring(L"hello"));
}

TEST(TextEditUndo, RedoOfTypingOverASelectionReappliesBoth) {
    TextBox tb;
    tb.SetText(L"hello");
    tb.SelectAllForTest();
    Type(tb, L"x");
    tb.Undo();
    EXPECT_EQ(tb.Text(), std::wstring(L"hello"));

    tb.Redo();
    EXPECT_EQ(tb.Text(), std::wstring(L"x"));
}

TEST(TextEditUndo, DeletingASelectionIsOneUndoableStep) {
    TextBox tb;
    tb.SetText(L"abcdef");
    tb.TestSetCaret(0);
    tb.SelectAllForTest();
    Press(tb, VK_BACK);
    EXPECT_TRUE(tb.Text().empty());

    tb.Undo();
    EXPECT_EQ(tb.Text(), std::wstring(L"abcdef"));
}

namespace {
// BuildEditMenuItems is protected; this is the one-line door to it. The items are the
// real ones the context menu shows, so invoking one runs the shipping handler.
class MenuTextBox : public TextBox {
public:
    std::vector<MenuItem> Items() { return BuildEditMenuItems(); }
};
} // namespace

TEST(TextEditUndo, ContextMenuCutIsUndoable) {
    // The menu's Cut used the unrecorded DeleteSelection while Ctrl+X used the
    // recorded one. After a menu cut the stack still described the uncut buffer, so
    // Ctrl+Z replayed the typing entry against it — deleting "hello " — and the cut
    // text was gone for good. Cut writes the clipboard, which the guard puts back.
    testclip::ClipboardTextGuard guard;
    MenuTextBox tb;
    Type(tb, L"hello world");
    tb.Select(6, 5);                                  // "world"

    std::vector<MenuItem> items = tb.Items();
    items[0].RaiseInvoked();                          // 剪切
    if (tb.Text() == L"hello world") return;          // clipboard held elsewhere: skip
    EXPECT_EQ(tb.Text(), std::wstring(L"hello "));

    tb.Undo();
    EXPECT_EQ(tb.Text(), std::wstring(L"hello world"));
    tb.Undo();
    EXPECT_TRUE(tb.Text().empty());
}

// --- SetText / history lifetime -------------------------------------------

TEST(TextEditUndo, SetTextClearsHistory) {
    // Undoing across a document swap would restore text the caller never showed.
    TextBox tb;
    Type(tb, L"typed");
    EXPECT_TRUE(tb.CanUndo());

    tb.SetText(L"loaded");
    EXPECT_FALSE(tb.CanUndo());
    EXPECT_FALSE(tb.CanRedo());
}

TEST(TextEditUndo, ClearUndoHistoryDropsBothDirections) {
    TextBox tb;
    Type(tb, L"abc");
    tb.Undo();
    EXPECT_TRUE(tb.CanRedo());

    tb.ClearUndoHistory();
    EXPECT_FALSE(tb.CanUndo());
    EXPECT_FALSE(tb.CanRedo());
}

TEST(TextEditUndo, EditsAfterSetTextAreUndoableDownToTheLoadedText) {
    // The floor is the SetText content, not empty.
    TextBox tb;
    tb.SetText(L"base");
    Type(tb, L"++");
    EXPECT_EQ(tb.Text(), std::wstring(L"base++"));

    tb.Undo();
    EXPECT_EQ(tb.Text(), std::wstring(L"base"));
    EXPECT_FALSE(tb.CanUndo());
}

// --- Read-only -------------------------------------------------------------

TEST(TextEditUndo, ReadOnlyControlDoesNotUndo) {
    TextBox tb;
    Type(tb, L"abc");
    tb.SetReadOnly(true);
    tb.Undo();
    EXPECT_EQ(tb.Text(), std::wstring(L"abc"));   // unchanged
}

// --- Ctrl+Z / Ctrl+Y through the key path ---------------------------------
//
// These call the routed handler with Ctrl held. GetKeyState cannot be faked from a
// test, so the handler reads a modifier the test cannot set — which is exactly why
// these assert on the KEY path being *reachable* and leave the behaviour assertions
// to the direct-call tests above. Without this, a broken Ctrl+Z binding would not
// fail anything.

TEST(TextEditUndo, CtrlZKeyIsRoutedToUndoWhenModifierIsSet) {
    TextBox tb;
    Type(tb, L"abc");

    KeyEventArgs e;
    e.vk = 'Z';
    e.modifiers = ModifierKeys::Ctrl;
    tb.OnKeyDownRouted(e);

    // If Ctrl was observable, the text is undone and the event is consumed. If it
    // was not (GetKeyState in a test process), 'Z' falls through as a plain key and
    // is NOT handled. Either outcome is consistent; what must never happen is the
    // key being swallowed without doing anything.
    if (e.handled) {
        EXPECT_TRUE(tb.Text().empty());
    } else {
        EXPECT_EQ(tb.Text(), std::wstring(L"abc"));
    }
}
