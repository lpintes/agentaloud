#include "win/console.h"

#include <string>

namespace win {
namespace {

bool Usable(HANDLE handle) {
  return handle != nullptr && handle != INVALID_HANDLE_VALUE;
}

// A console screen buffer and a file want different calls and different
// encodings, and telling them apart by asking whether GetConsoleMode works is
// the only reliable way: both are HANDLEs and both are writable.
//
// WriteConsoleW takes UTF-16 straight and gets the code page right by itself.
// WriteFile does not know what the bytes mean, so for a redirection the text
// is encoded as UTF-8 -- `ClaudeLens --help > help.txt` then produces a file
// that opens correctly in anything written this decade, rather than one where
// the diacritics are half a character each.
bool Write(HANDLE out, const std::wstring& text) {
  if (text.empty()) return true;
  DWORD mode = 0;
  if (GetConsoleMode(out, &mode)) {
    DWORD written = 0;
    return WriteConsoleW(out, text.c_str(),
                         static_cast<DWORD>(text.size()), &written,
                         nullptr) != 0;
  }
  const int bytes = WideCharToMultiByte(CP_UTF8, 0, text.c_str(),
                                        static_cast<int>(text.size()), nullptr,
                                        0, nullptr, nullptr);
  if (bytes <= 0) return false;
  std::string utf8(static_cast<std::size_t>(bytes), '\0');
  WideCharToMultiByte(CP_UTF8, 0, text.c_str(), static_cast<int>(text.size()),
                      utf8.data(), bytes, nullptr, nullptr);
  DWORD written = 0;
  return WriteFile(out, utf8.data(), static_cast<DWORD>(utf8.size()), &written,
                   nullptr) != 0;
}

}  // namespace

bool WriteToParentConsole(const std::wstring& text) {
  // A redirection is already here: cmd.exe hands the file or pipe over in the
  // standard handles even to a GUI process, and it must win over the console,
  // or `--help > help.txt` would print to the screen and leave an empty file.
  HANDLE out = GetStdHandle(STD_OUTPUT_HANDLE);
  if (Usable(out)) return Write(out, text);

  if (!AttachConsole(ATTACH_PARENT_PROCESS)) return false;
  // Attaching does not fill in the standard handles of a process that started
  // without them, so the newly attached screen buffer has to be opened by
  // name.  CONOUT$ always means "the console this process is attached to".
  out = GetStdHandle(STD_OUTPUT_HANDLE);
  if (!Usable(out)) {
    out = CreateFileW(L"CONOUT$", GENERIC_WRITE,
                      FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                      OPEN_EXISTING, 0, nullptr);
    if (!Usable(out)) return false;
  }
  // The shell gave up on this process the moment it started it -- it is a GUI
  // program, so cmd.exe does not wait -- and has printed its next prompt by
  // now.  The text would otherwise begin halfway along that prompt.  It is
  // cosmetic and it is the known price of this approach; the blank line is
  // what makes it look deliberate rather than broken.
  return Write(out, L"\n" + text);
}

}  // namespace win
