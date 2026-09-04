#ifndef PROTO_SESSION_H
#define PROTO_SESSION_H

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

#include "proto/control.h"
#include "proto/events.h"
#include "win/process.h"

namespace proto {

// What to do about a tool the CLI is asking permission for.  Returned by the
// caller, which in the finished application means a dialog.
struct PermissionDecision {
  bool allow = false;
  std::string denyMessage;   // used when !allow
  Json updatedInput;         // null keeps the tool's own input
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
    std::vector<std::wstring> extraArgs;  // for spikes and experiments
  };

  // Both are CALLED ON THE READER THREAD.  A GUI has to post across; here they
  // are called directly because a console has nothing to post to.
  using EventCallback = std::function<void(const Event&)>;
  using PermissionCallback =
      std::function<PermissionDecision(const PermissionRequest&)>;

  Session() = default;
  ~Session();
  Session(const Session&) = delete;
  Session& operator=(const Session&) = delete;

  bool Start(const Options& options, EventCallback onEvent,
             PermissionCallback onPermission);

  // Queues one user turn.  Returns as soon as it is written; the turn is over
  // when a Result event arrives.
  bool SendPrompt(const std::string& utf8Text);

  // Stops the turn in flight.  False when there is no turn to stop -- Esc on a
  // session that is not working is not an error, it just has nothing to do.
  // Returns as soon as the request is written; the turn is over when a Result
  // arrives, exactly as if it had ended on its own.
  bool Interrupt();

  // Blocks until the turn in flight ends.  Returns false on timeout, which
  // leaves the session usable -- the caller may want to wait again.
  bool WaitForTurn(unsigned milliseconds);

  // Ends the session cleanly: waits out the turn in flight, THEN closes stdin.
  // The order is the whole point; see the note in control.h.
  void Stop(unsigned turnTimeoutMs = 120000);

  // Both of these are written on the reader thread and read from whatever
  // thread asks, so they are handed out by value under the lock.  A reference
  // into a string another thread may be assigning is a race that shows up as
  // a truncated id once in a hundred runs.
  std::string sessionId() const;
  // What the CLI says it is running as, out of the initialize handshake --
  // available before the first turn, unlike everything in `system/init`.
  // Empty until the answer arrives, which is a few milliseconds after Start.
  std::string permissionMode() const;

 private:
  void OnBytes(std::string_view bytes);
  void OnLine(std::string_view line);
  bool SendJson(const Json& value);

  win::Process process_;
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
  std::string permissionMode_;

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
std::wstring BuildCommandLine(const Session::Options& options);

}  // namespace proto

#endif
