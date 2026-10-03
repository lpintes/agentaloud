#ifndef PROTO_CLAUDE_EVENTS_H
#define PROTO_CLAUDE_EVENTS_H

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

// What a `result` record says the session has spent so far.
//
// Read out of `modelUsage`, not out of the top-level `usage`: the numbers in
// modelUsage are CUMULATIVE over the session and the ones in usage belong to
// the last message alone.  Measured on tests/fixtures/basic.jsonl, whose four
// results carry outputTokens 348, 598, 863, 1198 in modelUsage while
// usage.output_tokens goes 348, 250, 265, 335.  A session total therefore
// means keeping the last record, not adding them up -- adding them up would
// count the whole session once per turn.
//
// The two costs are both kept because they answer different questions and
// they disagree.  `total_cost_usd` is what is being billed, and on a
// subscription it is 0; costUSD in modelUsage is the list price, which is the
// only number that says anything at all there.  Showing just one of them
// would either always read zero or claim money that is not being charged.
struct Usage {
  double billedUsd = 0;   // total_cost_usd -- 0 on a subscription
  double listUsd = 0;     // sum of modelUsage[*].costUSD
  long long inputTokens = 0;
  long long outputTokens = 0;
  long long cacheReadTokens = 0;
  long long cacheCreationTokens = 0;
  long long thinkingTokens = 0;
  // How many tokens the running model can hold at once.  A property of the
  // model and not of the session, but the CLI says it only here -- the
  // catalogue in the initialize handshake does not carry it, so it is not
  // known until a turn has ended.
  long long contextWindow = 0;
};

// `model` picks whose contextWindow is reported: modelUsage has an entry per
// model, and a session with a subagent in it has more than one.  An empty or
// unknown name takes the largest, which is the right answer whenever the
// session runs a single model and an honest ceiling when it does not.
//
// False when the record is not a result, or carries neither cost nor tokens.
bool ParseUsage(const Json& record, const std::string& model, Usage* out);

// How much context the model was sent for one request, out of an `assistant`
// record: the prompt plus everything read from and written to the cache.  The
// newest one is how full the window is now -- there is no record that says so
// directly, and the totals in `result` are no use for it, being sums over the
// whole session.
//
// False when the record is not an assistant message with usage on it.
bool ParseContextTokens(const Json& record, long long* out);

// The model that actually wrote an `assistant` record of the conversation
// itself -- the one to show as "the model", because system/init does not say
// it.  system/init carries the session's main-loop model, and with a
// mode-dependent alias that is not the one answering: under --model opusplan it
// reads claude-sonnet-5 in plan mode while every assistant record of the same
// turn is claude-opus-5 (measured 2026-09-11, claude-gui-lkk.40).
//
// False for a subagent's record (`parent_tool_use_id` set -- a subagent may run
// another model, and it is not what the session is running as) and for the
// CLI's own `<synthetic>` stand-ins, which no model wrote.  A record read off
// disk has no `parent_tool_use_id` at all and counts as the conversation's:
// ReadSessionRecords has already dropped the sidechains.
bool ParseAnsweringModel(const Json& record, std::string* out);

// For logs and for the "unknown record type" case, where the name is the whole
// of what we can say about it.
const char* KindName(EventKind kind);

}  // namespace proto

#endif
