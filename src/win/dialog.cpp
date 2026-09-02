#include "win/dialog.h"

#include <shobjidl.h>

namespace win {

INT_PTR CALLBACK Dialog::DlgProc(HWND dialog, UINT message, WPARAM wParam,
                                 LPARAM lParam) {
  Dialog* self = nullptr;
  if (message == WM_INITDIALOG) {
    self = reinterpret_cast<Dialog*>(lParam);
    self->hwnd_ = dialog;
    SetWindowLongPtrW(dialog, DWLP_USER, static_cast<LONG_PTR>(lParam));
    return self->OnInit() ? FALSE : TRUE;
  }
  self = reinterpret_cast<Dialog*>(GetWindowLongPtrW(dialog, DWLP_USER));
  if (!self) return FALSE;

  if (message == WM_COMMAND) {
    const int id = LOWORD(wParam);
    const int notification = HIWORD(wParam);
    if (self->OnCommand(id, notification)) return TRUE;
    if (id == IDOK) {
      if (self->OnOk()) EndDialog(dialog, IDOK);
      return TRUE;
    }
    if (id == IDCANCEL) {
      EndDialog(dialog, IDCANCEL);
      return TRUE;
    }
  }
  return FALSE;
}

INT_PTR Dialog::ShowModal(HWND owner, int templateId) {
  return DialogBoxParamW(GetModuleHandleW(nullptr),
                         MAKEINTRESOURCEW(templateId), owner, &Dialog::DlgProc,
                         reinterpret_cast<LPARAM>(this));
}

void Dialog::SetChecked(int id, bool on) const {
  CheckDlgButton(hwnd_, id, on ? BST_CHECKED : BST_UNCHECKED);
}

bool Dialog::IsChecked(int id) const {
  return IsDlgButtonChecked(hwnd_, id) == BST_CHECKED;
}

void Dialog::SetText(int id, const std::wstring& text) const {
  SetDlgItemTextW(hwnd_, id, text.c_str());
}

std::wstring Dialog::GetText(int id) const {
  const HWND item = Item(id);
  if (!item) return {};
  const int length = GetWindowTextLengthW(item);
  if (length <= 0) return {};
  std::wstring text(static_cast<std::size_t>(length) + 1, L'\0');
  const int copied = GetWindowTextW(item, text.data(), length + 1);
  text.resize(copied > 0 ? static_cast<std::size_t>(copied) : 0);
  return text;
}

void Dialog::SetEnabled(int id, bool on) const {
  if (const HWND item = Item(id)) EnableWindow(item, on ? TRUE : FALSE);
}

std::wstring PickFolder(HWND owner, const wchar_t* title) {
  IFileOpenDialog* dialog = nullptr;
  if (FAILED(CoCreateInstance(CLSID_FileOpenDialog, nullptr,
                              CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&dialog))))
    return {};
  DWORD options = 0;
  dialog->GetOptions(&options);
  dialog->SetOptions(options | FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM |
                     FOS_PATHMUSTEXIST);
  dialog->SetTitle(title);
  std::wstring result;
  if (SUCCEEDED(dialog->Show(owner))) {
    IShellItem* item = nullptr;
    if (SUCCEEDED(dialog->GetResult(&item))) {
      PWSTR path = nullptr;
      if (SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH, &path))) {
        result = path;
        CoTaskMemFree(path);
      }
      item->Release();
    }
  }
  dialog->Release();
  return result;
}

std::wstring PickFolderToCreate(HWND owner, const wchar_t* title,
                                const wchar_t* suggestedName) {
  IFileSaveDialog* dialog = nullptr;
  if (FAILED(CoCreateInstance(CLSID_FileSaveDialog, nullptr,
                              CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&dialog))))
    return {};
  DWORD options = 0;
  dialog->GetOptions(&options);
  // FOS_PATHMUSTEXIST stays on -- the folder this one goes *into* has to be
  // real -- but FOS_FILEMUSTEXIST is cleared, because what is being named
  // here is precisely something that does not exist yet.  FOS_NOREADONLYRETURN
  // rules out the places nothing can be written to before the name is typed
  // rather than after.
  dialog->SetOptions((options | FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM |
                      FOS_PATHMUSTEXIST | FOS_NOREADONLYRETURN) &
                     ~static_cast<DWORD>(FOS_FILEMUSTEXIST));
  dialog->SetTitle(title);
  if (suggestedName) dialog->SetFileName(suggestedName);
  std::wstring result;
  if (SUCCEEDED(dialog->Show(owner))) {
    IShellItem* item = nullptr;
    if (SUCCEEDED(dialog->GetResult(&item))) {
      PWSTR path = nullptr;
      if (SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH, &path))) {
        result = path;
        CoTaskMemFree(path);
      }
      item->Release();
    }
  }
  dialog->Release();
  return result;
}

}  // namespace win
