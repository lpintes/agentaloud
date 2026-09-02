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

// For logs and for the "unknown record type" case, where the name is the whole
// of what we can say about it.
const char* KindName(EventKind kind);

}  // namespace proto

#endif
