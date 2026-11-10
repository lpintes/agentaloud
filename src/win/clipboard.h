#ifndef WIN_CLIPBOARD_H
#define WIN_CLIPBOARD_H

// Putting text on the clipboard, which is four calls in a fixed order and one
// ownership rule that is easy to get wrong: after SetClipboardData the block
// belongs to the clipboard and must NOT be freed here.
//
// Nothing in win/ knows about AgentAloud.

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <string>

namespace win {

// False when the clipboard could not be opened -- another application may hold
// it -- or when the memory for the copy could not be had.  The caller has to
// say so: a copy that silently did not happen is found out at paste time, in
// another program, with the wrong text.
//
// `text` breaks lines with '\n' alone; they go onto the clipboard as "\r\n",
// which is what CF_UNICODETEXT means and what Notepad's older siblings and
// many an edit box paste as a line break rather than as nothing.
bool SetClipboardText(HWND owner, const std::wstring& text);

}  // namespace win

#endif
