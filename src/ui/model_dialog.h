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

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <functional>
#include <string>
#include <vector>

#include "agent/backend.h"

namespace ui {

struct ModelPick {
  const std::vector<agent::ModelChoice>* catalog = nullptr;
  // The id highlighted on the way in; none when it is not in the catalog.
  std::string current;
  // The change will reach only the next turn, and one is running now.
  bool nextTurnOnly = false;
  // Sends the id; false when it could not be sent.
  std::function<bool(const std::string&)> set;
  std::function<void(const std::wstring&)> announce;
};

// The id that was sent, or empty when the dialog was dismissed.
std::string PickModel(HWND owner, const ModelPick& pick);

}  // namespace ui

#endif
