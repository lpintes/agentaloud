#ifndef UI_KEYS_DIALOG_H
#define UI_KEYS_DIALOG_H

// F1: what this application's keyboard can do, in one modal dialog.
//
// It exists because there were eleven keys and the only place any of them was
// written down was the source.  The prompt's label carries one of them
// ("Ctrl+Enter odošle") and cannot carry more: NVDA reads that label every
// time the focus enters the box, so a second key there is a second thing
// heard on every visit to the prompt for the sake of something needed once.
//
// The text is a constant here rather than a resource string, so that the list
// and the code that implements it can be read side by side in the same commit.
// Nothing checks that the two agree -- that check cannot be written, because
// the keys live in two window procedures and not in a table -- so the rule is
// the human one: a new key is not finished until it is in this list.

//
// What differs between agents -- the modes Shift+Tab steps through, whether
// F4 has anything to list -- is written from agent::Capabilities, so that the
// list cannot promise one CLI's modes while another runs.

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <string>

#include "agent/backend.h"

namespace ui {

// The text of the list for this agent.  Apart from the dialog so that it can
// be read without a window.
std::wstring KeysText(const agent::Capabilities& capabilities);

// Modal, and says nothing of its own: a dialog is the one thing NVDA announces
// by itself, so the key does answer.  Same reasoning as ShowDetails.
void ShowKeys(HWND owner, const agent::Capabilities& capabilities);

}  // namespace ui

#endif
