#ifndef PROTO_ASK_H
#define PROTO_ASK_H

// AskUserQuestion: the one tool whose permission request is not a permission
// request at all.
//
// The shape below was measured, not read (tools/probe_ask.py, 2026-09-05), and
// the measurement corrected what the design assumed.  A question with options
// does NOT arrive as a control_request of subtype `request_user_dialog`.  It
// arrives as an ordinary `can_use_tool` for the tool named AskUserQuestion,
// carrying `requires_user_interaction: true`, and the answer travels back in
// the field that exists for editing a tool's arguments:
//
//   allow + updatedInput = the tool's own input with `answers` filled in,
//   an object keyed by the QUESTION TEXT (not by index, not by header).
//
// Measured end to end: answering {"Čo piješ radšej?": "Čaj"} produced the tool
// result `Your questions have been answered: "Čo piješ radšej?"="Čaj".` and the
// model went on with it.  Denying instead is what the application did until
// now, and it is what the user saw: the question was shown as a permission
// prompt with Yes and No, which cannot answer a question with three options.
//
// `request_user_dialog` is a real subtype with real kinds -- permission_bash,
// refusal_fallback_prompt, permission_ask_user_question and some thirty more,
// found in the binary -- but the CLI FAILS CLOSED on it: a kind is only ever
// sent to a client that listed it in `initialize.supportedDialogKinds`, and we
// list none.  So that branch is not merely unhandled here, it is unreachable,
// and answering it with an error (which session.cpp does) is doubly harmless:
// the CLI discards error-shaped answers to a dialog and parks it instead.
// Declaring a kind is a separate step, and a bigger one -- see claude-gui-lkk.

#include <string>
#include <vector>

#include "proto/jsonl.h"

namespace proto {

// The tool whose input this file understands.  Branching on the name and not
// on `requires_user_interaction` is deliberate: the flag says a human is
// wanted, the name says what the payload looks like, and it is the payload
// that is being parsed here.
extern const char kAskUserQuestionTool[];

struct AskOption {
  std::string label;        // what the answer must say back, verbatim
  std::string description;  // a sentence about the trade-off; may be empty
};

struct AskQuestion {
  std::string question;  // also the key the answer is filed under
  std::string header;    // a two-word chip, e.g. "Auth method"; may be empty
  bool multiSelect = false;
  std::vector<AskOption> options;
};

// False when the input is not an AskUserQuestion input we can put to a person:
// no questions, or a question with no options.  `out` is then untouched, and
// the caller falls back to whatever it does for tools in general -- better an
// unhelpful prompt than a dialog with nothing in it.
bool ParseAskUserQuestion(const Json& input, std::vector<AskQuestion>* out);

// The tool's own input with `answers` added, ready for updatedInput.
//
// `chosen` runs parallel to `questions`; an empty entry leaves that question
// unanswered, which the CLI tolerates -- it tells the model to ask again.
// A single-select question is answered with a string and a multi-select one
// with an array, because that is what the CLI's own validator accepts without
// rephrasing the tool result.
//
// A choice that is not one of the offered labels is allowed on purpose: the
// dialog offers a box for an answer of one's own, and the CLI passes anything
// through to the model, only wording the result differently.
Json MakeAskAnswers(const Json& input, const std::vector<AskQuestion>& questions,
                    const std::vector<std::vector<std::string>>& chosen);

}  // namespace proto

#endif
