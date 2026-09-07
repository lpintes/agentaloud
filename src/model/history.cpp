#include "model/history.h"

#include <string>

#include "model/utf.h"
#include "proto/events.h"
#include "proto/sessions.h"

namespace model {

HistoryCounts RestoreHistory(const std::vector<proto::Json>& records,
                             Transcript* into) {
  HistoryCounts counts;
  // What was there before, so that the count is of what this made.  The
  // transcript is not necessarily empty: the note that says which conversation
  // this is goes in first, so that a reader whose caret starts at offset zero
  // meets it before the history rather than after it.
  const size_t blocksBefore = into->blocks().size();
  for (const proto::Json& record : records) {
    ++counts.records;
    // A `user` record is up to three things at once, and the order below is
    // the order they were said in.  It is not an if/else chain by accident:
    // one record measured in this corpus carries six content parts, and text
    // beside a tool result is a shape the format allows.
    if (proto::IsInterruptMark(record)) {
      // The same one-line mark the live path writes when the reader presses
      // Esc -- written by us there, read back here, and the block kind is the
      // same either way.  Without it an answer that was cut short reads, later
      // on, exactly like one that ended by itself: it just stops.
      into->AppendInterrupted();
    } else if (const std::string prompt = proto::HumanPromptText(record);
               !prompt.empty()) {
      ++counts.prompts;
      into->AppendUserPrompt(Utf16FromUtf8(prompt));
    }
    // Assistant text, thinking and tool calls, and the tool results out of
    // user records -- everything the live path makes of the same record.  A
    // record that was a prompt has no tool_result in it, so this costs a walk
    // over an empty list and no second block.
    into->Append(proto::Classify(record));
  }
  counts.blocks = into->blocks().size() - blocksBefore;
  return counts;
}

}  // namespace model
