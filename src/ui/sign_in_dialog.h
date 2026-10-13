#ifndef UI_SIGN_IN_DIALOG_H
#define UI_SIGN_IN_DIALOG_H

// Signing the agent in: what to do next, the address to open and the code,
// while the agent waits for the browser (claude-gui-lkk.71.4).
//
// Live, like the task list: the dialog opens before the agent has answered
// Backend::StartSignIn, the pane's queue is drained under it, and the pane
// hands it the prompt and the outcome as they arrive (ShowPrompt,
// ShowFinished).  It stays open after the outcome, for the reason Ctrl+F
// does: the outcome is a sentence, and behind a closing dialog speech dies
// (invariant 6).
//
// The browser is not opened by itself: the reader may want the address on
// another device, or in a browser of their own choosing.  Open in browser
// does it on request.

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <functional>
#include <optional>
#include <string>

#include "agent/events.h"
#include "win/dialog.h"

namespace ui {

class SignInDialog : public win::Dialog {
 public:
  struct Actions {
    // The answer to a key or a button, said at once (Announce).
    std::function<void(const std::wstring&)> announce;
    // The code the page gave, to the backend.  False when it could not go.
    std::function<bool(const std::string&)> submit;
  };

  // `agentName` as the reader hears it, for the sentences that name it.
  SignInDialog(std::wstring agentName, Actions actions);

  // Each returns what the reader has to hear about the change, or nothing
  // when the dialog's own focus move already says it.  The pane says it,
  // queued: the change came on its own (invariant 7).
  std::wstring ShowPrompt(const agent::SignInPrompt& prompt);
  std::wstring ShowFinished(const agent::SignInFinished& finished);

  // Whether the sign-in is over; closing before then cancels it.
  bool finished() const { return finished_.has_value(); }

 protected:
  bool OnInit() override;
  bool OnCommand(int id, int notification) override;
  bool OnOk() override;

 private:
  // The controls brought up to prompt_ and finished_.
  void Refresh();
  std::wstring StatusText() const;
  void OpenInBrowser();
  void CopyCode();
  void SubmitCode();
  // Whether the keyboard focus is on that control of this dialog.
  bool Focused(int id) const;

  std::wstring agentName_;
  Actions actions_;
  std::optional<agent::SignInPrompt> prompt_;
  std::optional<agent::SignInFinished> finished_;
  // A code has gone to the backend and the outcome has not come yet.
  bool checking_ = false;
};

}  // namespace ui

#endif
