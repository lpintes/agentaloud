#include "model/history.h"

#include <variant>

namespace model {

HistoryCounts RestoreHistory(const std::vector<agent::Event>& events,
                             Transcript* into) {
  HistoryCounts counts;
  // What was there before, so that the count is of what this made.
  const size_t blocksBefore = into->blocks().size();
  for (const agent::Event& event : events) {
    if (std::holds_alternative<agent::UserPrompt>(event)) ++counts.prompts;
    // One event at a time, not the lot as one batch.  A batch files a tool
    // result behind a call that is already placed, and in a history the call
    // and its result are in the same list: as one batch, the results of calls
    // made in parallel would all go to the end, in the order they finished.
    into->Append({event});
  }
  counts.blocks = into->blocks().size() - blocksBefore;
  return counts;
}

}  // namespace model
