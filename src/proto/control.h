#ifndef PROTO_CONTROL_H
#define PROTO_CONTROL_H

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

#include <string>

#include "proto/jsonl.h"

namespace proto {

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
// model waits for system/init; the mode does not have to.
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

// updatedInput may be null, in which case the tool's own input is used
// unchanged.  Passing something else is how a dialog can let the user edit a
// commit message before it runs.
Json MakeAllow(const PermissionRequest& request, const Json& updatedInput);

// `message` is handed to the model as the reason, so it is worth writing for
// the model: "the user declined; ask before trying this again" gets a better
// result than "denied".
Json MakeDeny(const PermissionRequest& request, const std::string& message);

// For a control_request we could not answer.  Better than silence: silence
// leaves the CLI waiting until its own timeout.
Json MakeError(const std::string& requestId, const std::string& message);

}  // namespace proto

#endif
