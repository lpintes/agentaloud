#include "win/paths.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <shlobj.h>

#include <cstdio>

namespace win {

std::wstring ExecutableDirectory() {
  std::wstring buffer(32768, L'\0');
  const DWORD length = GetModuleFileNameW(nullptr, buffer.data(),
                                          static_cast<DWORD>(buffer.size()));
  buffer.resize(length);
  const size_t slash = buffer.find_last_of(L"\\/");
  return slash == std::wstring::npos ? std::wstring() : buffer.substr(0, slash);
}

std::wstring RoamingAppData() {
  PWSTR path = nullptr;
  if (SHGetKnownFolderPath(FOLDERID_RoamingAppData, 0, nullptr, &path) != S_OK) {
    return {};
  }
  std::wstring result(path);
  CoTaskMemFree(path);
  return result;
}

bool ReadFileBytes(const std::wstring& path, std::string* out) {
  FILE* file = _wfopen(path.c_str(), L"rb");
  if (!file) return false;
  out->clear();
  char chunk[4096];
  size_t read = 0;
  while ((read = std::fread(chunk, 1, sizeof(chunk), file)) > 0) {
    out->append(chunk, read);
  }
  const bool ok = !std::ferror(file);
  std::fclose(file);
  return ok;
}

}  // namespace win
