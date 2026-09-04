#ifndef UI_SESSION_DETAILS_H
#define UI_SESSION_DETAILS_H

// The "what is this session" dialog: the id, what it is running as, where, and
// what it has cost.
//
// It exists because none of that belongs in the status bar.  NVDA reads the
// bar whole, on NVDA+End, so anything put there is read out every time
// anything else there is asked for -- and 36 characters of hex read aloud is
// noise no matter where in the bar it sits.  The rule is already written down
// in status_bar.h for cost and tokens; the session id is the same case, only
// worse, because it cannot even be shortened: `claude -r 587fbc72` answers
// "Provided value is not a UUID".
//
// Until ClaudeLens can pick a session itself (claude-gui-lkk.7.3) this dialog
// is the only way back into a session from the terminal: the CLI's own
// --resume picker never offers ours, because it lists sessions by title and
// titles are made by `ai-title` records that only the interactive TUI writes.

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <functional>
#include <string>

#include "proto/events.h"
#include "win/dialog.h"

namespace ui {

struct SessionDetails {
  std::wstring id;              // empty until the first system/init
  std::wstring model;
  std::wstring permissionMode;  // empty means the CLI's own default
  std::wstring project;         // the full path, not the folder name
  bool haveUsage = false;       // false until the first result record
  proto::Usage usage;
  // How full the context is, out of the newest assistant message.  Kept apart
  // from `usage` because it comes off a different record and arrives sooner:
  // the size shows during the first turn, the window it is measured against
  // only when that turn ends.
  long long contextTokens = 0;
};

class SessionDetailsDialog : public win::Dialog {
 public:
  // onCopyId is the pane's own copy-and-say-so, the same one the keyboard
  // shortcut reaches.  A callback rather than the work itself, so that the
  // dialog does not have to know about the clipboard or about speech, and so
  // that the two ways to copy an id can never say different things.
  SessionDetailsDialog(const SessionDetails& details,
                       std::function<void()> onCopyId);

 protected:
  bool OnInit() override;
  bool OnCommand(int id, int notification) override;

 private:
  const SessionDetails& details_;
  std::function<void()> onCopyId_;
};

}  // namespace ui

#endif
