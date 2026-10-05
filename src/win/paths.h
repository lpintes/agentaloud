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

// The whole file as bytes.  False when it cannot be opened, which includes
// when it does not exist -- the caller decides whether that is a problem.
bool ReadFileBytes(const std::wstring& path, std::string* out);

// Writes the whole file or nothing: into "<path>.tmp" first, then over the
// old one in a single rename, so a failure halfway -- a full disk, a crash --
// cannot leave a file cut short where a whole one was.  The folder is created
// when it is missing, one level only.  False and the reason otherwise.
bool WriteFileBytes(const std::wstring& path, const std::string& bytes,
                    std::wstring* error);

}  // namespace win

#endif
