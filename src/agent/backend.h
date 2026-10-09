#ifndef AGENT_BACKEND_H
#define AGENT_BACKEND_H

// The port: one conversation with an agent CLI, whichever one it is.
//
// The pattern is ports and adapters.  The application says what it needs from
// a coding agent -- send a prompt, stop a turn, switch the mode, be asked
// about a tool -- and each CLI gets an adapter that does that with its own
// wire: proto/claude speaks `claude -p` stream-json, proto/codex speaks
// `codex app-server`.  ui/ holds a Backend and never learns which.
//
// What the CLIs do NOT share is said by Capabilities, not by the name of the
// CLI.  The pane asks "are there modes", never "is this Claude", so that the
// next CLI is a new adapter and not a walk through the whole UI.  A key whose
// capability is missing still answers, out loud, that it has nothing to do
// (invariant 6), and F1 lists only the keys that do something (invariant 19).
//
// Which adapter to make is decided in main.cpp, the one place that knows them
// all.  A factory in agent/ would have the port depend on its adapters.

#include <functional>
#include <string>
#include <vector>

#include "agent/events.h"

namespace agent {

// A permission mode, as Shift+Tab cycles through them.  Claude has one axis
// of these (default, acceptEdits, plan, auto); Codex has an approval policy, a
// sandbox and a plan mode, and its adapter offers named presets over the three
// -- the same thing its own terminal does.  Either way the pane sees a list.
//
// The label and the gloss are written by the adapter, in the reader's
// language (i18n), because only the adapter knows what the mode means; the
// pane says them and does not translate.
struct Mode {
  std::string id;     // what SetMode takes and ModeChanged reports
  std::string label;  // "plánovanie", said and shown in the status bar
  std::string gloss;  // one sentence: what the mode lets the agent do
  // False for a mode a session can be in but Shift+Tab does not move to --
  // Claude's dontAsk and bypassPermissions.  Such a mode still needs a label,
  // because a session can be started in it.
  bool inCycle = true;
  // The state a reader assumes.  Left out of the status bar: naming it every
  // time would crowd the field that has to be read in one breath.
  bool ordinary = false;
};

struct Capabilities {
  // The agent's name as a speaker: "claude: " in front of every answer,
  // in the transcript and in speech alike (invariant 6).  Lower case.
  std::string agentName;
  // In Shift+Tab order.  Empty means the mode cannot be switched at runtime.
  std::vector<Mode> modes;
  // Multiple-choice questions put to the reader (invariant 12).
  bool questions = false;
  // A list of slash commands for F4.  Without it F4 says there is none.
  bool slashCommands = false;
  // A conversation can be carried on after a restart: --resume and -c.
  bool resume = false;
  // A permission request can be answered "yes, and stop asking about this".
  bool allowForSession = false;
  // A task that runs without the turn (BackgroundTask) can be stopped, one
  // or all of them: StopTask and StopAllTasks.
  bool stopTask = false;
  // What runs in the foreground can be sent to the background while the turn
  // goes on: Background, Ctrl+B.
  bool backgroundNow = false;
  // Model names to offer before anything has started -- suggestions, not the
  // list of what the CLI takes: a full model id is always accepted too, and
  // an alias the CLI adds tomorrow must not be refused today.  Empty when the
  // adapter knows none without asking a running process.
  std::vector<std::string> models;
};

// The next mode in the cycle, or empty when `current` is empty -- the mode is
// not known yet, and that is the one case where the key must say so instead
// of guessing (invariant 20).  Any other mode outside the cycle, known or not,
// restarts at the second mode of the cycle, so the key always moves.  Empty
// too when the agent has no modes, or when every other mode is in `refused`.
// A refused mode is stepped over as if it were not in the cycle.
std::string NextMode(const Capabilities& capabilities,
                     const std::string& current,
                     const std::vector<std::string>& refused = {});

// The mode's entry, or nullptr.
const Mode* FindMode(const Capabilities& capabilities, const std::string& id);

struct SlashCommand {
  std::string name;
  std::string description;
  std::string argumentHint;
  std::vector<std::string> aliases;
};

struct Account {
  std::string email;
  std::string plan;  // "Claude Pro", "free"
  // A sentence when the cost numbers mean something unusual -- billed through
  // a cloud provider, say.  Empty in the ordinary case.
  std::string billingNote;
};

// ---- Requests that wait for a person ----------------------------------------
//
// Both are CALLED ON THE READER THREAD and block it until answered (invariant
// 2).  The adapter turns the answer into whatever its wire wants -- Claude's
// updatedInput keyed by question text, Codex's answers keyed by id.

enum class Verdict {
  Allow,
  AllowForSession,  // only when Capabilities::allowForSession
  Deny,             // the turn goes on without the tool
  Abort,            // the turn stops; offered only by a CLI that can do it
};

struct PermissionRequest {
  ToolCall call;  // complete, even where the wire sends it in two halves
  // What the dialog's caption names; the call's own name when empty.
  std::string title;
  // The model's own sentence about what the call is for.  May be empty.
  std::string description;
  // Why this was asked, as a sentence for the reader ("pravidlo v nastaveniach
  // hovorí pýtať sa").  Empty when the CLI gives no reason.
  std::string reason;
  std::vector<Verdict> offered;
};

struct PermissionAnswer {
  Verdict verdict = Verdict::Deny;
  // Handed to the model when the tool is denied, so it is written for the
  // model: "the user declined; ask before trying this again".  Empty leaves
  // the words to the adapter, which knows its CLI and writes them in English.
  std::string message;
};

struct QuestionRequest {
  std::vector<Question> questions;
};

// What the model is told when the reader declines is the adapter's to write:
// it is a sentence for the model, in the shape that CLI passes on.
struct QuestionAnswer {
  bool declined = false;
  // Parallel to QuestionRequest::questions.  An empty entry leaves that
  // question unanswered.
  std::vector<std::vector<std::string>> chosen;
};

// ---- Starting ---------------------------------------------------------------

struct StartOptions {
  std::wstring projectDir;  // required
  std::wstring model;       // empty: the CLI's configured default
  std::string mode;         // a Mode::id; empty: the CLI's default
  enum class Resume { None, ById, Latest };
  Resume resume = Resume::None;
  // For ById: the conversation to carry on.  Claude also takes a session
  // title here, which is why this is not checked for the shape of an id.
  // Empty means --resume was given with nothing after it; the adapter passes
  // that on to its CLI rather than guessing.
  //
  // Latest is "the newest conversation of this project" (-c), and which one
  // that is, is the adapter's question: only it knows where its CLI keeps
  // them.  None found means a new conversation, without a word.
  std::wstring resumeId;
  // Passed to the CLI as they are.  Spikes and experiments only: an argument
  // here means the same thing to exactly one adapter.
  std::vector<std::wstring> extraArgs;
};

// Why Start returned false.  Only a process that could not be started at all
// ends up here: one that starts and then refuses its arguments makes Start
// succeed, and says why itself, off stderr, in SessionEnded (invariant 25).
struct StartFailure {
  // What was looked for on PATH, as a reader would type it to try it by hand.
  std::wstring program;
  // The system's error code for the attempt.  A number and not words: the
  // port does not include windows.h, and the words are the UI's to find.
  unsigned long systemError = 0;
};

class Backend {
 public:
  struct Callbacks {
    // The live stream, one batch per message off the wire, in order.  A batch
    // is what the transcript appends as one edit where it can.  Never empty.
    // Called on the reader thread.
    std::function<void(std::vector<Event>)> onEvents;
    // The conversation so far, when resuming, before anything live -- all of
    // it in one call, so the transcript can insert it as one edit (invariant
    // 18).  May be called on the starting thread or the reader thread; a GUI
    // posts it across either way.
    std::function<void(std::vector<Event>)> onHistory;
    std::function<PermissionAnswer(const PermissionRequest&)> onPermission;
    std::function<QuestionAnswer(const QuestionRequest&)> onQuestion;
  };

  virtual ~Backend() = default;

  // Known before Start and constant afterwards: F1 and the status bar are
  // built from it, and a capability that came and went would make them lie.
  virtual const Capabilities& capabilities() const = 0;

  virtual bool Start(const StartOptions& options, Callbacks callbacks) = 0;
  // After a Start that returned false; meaningless otherwise.
  virtual StartFailure startFailure() const = 0;

  // Queues one turn.  The turn is over when TurnEnded arrives.
  virtual bool SendPrompt(const std::string& utf8Text) = 0;

  // Asks the turn in flight to stop.  False when there is none.  The turn is
  // over only when its own TurnEnded arrives, not when this returns.
  virtual bool Interrupt() = 0;

  // ---- Background tasks.  Only with Capabilities::stopTask or backgroundNow;
  // false without them.  Fire and forget, all three: what changed arrives as
  // BackgroundTasksChanged, and nothing else says it -- a task that ended on
  // its own a moment before is not an error, so there is no answer to wait
  // for.  False only when the request could not be sent.

  // One task, by BackgroundTask::id.
  virtual bool StopTask(const std::string& id) = 0;
  // Every task at once, in or out of a turn; a turn in flight is interrupted
  // with them.  Not StopTask in a loop: Claude answers each stopped subagent
  // with a turn of its own, all at once it answers with none
  // (tools/probe_subagents.notes.md, C1 and bg-stop).  How the adapter gets
  // there is its own business -- Claude's interrupt leaves commands running,
  // so they go one by one, and those bring no turn (bg-bash).
  virtual bool StopAllTasks() = 0;
  // Whatever the turn in flight waits for -- a command, a subagent -- goes on
  // in the background, and the turn goes on without waiting.  The one of the
  // three with an answer, BackgroundMoved: too early, nothing goes and
  // nothing in the list would say so.
  virtual bool Background() = 0;

  // Fire and forget; the change is reported as ModeChanged, and a refusal as a
  // ModeChanged back to the mode the CLI is really in.
  virtual bool SetMode(const std::string& id) = 0;

  // Interrupts the turn in flight and waits for its TurnEnded, THEN closes
  // the connection.  Both CLIs throw a turn away when their stdin closes
  // under it (invariant 1).
  virtual void Stop(unsigned turnTimeoutMs = 120000) = 0;

  // ---- What is known now.  Safe from any thread.

  // Empty until known.  Claude's is known before the process starts because
  // we choose it (invariant 14); Codex makes its own and says it in the answer
  // to thread/start.
  virtual std::string conversationId() const = 0;
  // A Mode::id; empty while it depends on settings nobody has stated yet.
  virtual std::string mode() const = 0;
  // Mode::ids the CLI refused to switch to in this session.  Shift+Tab steps
  // over them, or a refused mode next in the cycle would hold the reader on
  // the one before it (claude-gui-lkk.46).  Not part of Capabilities: it is
  // learned while running and holds for this session only.
  virtual std::vector<std::string> refusedModes() const = 0;
  // Both empty until Ready.
  virtual Account account() const = 0;
  virtual std::vector<SlashCommand> commands() const = 0;
  virtual bool ready() const = 0;
};

}  // namespace agent

#endif
