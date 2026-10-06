#ifndef PROTO_CLAUDE_TRANSLATE_H
#define PROTO_CLAUDE_TRANSLATE_H

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

#include "agent/backend.h"
#include "agent/events.h"
#include "proto/jsonl.h"

namespace proto {

// The stream of one session, record by record.  A class and not a function
// because one question has an answer that depends on what came before: which
// model is answering (invariant 21).  system/init names the session's
// main-loop model at the start of every turn, and under a mode-dependent
// alias that is not the one writing -- with opusplan in plan mode it says
// claude-sonnet-5 while every assistant record is claude-opus-5.  So once an
// assistant record has named the model, system/init no longer may.  The same
// model is what a result's usage is read for: modelUsage has an entry per
// model, and the context window wanted is the running one's.
//
// Not thread-safe; one translator belongs to one reader.
class Translator {
 public:
  // The model known before the stream says one: what --model asked for, or
  // the last one that answered in a resumed history.  It does NOT count as an
  // answer -- a resume launched with another --model is corrected by the
  // first system/init, which is the fresher word.
  void SeedModel(const std::string& model) { model_ = model; }

  // One record, as the events it carries, in the order it carries them.  A
  // record is one batch: the transcript appends a batch as one edit where it
  // can, the same as when it read the record itself.
  std::vector<agent::Event> Translate(const Json& record);

 private:
  std::string model_;
  bool answered_ = false;
};

// One record through a translator of its own -- for a record whose meaning
// does not depend on the ones before it, which is every record the transcript
// reads.  The tests feed the transcript this way.
std::vector<agent::Event> TranslateRecord(const Json& record);

// The records of a session file (proto::ReadSessionRecords), as one list.
// Differs from the stream in what a `user` record means: here it is also the
// prompt a human typed and the mark of an interruption, both of which the live
// path puts into the transcript itself (invariant 18).  The models that
// answered come along as ModelChanged, so the last of them can seed the live
// translator.
std::vector<agent::Event> TranslateHistory(const std::vector<Json>& records);

// What the adapter tells the model when the reader turns a tool down or
// closes a question unanswered.  English whatever the reader's language: an
// instruction to the model, not text for the reader (claude-gui-lkk.52).
//
// The CLI hands both back in the stream as the text of the tool's result --
// a plain string, byte for byte the message, is_error true, and no
// system/permission_denied record at all (measured on 2.1.288, 2026-10-06).
// The transcript would show the reader the model's instruction, so
// MakeToolResult recognises its own words and puts the reader's in their
// place: "zamietnuté" and "bez odpovede", Codex's words for the same two
// events.  Any other text -- a rule's, a hook's -- is shown as it came.
extern const char kDeniedInstruction[];
extern const char kQuestionDeclinedInstruction[];

// What Claude Code can do, in the port's words -- above all its permission
// modes, their names in the reader's language and the order Shift+Tab steps
// through them.  Here
// and not in ClaudeBackend so that a test can hold the cycle against the one
// it replaced without starting a process.
agent::Capabilities ClaudeCapabilities();

// A tool call out of its name and arguments.  The one way a call is built:
// the transcript gets it from a tool_use block and the permission dialog from
// a can_use_tool request, and both render what this made -- so allowing a
// command and then reading it in the transcript is reading the same text
// (invariant 13).
agent::ToolCall ToolCallFromInput(const std::string& name,
                                  const std::string& id, const Json& input);

}  // namespace proto

#endif
