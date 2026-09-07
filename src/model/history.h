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
// Two layers, and the seam is deliberate.  proto/sessions knows what is in
// that file -- which record types the disk format and the stream have in
// common, which text a human typed and which the CLI wrote in their name.
// This knows how a shared record becomes a block, and it does it by calling
// exactly the same Transcript methods the live path calls, so a restored turn
// and a turn that just happened are the same blocks.  Anything else would be a
// second rendering of the conversation, and the two would drift apart without
// anybody noticing -- the reader would have no way to compare them.
//
// One thing is NOT the same, and it is the reason this is not just a loop over
// Transcript::Append: a prompt off the disk is text in a `user` record, and
// Append deliberately takes nothing but tool results out of those.  Live, the
// prompt is put in by AppendUserPrompt at the moment it is sent.  So here it
// is put in by AppendUserPrompt too, from the record instead of from the edit
// box.

#include <cstddef>
#include <vector>

#include "model/transcript.h"
#include "proto/jsonl.h"

namespace model {

// What the replay made, for the note that says what was restored.
struct HistoryCounts {
  size_t records = 0;   // records the transcript was offered
  size_t prompts = 0;   // of those, ones a human had typed
  size_t blocks = 0;    // blocks the transcript ended up with
};

// Appends the whole of `records` to the end of `into`.
//
// The edits are dropped on purpose.  This runs before the transcript is on
// screen and before any caret can be in it, so applying a few thousand of them
// to a RichEdit one at a time would only be a slow way of arriving at the same
// text; the caller applies one edit for the lot.  That is sound because the
// replay only ever adds after what is already there -- a tool result is filed
// behind its own call, and every call it could be filed behind is part of the
// same replay.
HistoryCounts RestoreHistory(const std::vector<proto::Json>& records,
                             Transcript* into);

}  // namespace model

#endif
