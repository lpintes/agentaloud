#ifndef PROTO_CLAUDE_CONTROL_H
#define PROTO_CLAUDE_CONTROL_H

// The control protocol: the half of the stream where the CLI asks us
// something and waits for an answer, rather than telling us what happened.
//
// Everything here was established by running the CLI, not read from a
// specification -- see tools/spike_control.py and the notes on claude-gui-lkk.1.
// Two things that cost time and are easy to get wrong again:
//
//   * The routing has to be switched on with --permission-prompt-tool stdio.
//     Without it a rule that says "ask" is answered as "deny" and nothing is
//     ever put to us.  The switch is missing from --help but the CLI takes it.
//
//   * Standard input IS the control channel.  Close it before the turn is over
//     and the CLI reports the tool as
//         "Tool permission request failed: AbortError: Stream closed"
//     with non_execution_kind "permission-rule" -- which reads as a rule
//     denying the tool, not as a channel we broke.  The model then treats it
//     as a transient fault and retries, so the mistake costs tokens as well.
//     Session enforces the ordering; nothing else should close the handle.

#include <map>
#include <string>
#include <vector>

#include "proto/jsonl.h"

namespace proto {

// One entry of the CLI's own list of slash commands.  Measured 2026-09-06 with
// tools/probe_commands.py: 79 of them on this account, each carrying a name, a
// description and an argumentHint that is usually empty (22 of the 79 had
// one), and the plugin ones carrying `aliases` -- the same command under its
// short name.
//
// `system/init` has a list too and it is NOT a second source to fall back on:
// it carries BARE NAMES, no description and no hint, and it is a per-turn
// record, so it says less and says it later.  Everything here comes from the
// handshake.
struct SlashCommand {
  std::string name;          // "code-review", or "plugin:command"
  std::string description;
  std::string argumentHint;  // "[low|medium|high] [<pr#>]", often empty
  std::vector<std::string> aliases;
};

// The CLI asking to run a tool.  Field names are the wire's, not ours.
struct PermissionRequest {
  std::string requestId;   // the CLI's UUID; an answer must echo it exactly
  std::string toolName;    // "Bash"
  std::string displayName;
  std::string description;
  std::string toolUseId;   // ties back to the tool_use block in the transcript
  // Why we are being asked: "rule" means it hit an ask rule in settings or a
  // hook.  Worth showing -- "you asked to be asked about commits" is a better
  // prompt than a bare command line.
  std::string decisionReasonType;
  // True when the tool's whole job is to put something to a person --
  // AskUserQuestion is the one that matters here.  It says a human is wanted;
  // it does NOT say what the input looks like, so nothing may branch on it to
  // read a payload.  See proto/ask.h.
  bool requiresUserInteraction = false;
  Json input;              // the tool's arguments, e.g. {"command": "..."}
  // What the terminal offers as "Yes, and don't ask again for ...": rules
  // ({type: addRules, rules: [{toolName, ruleContent}]}), a mode (setMode
  // acceptEdits, for Write and Edit), a directory.  Null when the CLI sends
  // none, as for a call that hit an ask rule -- which no allow rule would
  // beat anyway.
  Json suggestions;
};

// Returns false when the record is a control_request of some other subtype;
// `out` is then untouched.
bool ParsePermissionRequest(const Json& record, PermissionRequest* out);

// Our opening request.  The response carries the slash commands, agents,
// models and the CLI's pid -- useful, but the point of sending it is that the
// control channel is established before the first turn.
Json MakeInitialize(const std::string& requestId);

// What the answer to MakeInitialize says about the session.  It arrives before
// the first turn, which makes it the earliest anything is known at all --
// everything else comes out of `system/init`, and that is a per-turn record.
//
// What is NOT in it is the model, and that is worth writing down because the
// response looks like it has one.  It carries a `models` array, but that is a
// catalogue: what may be passed to --model and what each of those words
// resolves to.  Nothing marks the one this session runs.  Measured on an
// account whose settings.json says "model": "opus", the entry for "default"
// resolves to claude-sonnet-5 -- so reading the model out of that list would
// have shown the wrong model until the first turn quietly replaced it.  The
// model waits for the stream -- and not for system/init either, see
// ParseAnsweringModel in events.h; the mode does not have to wait.
// The account, on the other hand, IS in it -- under `account`, and measured
// there (tools/probe_dialog.py, 2026-09-05) rather than assumed.  It matters
// for one reason: on a subscription the billed cost of a session is zero, so
// the cost shown in the details dialog reads as "this was free" with nothing
// on screen to say why.  "Claude Pro" one row above is the why.
//
// `organization` is deliberately not taken: on a personal account it is made
// out of the address ("<email>'s Organization") and would be the same fact
// read twice.  A team account would want it, and that is a bead, not a guess.
struct InitializeInfo {
  std::string requestId;       // echoes ours, so a stray response is ignored
  std::string permissionMode;  // current_permission_mode
  std::string accountEmail;
  std::string subscriptionType;  // "Claude Pro"
  // "firstParty", or Bedrock/Vertex.  Worth carrying because it changes what
  // the cost numbers mean, not because it is interesting in itself.
  std::string apiProvider;
  // What may be typed with a leading slash.  Empty until the handshake is
  // answered, which is not immediate -- see the note on Session::handshake().
  std::vector<SlashCommand> commands;
};

// False when the record is not a successful control_response, or carries none
// of the above.
bool ParseInitializeResponse(const Json& record, InitializeInfo* out);

// Stops the turn in flight.  NOT control_cancel_request -- that one withdraws
// a control_request of our own that we no longer want answered, and a turn is
// not one of those.  The subtype is `interrupt`, and `reason` is what the CLI
// forwards to the turn's AbortSignal: "interrupt" is the value its own Esc and
// Ctrl+C use, and tools branch on it to keep quiet about being aborted rather
// than reporting an error.
//
// Fire and forget: the CLI answers with a control_response listing the queued
// messages that survive, which we have none of, so nothing has to remember the
// request id.  The end of the turn arrives the usual way, as a Result.
Json MakeInterrupt(const std::string& requestId);

// The task_id of every command (local_bash) in a
// system/background_tasks_changed; empty for any other record.  The commands
// are what an interrupt leaves running (bg-bash, 2026-10-09), so StopAllTasks
// stops them one by one.
std::vector<std::string> ShellTaskIds(const Json& record);

// Stops one background task, by the task_id of system/background_tasks_changed.
// Measured 2026-10-08 (tools/probe_subagents.py bg-stop): the task is reported
// killed and dropped from the list, the answer is a control_response success
// with {}, and the CLI then starts a turn of its own to tell the model.
//
// An interrupt with no turn in flight is the other way to stop subagents, and
// it stops all of them without a turn after (scenario C1): MakeInterrupt, sent
// regardless of the turn.  It does not stop a command: the answer is success
// and the command runs on (bg-bash).  A command stopped by stop_task brings no
// turn after it either -- that is only what a stopped subagent does.
Json MakeStopTask(const std::string& requestId, const std::string& taskId);

// Ctrl+B: every foreground task of the turn in flight -- a command, a
// subagent -- goes on in the background.  Without tool_use_id it means all of
// them.  Measured 2026-10-08 (fg-bgall): each one shows up in
// background_tasks_changed, the answer is a success with {}, and the turn
// carries on.
//
// The answer is the same success when nothing went (fg-bash, 2026-10-09): a
// Bash command becomes a task about 8 s after its tool_use, and a request
// before that leaves it in the foreground.  What went is said only by a
// system/task_updated with {is_backgrounded: true} before the answer.
Json MakeBackgroundTasks(const std::string& requestId);

// What our background_tasks request ids start with, so that their answers can
// be told from the answers to everything else we ask.
extern const char kBackgroundRequestPrefix[];

// True for the answer to one of our background_tasks requests.
bool IsBackgroundAnswer(const Json& record);

// True for the system/task_updated that says a task went to the background.
bool IsMovedToBackground(const Json& record);

// Shift+Tab without restarting the session: change the permission mode of the
// turn in flight and every turn after it.  The mode a terminal is switched
// into this way is runtime state of that one process -- settings, hooks and
// CLAUDE.md carry into a headless session, this does not -- so it is the only
// way to reach acceptEdits or plan once the CLI is running.
//
// Verified 2026-09-05 with no credit spent (claude-gui-lkk.6.1): the CLI
// answers with a control_response success whose body is {"mode": "<mode>"},
// and a second `initialize` afterwards returned current_permission_mode moved
// to the new value -- so the change settles, it is not just acknowledged.
//
// `mode` is one the wire accepts: default, acceptEdits, plan, auto or dontAsk.
// The schema lists bypassPermissions too, but a stdio host is not allowed to
// switch into it at runtime -- measured 2026-09-10 (tools/probe_mode.py), the
// request comes back `subtype: "error"` with an empty body, so the only way to
// that mode is --dangerously-skip-permissions at launch.  auto and dontAsk both
// succeed; an unrecognised mode is answered with an error.
Json MakeSetPermissionMode(const std::string& requestId,
                           const std::string& mode);

// The mode a successful set_permission_mode response settled on.  False when
// the record is not a successful control_response.  `requestId` is filled so a
// caller can tell its own request's answer from another's; `mode` may come
// back empty even on success -- the schema says a non-headless host may ack
// with {} -- and the caller then keeps the mode it asked for.
bool ParseSetPermissionModeResponse(const Json& record, std::string* requestId,
                                    std::string* mode);

// The next mode in the Shift+Tab cycle, in the terminal's order:
//   default -> acceptEdits -> plan -> auto -> default
// This is the rotation the TUI's own Shift+Tab hint lists (read out of the CLI
// binary 2026-09-10: "default - ask before every edit / accept edits - edit
// freely, ask for commands / plan - research and propose, never touch files /
// auto - Claude decides what is safe").  bypassPermissions is a mode too but
// the control channel refuses it from a stdio host (see MakeSetPermissionMode);
// dontAsk is switchable but not in the rotation.  An unknown or off-cycle
// current mode restarts at acceptEdits, so the key always moves.
std::string NextPermissionMode(const std::string& current);

// The mode a record says the CLI is in now, when it says one.  Two records do:
//
//   system/status  -- sent on EVERY change of mode, whoever made it.  Our own
//                     set_permission_mode, but also the ones the CLI makes by
//                     itself: approving ExitPlanMode leaves plan, and auto is
//                     dropped when its gate closes.  Measured 2026-09-11: after
//                     set_permission_mode auto and then plan, each confirmation
//                     was followed by {"type":"system","subtype":"status",
//                     "status":null,"permissionMode":"auto"} (then "plan").  In
//                     the binary it is sessionState.onPermissionModeChanged.
//   system/init    -- the mode at the start of every turn.
//
// Without the first one the application knows only the changes it asked for,
// and a session that left plan through ExitPlanMode kept saying "plánovanie"
// (claude-gui-lkk.41).  Other status records ("requesting", compaction) carry
// no mode and give false.
bool ParsePermissionModeReport(const Json& record, std::string* mode);

// What our set_permission_mode request ids start with, so that their answers
// can be told from the answers to everything else we ask.
extern const char kModeRequestPrefix[];

// True for the answer, success or refusal, to one of our set_permission_mode
// requests.
bool IsModeAnswer(const Json& record);

// The permission mode the session is in, folded out of everything that says
// it.  Not thread-safe; Session holds it under its lock.  A class of its own,
// out of Session, so that the rules below can be tested on records rather than
// on a running CLI -- the first version of them had three silent bugs, and all
// three were in the order things arrive.
//
//   Seed        the mode asked for on the command line.  The CLI starts in it,
//               and knowing it now lets Shift+Tab cycle from it before the
//               handshake is answered, which with a SessionStart hook is
//               twenty seconds away.
//   Requested   a Shift+Tab sent.  current() moves to it at once, so that a
//               second press cycles from the new mode.
//   Observe     a record off the stream.  The handshake, system/status,
//               system/init and the echo of any set_permission_mode are
//               reports of the mode; the answer to the newest request settles
//               current() -- to the echo, or on refusal to the last mode the
//               CLI stated.
//
// Reports that come while a request is unanswered move only the last-stated
// mode and not current(): the CLI writes in order, so such a report may
// predate the request, and taking it would put the old mode back under a
// reader who has just heard the new one.  An older request answered after a
// newer one does not settle current() either -- it would restore a mode the
// reader has already pressed past.
//
// A mode the CLI refused once is kept in refused() for the rest of the
// session, and Shift+Tab steps over it (claude-gui-lkk.46).  Without that, a
// refused auto put the mode back on plan, the next press asked for auto again,
// and plan could not be left by the key at all.  For the session only: the
// refusal depends on the account and the gate, and the same account let auto
// through a few hours later.  Settings refuse it reliably --
// permissions.disableAutoMode "disable" gives {"subtype":"error","error":
// "Cannot set permission mode to auto: auto mode disabled by settings",
// "error_code":"auto_mode_settings"} (tools/probe_mode.py, 2026-10-08).
class PermissionModeTracker {
 public:
  void Seed(const std::string& mode);
  void Requested(const std::string& requestId, const std::string& mode);
  void Observe(const Json& record);
  // Empty while nothing was asked for and the CLI has not said: the mode then
  // comes from settings and is genuinely not known.
  const std::string& current() const { return current_; }
  const std::vector<std::string>& refused() const { return refused_; }

 private:
  void Report(const std::string& mode);

  std::string current_;
  std::string reported_;
  std::string pendingId_;
  // Every request not answered yet, newest or not: a refusal of an older one
  // is still a refusal of its mode.
  std::map<std::string, std::string> asked_;
  std::vector<std::string> refused_;
};

// updatedInput may be null, in which case the tool's own input is used
// unchanged.  Passing something else is how a dialog can let the user edit a
// commit message before it runs.
// `updatedPermissions`, when not null, is sent along: the rules the CLI is to
// keep from now on (SessionPermissions).
Json MakeAllow(const PermissionRequest& request, const Json& updatedInput,
               const Json& updatedPermissions = nullptr);

// The CLI's own suggestions, kept for this session only.  The CLI files them
// under "localSettings", which is .claude/settings.local.json -- a file an
// answer in a dialog has no business writing.  With "session" they hold the
// same and nothing is written (measured, tools/probe_permission_session.py,
// claude-gui-lkk.61).  Null when there is nothing to keep.
Json SessionPermissions(const Json& suggestions);

// `message` is handed to the model as the reason, so it is worth writing for
// the model: "the user declined; ask before trying this again" gets a better
// result than "denied".
Json MakeDeny(const PermissionRequest& request, const std::string& message);

// For a control_request we could not answer.  Better than silence: silence
// leaves the CLI waiting until its own timeout.
Json MakeError(const std::string& requestId, const std::string& message);

}  // namespace proto

#endif
