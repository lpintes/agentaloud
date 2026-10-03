#ifndef MODEL_HISTORY_H
#define MODEL_HISTORY_H

// What a conversation already held, replayed into a transcript.
//
// A resumed session gets no history from the CLI: --resume carries it on, but
// the stream sends only what happens next, so the window would open empty --
// indistinguishable from a resume that failed, which is the very symptom
// --resume is asked for to cure.  So the earlier turns are read back off the
// session file the CLI keeps in ~/.claude/projects.
//
// Two layers, and the seam is deliberate.  The adapter knows what is in that
// file -- which record types the disk format and the stream have in common,
// which text a human typed and which the CLI wrote in their name -- and hands
// over the port's events (proto::TranslateHistory).  This turns them into
// blocks by exactly the same Transcript::Append the live path calls, so a
// restored turn and a turn that just happened are the same blocks.  Anything
// else would be a second rendering of the conversation, and the two would
// drift apart without anybody noticing -- the reader would have no way to
// compare them.
//
// What a history has and a live stream does not is the prompt and the mark of
// an interruption: live, the pane puts both in itself.  In a history they come
// as events of their own (agent::UserPrompt, agent::Interrupted) and make the
// same blocks AppendUserPrompt and AppendInterrupted make.

#include <cstddef>
#include <vector>

#include "agent/events.h"
#include "model/transcript.h"

namespace model {

// What the replay made.
struct HistoryCounts {
  size_t prompts = 0;   // prompts a human had typed
  size_t blocks = 0;    // blocks the transcript ended up with
};

// Appends the whole of `events` to the end of `into`.
//
// The edits are dropped on purpose.  This runs before the transcript is on
// screen and before any caret can be in it, so applying a few thousand of them
// to a RichEdit one at a time would only be a slow way of arriving at the same
// text; the caller applies one edit for the lot.  That is sound because the
// replay only ever adds after what is already there -- a tool result is filed
// behind its own call, and every call it could be filed behind is part of the
// same replay.
HistoryCounts RestoreHistory(const std::vector<agent::Event>& events,
                             Transcript* into);

}  // namespace model

#endif
