#ifndef WIN_CONSOLE_H
#define WIN_CONSOLE_H

// Writing plain text out of a -mwindows process, which has no console of its
// own and must not grow one: a black rectangle appearing beside the window is
// the thing this application exists to get away from.
//
// The one case where a window is the wrong answer is `--help`.  Whoever types
// it typed it at a prompt and wants text they can scroll back to and copy, and
// a message box gives neither.  So the text goes to whatever the process was
// started from -- a redirection if there is one, otherwise the parent's
// console -- and the process ends without ever creating a window.
//
// Nothing in win/ knows about the emulator.

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <string>

namespace win {

// False when there was nowhere to write: started from Explorer, with no
// console anywhere up the tree and no redirection.  The caller has to say so,
// because the alternative is a program that answers --help with silence and
// looks like it crashed.
bool WriteToParentConsole(const std::wstring& text);

}  // namespace win

#endif
