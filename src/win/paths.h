#ifndef WIN_PATHS_H
#define WIN_PATHS_H

// The two places a program asks about before it can find its own files: the
// folder it runs from, and the user's roaming application data.

#include <string>

namespace win {

// The folder of the running .exe, without a trailing separator.
std::wstring ExecutableDirectory();

// %APPDATA%.  Asked of the shell rather than read from the environment: an
// environment variable is inherited and can arrive already wrong from
// whatever started this process.  Empty when the shell will not say.
std::wstring RoamingAppData();

// The last component of the path, trailing separators ignored: the name a
// project goes by in titles, the status bar and the list of recent folders.
std::wstring FolderName(std::wstring path);

// The same folder however it was typed: Windows paths ignore case and take
// either slash, and two spellings of one checkout must count as one folder --
// one session from the command line and one from the dialog, or a Claude
// conversation and a Codex thread whose CLIs stored the path differently.
// For comparing only; never shown and never opened.
std::wstring FolderKey(std::wstring path);

// The whole file as bytes.  False when it cannot be opened, which includes
// when it does not exist -- the caller decides whether that is a problem.
bool ReadFileBytes(const std::wstring& path, std::string* out);

// Writes the whole file or nothing: into "<path>.tmp" first, then over the
// old one in a single rename, so a failure halfway -- a full disk, a crash --
// cannot leave a file cut short where a whole one was.  The folder is created
// when it is missing, one level only.  False and the reason otherwise -- in
// English, because win/ knows nothing of the application and its catalog;
// a caller that shows the reason to a reader says it in the reader's words.
bool WriteFileBytes(const std::wstring& path, const std::string& bytes,
                    std::wstring* error);

// The folder and every missing one above it.  For a folder this program
// owns the whole of, like bookmarks\<backend> under the settings: the one
// level WriteFileBytes makes is not enough there, and the settings folder
// itself may not be there yet on a first run.  The path must be absolute.
// True when the folder is there afterwards, made now or before.
bool CreateFolders(const std::wstring& path);

}  // namespace win

#endif
