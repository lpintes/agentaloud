#ifndef AGENT_EVENTS_H
#define AGENT_EVENTS_H

// What an agent CLI says during a session, in words that are not any one
// CLI's.  model/ and ui/ see these and nothing else; turning a CLI's own
// records into them is the adapter's job (proto/claude, proto/codex).
//
// The test of whether something belongs here is whether the transcript or the
// pane would branch on it.  A field that only one CLI has and nothing above
// the adapter reads stays in that adapter -- the point of the port is that the
// next CLI does not have to fake it.
//
// Everything is UTF-8 std::string, as it came off the wire.  Widening,
// newline normalising and escaping control characters stay where they are,
// in model/ (invariants 4 and 8): one place that every piece of text passes
// through, whichever CLI sent it.
//
// The vocabulary follows ACP (Agent Client Protocol) where ACP has a word for
// the thing -- it is the closest there is to an agreed common denominator of
// coding agents -- but this is not ACP and is not spoken to anyone.  See
// claude-gui-lkk.44 for why.

#include <optional>
#include <string>
#include <variant>
#include <vector>

namespace agent {

// What a tool call is FOR, which is what the transcript needs to know about
// it: whether its first argument is a path to shorten, whether a successful
// result is worth a line or just a word ("zapísané").  The tool's own name is
// kept as well, for showing; nothing may branch on that.
enum class ToolKind {
  Shell,       // Claude Bash/PowerShell, Codex commandExecution
  ReadFile,
  EditFile,    // Claude Edit/NotebookEdit, Codex fileChange of an existing file
  CreateFile,  // Claude Write, Codex fileChange of kind add
  Search,      // Glob, Grep
  Fetch,       // WebFetch
  Question,    // a multiple-choice question put to the reader
  Other,
};

// One argument, for the calls that have no shape of their own: the field dump
// that is exactly right for a command or a grep pattern.
struct ToolField {
  std::string name;
  std::string value;  // a non-string argument arrives as its JSON text
};

// An edit given as the two texts, the way Claude's Edit tool carries it.
// Shown whole and one after the other, never as a diff: see RenderToolCall.
struct TextReplacement {
  std::string before;
  std::string after;
  bool everywhere = false;
};

struct QuestionOption {
  std::string label;        // what an answer says back, verbatim
  std::string description;  // may be empty
};

struct Question {
  // How the answer is filed.  Claude keys answers by the question's text and
  // Codex by an id it makes up; the adapter fills this in either way and the
  // dialog never looks at it.
  std::string id;
  std::string text;
  std::string header;  // a two-word chip; may be empty
  bool multiSelect = false;
  // Whether an answer outside the options is welcome.  Claude passes anything
  // through, so its adapter says true; Codex says it per question.
  bool allowsOther = true;
  std::vector<QuestionOption> options;
};

// A tool call, complete -- which matters most where a CLI does NOT send it
// complete.  Codex asks permission for a file change without the diff and
// sends the diff earlier, under the same item id; pairing the two is the
// adapter's business, and the permission dialog gets the whole call.
//
// The same struct is shown twice, in the permission dialog before the call
// runs and in the transcript afterwards, and both render it with the one
// model::RenderToolCall (invariant 13).
struct ToolCall {
  std::string id;    // ties the call to its result; unique within a session
  std::string name;  // as shown: "Bash", "Edit", "shell"
  ToolKind kind = ToolKind::Other;
  // The one thing that says what the call does -- a command, a path, a
  // pattern -- for the one-line summary.  Empty falls back to the fields.
  std::string primary;
  bool primaryIsPath = false;

  // The body.  Which of these is filled depends on `kind`, and anything a
  // kind does not use stays empty; `fields` is the fallback for all of them.
  std::vector<ToolField> fields;
  std::vector<TextReplacement> replacements;  // EditFile, as two texts
  std::string diff;                           // EditFile, as a unified diff
  // CreateFile.  Optional because creating an empty file is a call worth
  // telling apart from one whose content did not come.
  std::optional<std::string> newContent;
  std::vector<Question> questions;            // Question
};

struct ToolResult {
  std::string callId;
  std::string text;  // images and other parts already turned into text
  bool isError = false;
};

// Token counts as the CLI reports them.  The cost is optional because not
// every CLI states one, and a zero would be read as "free".  Two costs because
// on a subscription what is billed is zero and only the list price says
// anything -- see proto::Usage for how that was found.
struct Usage {
  long long inputTokens = 0;
  long long outputTokens = 0;
  long long cacheReadTokens = 0;
  long long cacheWriteTokens = 0;
  long long reasoningTokens = 0;
  long long contextWindow = 0;  // 0 when not known yet
  std::optional<double> billedUsd;
  std::optional<double> listUsd;
};

enum class LimitState { Ok, Warning, Exhausted };

// One rate-limit window.  Named by its length rather than by the CLI's word
// for it ("five_hour", "primary"), because the length is what is said aloud.
struct LimitWindow {
  int minutes = 0;     // 300, 10080; 0 when the CLI did not say
  double used = -1;    // 0..1, negative when not said
  long long resetsAt = 0;  // unix seconds, 0 when not said
};

// ---- The events -------------------------------------------------------------

struct AssistantText { std::string text; };

// Text of the model's reasoning, when there is any.  Claude no longer sends
// it and Codex sends only a heading (claude-gui-lkk.44.1); an adapter that
// has none sends ThinkingTick instead and never an empty Thinking.
struct Thinking { std::string text; };

// A sign that a turn is busy thinking, with nothing to show.  What "premýšľam"
// is said on (invariant 6).
struct ThinkingTick {};

struct ToolCallStarted { ToolCall call; };
struct ToolCallFinished { ToolResult result; };

// A call that a rule or the reader turned down, when the CLI reports it as a
// record of its own rather than as an error result.
struct ToolDenied {
  std::string toolName;
  std::string message;
};

// A question the agent put without waiting for the answer: its turn goes on,
// or ends, and the answer is the reader's next prompt.  Codex's
// request_user_input_async does this, and the model picks it over the
// blocking request_user_input as it sees fit (measured, claude-gui-lkk.44.5).
// The call is in the transcript already, as a ToolCallStarted beside this;
// what this adds is that the pane offers the same dialog once the turn is
// over, and sends what was chosen as a prompt.
//
// The pane files the outcome behind the call, as the blocking question gets
// one from its adapter: what was chosen, or "bez odpovede".  Without it a
// dismissed question looked exactly like one still waiting (reported from
// use, claude-gui-lkk.44.11).
struct QuestionByPrompt {
  std::string callId;
  std::vector<Question> questions;
};

// Only in a replayed history.  A live prompt goes into the transcript the
// moment it is sent, and a live interruption is marked by the pane itself.
struct UserPrompt { std::string text; };
struct Interrupted {};

// The project directory, which tool paths are shortened against.
struct WorkingDirectory { std::string path; };

// The model that is answering NOW.  Which record says that is a question with
// a different answer per CLI -- for Claude it is message.model of an
// assistant record and not system/init (invariant 21), for Codex the thread
// settings and model/rerouted -- and the adapter answers it.  The pane
// shows whatever arrived last.
struct ModelChanged { std::string model; };

// Every change of mode, whoever made it: our own SetMode, or the CLI leaving
// plan mode by itself (invariant 20).  `id` is a Mode::id from Capabilities.
struct ModeChanged { std::string id; };

// How full the context window is after the newest request.
struct ContextUsed {
  long long tokens = 0;
  long long window = 0;  // 0 when not known
};

struct RateLimitChanged {
  LimitState state = LimitState::Ok;
  std::vector<LimitWindow> windows;
  // The two below are for a CLI that reports one limit at a time rather than
  // every window: Claude's rate_limit_event names the window it is about and
  // gives one reset time for the whole record.
  double used = -1;        // 0..1, when there are no windows to say it
  long long resetsAt = 0;  // when the limit this report is about resets
};

// The CLI has finished starting: commands(), account() and the mode are now
// what they are going to be.  With a SessionStart hook that is twenty seconds
// in (invariant 17), so "not ready" is an ordinary state that has to be said.
struct Ready {};

enum class TurnOutcome { Completed, Interrupted, Failed };

// The end of a turn, and the only thing that ends one.  An interruption ends
// with this too, never with the request to interrupt (invariant 1).
struct TurnEnded {
  TurnOutcome outcome = TurnOutcome::Completed;
};

// Session totals so far.  An event of its own and not part of TurnEnded,
// because when it comes differs: Claude says it in the turn's `result`, Codex
// after every request to the model.  The pane keeps the newest.
struct UsageChanged { Usage usage; };

// A record the adapter did not recognise.  Not an error; the soak counts them
// so a new record type is noticed by us and not by a user.
struct Unrecognised { std::string type; };

using Event = std::variant<AssistantText, Thinking, ThinkingTick,
                           ToolCallStarted, ToolCallFinished, ToolDenied,
                           QuestionByPrompt, UserPrompt, Interrupted, WorkingDirectory,
                           ModelChanged, ModeChanged, ContextUsed,
                           UsageChanged, RateLimitChanged, Ready, TurnEnded,
                           Unrecognised>;

}  // namespace agent

#endif
