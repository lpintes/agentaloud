#ifndef UI_FORM_DIALOG_H
#define UI_FORM_DIALOG_H

// The dialogs for a form an MCP server asks for (agent::FormRequest): one
// field at a time, or a link to open.
//
// One field per dialog for the reason the question dialog has one question
// per dialog (ui/ask_dialog.h): a template with a control for every field a
// schema can hold would be mostly hidden controls, and the caption can say
// which field this is and how many there are -- NVDA reads the caption.
// Unlike questions, fields can be gone back to (Back): a form is filled in,
// and a typo in the first of nine fields is found at the ninth.
//
// Each value is checked as it is left forward (model::CheckFormField); a
// refusal is said in a message box and the focus goes back to the value.
// Back keeps what was typed without checking it.

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include "agent/backend.h"

namespace ui {

// Blocks in a modal loop until the reader is done.  Accept carries a value
// list parallel to request.fields; Decline is the Decline button, Cancel is
// Esc or the close box -- MCP tells the two apart, and so does the server.
agent::FormAnswer FillForm(HWND owner, const agent::FormRequest& request);

}  // namespace ui

#endif
