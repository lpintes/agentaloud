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
#include <optional>
#include <string>
#include <utility>
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
  // The agent can be signed in to its account from here: StartSignIn.
  bool signIn = false;
  // Model names to offer before anything has started -- suggestions, not the
  // list of what the CLI takes: a full model id is always accepted too, and
  // an alias the CLI adds tomorrow must not be refused today.  Empty when the
  // adapter knows none without asking a running process.
  std::vector<std::string> models;
  // The model of a running session can be changed: SetModel, F5
  // (claude-gui-lkk.86).
  bool switchModel = false;
  // ...and the change reaches the turn in flight, from its next request to
  // the model on.  False: it waits for the next turn (Codex, measured), and
  // the reader is told so, because the two CLIs differ exactly here.
  bool switchModelInTurn = false;
  // The reasoning effort of a running session can be changed: SetEffort, in
  // the same dialog as the model (claude-gui-lkk.87).
  bool switchEffort = false;
  // ...and the change reaches the turn in flight, as switchModelInTurn.
  bool switchEffortInTurn = false;
  // Where the CLI keeps its sign-in and settings, for the sentences that say
  // why it will not answer (claude-gui-lkk.73).  Per backend, not per CLI:
  // the adapter fills it in from its Program when it is made.
  ConfigHome configHome;
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

// One entry of the catalog a running CLI offers for SetModel.  The words are
// the CLI's own, in English: a model's name is not translated, and the
// description is short enough to read as it is.
struct ModelChoice {
  std::string id;           // what SetModel takes: "sonnet", "gpt-6-luna"
  std::string name;         // "Sonnet 5.5"; `id` when the CLI gives none
  std::string description;  // "Most efficient for simpler tasks"; may be empty
  // The full id an alias stands for, which is what a session reports as its
  // model.  Empty when the CLI gives none.
  std::string resolved;
  // What SetEffort takes on this model, lowest first, in the CLI's own words:
  // the stages differ from model to model.  Empty: this model has no effort.
  std::vector<std::string> efforts;
};

// The entry `model` names, by its id or by the full id it resolves to, or
// nullptr.
const ModelChoice* FindModel(const std::vector<ModelChoice>& catalog,
                             const std::string& model);

struct Account {
  std::string email;
  std::string plan;  // "Claude Pro", "free"
  // A sentence when the cost numbers mean something unusual -- billed through
  // a cloud provider, say.  Empty in the ordinary case.
  std::string billingNote;
};

// ---- Requests that wait for a person ----------------------------------------
//
// All are CALLED ON THE READER THREAD and block it until answered (invariant
// 2).  The adapter turns the answer into whatever its wire wants -- Claude's
// updatedInput keyed by question text, Codex's answers keyed by id.

enum class Verdict {
  Allow,
  AllowForSession,  // only when Capabilities::allowForSession
  // Kept in the project's settings by the CLI itself, past this session.
  // Offered only where the CLI suggested exactly that (claude-gui-lkk.29).
  AllowInProject,
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
  // What AllowForSession lets through from now on, one line each, in the
  // reader's language: often more than the call shown -- a chain of commands
  // becomes one rule per command, a file edit the acceptEdits mode for every
  // file (claude-gui-lkk.62).  Empty when not offered or not known.
  std::vector<std::string> sessionScope;
  // What AllowInProject lets through, as sessionScope.  A line that holds
  // for this session only says so: one answer can carry both kinds.
  std::vector<std::string> projectScope;
  // The subagent asking, by the name its blocks go by ("Explore 2"); empty
  // for the conversation itself.  Several may wait at once, and a dialog that
  // does not say whose call it is cannot be answered (claude-gui-b8n.10).
  std::string by;
  // Not empty for a call that ends planning -- Claude's ExitPlanMode.  Then
  // the request is the plan put up for approval: Allow comes with a choice of
  // the mode to go on in (ids from Capabilities::modes, in the order to offer
  // them), and Deny means "keep planning": the turn ends there, and what to
  // change is the reader's next prompt (claude-gui-lkk.67).  Without the choice the CLI goes back to the mode planning was
  // entered from, which after Shift+Tab is seldom the one wanted
  // (claude-gui-lkk.43).
  std::vector<std::string> modesAfter;
  // The one of modesAfter the CLI would go to by itself; preselected.
  std::string modeAfter;
};

struct PermissionAnswer {
  Verdict verdict = Verdict::Deny;
  // Handed to the model when the tool is denied, so it is written for the
  // model: "the user declined; ask before trying this again".  Empty leaves
  // the words to the adapter, which knows its CLI and writes them in English.
  std::string message;
  // With Allow, when the request had modesAfter: the mode chosen.  Empty
  // leaves it to the CLI.
  std::string mode;
};

struct QuestionRequest {
  std::vector<Question> questions;
  std::string by;  // as PermissionRequest::by
};

// What the model is told when the reader declines is the adapter's to write:
// it is a sentence for the model, in the shape that CLI passes on.
struct QuestionAnswer {
  bool declined = false;
  // Parallel to QuestionRequest::questions.  An empty entry leaves that
  // question unanswered.
  std::vector<std::vector<std::string>> chosen;
};

// A form an MCP server asks the reader to fill in -- MCP elicitation, which
// Codex passes on unchanged and Claude would too (claude-gui-lkk.44.18).  A
// form with no fields is a yes or a no and goes as a PermissionRequest; this
// is the rest: fields, or a link to open.
//
// Values travel as text, one entry per value, because the port has no JSON:
// the adapter turns them back into the type the field says.  Neither CLI
// checks a value against the schema (tools/probe_elicit_form.notes.md), so
// the dialog does, with model::CheckFormField.

enum class FieldType {
  Text,
  Number,
  Integer,
  Boolean,  // values "true" and "false"
  Choice,   // one of `options`
  Choices,  // any of `options`, within minItems..maxItems
};

struct FormOption {
  std::string value;  // what the answer carries
  std::string label;  // what the reader sees; `value` when the schema has none
};

struct FormField {
  std::string name;   // the key in the answer
  std::string title;  // the label; empty means `name`
  std::string description;
  FieldType type = FieldType::Text;
  bool required = false;
  // The schema's default as values, offered prefilled.  Empty when none.
  std::vector<std::string> defaults;
  // Text: lengths in characters, and "email", "uri", "date" or "date-time".
  std::optional<long long> minLength;
  std::optional<long long> maxLength;
  std::string format;
  // Number and Integer.
  std::optional<double> minimum;
  std::optional<double> maximum;
  // Choice and Choices.
  std::vector<FormOption> options;
  std::optional<long long> minItems;
  std::optional<long long> maxItems;
};

struct FormRequest {
  std::string server;   // the MCP server asking
  std::string message;  // the server's own words, in its language
  // Set for a link to open instead of fields: signing in to the server's
  // service, say.  The answer then says only whether it was opened; nothing
  // reports back when the reader is done on the page (measured).
  std::string url;
  // Required first, the rest after, each part by name: the server's own
  // order is lost on the way (measured), and the alphabet is at least an
  // order the reader can predict.
  std::vector<FormField> fields;
  std::string by;  // as PermissionRequest::by
};

struct FormAnswer {
  enum class Action {
    Accept,   // filled in, or the link opened
    Decline,  // the reader said no
    Cancel,   // the reader closed it without saying
  };
  Action action = Action::Cancel;
  // Parallel to FormRequest::fields, with Accept.  An empty entry leaves the
  // field out of the answer.
  std::vector<std::vector<std::string>> values;
};

// ---- Starting ---------------------------------------------------------------

// How to start the CLI, given to the adapter when it is made, because every
// process it starts needs it -- the conversation list too, and that is asked
// of a backend that is never started (claude-gui-lkk.72).  A portable
// installation is the reason: an executable that is not on PATH or not under
// the name the adapter knows, and a home of its own instead of the profile.
struct Program {
  // The executable, absolute.  Empty: the adapter's own name, found on PATH.
  std::wstring path;
  // Set for the CLI on top of the environment it would inherit, in order.
  std::vector<std::pair<std::wstring, std::wstring>> environment;

  // The value `name` is given here, the last one when given twice, or null.
  // Names compare ASCII case-insensitively, as Windows compares them.
  const std::wstring* Find(const std::wstring& name) const;
};

// The home the CLI ends up with: `variable` as `program` sets it, else as this
// process inherited it (the CLI inherits it too), else `profileFolder` under
// %USERPROFILE%.  What an empty value means differs: Codex takes it for not
// set, Claude takes it for a home of "" and is signed out in it (measured
// 2026-10-16), and that is `emptyIsUnset`.
ConfigHome ResolveConfigHome(const Program& program,
                             const std::wstring& variable,
                             const std::wstring& profileFolder,
                             bool emptyIsUnset);

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

// A conversation a folder already has: one line of a list the reader picks
// from to carry it on (claude-gui-lkk.7.11).  Headless conversations have no
// title -- Claude writes one only in its terminal, Codex leaves its name empty
// nearly always -- so what names one is the first thing the reader said in
// it, and when it was last spoken in.
struct Conversation {
  std::wstring id;          // what StartOptions::resumeId takes
  std::string firstPrompt;  // UTF-8, one line; empty when nobody typed one
  // Seconds since 1970 UTC, so that the two CLIs' ways of writing a time
  // compare and sort alike.  0 when unknown.
  long long lastActive = 0;
};

// Why Start returned false.  Only a process that could not be started at all
// ends up here: one that starts and then refuses its arguments makes Start
// succeed, and says why itself, off stderr, in SessionEnded (invariant 25).
struct StartFailure {
  // What was started, as a reader would type it to try it by hand: the name
  // looked for on PATH, or Program::path when there is one.
  std::wstring program;
  // The system's error code for the attempt.  A number and not words: the
  // port does not include windows.h, and the words are the UI's to find.
  unsigned long systemError = 0;
  // Whether `program` is Program::path rather than a name looked for on PATH:
  // "not found" then means a wrong setting, not a missing installation.
  bool configured = false;
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
    std::function<FormAnswer(const FormRequest&)> onForm;
  };

  virtual ~Backend() = default;

  // Known before Start and constant afterwards: F1 and the status bar are
  // built from it, and a capability that came and went would make them lie.
  virtual const Capabilities& capabilities() const = 0;

  // The conversations `projectDir` already has, the most recently active
  // first: what Resume::ById can carry on.  Only with Capabilities::resume.
  //
  // Asked of a backend that has NOT been started, and need not ever be: the
  // list is wanted before the reader picks, and the session that carries one
  // on is a backend of its own, made after.  So whoever makes backends --
  // main.cpp, and nobody in agent/ -- makes one to ask, too.
  //
  // BLOCKS, for as long as the adapter needs: Claude reads its files, Codex
  // starts a process of its own for it, which is about half a second
  // (tools/probe_codex_list.notes.md).  Not on a window's thread.
  //
  // True with an empty list is a folder with no conversations; false is a
  // list that could not be had.
  virtual bool ListConversations(const std::wstring& projectDir,
                                 std::vector<Conversation>* out) = 0;

  virtual bool Start(const StartOptions& options, Callbacks callbacks) = 0;
  // After a Start that returned false; meaningless otherwise.
  virtual StartFailure startFailure() const = 0;

  // Sends one prompt, also while a turn runs: the agent takes it into that
  // turn or runs a turn for it after (TurnEnded::more).  A prompt sent is
  // never dropped, not even by Interrupt.  All is over at a TurnEnded without
  // `more`.
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

  // Only with Capabilities::switchModel; false without it.  `id` is a
  // ModelChoice::id.  Fire and forget, and nothing reports back: the status
  // bar goes on naming the model that answers (invariant 21), so the change
  // shows there with the first answer it reaches.  Not saved anywhere
  // (invariant 22).
  virtual bool SetModel(const std::string& id) = 0;

  // Only with Capabilities::switchEffort; false without it.  `level` is one of
  // ModelChoice::efforts, or empty for the default of the model.  Fire and
  // forget, and offer only what the catalog lists: Claude answers success to a
  // stage it does not know and ignores it (tools/probe_claude_effort.notes.md).
  // Not saved anywhere (invariant 22).
  virtual bool SetEffort(const std::string& level) = 0;

  // ---- Signing in.  Only with Capabilities::signIn; false without it.
  // Where to go arrives as SignInPrompt and the outcome as SignInFinished,
  // both through onEvents.  One sign-in at a time: StartSignIn while one is
  // running is false.  The session runs on meanwhile.

  virtual bool StartSignIn() = 0;
  // The code the page showed, when SignInPrompt::wantsCode.
  virtual bool SubmitSignInCode(const std::string& code) = 0;
  // Ends with SignInFinished as well, unless none was running.
  virtual bool CancelSignIn() = 0;

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
  // What SetModel takes, in the CLI's own order.  Empty until the CLI has
  // said, which may be after Ready; F5 then says "not yet", as F4 does.
  virtual std::vector<ModelChoice> modelCatalog() const = 0;
  // The stage last set or last reported; empty for the model's default.
  virtual std::string effort() const = 0;
  virtual bool ready() const = 0;
};

}  // namespace agent

#endif
