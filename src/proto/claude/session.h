#ifndef PROTO_CLAUDE_SESSION_H
#define PROTO_CLAUDE_SESSION_H

// One conversation with Claude: a `claude -p` process, its stream, and the
// rules about when it may be shut down.
//
// This lives in proto/ rather than model/ because starting the process, the
// pipes, the JSONL and the control channel all stop being true at the same
// moment -- when the CLI changes.  Keeping them together means one place to
// look when that happens.

#include <atomic>
#include <condition_variable>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include "proto/claude/control.h"
#include "proto/claude/events.h"
#include "win/process.h"

namespace proto {

// What to do about a tool the CLI is asking permission for.  Returned by the
// caller, which in the finished application means a dialog.
struct PermissionDecision {
  bool allow = false;
  std::string denyMessage;   // used when !allow
  Json updatedInput;         // null keeps the tool's own input
  Json updatedPermissions;   // null keeps no rule (SessionPermissions)
};

class Session {
 public:
  struct Options {
    std::wstring workingDir;              // required: this is the project
    std::wstring model;                   // empty means the configured default
    // acceptEdits | auto | bypassPermissions | manual | dontAsk | plan.
    // Empty means whatever the CLI defaults to, which is "ask about
    // everything".  It has to be said here because the mode a terminal
    // session is switched into with Shift+Tab is runtime state of that
    // session: settings, hooks and CLAUDE.md carry over into a headless one,
    // that does not.
    std::wstring permissionMode;
    // The conversation's id, decided here rather than read off the stream.
    // Empty means Start() makes one with NewSessionId() -- unless extraArgs
    // already say which conversation this is, in which case it makes none.
    //
    // It has to be a UUID the CLI has not seen in this project.  A second run
    // with an id that already has a file on disk does not resume it, it dies:
    // "Error: Session ID ... is already in use." on stderr, exit code 1,
    // nothing at all on stdout.  Resuming is --resume, it keeps the id it
    // resumes, and it refuses to be given another one without --fork-session.
    // Measured 2026-09-06 with tools/probe_session_id.py; see
    // claude-gui-lkk.7.2.
    std::wstring sessionId;
    std::vector<std::wstring> extraArgs;  // for spikes and experiments
  };

  // Both are CALLED ON THE READER THREAD.  A GUI has to post across; here they
  // are called directly because a console has nothing to post to.
  using EventCallback = std::function<void(const Event&)>;
  using PermissionCallback =
      std::function<PermissionDecision(const PermissionRequest&)>;
  // The process ended on its own -- after the last event, and never for an
  // end that Stop() asked for.  Also on the reader thread.
  using ExitCallback = std::function<void(const win::Process::Exit&)>;

  Session() = default;
  ~Session();
  Session(const Session&) = delete;
  Session& operator=(const Session&) = delete;

  // On false, startError() is the GetLastError of what failed -- the process
  // not starting at all; a CLI that starts and then refuses its arguments
  // returns true here and is reported through onExit.
  bool Start(const Options& options, EventCallback onEvent,
             PermissionCallback onPermission, ExitCallback onExit = nullptr);
  DWORD startError() const { return process_.startError(); }

  // Queues one user turn.  Returns as soon as it is written; the turn is over
  // when a Result event arrives.
  bool SendPrompt(const std::string& utf8Text);

  // Stops the turn in flight.  False when there is no turn to stop -- Esc on a
  // session that is not working is not an error, it just has nothing to do.
  // Returns as soon as the request is written; the turn is over when a Result
  // arrives, exactly as if it had ended on its own.
  bool Interrupt();

  // Background tasks (MakeStopTask, MakeBackgroundTasks).  Fire and forget;
  // false only when the request could not be written.  StopAllTasks is an
  // interrupt sent whether or not a turn is in flight -- it stops the
  // subagents, and like Interrupt it leaves turnInFlight_ alone -- and a
  // stop_task for every command, which the interrupt does not stop.
  bool StopTask(const std::string& taskId);
  bool StopAllTasks();
  bool BackgroundTasks();

  // Shift+Tab: switch the permission mode without restarting.  False when the
  // request could not be written.  Fire and forget otherwise -- the CLI's
  // control_response confirms the mode, and OnLine folds it into
  // permissionMode() when it arrives.  The mode is also set optimistically here
  // so that a fast second press cycles from the new value, not the old one; a
  // refused mode (auto on an account without it) is put back to the last mode
  // the CLI stated when the refusal arrives.
  bool SetPermissionMode(const std::string& mode);

  // Blocks until the turn in flight ends.  Returns false on timeout, which
  // leaves the session usable -- the caller may want to wait again.
  bool WaitForTurn(unsigned milliseconds);

  // Ends the session cleanly: interrupts the turn in flight and waits for its
  // result, THEN closes stdin.  The order is the whole point; see the note in
  // control.h.  Whoever wants the turn to finish calls WaitForTurn first.
  void Stop(unsigned turnTimeoutMs = 120000);

  // The conversation's id.  Known from Start() onwards, because we are the
  // one who chose it -- the stream would only say it later, and in a project
  // without SessionStart hooks not until the first turn is over.
  //
  // Handed out by value under the lock like handshake() below: a reference
  // into a string another thread may be assigning is a race that shows up as
  // a truncated id once in a hundred runs.
  std::string sessionId() const;
  // What the initialize handshake said: the permission mode and the account.
  // Available before the first turn, unlike everything in `system/init`.
  //
  // Handed out whole rather than field by field, because it all arrives in one
  // record and a second accessor would only be a second chance to read it at a
  // different moment.
  //
  // Empty until the answer arrives, and that is NOT immediate: the CLI answers
  // the handshake only after its SessionStart hooks have run.  Measured in
  // this repository, whose hook runs `bd prime`, the answer was not there
  // after 8 seconds and was after 25.  So "known at startup" means "known once
  // the CLI has finished starting", and a caller that reads this too early
  // gets an empty string rather than a wrong one.
  InitializeInfo handshake() const;

  // The permission mode the session is running under right now, and the one
  // place that knows it -- the pane asks here rather than keeping a copy from
  // the records.  In order of arrival: Options::permissionMode, then the
  // initialize handshake, then every mode the CLI states afterwards
  // (system/status on each change, including the ones it makes itself;
  // system/init; the echo of a set_permission_mode), with SetPermissionMode
  // moving it ahead optimistically in between.  Empty only when nothing was
  // asked for on the command line and the handshake has not been answered --
  // then the mode depends on settings and is genuinely not known.  Kept apart
  // from handshake() because that one is a snapshot of one record and this one
  // changes.
  std::string permissionMode() const;
  // Modes the CLI refused to switch to in this session (PermissionModeTracker).
  std::vector<std::string> refusedModes() const;

 private:
  void OnBytes(std::string_view bytes);
  void OnLine(std::string_view line);
  void OnExit(const win::Process::Exit& exit);
  bool SendJson(const Json& value);

  win::Process process_;
  ExitCallback onExit_;
  // Set by Stop(), so that the end it causes is not reported as the CLI's.
  std::atomic<bool> stopping_{false};
  // One assembler for the life of the session: a record split across two
  // reads has to survive the gap between them.  Built in Start() because it
  // needs `this`.
  std::unique_ptr<LineAssembler> assembler_;
  EventCallback onEvent_;
  PermissionCallback onPermission_;
  // The id of the initialize request, so that an answer to something else is
  // not mistaken for the handshake.  Set once, in Start, before the reader
  // thread exists.
  std::string initRequestId_;

  // Mutable because sessionId() and permissionMode() are const questions with
  // an answer that another thread may be writing at that moment.
  mutable std::mutex mutex_;
  std::condition_variable turnEnded_;
  bool turnInFlight_ = false;
  // Under mutex_: written by the reader thread, read by whoever asks.
  std::string sessionId_;
  InitializeInfo handshake_;
  // The commands in the newest list of background tasks: what StopAllTasks
  // has to stop one by one, because the interrupt leaves them running.
  std::vector<std::string> shellTasks_;
  // What permissionMode() answers.  Seeded in Start, fed every record in
  // OnLine and every Shift+Tab in SetPermissionMode.
  PermissionModeTracker mode_;

  // Writes come from the caller's thread and from the reader thread answering
  // a permission request, so the handle needs its own lock.
  std::mutex writeMutex_;
  // Read from the GUI thread (Interrupt) as well as from Start, so it cannot
  // be a plain unsigned.
  std::atomic<unsigned> nextRequestId_{1};
};

// The command line Session runs, exposed so a test or a log can show exactly
// what was launched.  --permission-prompt-tool stdio is not optional: without
// it no permission is ever put to us.
//
// It is the line for the options AS GIVEN: the id Start makes for itself when
// Options::sessionId is empty is not in it, so a caller that wants to print
// the real line fills the id in first (see spike_console).
std::wstring BuildCommandLine(const Session::Options& options);

// A fresh id for a conversation: a bare lower-case UUID, which is the spelling
// --session-id takes.  Empty when the system refuses to make a GUID, and the
// caller's answer to that is to launch without the switch rather than not
// launch -- an id we do not know is worse than nothing only for us.
std::wstring NewSessionId();

// Do these arguments carry on a conversation that already exists -- --resume,
// -r, --continue, -c?  Two callers ask, and they ask for different reasons:
// Start(), because such a session keeps the id it resumes and refuses to be
// given another one, and the pane, because the stream does not replay the
// history and an empty window otherwise looks exactly like a session that got
// lost.  A custom --session-id is deliberately NOT one of these: it names a
// conversation without continuing one.
bool ResumesConversation(const std::vector<std::wstring>& extraArgs);

// WHICH conversation --resume names, or empty.  Empty is not the same as "not
// resuming": --continue has no value to give, and --resume takes a session
// TITLE as readily as an id, in which case what comes back is not a file name
// and the lookup that uses it will find nothing.  Both are the caller's
// business -- history that cannot be found is a sentence to say, not an error.
std::wstring ResumedConversation(const std::vector<std::wstring>& extraArgs);

}  // namespace proto

#endif
