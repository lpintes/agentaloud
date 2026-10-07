#ifndef UI_SESSION_PANE_H
#define UI_SESSION_PANE_H

// One conversation on screen: the transcript above, the prompt below, and the
// agent behind them.
//
// It does not know whether it is the whole of a window or one page of a tab
// control, and nothing in here may ask.  That is what makes "open in a new
// window" a menu item later rather than a rewrite -- see claude-gui-lkk.7.
//
// Nor does it know which CLI the agent is.  It holds an agent::Backend and
// asks it what it can do (agent::Capabilities); what Claude or Codex call
// things is their adapter's business.
//
// The host window has one duty: forward the kMsg* messages below.  They exist
// because the backend's callbacks run on its reader thread and every one of
// these controls may only be touched from the thread that made it.

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

#include "agent/backend.h"
#include "model/bookmarks.h"
#include "model/transcript.h"
#include "ui/session_details.h"
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
// The model is asking the reader a multiple-choice question.  Sent, for the
// same reason as kMsgPermission.
constexpr UINT kMsgQuestion = WM_APP + 3;
// A resumed conversation's history, when the backend hands it over on its
// reader thread.  Posted; the history waits in the pane until it is drained.
constexpr UINT kMsgHistory = WM_APP + 4;
// A turn ended on a question that is answered by the next prompt
// (agent::QuestionByPrompt).  Posted from the drain rather than handled in
// it: the dialog runs a modal loop, and a drain inside a drain would apply
// one batch in the middle of another.
constexpr UINT kMsgQuestionByPrompt = WM_APP + 5;

class SessionPane {
 public:
  bool Create(HWND host, HINSTANCE instance);
  // The bar belongs to the window, not to the pane: there are several panes
  // and one bar, and it shows the active one (claude-gui-lkk.7.9).  The pane
  // keeps its four fields itself and hands them over whole when it gets the
  // bar, so a session switched to shows its own facts at once rather than
  // whatever the previous one left there.  Null while the pane is not the
  // active one; everything here works without it.
  void SetStatusBar(StatusBar* bar);
  // Whether this is the session the reader is in.  The pane cannot ask -- it
  // does not know what hosts it -- so the host says.  A session that is not
  // active is spoken for as if its window were in the background
  // (invariant 11): its progress is silent and its end is a sound.
  void SetActive(bool active) { active_ = active; }
  void Layout(int width, int height);
  // The pane owns the backend from here on.
  bool Start(std::unique_ptr<agent::Backend> backend,
             const agent::StartOptions& options);
  // Which of the sessions in this folder this one is, zero when it is the
  // only one (app::SessionOrdinals).  The host says, and says again whenever
  // a session in the same folder opens or closes; it goes into the title and
  // the bar's project field, both of which would otherwise be the same for
  // every session in the folder.
  void SetOrdinal(int ordinal);

  // Called by the host for the kMsg* messages above.
  void OnDrain();
  LRESULT OnPermission(LPARAM pending);
  LRESULT OnQuestion(LPARAM pending);
  void OnHistoryPosted();
  void OnQuestionByPrompt();

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
  // What Send does once it has the text -- also the way an answer chosen in
  // the dialog of a QuestionByPrompt goes out, so that it is a prompt in
  // every respect: in the transcript, in the status bar and in speech.
  void SendText(const std::wstring& text);

  // Enter in the transcript.  The smallest possible piece of step 5, brought
  // forward because without it the output of every tool is in the model and
  // unreachable on screen -- a hole, not a missing convenience.
  void ToggleBlockAtCaret();

  // Esc, from either box.  Stops the turn in flight and writes a mark into
  // the transcript where it was stopped.  Says so out loud in both cases,
  // including the case where nothing was running: a key that answers with
  // silence cannot be told from a key that never arrived.
  void Interrupt();

  // Shift+Tab: step the permission mode through default -> acceptEdits -> plan
  // -> auto -> default, the way the terminal does.  Says the new mode out loud
  // -- it is a key whose only other trace is the status bar, which NVDA does
  // not read on its own (invariant 6) -- and rewrites the model field of the
  // bar.  Refuses, and says why, while the mode is not known at all.
  void CyclePermissionMode();

  // One of t/r/p/a/k/e (capital letter meaning backwards).  Returns false when
  // the character is none of them, so the caller can pass the key on.  Public
  // because both the transcript and the prompt reach it -- see the note on
  // PromptProc about why one arrives as a character and the other as a key.
  bool Navigate(wchar_t key);

  // F1: the list of keys, in a modal dialog.  The list itself is in
  // ui/keys_dialog.cpp; this is only the key that opens it.
  //
  // F1 and not a menu item: F1 is what a reader tries first, and the menu
  // bar holds what belongs to the window, not to one session.
  void ShowKeys();
  // F2: what this session is and what it has cost, in a modal dialog.  See
  // ui/session_details.h for why a dialog and not the status bar.
  void ShowDetails();
  // F4: the CLI's list of slash commands, and the chosen one typed into the
  // prompt for you.  It inserts rather than sends: most commands take
  // arguments, and one that does not is one Ctrl+Enter away.
  //
  // F4 and not Ctrl+/ as the design said.  "/" is not a key on the Slovak
  // layout -- measured, VK_OEM_2 there produces "=" -- so a chord named after
  // the character would be a chord whose name is wrong on the keyboard this is
  // written for.  A function key is positional, produces no WM_CHAR, and sits
  // beside F2, which already opens the other dialog.
  void ShowCommands();
  // Ctrl+F: what to look for, in a dialog that stays open, each Enter the
  // next match forwards from the caret.  F3 and Shift+F3 repeat it either way
  // without the dialog, and open it when nothing has been searched for yet.
  void ShowFind();
  void FindNext(bool backwards);
  // Ctrl+Shift+C: the session id onto the clipboard, so that `claude -r <id>`
  // in a terminal reaches the same conversation.  Its own key and not just a
  // button in the dialog, because it is the one thing in there that is never
  // read -- it is pasted.
  void CopySessionId();

  // Ctrl+Shift+<digit> marks, Ctrl+<digit> comes back.  Digit 0 is not
  // markable: it is where the reader was standing when new blocks arrived, and
  // the application writes it.
  void SetBookmark(size_t slot);
  void GoToBookmark(size_t slot);

  const std::wstring& statusLine() const { return status_; }
  bool busy() const { return busy_; }

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
  // The end of a turn, however it ended: the bar is cleared, and the reader
  // hears that it is over -- "prerušené" if they stopped it, otherwise
  // SignalTurnEnd.
  void OnTurnEnded();
  void SetStatus(std::wstring text);
  // The model that is answering now.  Which record says that is the
  // translator's business (invariant 21); this only shows it.
  void ShowModel(const std::string& model);
  // Brings details_ and the bar up to the mode the backend reported last, and
  // says a change this pane did not make itself: a refused Shift+Tab,
  // ExitPlanMode approved, auto dropped by the CLI.  Called once per drain
  // with the newest ModeChanged in it, so a drain that carried two changes
  // says only where it ended up.
  void FollowPermissionMode(const std::string& live);
  // The mode's name and what it does, out of the backend's list; the id
  // itself when the backend does not know it, which is still more than
  // nothing.
  std::wstring ModeLabel(const std::wstring& id) const;
  std::wstring ModeSentence(const std::wstring& id) const;
  // The resumed conversation, into the transcript as one edit, with the caret
  // put at its end.
  void RestoreHistory(const std::vector<agent::Event>& events);
  // Rewrites the bar's model field from details_.model and
  // details_.permissionMode.  Shared by everything that moves either, so they
  // never format it differently.
  void RefreshModelField();
  void ShowRateLimit(const agent::RateLimitChanged& limit);
  // Puts the caret at the start of a block and says which line that is.
  void GoToBlock(size_t index);
  // Whether the caret is still where the application left it, or at the end.
  // The reader is then listening rather than reading, and text that arrives
  // may both move the caret and be spoken.  False means they went somewhere in
  // the transcript themselves, and the place -- and the silence -- is theirs.
  bool Following() const;
  // Whether this pane's window is the one the user is working in.
  bool InForeground() const;
  // Whether a running turn may speak at all.  See the comment on the
  // definition: three states, and only one of them is spoken to.
  bool WantsProgressSpeech() const;
  // Says something, or beeps when there is no screen reader to say it to.
  // Never silent: a key that answers with nothing is indistinguishable from a
  // key that did not arrive.
  void Announce(const std::wstring& text);
  // One field of the bar, kept and shown when there is a bar.
  void SetField(StatusBar::Field field, const std::wstring& text);
  // The title and the project field from path_, ordinal_ and mismatch_.
  void ShowName();
  // Puts the caret at an offset and says the line it landed on.
  void GoToOffset(size_t offset);
  // Says that the turn is over.  The answer itself was already read as it
  // arrived, so this only marks the end.
  void SignalTurnEnd();
  // A modal box is about to go up while the reader is somewhere else.  Flashes
  // the taskbar button and plays a sound; silent when the window is in front,
  // where the box speaks for itself.  See the definition for why speech is not
  // an option here and why the sound is neither of the two already in use.
  void SignalWaiting() const;
  // Takes the id and the account off the backend at the moment they are
  // needed.  See the note on the definition for why they are not kept up to
  // date instead, and why the permission mode is no longer among them.
  void RefreshFacts();
  // Says what the turn is doing, in the order it does it: the text, the tool
  // calls and the tool results made since the given block id.
  void AnnounceProgress(size_t firstNewId);
  // The caret onto the next match, expanding the block it is in.  Says
  // nothing: the two callers say different things, or nothing at all.
  std::optional<model::SearchHit> FindText(const std::wstring& text,
                                           bool backwards);
  // FindText, and then the line with the match or that there was none.
  // Shared by F3 and the dialog, which stays open so that this can be heard.
  bool SearchAndSay(const std::wstring& text, bool backwards);

  HWND host_ = nullptr;
  HWND transcriptLabel_ = nullptr;
  HWND transcript_ = nullptr;
  HWND promptLabel_ = nullptr;
  HWND prompt_ = nullptr;
  HFONT font_ = nullptr;
  // Kept by the two subclass procedures, read by RestoreFocus.  A window
  // handle and not a flag, so that a third box later needs nothing here.
  HWND lastFocus_ = nullptr;
  // The last thing Ctrl+F looked for, for F3 and for the dialog next time.
  std::wstring searchText_;

  StatusBar* statusBar_ = nullptr;
  std::wstring fields_[StatusBar::kFieldCount];
  bool active_ = true;
  model::Transcript model_;
  model::Bookmarks bookmarks_;
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
  // One batch per message off the wire.  A batch is what the transcript
  // appends as one edit where it can.
  std::vector<std::vector<agent::Event>> queue_;
  bool drainPosted_ = false;
  // A history handed over on the reader thread, waiting for kMsgHistory.
  // Under queueMutex_.
  std::vector<agent::Event> history_;

  bool busy_ = false;
  // The questions of the turn now running that wait for the next prompt.
  // Offered when the turn ends, unless the reader stopped it.
  std::vector<agent::QuestionByPrompt> questionsByPrompt_;
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
  // The whole path, the title's base.
  std::wstring path_;
  int ordinal_ = 0;
  // Once the widget and the model have disagreed, the title says so for the
  // rest of the session -- renumbering must not wipe it.
  bool mismatch_ = false;
  // Everything the dialog shows, gathered as it arrives.  Kept here rather
  // than asked for when the dialog opens, because most of it comes off records
  // that have long gone past by then.
  SessionDetails details_;

  // LAST, so that it is destroyed FIRST.  Its destructor waits out the turn
  // and stops the reader thread, and that thread writes into queue_ and sends
  // to host_ until it stops -- members destroyed before it would be destroyed
  // under a thread still using them.
  std::unique_ptr<agent::Backend> backend_;
};

}  // namespace ui

#endif
