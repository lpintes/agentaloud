#ifndef UI_PLAN_DIALOG_H
#define UI_PLAN_DIALOG_H

// The plan put up for approval: Claude's ExitPlanMode, which arrives as a
// permission request but asks for more than a yes.
//
// Approving it also decides the mode the session goes on in.  Left to itself
// the CLI goes back to the mode planning was entered from, and after Shift+Tab
// through the cycle that is acceptEdits for somebody who came from auto --
// nothing in the old Allow/Deny box said so, and nothing in it could change it
// (claude-gui-lkk.43).  Sending the plan back is the other half: a bare "no"
// leaves the model guessing, so the reader can say what to change.

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <vector>

#include "agent/backend.h"

namespace ui {

// Fills answer->verdict (Allow or Deny), answer->mode with Allow and
// answer->feedback with Deny.  `modes` names the ids in request.modesAfter;
// an id it does not know is shown as itself.  Esc sends the plan back with no
// words, as does closing the box: planning on is the answer that changes
// nothing.
void AskPlanApproval(HWND owner, const agent::PermissionRequest& request,
                     const std::vector<agent::Mode>& modes,
                     agent::PermissionAnswer* answer);

}  // namespace ui

#endif
