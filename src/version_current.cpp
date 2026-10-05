#include "version.h"

// Generated from git describe into build/ -- see the Makefile.
#include "app_version.h"

namespace version {

std::wstring_view Current() { return L"" APP_VERSION; }

}  // namespace version
