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

#include "model/bookmarks.h"
#include "model/transcript.h"
#include "proto/session.h"
#include "ui/speech.h"
#include "ui/status_bar.h"

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
  // The bar belongs to the window, not to the pane -- with tabs there will be
  // several panes and one bar (claude-gui-lkk.7).  May stay null; everything
  // here works without it, it just has nowhere to put the four facts.
  void SetStatusBar(StatusBar* bar) { statusBar_ = bar; }
  void Layout(int width, int height);
  bool Start(const proto::Session::Options& options);

  // Called by the host for kMsgDrain and kMsgPermission.
  void OnDrain();
  LRESULT OnPermission(LPARAM pending);

  void FocusPrompt() const;
  // Where the focus was when the window last lost it -- the prompt the first
  // time, because that is where a session starts.  The window itself is never
  // a place for the focus to sit, but neither is the prompt when the reader
  // was in the middle of the transcript: coming back from another application
  // would cost them their place in the text.
  void RestoreFocus() const;
  // Sends what is in the prompt box and clears it.  No-op while a turn is in
  // flight, because a second prompt would queue behind the first with nothing
  // on screen to say so.
  void Send();

  // Enter in the transcript.  The smallest possible piece of step 5, brought
  // forward because without it the output of every tool is in the model and
  // unreachable on screen -- a hole, not a missing convenience.
  void ToggleBlockAtCaret();

  // Esc, from either box.  Stops the turn in flight and writes a mark into
  // the transcript where it was stopped.  Says so out loud in both cases,
  // including the case where nothing was running: a key that answers with
  // silence cannot be told from a key that never arrived.
  void Interrupt();

  // One of t/r/p/a/k/e (capital letter meaning backwards).  Returns false when
  // the character is none of them, so the caller can pass the key on.  Public
  // because both the transcript and the prompt reach it -- see the note on
  // PromptProc about why one arrives as a character and the other as a key.
  bool Navigate(wchar_t key);

  // Ctrl+Shift+<digit> marks, Ctrl+<digit> comes back.  Digit 0 is not
  // markable: it is where the reader was standing when new blocks arrived, and
  // the application writes it.
  void SetBookmark(size_t slot);
  void GoToBookmark(size_t slot);

  const std::wstring& statusLine() const { return status_; }
  bool busy() const { return busy_; }
  // False when nvdaControllerClient.dll is not beside the executable.  The
  // host asks after Create, because that is a thing to be told once at the
  // start and never again -- see Announce for why it cannot be told later.
  bool speechInstalled() const { return speech_.loaded(); }

  // The window moved to a screen with a different scaling.  The controls need
  // a font for the new dpi; the layout follows from the WM_SIZE that comes
  // with the move.
  void OnDpiChanged();

 private:
  // This window's dpi, 96 when there is no window yet to ask.
  UINT Dpi() const;
  // Builds the shell font for the current dpi and hands it to the controls.
  void ApplyFont();

  static LRESULT CALLBACK PromptProc(HWND window, UINT message, WPARAM wParam,
                                     LPARAM lParam, UINT_PTR id,
                                     DWORD_PTR data);
  static LRESULT CALLBACK TranscriptProc(HWND window, UINT message,
                                         WPARAM wParam, LPARAM lParam,
                                         UINT_PTR id, DWORD_PTR data);

  void Apply(const model::Edit& edit);
  void SetStatus(std::wstring text);
  // Model, permission mode and the like, out of system/init.
  void ShowSessionFacts(const proto::Event& event);
  void ShowRateLimit(const proto::Event& event);
  // Puts the caret at the start of a block and says which line that is.
  void GoToBlock(size_t index);
  // Says something, or beeps when there is no screen reader to say it to.
  // Never silent: a key that answers with nothing is indistinguishable from a
  // key that did not arrive.
  void Announce(const std::wstring& text);
  // Puts the caret at an offset and says the line it landed on.
  void GoToOffset(size_t offset);
  // Says that the turn is over.  The answer itself was already read as it
  // arrived, so this only marks the end.
  void SignalTurnEnd();
  // Says what the turn is doing, in the order it does it: the text, the tool
  // calls and the tool results made since the given block id.
  void AnnounceProgress(size_t firstNewId);

  HWND host_ = nullptr;
  HWND transcriptLabel_ = nullptr;
  HWND transcript_ = nullptr;
  HWND promptLabel_ = nullptr;
  HWND prompt_ = nullptr;
  HFONT font_ = nullptr;
  // Kept by the two subclass procedures, read by RestoreFocus.  A window
  // handle and not a flag, so that a third box later needs nothing here.
  HWND lastFocus_ = nullptr;

  StatusBar* statusBar_ = nullptr;
  model::Transcript model_;
  model::Bookmarks bookmarks_;
  proto::Session session_;
  Speech speech_;
  // The id this turn's blocks start from, so that when it ends we know which
  // of the answers is the new one to read out.  An id and not an index: a tool
  // result is inserted behind its call, so the turn's blocks are not the tail
  // of the vector.
  size_t turnFirstId_ = 0;
  // Where Send() last put the caret.  If it is still there when the next
  // prompt goes out, nobody has moved it and the caret is ours to move; if it
  // has moved, the reader is reading and it is theirs.
  size_t anchor_ = 0;

  std::mutex queueMutex_;
  std::vector<proto::Event> queue_;
  bool drainPosted_ = false;

  bool busy_ = false;
  // Set by Esc, cleared by the Result that follows it and by the next prompt.
  // Its whole job is to keep that one Result quiet -- see OnDrain.
  bool interrupted_ = false;
  // Whether "premýšľam" has already been said for the stretch of thinking now
  // running.  Cleared by the prompt and by every tool call announced, so that
  // it is said once per stretch and not once per record -- there are dozens of
  // thinking_tokens per turn.
  bool thinkingSaid_ = false;
  // Whether anything at all was said during this turn.  Decides how its end is
  // marked: a turn that spoke gets a word, a turn that stayed silent gets the
  // beep -- see SignalTurnEnd.
  bool spokeThisTurn_ = false;
  std::wstring status_;
  // The folder name, kept because the bar is rewritten field by field and the
  // project one has to be put back after anything that clears it.
  std::wstring project_;
};

}  // namespace ui

#endif
