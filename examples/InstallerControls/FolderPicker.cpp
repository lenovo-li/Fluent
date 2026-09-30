// FolderPicker.cpp
#include "FolderPicker.h"
#include <ShObjIdl.h>  // IFileOpenDialog, FOS_PICKFOLDERS
#include <wrl/client.h>

using Microsoft::WRL::ComPtr;

namespace fluent {
namespace wizard {

std::wstring FolderPicker::ShowDialog(HWND owner,
                                       const std::wstring& title,
                                       const std::wstring& defaultPath) {
    // Create IFileOpenDialog with FOS_PICKFOLDERS flag.
    ComPtr<IFileOpenDialog> dialog;
    HRESULT hr = CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER,
                                   IID_PPV_ARGS(&dialog));
    if (FAILED(hr) || !dialog) return {};

    // Set folder-picker mode.
    DWORD options = 0;
    if (SUCCEEDED(dialog->GetOptions(&options))) {
        options |= FOS_PICKFOLDERS;
        dialog->SetOptions(options);
    }

    // Set title.
    if (!title.empty()) {
        dialog->SetTitle(title.c_str());
    }

    // Set default folder if provided.
    if (!defaultPath.empty()) {
        ComPtr<IShellItem> defaultItem;
        if (SUCCEEDED(SHCreateItemFromParsingName(defaultPath.c_str(), nullptr,
                                                   IID_PPV_ARGS(&defaultItem)))) {
            dialog->SetFolder(defaultItem.Get());
        }
    }

    // Show modal dialog.
    hr = dialog->Show(owner);
    if (FAILED(hr)) return {};  // User cancelled or error.

    // Retrieve selected folder.
    ComPtr<IShellItem> result;
    if (FAILED(dialog->GetResult(&result))) return {};

    PWSTR path = nullptr;
    if (FAILED(result->GetDisplayName(SIGDN_FILESYSPATH, &path))) return {};

    std::wstring folderPath = path;
    CoTaskMemFree(path);
    return folderPath;
}

} // namespace wizard
} // namespace fluent
