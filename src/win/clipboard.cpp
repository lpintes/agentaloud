#include "win/clipboard.h"

#include <cstring>

namespace win {

bool SetClipboardText(HWND owner, const std::wstring& text) {
  const std::size_t bytes = (text.size() + 1) * sizeof(wchar_t);
  HGLOBAL block = GlobalAlloc(GMEM_MOVEABLE, bytes);
  if (!block) return false;
  void* target = GlobalLock(block);
  if (!target) {
    GlobalFree(block);
    return false;
  }
  std::memcpy(target, text.c_str(), bytes);
  GlobalUnlock(block);

  if (!OpenClipboard(owner)) {
    GlobalFree(block);
    return false;
  }
  EmptyClipboard();
  // From here the block is the clipboard's.  Freeing it after a successful
  // SetClipboardData is a use-after-free that shows up as garbage in whatever
  // is pasted next, in another program, minutes later.
  const bool ok = SetClipboardData(CF_UNICODETEXT, block) != nullptr;
  CloseClipboard();
  if (!ok) GlobalFree(block);
  return ok;
}

}  // namespace win
