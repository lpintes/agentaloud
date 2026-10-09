#include "ui/tasks_dialog.h"

#include <algorithm>
#include <utility>

#include "i18n/i18n.h"
#include "model/utf.h"
#include "ui/dialog_texts.h"
#include "ui/resource.h"

namespace ui {

std::wstring TaskLabel(const agent::BackgroundTask& task) {
  // The id is a name of last resort: the CLI may leave the description out,
  // and two rows that read the same cannot be told apart.
  const std::wstring what = model::Utf16FromUtf8(
      task.description.empty() ? task.id : task.description);
  return i18n::Format(task.kind == agent::TaskKind::Shell ? i18n::Str::kTaskShell
                                                          : i18n::Str::kTaskAgent,
                      {what});
}

TasksDialog::TasksDialog(std::vector<agent::BackgroundTask> tasks,
                         std::function<void(const agent::BackgroundTask*)> stop)
    : tasks_(std::move(tasks)), stop_(std::move(stop)) {}

bool TasksDialog::OnInit() {
  LocalizeDialog(hwnd_, IDD_TASKS);
  for (const agent::BackgroundTask& task : tasks_) AddRow(task);
  ShowEmpty(tasks_.empty());
  SendMessageW(Item(IDC_TASKS_LIST), LB_SETCURSEL, 0, 0);
  return false;  // the list is the first tab stop
}

void TasksDialog::AddRow(const agent::BackgroundTask& task) {
  const std::wstring label = TaskLabel(task);
  SendMessageW(Item(IDC_TASKS_LIST), LB_ADDSTRING, 0,
               reinterpret_cast<LPARAM>(label.c_str()));
}

void TasksDialog::ShowEmpty(bool empty) {
  HWND list = Item(IDC_TASKS_LIST);
  if (empty && !emptyRow_) {
    SendMessageW(list, LB_ADDSTRING, 0,
                 reinterpret_cast<LPARAM>(i18n::Text(i18n::Str::kTasksNone)));
    SendMessageW(list, LB_SETCURSEL, 0, 0);
  } else if (!empty && emptyRow_) {
    SendMessageW(list, LB_DELETESTRING, 0, 0);
  }
  emptyRow_ = empty;
}

void TasksDialog::SetTasks(const std::vector<agent::BackgroundTask>& tasks) {
  if (!hwnd_) {
    tasks_ = tasks;
    return;
  }
  HWND list = Item(IDC_TASKS_LIST);
  auto has = [](const std::vector<agent::BackgroundTask>& in,
                const std::string& id) {
    return std::any_of(in.begin(), in.end(),
                       [&id](const agent::BackgroundTask& task) {
                         return task.id == id;
                       });
  };
  LRESULT selected = SendMessageW(list, LB_GETCURSEL, 0, 0);
  bool selectionGone = false;
  // From the end, so that a deleted row does not renumber the rows still to
  // be looked at.
  for (size_t i = tasks_.size(); i-- > 0;) {
    if (has(tasks, tasks_[i].id)) continue;
    SendMessageW(list, LB_DELETESTRING, i, 0);
    if (selected == static_cast<LRESULT>(i)) selectionGone = true;
    if (selected > static_cast<LRESULT>(i)) --selected;
    tasks_.erase(tasks_.begin() + static_cast<std::ptrdiff_t>(i));
  }
  if (!tasks.empty()) ShowEmpty(false);
  for (const agent::BackgroundTask& task : tasks) {
    if (has(tasks_, task.id)) continue;
    tasks_.push_back(task);
    AddRow(task);
  }
  if (tasks_.empty()) {
    ShowEmpty(true);
    return;
  }
  if (selectionGone || selected == LB_ERR) {
    const LRESULT last = static_cast<LRESULT>(tasks_.size()) - 1;
    SendMessageW(list, LB_SETCURSEL, std::min(std::max<LRESULT>(selected, 0), last),
                 0);
  }
}

// Never closes: see the header.
bool TasksDialog::OnOk() {
  const LRESULT selected =
      SendMessageW(Item(IDC_TASKS_LIST), LB_GETCURSEL, 0, 0);
  if (emptyRow_ || selected == LB_ERR ||
      static_cast<size_t>(selected) >= tasks_.size()) {
    stop_(nullptr);
    return false;
  }
  stop_(&tasks_[static_cast<size_t>(selected)]);
  return false;
}

}  // namespace ui
