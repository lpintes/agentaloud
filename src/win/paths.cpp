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

bool WriteFileBytes(const std::wstring& path, const std::string& bytes,
                    std::wstring* error) {
  const size_t slash = path.find_last_of(L"\\/");
  if (slash != std::wstring::npos) {
    CreateDirectoryW(path.substr(0, slash).c_str(), nullptr);
  }
  const std::wstring temporary = path + L".tmp";
  const HANDLE file = CreateFileW(temporary.c_str(), GENERIC_WRITE, 0, nullptr,
                                  CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
  if (file == INVALID_HANDLE_VALUE) {
    *error = L"súbor " + temporary + L" sa nedá vytvoriť (chyba " +
             std::to_wstring(GetLastError()) + L")";
    return false;
  }
  DWORD written = 0;
  const bool wrote = WriteFile(file, bytes.data(),
                               static_cast<DWORD>(bytes.size()), &written,
                               nullptr) &&
                     written == bytes.size() && FlushFileBuffers(file);
  CloseHandle(file);
  if (!wrote) {
    DeleteFileW(temporary.c_str());
    *error = L"súbor " + temporary + L" sa nedá zapísať celý";
    return false;
  }
  if (!MoveFileExW(temporary.c_str(), path.c_str(),
                   MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
    *error = L"súbor " + path + L" sa nedá prepísať (chyba " +
             std::to_wstring(GetLastError()) + L")";
    DeleteFileW(temporary.c_str());
    return false;
  }
  return true;
}

}  // namespace win
