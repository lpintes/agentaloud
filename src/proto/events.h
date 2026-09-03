#ifndef PROTO_EVENTS_H
#define PROTO_EVENTS_H

// Envelope-level classification of what comes off the stream.
//
// Deliberately shallow: this says what kind of record arrived and keeps the
// JSON, it does not take apart content blocks.  That is model/transcript's
// work, and putting it here would mean the protocol layer had an opinion about
// what a transcript looks like.
//
// Unknown is a normal outcome, not a failure.  Claude Code gains record types
// between releases and an application that stops at the first one it does not
// recognise would break on somebody else's schedule.

#include <string>

#include "proto/jsonl.h"

namespace proto {

enum class EventKind {
  Unknown,
  SystemInit,             // tools, model, cwd, session_id, slash_commands
  SystemPermissionDenied, // a tool that a rule would not let through
  SystemHook,             // hook_started / hook_response
  // Ticks while the model is thinking, one every few tokens.  Carries no text
  // worth showing -- only a running token count -- but it is the earliest
  // sign that a turn is doing something, and the only one during a long
  // think.  That is what it is classified for.
  SystemThinkingTokens,
  SystemOther,
  Assistant,              // one message, content blocks inside
  User,                   // our prompt replayed, or a tool_result
  ControlRequest,         // the CLI asking us something -- see control.h
  ControlResponse,        // its answer to something we asked
  RateLimit,
  Result,                 // end of turn: cost, tokens, stop_reason
};

struct Event {
  EventKind kind = EventKind::Unknown;
  std::string subtype;    // empty unless the record carries one
  std::string sessionId;  // empty on records that do not carry one
  Json raw;
};

Event Classify(Json record);

// What the CLI says about how close we are to a rate limit.  Field names and
// the set of allowed values are the wire's; they were read out of the CLI
// binary, because a rate limit is not something one can produce on demand to
// watch it go past.  Everything but `status` is optional there, so everything
// but `status` is optional here.
struct RateLimit {
  std::string status;         // allowed | allowed_warning | rejected
  std::string limitType;      // five_hour | seven_day | seven_day_opus | ...
  double utilization = -1;    // 0..1, negative when the record did not say
  long long resetsAt = 0;     // unix seconds, 0 when the record did not say
  // The same three numbers per window, when the record carries them.  Shown in
  // preference to the single utilization above: "five hours 42 %, seven days
  // 13 %" is what a person actually wants to know.
  double fiveHourUtilization = -1;
  double sevenDayUtilization = -1;
};

// False when the record is not a rate_limit_event or carries no usable info.
bool ParseRateLimit(const Json& record, RateLimit* out);

// For logs and for the "unknown record type" case, where the name is the whole
// of what we can say about it.
const char* KindName(EventKind kind);

}  // namespace proto

#endif
