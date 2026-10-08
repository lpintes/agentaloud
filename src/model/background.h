#ifndef MODEL_BACKGROUND_H
#define MODEL_BACKGROUND_H

// What runs in the background, as the status bar says it.
//
// The turn field and not a field of its own: it is the one that answers "is
// anything going on", and subagents running while it said nothing was the
// complaint this answers (claude-gui-b8n).  No speech -- the bar is asked
// with NVDA+End; an end worth hearing is the transcript's to say.

#include <string>
#include <vector>

#include "agent/events.h"

namespace model {

// "na pozadí: 2 agenti, 1 príkaz", empty when nothing runs.
std::wstring BackgroundText(const std::vector<agent::BackgroundTask>& tasks);

// The turn's own word followed by the above, either of them possibly empty.
std::wstring TurnStatus(const std::wstring& turn,
                        const std::vector<agent::BackgroundTask>& tasks);

}  // namespace model

#endif
