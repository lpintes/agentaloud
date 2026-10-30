#ifndef UI_MODEL_DIALOG_H
#define UI_MODEL_DIALOG_H

// F5 (claude-gui-lkk.86): the model of the running session, out of the
// catalog the CLI itself offers.
//
// The model is set from inside the dialog, which closes only once it has
// been sent -- the same shape as the links dialog and for the same reason: a
// failure is a sentence, and behind a closing dialog speech dies (invariant
// 6).  For the same reason what a reader must know before choosing -- that
// the change waits for the next turn -- is in the list's label, which NVDA
// reads on the way in, and not said afterwards.
//
// The reasoning effort (claude-gui-lkk.87) is the second list: the stages of
// the model highlighted in the first, and they differ from model to model.
// Only what changed is sent, the model first, because Claude cuts the effort
// down to the model it finds.

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <functional>
#include <string>
#include <vector>

#include "agent/backend.h"

namespace ui {

struct ModelPick {
  const std::vector<agent::ModelChoice>* catalog = nullptr;
  // The model highlighted on the way in, by its id or the full id it resolves
  // to; none when it is not in the catalog.
  std::string current;
  // The stage in force; empty for the model's default.
  std::string effort;
  // The change will reach only the next turn, and one is running now.
  bool nextTurnOnly = false;
  bool effortNextTurnOnly = false;
  // Sends the id; false when it could not be sent.
  std::function<bool(const std::string&)> set;
  // Sends the stage, the same way.  Unset: this agent has no effort to change.
  std::function<bool(const std::string&)> setEffort;
  std::function<void(const std::wstring&)> announce;
};

// The id of the model that was sent, or empty when none was.
std::string PickModel(HWND owner, const ModelPick& pick);

}  // namespace ui

#endif
