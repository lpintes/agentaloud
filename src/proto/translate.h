#ifndef PROTO_TRANSLATE_H
#define PROTO_TRANSLATE_H

// Claude's records, said in the port's words (agent/events.h).
//
// Everything here used to be in model/transcript.cpp, which read Claude's
// content blocks itself: tool_use and tool_result, the <tool_use_error>
// wrapper, the catalogue of which tool keeps its command in which field.
// That made the transcript a Claude transcript.  It is here now because it
// is knowledge about Claude's wire, and the next CLI has a translator of its
// own -- the transcript renders what any of them says.
//
// What does NOT move is what happens to the text afterwards.  Escapes, line
// breaks and control characters are dealt with in model/, once, for every
// CLI (invariants 4 and 8); so is the Slovak that a call is rendered in.  This
// hands over structure, and the strings in it are as they came off the wire.

#include <string>
#include <vector>

#include "agent/events.h"
#include "proto/jsonl.h"

namespace proto {

// One record of the stream, as the events it carries, in the order it
// carries them.  A record is one batch: the transcript appends a batch as one
// edit where it can, the same as when it read the record itself.
//
// So far only what the transcript reads is translated; the pane still reads
// the rest off the record (claude-gui-lkk.44.4).
std::vector<agent::Event> TranslateRecord(const Json& record);

// The records of a session file (proto::ReadSessionRecords), as one list.
// Differs from the stream in what a `user` record means: here it is also the
// prompt a human typed and the mark of an interruption, both of which the live
// path puts into the transcript itself (invariant 18).
std::vector<agent::Event> TranslateHistory(const std::vector<Json>& records);

// A tool call out of its name and arguments.  The one way a call is built:
// the transcript gets it from a tool_use block and the permission dialog from
// a can_use_tool request, and both render what this made -- so allowing a
// command and then reading it in the transcript is reading the same text
// (invariant 13).
agent::ToolCall ToolCallFromInput(const std::string& name,
                                  const std::string& id, const Json& input);

}  // namespace proto

#endif
