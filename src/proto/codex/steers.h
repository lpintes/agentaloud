#ifndef PROTO_CODEX_STEERS_H
#define PROTO_CODEX_STEERS_H

// Prompts sent into a running turn (turn/steer) and not taken up yet.
//
// Codex takes a steer into the turn at its next request to the model and only
// then echoes it, as a userMessage item of that turn.  An interrupt before
// that drops it without a word: no echo, no turn for it, and the next turn
// does not carry it either (tools/probe_codex_midturn.notes.md, Codex 0.160).
// A prompt sent is never dropped (agent::Backend::SendPrompt), so what is
// still here when the turn completes is sent again, as a turn of its own.
//
// Matched by text: the echo has no id of ours (clientId is null), and the
// text comes back as it was sent.

#include <string>
#include <vector>

#include "proto/jsonl.h"

namespace proto::codex {

class Steers {
 public:
  void Sent(const std::string& utf8Text);
  // Every notification of our own thread; only a userMessage item counts.
  void Observe(const Json& message);
  // Removes one prompt of that text; false when none was waiting -- it was
  // echoed, or already taken by TakeDropped.
  bool Forget(const std::string& utf8Text);
  // At turn/completed: what the turn never took up, in the order sent.
  std::vector<std::string> TakeDropped();
  bool empty() const { return waiting_.empty(); }

 private:
  std::vector<std::string> waiting_;
};

}  // namespace proto::codex

#endif
