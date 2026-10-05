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

}  // namespace win

#endif
