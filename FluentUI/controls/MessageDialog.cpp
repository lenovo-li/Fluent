#include "MessageDialog.h"
#include "TextBlock.h"
#include "../layout/ScrollPanel.h"

namespace fluent {

MessageDialog::MessageDialog(std::wstring title, std::wstring message,
                             DialogButtons buttons) {
    SetTitle(std::move(title));

    // A tighter title bar than the generic 48 DIP a dialog inherits. A message
    // box's title is two or three words ("Confirm", "Error") while the MESSAGE is
    // what the user has to read, so the taller bar put the visual weight in the
    // wrong place and stopped reading as a message box at all.
    SetTitleBarHeight(32.0f);

    // MessageDialog 的尺寸完全由框架自适应计算，调用方无需关心。
    // "you do not have to think about the size" is the entire reason to reach
    // for MessageDialog — a caller who does want to choose should be using
    // ContentDialog directly.
    //
    // 构造函数末尾的 BuildContent 会触发 ContentDialog::SetContent，tree
    // 在 OnInitialize 时建好。DialogWindow::Start 在 PrepareContent 之后、
    // Create 之前测量 root，自动计算出紧凑尺寸。
    BuildContent(std::move(message), buttons);
}

void MessageDialog::BuildContent(std::wstring message, DialogButtons buttons) {
    auto text = std::make_unique<TextBlock>();
    text->SetText(std::move(message));
    text->SetWrap(true);

    // The message lives inside a ScrollPanel so a long one degrades to scrolling
    // rather than to a clipped dialog. Auto-sizing keeps short messages exactly as
    // tall as their text, so the scrollbar never appears in the common case; it only
    // engages once the message would push the dialog past the auto-size height cap.
    auto scroll = std::make_unique<ScrollPanel>();
    scroll->Add(std::move(text));
    SetContent(std::move(scroll));

    // The affirmative button is always Primary and the dismissive always Cancel —
    // see the header on why the label does not get its own DialogResult.
    switch (buttons) {
        case DialogButtons::Ok:
            AddButton(L"OK", DialogResult::Primary);
            break;
        case DialogButtons::OkCancel:
            AddButton(L"OK", DialogResult::Primary);
            AddButton(L"Cancel", DialogResult::Cancel);
            break;
        case DialogButtons::YesNo:
            AddButton(L"Yes", DialogResult::Primary);
            AddButton(L"No", DialogResult::Cancel);
            break;
    }
    SetDefaultResult(DialogResult::Primary);
}

DialogResult MessageDialog::Show(NativeWindowHost& owner, std::wstring title,
                                 std::wstring message, DialogButtons buttons) {
    MessageDialog dialog(std::move(title), std::move(message), buttons);
    return dialog.ShowDialog(owner);
}

} // namespace fluent
