#include "proto/claude/translate.h"

#include "i18n/i18n.h"
#include "proto/claude/ask.h"
#include "proto/claude/control.h"
#include "proto/claude/events.h"
#include "proto/claude/sessions.h"

namespace proto {

const char kDeniedInstruction[] =
    "The user denied this. Do not continue; ask what to do next.";
const char kQuestionDeclinedInstruction[] =
    "The user closed the question without answering. Do not ask it again; "
    "ask in plain text what to do next.";

namespace {

std::string StringField(const Json& object, const char* name) {
  auto found = object.find(name);
  if (found == object.end() || !found->is_string()) return {};
  return found->get<std::string>();
}

// What each tool is for, and which of its arguments says what the call does.
// Falling back to the whole input would put a diff into a summary line.
//
// This is the catalogue of Claude Code's own tools, which is why it is here
// and not in model/: Codex calls the same things commandExecution and
// fileChange, and the transcript should not have to know either name.
struct ToolEntry {
  const char* tool;
  agent::ToolKind kind;
  const char* primary;  // nullptr: no field says it on its own
  bool isPath;
};

constexpr ToolEntry kTools[] = {
    {"Bash", agent::ToolKind::Shell, "command", false},
    {"PowerShell", agent::ToolKind::Shell, "command", false},
    {"Read", agent::ToolKind::ReadFile, "file_path", true},
    {"Edit", agent::ToolKind::EditFile, "file_path", true},
    {"Write", agent::ToolKind::CreateFile, "file_path", true},
    {"NotebookEdit", agent::ToolKind::EditFile, "notebook_path", true},
    {"Glob", agent::ToolKind::Search, "pattern", false},
    {"Grep", agent::ToolKind::Search, "pattern", false},
    {"WebFetch", agent::ToolKind::Fetch, "url", false},
    {"Skill", agent::ToolKind::Other, "skill", false},
    {kAskUserQuestionTool, agent::ToolKind::Question, nullptr, false},
};

// The questions of an AskUserQuestion call, read leniently.  ParseAskUserQuestion
// in ask.h is the strict one -- it decides whether a dialog can be put up, and
// a question without options cannot be answered there.  This is for showing
// the call, and a question is worth showing even with nothing to choose from.
std::vector<agent::Question> ReadQuestions(const Json& input) {
  std::vector<agent::Question> questions;
  auto list = input.find("questions");
  if (list == input.end() || !list->is_array()) return questions;
  for (const Json& item : *list) {
    if (!item.is_object()) continue;
    agent::Question question;
    question.text = StringField(item, "question");
    if (question.text.empty()) continue;
    // Claude files an answer under the text of its question.
    question.id = question.text;
    question.header = StringField(item, "header");
    question.multiSelect = item.value("multiSelect", false);
    auto options = item.find("options");
    if (options != item.end() && options->is_array()) {
      for (const Json& option : *options) {
        if (!option.is_object()) continue;
        agent::QuestionOption entry;
        entry.label = StringField(option, "label");
        if (entry.label.empty()) continue;
        entry.description = StringField(option, "description");
        question.options.push_back(std::move(entry));
      }
    }
    questions.push_back(std::move(question));
  }
  return questions;
}

// tool_result content is a string on the simple path and an array of parts
// when the tool returned an image or several pieces.
std::string ResultText(const Json& block) {
  auto content = block.find("content");
  if (content == block.end()) return {};
  if (content->is_string()) return content->get<std::string>();
  if (!content->is_array()) return content->dump();

  std::string text;
  bool first = true;
  for (const Json& part : *content) {
    if (!part.is_object()) continue;
    if (!first) text.push_back('\n');
    first = false;
    const std::string type = StringField(part, "type");
    if (type == "text") {
      text += StringField(part, "text");
    } else if (type == "image") {
      // Nothing useful can be done with it in a RichEdit; the view offers to
      // open it instead.  See claude-gui-lkk.5.
      text += i18n::Utf8(i18n::Str::kImage);
    } else {
      text += part.dump();
    }
  }
  return text;
}

// The wrapper the CLI puts around a failed tool's message.  It is protocol,
// not text for a reader: spoken, "menšie ako tool podčiarkovník use..." is
// noise in front of the one sentence that says what went wrong (invariant 8).
//
// It is also the second witness that this is an error.  is_error does come on
// the wire -- tests/fixtures/basic.jsonl has it -- so the field is read first
// and this only fills in behind it.  Reading the tag is not guessing: nothing
// else in the corpus is wrapped in it.
//
// Here and not in the text cleaning that model/ does to everything: that runs
// over the assistant's answers too, and would eat a sentence ABOUT the tag.
bool UnwrapToolError(std::string& text) {
  static const std::string open = "<tool_use_error>";
  static const std::string close = "</tool_use_error>";
  if (text.size() < open.size() + close.size()) return false;
  if (text.compare(0, open.size(), open) != 0) return false;
  if (text.compare(text.size() - close.size(), close.size(), close) != 0) {
    return false;
  }
  text = text.substr(open.size(), text.size() - open.size() - close.size());
  return true;
}

agent::ToolResult MakeToolResult(const Json& block) {
  agent::ToolResult result;
  result.callId = StringField(block, "tool_use_id");
  result.text = ResultText(block);
  const bool wrapped = UnwrapToolError(result.text);
  result.isError = block.value("is_error", false) || wrapped;
  // Our own instruction to the model, echoed back -- see the header.  The
  // words are Codex's for the same two events, so a denial reads the same
  // whichever agent was refused.
  if (result.isError && result.text == kDeniedInstruction) {
    result.text = i18n::Utf8(i18n::Str::kToolDeclined);
  } else if (result.isError && result.text == kQuestionDeclinedInstruction) {
    result.text = i18n::Utf8(i18n::Str::kNoAnswer);
  }
  return result;
}

// The content blocks of an assistant or user message, or nullptr.
const Json* Content(const Json& record) {
  auto message = record.find("message");
  if (message == record.end() || !message->is_object()) return nullptr;
  auto content = message->find("content");
  if (content == message->end() || !content->is_array()) return nullptr;
  return &*content;
}

}  // namespace

agent::ToolCall ToolCallFromInput(const std::string& name,
                                  const std::string& id, const Json& input) {
  agent::ToolCall call;
  call.id = id;
  call.name = name;

  const ToolEntry* entry = nullptr;
  for (const ToolEntry& candidate : kTools) {
    if (name == candidate.tool) entry = &candidate;
  }
  if (entry != nullptr) call.kind = entry->kind;

  if (!input.is_object()) {
    // Not a shape any tool has.  Shown whole, as JSON, under no field name.
    call.fields.push_back({std::string(), input.dump(2)});
    call.primary = input.dump();
    return call;
  }

  for (auto it = input.begin(); it != input.end(); ++it) {
    call.fields.push_back({it.key(), it.value().is_string()
                                         ? it.value().get<std::string>()
                                         : it.value().dump()});
  }

  if (entry != nullptr && entry->primary != nullptr) {
    call.primary = StringField(input, entry->primary);
    call.primaryIsPath = entry->isPath && !call.primary.empty();
  }
  if (call.primary.empty()) {
    // The description is the model's own one-line account of the call, and
    // the best there is when no argument says it alone.  A question renders
    // its summary out of the questions and comes here only when it has none.
    call.primary = StringField(input, "description");
    if (call.primary.empty()) call.primary = input.dump();
  }

  if (name == "Edit") {
    agent::TextReplacement replacement;
    replacement.before = StringField(input, "old_string");
    replacement.after = StringField(input, "new_string");
    replacement.everywhere = input.value("replace_all", false);
    if (!replacement.before.empty() || !replacement.after.empty()) {
      call.replacements.push_back(std::move(replacement));
    }
  } else if (name == "Write") {
    auto content = input.find("content");
    if (content != input.end() && content->is_string()) {
      call.newContent = content->get<std::string>();
    }
  } else if (name == kAskUserQuestionTool) {
    call.questions = ReadQuestions(input);
  }
  return call;
}

std::string Translator::NameSubagent(const std::string& callId,
                                     std::string type) {
  auto known = subagents_.find(callId);
  if (known != subagents_.end()) return known->second;
  // What the CLI runs when the call names no type.
  if (type.empty()) type = "general-purpose";
  std::string name = type + " " + std::to_string(++subagentCounts_[type]);
  subagents_[callId] = name;
  return name;
}

std::string Translator::Author(const Json& record) {
  auto parent = record.find("parent_tool_use_id");
  if (parent == record.end() || !parent->is_string()) return std::string();
  // A subagent whose call we never saw.  Not one from before a resume: the
  // subagent dies with its CLI, so this is only a guard against a stream
  // that names a parent it did not send.
  return NameSubagent(parent->get<std::string>(), std::string());
}

std::vector<agent::Event> Translator::Translate(const Json& record) {
  std::vector<agent::Event> events;
  const Event event = Classify(record);

  switch (event.kind) {
    case EventKind::Assistant: {
      const Json* content = Content(event.raw);
      if (content == nullptr) break;
      // Every record of a subagent comes on the same stream as the
      // conversation's and in among them (tools/probe_subagents.notes.md);
      // parent_tool_use_id is the one thing that tells them apart.
      const std::string by = Author(event.raw);
      for (const Json& item : *content) {
        if (!item.is_object()) continue;
        const std::string type = StringField(item, "type");
        // Empty text and empty thinking blocks do arrive -- a message can
        // carry a block that never got any content.  They are not worth a
        // line each; "premýšľanie (0 riadkov)" is noise between the things
        // the reader came for.  An empty tool_result is different and stays:
        // that a command printed nothing is an answer.
        if (type == "thinking") {
          std::string text = StringField(item, "thinking");
          if (!text.empty()) {
            events.push_back(agent::Thinking{std::move(text), by});
          }
        } else if (type == "text") {
          std::string text = StringField(item, "text");
          if (!text.empty()) {
            events.push_back(agent::AssistantText{std::move(text), by});
          }
        } else if (type == "tool_use") {
          auto input = item.find("input");
          const Json arguments =
              input != item.end() ? *input : Json::object();
          const std::string name = StringField(item, "name");
          agent::ToolCall call =
              ToolCallFromInput(name, StringField(item, "id"), arguments);
          // "Task" is what the tool was called before it was "Agent".
          if (name == "Agent" || name == "Task") {
            // The name goes into the call's summary as well, which is the
            // one place that says what "Explore 2" was sent to do.
            const std::string named = NameSubagent(
                call.id, StringField(arguments, "subagent_type"));
            call.primary = named + ": " + call.primary;
          }
          events.push_back(agent::ToolCallStarted{std::move(call), by});
        }
      }
      // Behind the content, so that what the turn said is said before the
      // status bar changes under it.
      if (std::string answering; ParseAnsweringModel(event.raw, &answering)) {
        answered_ = true;
        if (answering != model_) {
          model_ = answering;
          events.push_back(agent::ModelChanged{answering});
        }
      }
      // How full the window is right now.  The newest message wins, and there
      // are several per turn -- each one was sent everything before it, so the
      // last is the only one that is still true.
      if (long long tokens = 0; ParseContextTokens(event.raw, &tokens)) {
        events.push_back(agent::ContextUsed{tokens, 0});
      }
      break;
    }
    case EventKind::User: {
      // Only tool results.  Our own prompts go into the transcript when they
      // are sent, and the CLI's synthetic nudges ("your previous response had
      // no visible output") are machinery the user did not write and should
      // not read.
      const Json* content = Content(event.raw);
      if (content == nullptr) break;
      for (const Json& item : *content) {
        if (!item.is_object()) continue;
        if (StringField(item, "type") == "tool_result") {
          events.push_back(agent::ToolCallFinished{MakeToolResult(item)});
        }
      }
      break;
    }
    case EventKind::SystemPermissionDenied:
      events.push_back(agent::ToolDenied{StringField(event.raw, "tool_name"),
                                         StringField(event.raw, "message")});
      break;
    // The permission mode in it is not read here: Session folds it in with
    // every other report of the mode.
    // init opens every turn, including one the CLI starts by itself when a
    // task in the background ends -- and that one has no user record to
    // announce it (measured, tools/probe_selfturn.notes.md).
    case EventKind::SystemInit: {
      events.push_back(agent::TurnStarted{});
      events.push_back(agent::WorkingDirectory{StringField(event.raw, "cwd")});
      // Only until an assistant record has named the model -- see Translator.
      const std::string model = StringField(event.raw, "model");
      if (!answered_ && !model.empty() && model != model_) {
        model_ = model;
        events.push_back(agent::ModelChanged{model});
      }
      break;
    }
    // Ticks while the model thinks, one every few tokens, with nothing in them
    // worth showing -- but the earliest sign that a turn is doing something.
    case EventKind::SystemThinkingTokens:
      events.push_back(agent::ThinkingTick{});
      break;
    // Sent only for tasks in the background, never for a subagent the turn
    // waits on, and it comes outside turns as well (measured,
    // tools/probe_subagents.notes.md).
    case EventKind::SystemBackgroundTasks: {
      agent::BackgroundTasksChanged changed;
      auto tasks = event.raw.find("tasks");
      if (tasks != event.raw.end() && tasks->is_array()) {
        for (const Json& task : *tasks) {
          if (!task.is_object()) continue;
          agent::BackgroundTask out;
          out.id = StringField(task, "task_id");
          // local_agent, local_bash; anything newer is more likely another
          // kind of agent than a command.
          if (StringField(task, "task_type") == "local_bash") {
            out.kind = agent::TaskKind::Shell;
          }
          out.description = StringField(task, "description");
          changed.tasks.push_back(std::move(out));
        }
      }
      events.push_back(std::move(changed));
      break;
    }
    case EventKind::Result: {
      // The usage first: whatever is said at the end of a turn is said about a
      // turn whose numbers are already in.
      Usage usage;
      if (ParseUsage(event.raw, model_, &usage)) {
        agent::Usage out;
        out.inputTokens = usage.inputTokens;
        out.outputTokens = usage.outputTokens;
        out.cacheReadTokens = usage.cacheReadTokens;
        out.cacheWriteTokens = usage.cacheCreationTokens;
        out.reasoningTokens = usage.thinkingTokens;
        out.contextWindow = usage.contextWindow;
        // Both always, because Claude always says both -- and on a
        // subscription the billed one is a true zero, not a missing number.
        out.billedUsd = usage.billedUsd;
        out.listUsd = usage.listUsd;
        events.push_back(agent::UsageChanged{out});
      }
      agent::TurnEnded ended;
      if (StringField(event.raw, "terminal_reason") == "aborted_streaming") {
        ended.outcome = agent::TurnOutcome::Interrupted;
      } else if (event.subtype != "success") {
        ended.outcome = agent::TurnOutcome::Failed;
      }
      events.push_back(ended);
      break;
    }
    case EventKind::RateLimit: {
      RateLimit limit;
      if (!ParseRateLimit(event.raw, &limit)) break;
      agent::RateLimitChanged out;
      if (limit.status == "rejected") {
        out.state = agent::LimitState::Exhausted;
      } else if (limit.status == "allowed_warning") {
        out.state = agent::LimitState::Warning;
      }
      if (limit.fiveHourUtilization >= 0) {
        out.windows.push_back({300, limit.fiveHourUtilization, 0});
      }
      if (limit.sevenDayUtilization >= 0) {
        out.windows.push_back({7 * 24 * 60, limit.sevenDayUtilization, 0});
      }
      out.used = limit.utilization;
      out.resetsAt = limit.resetsAt;
      events.push_back(out);
      break;
    }
    case EventKind::Unknown:
      events.push_back(agent::Unrecognised{
          event.raw.value("type", std::string("<no type>"))});
      break;
    case EventKind::SystemOther:
      if (IsMovedToBackground(event.raw)) movedSinceAnswer_ = true;
      break;
    case EventKind::ControlResponse:
      if (IsBackgroundAnswer(event.raw)) {
        events.push_back(agent::BackgroundMoved{movedSinceAnswer_});
        movedSinceAnswer_ = false;
      }
      break;
    case EventKind::SystemHook:
    case EventKind::ControlRequest:
      break;
  }
  return events;
}

std::vector<agent::Event> TranslateRecord(const Json& record) {
  return Translator().Translate(record);
}

std::vector<agent::Event> TranslateHistory(const std::vector<Json>& records) {
  std::vector<agent::Event> events;
  Translator translator;
  for (const Json& record : records) {
    // A `user` record is up to three things at once, and the order below is
    // the order they were said in.  It is not an if/else chain by accident:
    // one record measured in the corpus carries six content parts, and text
    // beside a tool result is a shape the format allows.
    if (IsInterruptMark(record)) {
      // The same mark the live path writes when the reader presses Esc.
      // Without it an answer that was cut short reads, later on, exactly like
      // one that ended by itself: it just stops.
      events.push_back(agent::Interrupted{});
    } else if (std::string prompt = HumanPromptText(record); !prompt.empty()) {
      events.push_back(agent::UserPrompt{std::move(prompt)});
    }
    // Everything the live path makes of the same record.  A record that was a
    // prompt has no tool_result in it, so this adds nothing twice.
    for (agent::Event& event : translator.Translate(record)) {
      events.push_back(std::move(event));
    }
  }
  return events;
}

// The modes in a word of Slovak, for the status bar and for the spoken
// confirmation.  Both are read aloud -- the bar on NVDA+End, the confirmation
// on the key -- and a Slovak screen reader makes "acceptEdits" into noise.
// Short, because in the bar the mode stands next to three other fields and
// NVDA reads the lot in one breath.
//
// The gloss is what the mode actually does, the way the CLI's own Shift+Tab
// hint glosses it: "did that work" wants the name, "what did I just turn on"
// wants this.
//
// The cycle is the terminal's, read out of the CLI binary 2026-09-10:
//   default -> acceptEdits -> plan -> auto -> default
// bypassPermissions is a mode too but the control channel refuses it from a
// stdio host (see MakeSetPermissionMode); dontAsk is switchable but not in the
// rotation.  Both still get a label: a session can be started in them.
agent::Capabilities ClaudeCapabilities() {
  agent::Capabilities capabilities;
  capabilities.agentName = "claude";
  using i18n::Str;
  using i18n::Utf8;
  capabilities.modes = {
      {"default", Utf8(Str::kModeNormal), Utf8(Str::kClaudeModeNormalGloss),
       true, true},
      // The same mode under the name the CLI's own --help gives it: the help
      // lists "manual" and not "default", and the CLI maps the one to the other
      // and reports "default" back.  Measured on 2.1.288 (tools/
      // probe_cli_args.py, 2026-10-03).  Here so that the word the CLI
      // advertises is not refused by AgentAloud; off the cycle, so that
      // Shift+Tab never lands on it.
      {"manual", Utf8(Str::kModeNormal), Utf8(Str::kClaudeModeNormalGloss),
       false, true},
      {"acceptEdits", Utf8(Str::kClaudeModeAcceptEdits),
       Utf8(Str::kClaudeModeAcceptEditsGloss), true, false},
      {"plan", Utf8(Str::kModePlan), Utf8(Str::kModePlanGloss), true, false},
      {"auto", Utf8(Str::kModeAuto), Utf8(Str::kClaudeModeAutoGloss),
       true, false},
      {"bypassPermissions", Utf8(Str::kClaudeModeBypass),
       Utf8(Str::kClaudeModeBypassGloss), false, false},
      {"dontAsk", Utf8(Str::kClaudeModeDontAsk),
       Utf8(Str::kClaudeModeDontAskGloss), false, false},
  };
  capabilities.questions = true;
  capabilities.slashCommands = true;
  capabilities.resume = true;
  capabilities.allowForSession = false;
  capabilities.stopTask = true;
  capabilities.backgroundNow = true;
  // The aliases --help names, plus opusplan.  Aliases rather than ids
  // because they follow the newest model on their own; the list that is
  // true for the account comes only with the initialize answer, which is
  // too late for choosing what to start.
  capabilities.models = {"sonnet", "opus", "haiku", "opusplan"};
  return capabilities;
}

}  // namespace proto
