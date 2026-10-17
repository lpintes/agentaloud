#ifndef WIN_FILE_DIALOG_H
#define WIN_FILE_DIALOG_H

// The standard "Save as" dialog.  The common dialog and not one of our own:
// a screen reader user already knows its every field, and the folder tree,
// the overwrite question and the typed-in path are a lot to do again worse.
//
// Nothing in win/ knows about AgentAloud.

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <string>
#include <vector>

namespace win {

struct FileFilter {
  std::wstring label;    // "Text files (*.txt)"
  std::wstring pattern;  // "*.txt"
};

// The path the reader chose, or empty when they cancelled.  Starts with no
// file name every time -- a "save as" with nothing remembered; the folder is
// whatever the system last used.  `extension` (no dot) is added to a name typed
// without one.  Asks before overwriting.
std::wstring AskSavePath(HWND owner, const std::wstring& title,
                         const std::vector<FileFilter>& filters,
                         const std::wstring& extension);

}  // namespace win

#endif
