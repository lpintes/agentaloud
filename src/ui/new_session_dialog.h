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

#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "agent/backend.h"
#include "win/dialog.h"

namespace ui {

// agent::Backend::ListConversations on a backend made for the asking.  Called
// off the dialog's thread, so it must not touch a window.
using ConversationLister = std::function<bool(
    const std::wstring& projectDir, std::vector<agent::Conversation>* out)>;

struct NewSessionBackend {
  std::string name;                 // what --backend takes, shown as it is
  std::vector<std::string> models;  // Capabilities::models
  // What the model field holds when this backend is chosen: --model, or
  // the settings file's line for this backend.  Empty means the CLI's own
  // default.
  std::wstring model;
  // Empty when the backend cannot carry a conversation on.
  ConversationLister list;
};

// A conversation some session of this application has open.  Starting it a
// second time is refused by the CLI (invariant 14), so the list marks it and
// choosing it brings its window forward instead.
struct OpenConversation {
  std::string backend;  // NewSessionBackend::name
  std::wstring project;
  std::string id;       // agent::Backend::conversationId; empty until known
};

struct NewSession {
  std::vector<NewSessionBackend> backends;
  // In: the one selected first.  Out: the one chosen.
  std::string backend;
  // Out only: what the model field said, trimmed.  May be empty.
  std::wstring model;
  // In and out.  Out, it names a folder that existed when OK was pressed.
  std::wstring project;
  // In only.
  std::vector<OpenConversation> open;
  // Out: the conversation to carry on, empty for a new one.
  std::wstring resumeId;
  // Out: an index into `open` whose window should come forward instead of a
  // session being started; -1 otherwise.
  int switchTo = -1;
};

class NewSessionDialog : public win::Dialog {
 public:
  explicit NewSessionDialog(NewSession* session);
  ~NewSessionDialog() override;

 protected:
  bool OnInit() override;
  bool OnCommand(int id, int notification) override;
  bool OnOk() override;
  bool OnMessage(UINT message, WPARAM wParam, LPARAM lParam) override;

 private:
  // What the worker thread and the dialog share.  The worker posts only
  // while `dialog` is set, and the dialog clears it under the lock when it
  // goes, so a list that arrives late is dropped rather than posted to a
  // window that is no more.
  struct Shared;
  struct Listed;

  void ShowModels(int backend);
  void Browse();
  void Refuse(int focus, const std::wstring& why);
  int SelectedBackend() const;
  // Asks for the conversations of the backend and folder in the fields,
  // unless those are what the list already shows.
  void ListConversations();
  void ShowConversations(const Listed& listed);
  // One line of the list, for the row after "Nový rozhovor".
  std::wstring RowText(const agent::Conversation& conversation) const;
  int OpenIndex(const std::string& backend, const std::wstring& id) const;

  NewSession* session_;
  std::shared_ptr<Shared> shared_;
  // Which request the list shows or waits for; a reply to an older one is
  // dropped.
  unsigned generation_ = 0;
  std::string listedBackend_;
  std::wstring listedProject_;
  // Parallel to the list box after its first row.
  std::vector<agent::Conversation> conversations_;
};

}  // namespace ui

#endif
