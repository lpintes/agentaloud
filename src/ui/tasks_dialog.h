#ifndef UI_TASKS_DIALOG_H
#define UI_TASKS_DIALOG_H

// Ctrl+T: what runs in the background, and a button to stop one of it.
//
// The list is live.  The dialog is modal, but the pane's queue is still
// drained under it -- DialogBox runs a message loop of its own -- and the pane
// hands every new list to the dialog while it is open (SetTasks).  A list
// frozen at the moment Ctrl+T was pressed would offer to stop a task that
// finished a minute ago, and would never show the one started since.
//
// The dialog stays open after Stop, for the reason Ctrl+F does: the answer is
// a sentence, and behind a closing dialog speech dies (invariant 6).  Esc
// closes it.

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <functional>
#include <string>
#include <vector>

#include "agent/events.h"
#include "win/dialog.h"

namespace ui {

class TasksDialog : public win::Dialog {
 public:
  // `stop` is called with the selected task, or with nullptr when there is
  // none to stop; it says what happened itself, in both cases (invariant 6).
  TasksDialog(std::vector<agent::BackgroundTask> tasks,
              std::function<void(const agent::BackgroundTask*)> stop);

  // Rows that are still there stay where they are, with the selection on
  // them: rebuilding the list would move the reader and make NVDA read the
  // selection again for a change that was not theirs.  Only when the selected
  // row goes does the selection move, to the row that took its place.
  void SetTasks(const std::vector<agent::BackgroundTask>& tasks);

 protected:
  bool OnInit() override;
  bool OnOk() override;

 private:
  void AddRow(const agent::BackgroundTask& task);
  void ShowEmpty(bool empty);

  std::vector<agent::BackgroundTask> tasks_;  // one per row, in row order
  std::function<void(const agent::BackgroundTask*)> stop_;
  // The list shows one row saying so when tasks_ is empty.
  bool emptyRow_ = false;
};

// One row of the list and one name in speech: "agent: Explore …".
std::wstring TaskLabel(const agent::BackgroundTask& task);

}  // namespace ui

#endif
