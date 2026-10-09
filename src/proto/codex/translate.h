#ifndef PROTO_CODEX_TRANSLATE_H
#define PROTO_CODEX_TRANSLATE_H

// What `codex app-server` says, in the port's words (agent/events.h).
//
// Everything here is knowledge about Codex's wire, measured with
// tools/probe_codex_*.py on codex-cli 0.160.0 (claude-gui-lkk.44.1) and pinned
// by the fixtures tests/fixtures/codex-*.jsonl.  It owns no process: the
// connection is proto/codex/connection, and this only turns messages into
// events and answers back into messages, so that all of it can be tested on
// the fixtures.
//
// The wire is JSON-RPC without the "jsonrpc" field, one message per line.  A
// message with a method and no id is a notification; with both, a request
// the SERVER puts to us (an approval, a question); with an id and no method,
// the answer to one of ours.  The server numbers its own requests from 0, so
// an id alone says nothing about whose it is -- the method does.
//
// Separate namespace, because Claude's adapter lives in proto:: itself and
// the two have a Translator each.

#include <map>
#include <string>
#include <utility>
#include <vector>

#include "agent/backend.h"
#include "agent/events.h"
#include "proto/jsonl.h"

namespace proto::codex {

enum class MessageKind {
  Notification,   // method, no id
  ServerRequest,  // method and id: the server waits for our answer
  Response,       // id, no method: the answer to a request of ours
  Malformed,
};

MessageKind KindOf(const Json& message);

// ---- Modes ----------------------------------------------------------------
//
// Codex has no single mode.  It has an approval policy (when to ask), a
// sandbox (what a command may touch at all), a reviewer (who is asked) and a
// collaboration mode (plan or not), and approving a command does NOT widen
// the sandbox -- read-only plus "accept" is still "Access denied" (measured).
// The terminal Codex offers presets over the first three (/approvals) and
// toggles plan on its own; the adapter offers the same presets as
// agent::Modes, and plan as one more.
//
// "auto" is the preset in which a reviewer model answers what would have
// been put to the reader (approvalsReviewer "auto_review", the terminal's
// "Approve for me"), the same thing the word means for Claude.  Until
// claude-gui-lkk.44.15 the word named the ordinary preset, which is "default"
// now: a settings line written for the old meaning gets the reviewer.

// The ids, for the code that sets them.
extern const char kModeReadOnly[];
extern const char kModeDefault[];
extern const char kModeAuto[];
extern const char kModePlan[];
extern const char kModeFullAccess[];
// Any combination that is none of the presets -- a config.toml of the
// reader's own, say.  Off the cycle; Shift+Tab out of it goes to a preset.
extern const char kModeCustom[];

agent::Capabilities CodexCapabilities();

// Which preset the four settings are.  `approvalPolicy` is a string or the
// {granular: ...} object, `sandboxPolicy` the {type: ...} object as
// thread/settings/updated and the answer to thread/start carry them,
// `reviewer` the approvalsReviewer ("user", "auto_review"), empty when not
// stated, which is Codex's default "user", `collaboration` the
// collaborationMode's "mode" ("plan", "default"), empty when not stated.
std::string ModeFromSettings(const Json& approvalPolicy,
                             const Json& sandboxPolicy,
                             const std::string& reviewer,
                             const std::string& collaboration);

// The mode out of a thread/settings/updated notification's threadSettings.
std::string ModeFromThreadSettings(const Json& threadSettings);

// The fields that put a thread into `mode`, for thread/start, thread/resume
// and thread/settings/update alike -- except that the first two spell the
// sandbox as a word ("read-only") and the third as an object, so both are
// here.  False for a mode that is not a preset (custom), which cannot be set.
struct ModeSettings {
  std::string approvalPolicy;  // "on-request", "never"
  std::string sandbox;         // "read-only", "workspace-write", ...
  Json sandboxPolicy;          // {type: readOnly}, ...
  std::string reviewer;        // "user" or "auto_review"
  std::string collaboration;   // "plan" or "default"
  // Plan mode changes only the collaboration mode and leaves the other two
  // as they were: it is a way of working, not a permission.
  bool permissions = true;
};
bool SettingsForMode(const std::string& mode, ModeSettings* out);

// Which mode the session is in, as far as anyone can say.  The same problem
// as Claude's PermissionModeTracker, in a smaller shape: Shift+Tab moves the
// mode ahead at once, so that a second fast press cycles from the new value,
// and the server's own word -- thread/settings/updated, which comes after
// every change and at the start of every turn -- is taken over only when no
// change of ours is still unanswered; until then it may be older than the
// last press.  A refusal falls back to the server's last word, not to the
// mode before the press.
//
// The answer to thread/settings/update comes BEFORE the notification that
// states the new settings (measured, noturn.log), so a change is settled by
// the notification that follows its answer.
class ModeTracker {
 public:
  void Seed(const std::string& mode) { current_ = mode; }
  void Requested(const std::string& mode) {
    ++pending_;
    current_ = mode;
  }
  void Answered(bool accepted) {
    if (pending_ > 0) --pending_;
    if (!accepted && pending_ == 0 && !reported_.empty()) current_ = reported_;
  }
  void Reported(const std::string& mode) {
    reported_ = mode;
    if (pending_ == 0) current_ = mode;
  }
  const std::string& current() const { return current_; }

 private:
  std::string current_;
  std::string reported_;
  int pending_ = 0;
};

// ---- Items ----------------------------------------------------------------

// Is this item a tool call -- something with a call and a result?  Messages,
// reasoning and the user's own prompt are not.
bool IsToolItem(const Json& item);

// The call an item makes.  The same function builds it for the transcript
// (item/started) and for the permission dialog (the approval request names
// only the itemId, and the adapter looks up what item/started said), so the
// two show the same text (invariant 13).
agent::ToolCall ToolCallFromItem(const Json& item);

// The result of a finished tool item.
agent::ToolResult ToolResultFromItem(const Json& item);

// ---- Questions ------------------------------------------------------------
//
// item/tool/requestUserInput has no item in the stream at all: no
// item/started, no item/completed.  The adapter makes the call and its result
// itself, out of the request and the answer, so the transcript says that a
// question was asked and what was answered.

std::vector<agent::Question> ReadQuestions(const Json& params);
agent::ToolCall QuestionCall(const Json& params);
// The answer as the server wants it: keyed by the question's id, the value
// always a list (measured).  A declined question is answered with no answers.
Json MakeQuestionAnswer(const std::vector<agent::Question>& questions,
                        const agent::QuestionAnswer& answer);
agent::ToolResult QuestionResult(const std::string& callId,
                                 const std::vector<agent::Question>& questions,
                                 const agent::QuestionAnswer& answer);

// ---- MCP elicitation ------------------------------------------------------
//
// mcpServer/elicitation/request is how an MCP server asks the reader
// something, and two different askers use it (measured with a stand-in for
// computer use, tools/probe_computer_use.py, claude-gui-lkk.44.12): Codex
// itself, before it calls a server's tool (`_meta.codex_approval_kind`
// "mcp_tool_call", the arguments in `_meta.tool_params`), and the server,
// whose own question Codex passes on unchanged (`_meta` null) -- computer use
// asking "Allow Codex to use Notepad?".  Both are a form with no fields, that
// is a yes or a no.  Refused, the first fails the call and the second is what
// the plugin reports to the model as "not approved", with no dialog anywhere.

// The request as a permission, when a yes or a no answers it.  False for a
// form with fields and for a URL to open, which this dialog cannot do.
bool ElicitationPermission(const Json& params, agent::PermissionRequest* out);
Json MakeElicitationAnswer(agent::Verdict verdict);

// ---- The stream -----------------------------------------------------------

// The live stream, message by message.  A class, because a call is complete
// only in item/started -- the approval that follows names just the item --
// and because the model is reported when it changes, not every time a
// message repeats it.
//
// Not thread-safe; one translator belongs to one reader.
class Translator {
 public:
  void SeedModel(const std::string& model) { model_ = model; }
  // The conversation's own thread.  A subagent is a thread of its own, with
  // turns of its own on the same wire, and those must not start or end ours
  // (tools/probe_codex_subagents.notes.md).  Until it is known, every
  // message counts as the conversation's.
  void SetThread(const std::string& threadId) { thread_ = threadId; }

  // Server requests and responses translate to nothing: answering them is
  // the adapter's job.
  std::vector<agent::Event> Translate(const Json& message);

  // The call an item/started announced and that has not finished yet.
  bool FindCall(const std::string& itemId, agent::ToolCall* out) const;

  const std::string& model() const { return model_; }

  // Who a message with this threadId is from: empty for the conversation,
  // the subagent's name otherwise -- the `by` of its blocks and of the
  // requests it sends, which carry a threadId too (claude-gui-b8n.10).
  std::string Author(const Json& params) const;

  // Subagents started since the last call, by thread id.  Their nickname
  // ("Peirce") is in no notification, only in thread/read, which the adapter
  // asks for each of these (claude-gui-b8n.9).
  std::vector<std::string> TakeNewSubagents() {
    return std::exchange(unnamed_, {});
  }
  // The thread out of a thread/read answer.  Renames the subagent after its
  // nickname; until then, and if it has none, the name is its agentPath.
  std::vector<agent::Event> Nickname(const Json& thread);

 private:
  void ReportModel(const std::string& model, std::vector<agent::Event>* out);
  bool Foreign(const Json& params) const;
  // The subagent and command items that change the list of tasks.
  void TrackTask(const Json& item, bool completed,
                 std::vector<agent::Event>* out);
  void ReportTasks(std::vector<agent::Event>* out) const;

  std::map<std::string, agent::ToolCall> running_;
  std::string thread_;
  // Codex sends no list of what runs in the background, so it is kept here:
  // subagents from subAgentActivity, by thread id, in the order they started;
  // commands that a turn of ours left running, by item id.  A command of the
  // turn in flight is only a candidate -- every command has a processId, and
  // only the end of the turn tells one left running from one waited for.
  std::vector<agent::BackgroundTask> agents_;
  std::vector<agent::BackgroundTask> shells_;
  std::map<std::string, std::string> commands_;
  // A subagent's name by its thread, for the `by` of what it says.  Apart
  // from agents_ because that one forgets a subagent at subAgentActivity
  // completed, and nothing promises its last items came before that.
  std::map<std::string, std::string> subagentNames_;
  std::vector<std::string> unnamed_;
  // Why the reviewer said no, by the item it judged, until that item ends.
  std::map<std::string, std::string> deniedByReviewer_;
  std::string model_;
};

// The conversation so far, out of the answer to thread/resume.  Unlike
// Claude's, Codex's history comes back over the wire, in the same shape as
// the live items, so nothing is read off disk (claude-gui-lkk.44.1).
std::vector<agent::Event> TranslateHistory(const Json& resumeResult);

}  // namespace proto::codex

#endif
