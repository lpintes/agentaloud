#ifndef UI_NEW_SESSION_DIALOG_H
#define UI_NEW_SESSION_DIALOG_H

// "Nová session": which agent, which model and in which folder.
//
// Shown when the command line names no folder -- where the bare shell folder
// picker used to be -- and meant to be the same dialog a menu opens later.
// That is why it takes the backends as names and lists, and not as a choice
// made in main.cpp: the pane must not learn which CLIs exist, and neither
// must this.
//
// The folder is a text field with a button beside it, not the picker alone.
// A path is often already on the clipboard, and typing or pasting it is one
// edit box a screen reader reads out, where the shell picker is a tree to be
// walked.

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <string>
#include <vector>

#include "win/dialog.h"

namespace ui {

struct NewSessionBackend {
  std::string name;                 // what --backend takes, shown as it is
  std::vector<std::string> models;  // Capabilities::models
  // What the model field holds when this backend is chosen: --model, or
  // the settings file's line for this backend.  Empty means the CLI's own
  // default.
  std::wstring model;
};

struct NewSession {
  std::vector<NewSessionBackend> backends;
  // In: the one selected first.  Out: the one chosen.
  std::string backend;
  // Out only: what the model field said, trimmed.  May be empty.
  std::wstring model;
  // In and out.  Out, it names a folder that existed when OK was pressed.
  std::wstring project;
};

class NewSessionDialog : public win::Dialog {
 public:
  explicit NewSessionDialog(NewSession* session);

 protected:
  bool OnInit() override;
  bool OnCommand(int id, int notification) override;
  bool OnOk() override;

 private:
  void ShowModels(int backend);
  void Browse();
  void Refuse(int focus, const std::wstring& why);

  NewSession* session_;
};

}  // namespace ui

#endif
