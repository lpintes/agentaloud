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
  Json input;              // the tool's arguments, e.g. {"command": "..."}
};

// Returns false when the record is a control_request of some other subtype;
// `out` is then untouched.
bool ParsePermissionRequest(const Json& record, PermissionRequest* out);

// Our opening request.  The response carries the slash commands, agents,
// models and the CLI's pid -- useful, but the point of sending it is that the
// control channel is established before the first turn.
Json MakeInitialize(const std::string& requestId);

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
