#include "win/file_dialog.h"

#include <commdlg.h>

namespace win {

std::wstring AskSavePath(HWND owner, const std::wstring& title,
                         const std::vector<FileFilter>& filters,
                         const std::wstring& extension) {
  // The filter is one string of pairs, each part ended by NUL and the whole by
  // a second one -- std::wstring holds embedded NULs, so it is built as one.
  std::wstring filter;
  for (const FileFilter& entry : filters) {
    filter += entry.label;
    filter.push_back(L'\0');
    filter += entry.pattern;
    filter.push_back(L'\0');
  }
  filter.push_back(L'\0');

  // Long paths are allowed past MAX_PATH; the buffer has room for them.
  std::wstring path(32768, L'\0');
  OPENFILENAMEW request = {};
  request.lStructSize = sizeof(request);
  request.hwndOwner = owner;
  request.lpstrFilter = filters.empty() ? nullptr : filter.c_str();
  request.nFilterIndex = 1;
  request.lpstrFile = path.data();
  request.nMaxFile = static_cast<DWORD>(path.size());
  request.lpstrTitle = title.c_str();
  request.lpstrDefExt = extension.empty() ? nullptr : extension.c_str();
  // OFN_NOCHANGEDIR: the dialog would otherwise move the process's current
  // directory to wherever the file went, and children started later inherit it.
  request.Flags = OFN_EXPLORER | OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST |
                  OFN_NOCHANGEDIR | OFN_HIDEREADONLY;
  if (!GetSaveFileNameW(&request)) return {};
  path.resize(path.find(L'\0'));
  return path;
}

}  // namespace win
