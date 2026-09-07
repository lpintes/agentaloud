#ifndef UI_COMMAND_DIALOG_H
#define UI_COMMAND_DIALOG_H

// The slash command picker: the CLI's own list of commands, filtered by
// typing, chosen with Enter.
//
// It is a dialog and not an autocomplete in the prompt box, and that is a
// decision rather than an economy.  An autocomplete changes the text under the
// caret while it is being typed; a screen reader then reads changes nobody
// asked for, which is the exact kind of noise this application exists to get
// rid of.  A dialog announces itself once, has a list that can be walked, and
// leaves when it is done.
//
// What it produces is text for the prompt box, not a command that is run:
// measured 2026-09-06 (tools/probe_slash.py), a headless session does not
// expand slash commands itself -- the text reaches the model, and the model
// launches the skill behind it with the Skill tool.  So the picker's whole job
// is to save the typing and the remembering of the name.

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <vector>

#include "proto/control.h"

namespace ui {

// True when one was chosen; `chosen` then holds the whole entry, because what
// goes into the prompt depends on more than the name -- a command with an
// argument hint gets a space after it and one without does not.  False means
// the dialog was dismissed, which is not an error and nothing is to be
// inserted.
//
// The hint itself goes no further than this dialog.  Saying it out loud after
// the dialog closes does not work: see the note at the end of ShowCommands.
bool PickCommand(HWND owner, const std::vector<proto::SlashCommand>& commands,
                 proto::SlashCommand* chosen);

}  // namespace ui

#endif
