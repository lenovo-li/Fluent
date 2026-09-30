// FolderPicker.h — Windows folder selection dialog wrapper.
//
// Thin wrapper over IFileOpenDialog with FOS_PICKFOLDERS flag. Shows a modal
// folder selection dialog and returns the chosen path (empty on cancel).
//
// Usage:
//   std::wstring path = FolderPicker::ShowDialog(hwnd, L"Select Install Folder");
//   if (!path.empty()) { /* user selected a folder */ }
//
// Based on FluentUI's FileDialog.h pattern but specialized for folders only.
#pragma once

#include "../../FluentUI/fl_common.h"
#include <string>

namespace fluent {
namespace wizard {

class FolderPicker {
public:
    // Show a modal folder selection dialog. Returns the selected folder path,
    // or empty string on cancel. `owner` is the parent HWND; pass nullptr for
    // desktop-modal (not recommended). `title` appears in the dialog caption bar.
    // `defaultPath` sets the initial folder shown in the dialog.
    static std::wstring ShowDialog(HWND owner,
                                    const std::wstring& title = L"Select Folder",
                                    const std::wstring& defaultPath = L"");
};

} // namespace wizard
} // namespace fluent
