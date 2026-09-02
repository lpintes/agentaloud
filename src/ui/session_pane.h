#ifndef UI_SESSION_PANE_H
#define UI_SESSION_PANE_H

// One conversation on screen: the transcript above, the prompt below, and the
// Session behind them.
//
// It does not know whether it is the whole of a window or one page of a tab
// control, and nothing in here may ask.  That is what makes "open in a new
// window" a menu item later rather than a rewrite -- see claude-gui-lkk.7.
//
// The host window has one duty: forward the kMsg* messages below.  They exist
// because Session's callbacks run on its reader thread and every one of these
// controls may only be touched from the thread that made it.

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <mutex>
#include <optional>
#include <string>
#include <vector>

#include "model/transcript.h"
#include "proto/session.h"

namespace ui {

// Events have arrived; drain the queue.  Posted, not sent: the reader thread
// must not wait for the screen.
constexpr UINT kMsgDrain = WM_APP + 1;
// A tool is waiting for a decision.  SENT, not posted -- the reader thread
// blocks until there is an answer, which is exactly what is wanted: the CLI
// is holding the tool until we reply.
constexpr UINT kMsgPermission = WM_APP + 2;

class SessionPane {
 public:
  bool Create(HWND host, HINSTANCE instance);
  void Layout(int width, int height);
  bool Start(const proto::Session::Options& options);

  // Called by the host for kMsgDrain and kMsgPermission.
  void OnDrain();
  LRESULT OnPermission(LPARAM pending);

  void FocusPrompt() const;
  // Sends what is in the prompt box and clears it.  No-op while a turn is in
  // flight, because a second prompt would queue behind the first with nothing
  // on screen to say so.
  void Send();

  // Enter in the transcript.  The smallest possible piece of step 5, brought
  // forward because without it the output of every tool is in the model and
  // unreachable on screen -- a hole, not a missing convenience.
  void ToggleBlockAtCaret();

  const std::wstring& statusLine() const { return status_; }
  bool busy() const { return busy_; }

 private:
  static LRESULT CALLBACK PromptProc(HWND window, UINT message, WPARAM wParam,
                                     LPARAM lParam, UINT_PTR id,
                                     DWORD_PTR data);
  static LRESULT CALLBACK TranscriptProc(HWND window, UINT message,
                                         WPARAM wParam, LPARAM lParam,
                                         UINT_PTR id, DWORD_PTR data);

  void Apply(const model::Edit& edit);
  void SetStatus(std::wstring text);

  HWND host_ = nullptr;
  HWND transcriptLabel_ = nullptr;
  HWND transcript_ = nullptr;
  HWND promptLabel_ = nullptr;
  HWND prompt_ = nullptr;
  HFONT font_ = nullptr;

  model::Transcript model_;
  proto::Session session_;

  std::mutex queueMutex_;
  std::vector<proto::Event> queue_;
  bool drainPosted_ = false;

  bool busy_ = false;
  std::wstring status_ = L"pripravené";
};

}  // namespace ui

#endif
