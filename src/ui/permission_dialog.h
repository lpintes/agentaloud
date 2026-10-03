#ifndef UI_PERMISSION_DIALOG_H
#define UI_PERMISSION_DIALOG_H

// The prompt that asks whether a tool may run.
//
// It exists because the thing it replaced could not be read.  A permission was
// a MessageBox holding `input.dump(2)`: JSON, with every line break in it
// written as the two characters "\n" -- read aloud, "backslash en" -- and with
// no way to move a caret through it.  A multi-line commit message was one long
// line nobody could walk, so it was not read, it was clicked away.  That is the
// opposite of what a permission prompt is for.
//
// What is shown is the same text the transcript will hold for the same call
// (model::RenderToolCall), and that is deliberate: allowing a command and then
// reading a different one back would be worse than either alone.
//
// AskUserQuestion never gets here.  It arrives as a permission request but is
// not one -- see proto/claude/ask.h and ui/ask_dialog.h.

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include "agent/backend.h"

namespace ui {

// True = allow the call.  False is every other way out, Esc included: a
// permission prompt closed without an answer is a refusal, because the tool
// runs only on a "yes".
bool AskPermission(HWND owner, const agent::PermissionRequest& request);

}  // namespace ui

#endif
