#include "proto/codex/translate.h"

#include <set>

#include "i18n/i18n.h"

namespace proto::codex {

const char kModeReadOnly[] = "read-only";
const char kModeAuto[] = "auto";
const char kModePlan[] = "plan";
const char kModeFullAccess[] = "full-access";
const char kModeCustom[] = "custom";

namespace {

std::string StringField(const Json& object, const char* name) {
  if (!object.is_object()) return {};
  auto found = object.find(name);
  if (found == object.end() || !found->is_string()) return {};
  return found->get<std::string>();
}

long long NumberField(const Json& object, const char* name) {
  if (!object.is_object()) return 0;
  auto found = object.find(name);
  if (found == object.end() || !found->is_number()) return 0;
  return found->get<long long>();
}

const Json& Field(const Json& object, const char* name) {
  static const Json null;
  if (!object.is_object()) return null;
  auto found = object.find(name);
  return found == object.end() ? null : *found;
}

// Notifications that say nothing the transcript or the pane would branch on.
// Named one by one rather than ignored wholesale, so that a notification the
// adapter has never seen is reported as Unrecognised and noticed by the
// fixtures -- the same rule as Claude's unknown record types.
//
// The deltas are here on purpose and not as an oversight: they always come
// and cannot be switched off, and reading them would be streaming by token,
// which was considered and rejected (claude-gui-lkk.5.17).  item/completed
// carries the whole text.
const std::set<std::string>& IgnoredMethods() {
  static const std::set<std::string> methods = {
      "item/agentMessage/delta",
      "item/commandExecution/outputDelta",
      "item/fileChange/outputDelta",
      "item/fileChange/patchUpdated",
      "item/plan/delta",
      "item/reasoning/summaryPartAdded",
      "item/reasoning/summaryTextDelta",
      "item/reasoning/textDelta",
      "item/mcpToolCall/progress",
      "turn/started",
      "turn/diff/updated",
      "turn/plan/updated",
      "thread/status/changed",
      "thread/goal/updated",
      "thread/goal/cleared",
      "thread/name/updated",
      "thread/compacted",
      "serverRequest/resolved",
      "mcpServer/startupStatus/updated",
      "remoteControl/status/changed",
      "account/updated",
      "skills/changed",
      "deprecationNotice",
      // A turn that fails says so in its own turn/completed, which is what
      // ends it; the error notification before it may be retried and is not
      // an end.
      "error",
  };
  return methods;
}

// Items that are not calls and have nothing to show either.
const std::set<std::string>& QuietItems() {
  static const std::set<std::string> types = {
      "contextCompaction", "enteredReviewMode", "exitedReviewMode",
      "hookPrompt",        "subAgentActivity",  "sleep",
      "functionCallOutput",
  };
  return types;
}

// The command a reader would have typed.  `command` is the whole wrapper --
// "C:\Program Files\PowerShell\7\pwsh.exe" -Command 'git log' -- and
// commandActions is Codex's own parse of what is inside it.  One action is the
// command; several are a pipeline Codex took apart, and then the wrapper is
// the only place that has it whole.
std::string ReadableCommand(const Json& item) {
  const Json& actions = Field(item, "commandActions");
  if (actions.is_array() && actions.size() == 1) {
    const std::string command = StringField(actions[0], "command");
    if (!command.empty()) return command;
  }
  return StringField(item, "command");
}

agent::ToolCall CommandCall(const Json& item) {
  agent::ToolCall call;
  call.name = "shell";
  call.kind = agent::ToolKind::Shell;
  call.primary = ReadableCommand(item);
  call.fields.push_back({"command", call.primary});
  return call;
}

agent::ToolCall FileChangeCall(const Json& item) {
  agent::ToolCall call;
  call.name = "apply_patch";
  call.kind = agent::ToolKind::EditFile;
  const Json& changes = Field(item, "changes");
  if (!changes.is_array() || changes.empty()) return call;

  if (changes.size() == 1) {
    const Json& change = changes[0];
    call.primary = StringField(change, "path");
    call.primaryIsPath = !call.primary.empty();
    const std::string kind = StringField(Field(change, "kind"), "type");
    const std::string diff = StringField(change, "diff");
    if (kind == "add") {
      // For a new file the "diff" is the file's content, as it will be
      // written (measured: "Čaj je lepší než káva.\n").
      call.kind = agent::ToolKind::CreateFile;
      call.newContent = diff;
    } else if (kind == "delete") {
      call.fields.push_back({"delete", call.primary});
    } else {
      call.diff = diff;
      const std::string moved = StringField(Field(change, "kind"), "move_path");
      if (!moved.empty()) call.fields.push_back({"move_path", moved});
    }
    return call;
  }

  // Several files in one patch: each one's diff under its path, which is how
  // a unified diff names them anyway.
  for (const Json& change : changes) {
    const std::string path = StringField(change, "path");
    if (!call.primary.empty()) call.primary += ", ";
    call.primary += path;
    if (!call.diff.empty()) call.diff += "\n";
    call.diff += path + " (" + StringField(Field(change, "kind"), "type") +
                 ")\n" + StringField(change, "diff");
  }
  return call;
}

void ArgumentFields(const Json& arguments, agent::ToolCall* call) {
  if (arguments.is_object()) {
    for (auto it = arguments.begin(); it != arguments.end(); ++it) {
      call->fields.push_back({it.key(), it.value().is_string()
                                            ? it.value().get<std::string>()
                                            : it.value().dump()});
    }
  } else if (!arguments.is_null()) {
    call->fields.push_back({std::string(), arguments.dump(2)});
  }
  if (call->primary.empty() && !call->fields.empty()) {
    call->primary = call->fields.front().value;
  }
}

// MCP and dynamic tool results: a list of content parts like an MCP server's.
std::string ContentText(const Json& content) {
  if (!content.is_array()) return content.is_null() ? "" : content.dump();
  std::string text;
  for (const Json& part : content) {
    if (!text.empty()) text.push_back('\n');
    const std::string type = StringField(part, "type");
    if (type == "text" || type == "inputText") {
      text += StringField(part, "text");
    } else if (type == "image" || type == "inputImage") {
      text += i18n::Utf8(i18n::Str::kImage);
    } else {
      text += part.dump();
    }
  }
  return text;
}

std::string UserText(const Json& item) {
  std::string text;
  const Json& content = Field(item, "content");
  if (!content.is_array()) return text;
  for (const Json& part : content) {
    const std::string type = StringField(part, "type");
    std::string piece;
    if (type == "text") {
      piece = StringField(part, "text");
    } else if (type == "image" || type == "localImage") {
      piece = i18n::Utf8(i18n::Str::kImage);
    } else if (type == "skill" || type == "mention") {
      piece = "@" + StringField(part, "name");
    }
    if (piece.empty()) continue;
    if (!text.empty()) text.push_back('\n');
    text += piece;
  }
  return text;
}

std::string ReasoningText(const Json& item) {
  // The summary is all there is: content came empty in every probe, and the
  // summary is a heading ("**Vypočítavam ...**") rather than the reasoning.
  std::string text;
  for (const char* field : {"summary", "content"}) {
    const Json& parts = Field(item, field);
    if (!parts.is_array()) continue;
    for (const Json& part : parts) {
      if (!part.is_string() || part.get<std::string>().empty()) continue;
      if (!text.empty()) text.push_back('\n');
      text += part.get<std::string>();
    }
  }
  return text;
}

// The questions of request_user_input_async, which ride on an agentMessage:
// {title, options: [label, ...] | null}.  No id and no description; the
// answer goes back as a prompt, so the title is all the filing there is.
std::vector<agent::Question> AsyncQuestions(const Json& item) {
  std::vector<agent::Question> questions;
  const Json& list = Field(item, "questions");
  if (!list.is_array()) return questions;
  for (const Json& entry : list) {
    agent::Question question;
    question.text = StringField(entry, "title");
    if (question.text.empty()) continue;
    question.id = question.text;
    question.allowsOther = true;  // the answer is a prompt; anything goes
    const Json& options = Field(entry, "options");
    if (options.is_array()) {
      for (const Json& option : options) {
        if (option.is_string() && !option.get<std::string>().empty()) {
          question.options.push_back({option.get<std::string>(), ""});
        }
      }
    }
    questions.push_back(std::move(question));
  }
  return questions;
}

// What an item that is not a tool call says.  False when the item is not one
// this knows.  `live` is false for a replayed history, where a question was
// answered long ago -- or not -- and offering it again would be wrong.
bool TranslateMessageItem(const Json& item, std::vector<agent::Event>* out,
                          bool live) {
  const std::string type = StringField(item, "type");
  if (type == "agentMessage" && StringField(item, "delivery") == "async") {
    std::vector<agent::Question> questions = AsyncQuestions(item);
    if (!questions.empty()) {
      // The text of such a message is the question again with the options
      // as a bulleted list ("Čaj alebo káva?\n- Čaj\n- Káva", measured), so
      // it is shown as the question block alone and not twice.
      agent::ToolCall call;
      const std::string id = StringField(item, "id");
      call.id = id;
      call.name = "request_user_input_async";
      call.kind = agent::ToolKind::Question;
      call.questions = questions;
      for (const agent::Question& question : questions) {
        call.fields.push_back({"title", question.text});
      }
      out->push_back(agent::ToolCallStarted{std::move(call)});
      if (live) {
        out->push_back(agent::QuestionByPrompt{id, std::move(questions)});
      }
      return true;
    }
  }
  if (type == "agentMessage" || type == "plan") {
    // Both phases.  "commentary" is the sentence between two commands and
    // "final_answer" the answer; heard in the order they came, they are what
    // the terminal shows (invariant 6).
    std::string text = StringField(item, "text");
    if (!text.empty()) out->push_back(agent::AssistantText{std::move(text)});
    return true;
  }
  if (type == "reasoning") {
    std::string text = ReasoningText(item);
    if (!text.empty()) out->push_back(agent::Thinking{std::move(text)});
    return true;
  }
  if (type == "userMessage") return true;  // the pane wrote it when sending
  return QuietItems().count(type) != 0;
}

}  // namespace

MessageKind KindOf(const Json& message) {
  if (!message.is_object()) return MessageKind::Malformed;
  const bool method = message.contains("method");
  const bool id = message.contains("id");
  if (method && id) return MessageKind::ServerRequest;
  if (method) return MessageKind::Notification;
  if (id) return MessageKind::Response;
  return MessageKind::Malformed;
}

agent::Capabilities CodexCapabilities() {
  agent::Capabilities capabilities;
  capabilities.agentName = "codex";
  // Shift+Tab order.  The ordinary preset first, then plan, which is what
  // the terminal Codex's own Shift+Tab toggles, then the stricter one.
  // Full access is a mode a session can be started in but the key never
  // steps onto, for the same reason Claude's bypassPermissions is off the
  // cycle: it is not a place to land on by pressing a key once too often.
  using i18n::Str;
  using i18n::Utf8;
  capabilities.modes = {
      {kModeAuto, Utf8(Str::kModeNormal), Utf8(Str::kCodexModeNormalGloss),
       true, true},
      {kModePlan, Utf8(Str::kModePlan), Utf8(Str::kModePlanGloss), true,
       false},
      {kModeReadOnly, Utf8(Str::kCodexModeReadOnly),
       Utf8(Str::kCodexModeReadOnlyGloss), true, false},
      {kModeFullAccess, Utf8(Str::kCodexModeFullAccess),
       Utf8(Str::kCodexModeFullAccessGloss), false, false},
      {kModeCustom, Utf8(Str::kCodexModeCustom),
       Utf8(Str::kCodexModeCustomGloss), false, false},
  };
  capabilities.questions = true;
  // app-server has no list of slash commands: they belong to the terminal
  // client, and what they do is RPC (claude-gui-lkk.44.1).
  capabilities.slashCommands = false;
  capabilities.resume = true;
  capabilities.allowForSession = true;
  // None: Codex says which models there are only through model/list, which
  // needs app-server running.  A name written here from memory would go
  // stale with the next release, silently.
  return capabilities;
}

std::string ModeFromSettings(const Json& approvalPolicy,
                             const Json& sandboxPolicy,
                             const std::string& collaboration) {
  if (collaboration == "plan") return kModePlan;
  const std::string approval =
      approvalPolicy.is_string() ? approvalPolicy.get<std::string>() : "";
  const std::string sandbox = StringField(sandboxPolicy, "type");
  if (approval == "on-request" && sandbox == "workspaceWrite") return kModeAuto;
  if (approval == "on-request" && sandbox == "readOnly") return kModeReadOnly;
  if (approval == "never" && sandbox == "dangerFullAccess") {
    return kModeFullAccess;
  }
  return kModeCustom;
}

std::string ModeFromThreadSettings(const Json& threadSettings) {
  return ModeFromSettings(
      Field(threadSettings, "approvalPolicy"),
      Field(threadSettings, "sandboxPolicy"),
      StringField(Field(threadSettings, "collaborationMode"), "mode"));
}

bool SettingsForMode(const std::string& mode, ModeSettings* out) {
  ModeSettings settings;
  settings.collaboration = "default";
  if (mode == kModeAuto) {
    settings.approvalPolicy = "on-request";
    settings.sandbox = "workspace-write";
    settings.sandboxPolicy = {{"type", "workspaceWrite"},
                              {"writableRoots", Json::array()},
                              {"networkAccess", false},
                              {"excludeTmpdirEnvVar", false},
                              {"excludeSlashTmp", false}};
  } else if (mode == kModeReadOnly) {
    settings.approvalPolicy = "on-request";
    settings.sandbox = "read-only";
    settings.sandboxPolicy = {{"type", "readOnly"}, {"networkAccess", false}};
  } else if (mode == kModeFullAccess) {
    settings.approvalPolicy = "never";
    settings.sandbox = "danger-full-access";
    settings.sandboxPolicy = {{"type", "dangerFullAccess"}};
  } else if (mode == kModePlan) {
    settings.collaboration = "plan";
    settings.permissions = false;
  } else {
    return false;
  }
  *out = std::move(settings);
  return true;
}

bool IsToolItem(const Json& item) {
  static const std::set<std::string> types = {
      "commandExecution", "fileChange",          "mcpToolCall",
      "dynamicToolCall",  "collabAgentToolCall", "webSearch",
      "imageView",        "imageGeneration",
  };
  return types.count(StringField(item, "type")) != 0;
}

agent::ToolCall ToolCallFromItem(const Json& item) {
  const std::string type = StringField(item, "type");
  agent::ToolCall call;
  if (type == "commandExecution") {
    call = CommandCall(item);
  } else if (type == "fileChange") {
    call = FileChangeCall(item);
  } else if (type == "mcpToolCall") {
    call.name = StringField(item, "server") + "." + StringField(item, "tool");
    ArgumentFields(Field(item, "arguments"), &call);
  } else if (type == "dynamicToolCall" || type == "collabAgentToolCall") {
    call.name = StringField(item, "tool");
    if (type == "collabAgentToolCall") {
      call.primary = StringField(item, "prompt");
    }
    ArgumentFields(Field(item, "arguments"), &call);
  } else if (type == "webSearch") {
    call.name = "web_search";
    call.kind = agent::ToolKind::Fetch;
    call.primary = StringField(item, "query");
    call.fields.push_back({"query", call.primary});
  } else if (type == "imageView") {
    call.name = "view_image";
    call.kind = agent::ToolKind::ReadFile;
    call.primary = StringField(item, "path");
    call.primaryIsPath = !call.primary.empty();
    call.fields.push_back({"path", call.primary});
  } else {
    call.name = type;
  }
  call.id = StringField(item, "id");
  return call;
}

agent::ToolResult ToolResultFromItem(const Json& item) {
  agent::ToolResult result;
  result.callId = StringField(item, "id");
  const std::string type = StringField(item, "type");
  const std::string status = StringField(item, "status");

  if (type == "commandExecution") {
    result.text = StringField(item, "aggregatedOutput");
    // The break a command prints after its last line.  Claude's CLI takes it
    // off and Codex does not, so "ahoj\r\n" came out as two lines, the second
    // empty, and a one-line output stopped being shown whole (live, 4. 10.).
    if (!result.text.empty() && result.text.back() == '\n') {
      result.text.pop_back();
      if (!result.text.empty() && result.text.back() == '\r') {
        result.text.pop_back();
      }
    }
    const Json& exitCode = Field(item, "exitCode");
    result.isError = status == "failed" || status == "declined" ||
                     (exitCode.is_number() && exitCode.get<long long>() != 0);
  } else if (type == "mcpToolCall") {
    const Json& error = Field(item, "error");
    if (error.is_object()) {
      result.text = StringField(error, "message");
      result.isError = true;
    } else {
      result.text = ContentText(Field(Field(item, "result"), "content"));
    }
  } else if (type == "dynamicToolCall") {
    result.text = ContentText(Field(item, "contentItems"));
    const Json& success = Field(item, "success");
    result.isError = success.is_boolean() && !success.get<bool>();
  }
  if (status == "failed" || status == "declined") result.isError = true;
  // A declined call comes back with nothing in it -- aggregatedOutput null
  // (measured) -- and "chyba (prázdny)" would not say what happened.
  if (status == "declined" && result.text.empty()) {
    result.text = i18n::Utf8(i18n::Str::kToolDeclined);
  }
  return result;
}

std::vector<agent::Question> ReadQuestions(const Json& params) {
  std::vector<agent::Question> questions;
  const Json& list = Field(params, "questions");
  if (!list.is_array()) return questions;
  for (const Json& item : list) {
    agent::Question question;
    // Codex files an answer under an id it made up (measured:
    // "drink_preference"), not under the text as Claude does.
    question.id = StringField(item, "id");
    question.text = StringField(item, "question");
    if (question.id.empty() || question.text.empty()) continue;
    question.header = StringField(item, "header");
    // No multiSelect on this wire: the answer is a list, but the question
    // does not say it takes more than one.
    question.multiSelect = false;
    const Json& other = Field(item, "isOther");
    question.allowsOther = other.is_boolean() ? other.get<bool>() : true;
    const Json& options = Field(item, "options");
    if (options.is_array()) {
      for (const Json& option : options) {
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

agent::ToolCall QuestionCall(const Json& params) {
  agent::ToolCall call;
  call.id = StringField(params, "itemId");
  call.name = "request_user_input";
  call.kind = agent::ToolKind::Question;
  call.questions = ReadQuestions(params);
  for (const agent::Question& question : call.questions) {
    call.fields.push_back({question.id, question.text});
  }
  return call;
}

Json MakeQuestionAnswer(const std::vector<agent::Question>& questions,
                        const agent::QuestionAnswer& answer) {
  Json answers = Json::object();
  if (!answer.declined) {
    for (size_t i = 0; i < questions.size() && i < answer.chosen.size(); ++i) {
      if (answer.chosen[i].empty()) continue;
      answers[questions[i].id] = {{"answers", answer.chosen[i]}};
    }
  }
  return {{"answers", answers}};
}

agent::ToolResult QuestionResult(const std::string& callId,
                                 const std::vector<agent::Question>& questions,
                                 const agent::QuestionAnswer& answer) {
  agent::ToolResult result;
  result.callId = callId;
  if (answer.declined) {
    result.text = i18n::Utf8(i18n::Str::kNoAnswer);
    result.isError = true;
    return result;
  }
  for (size_t i = 0; i < questions.size() && i < answer.chosen.size(); ++i) {
    if (answer.chosen[i].empty()) continue;
    if (!result.text.empty()) result.text.push_back('\n');
    result.text += questions[i].text + " ";
    for (size_t j = 0; j < answer.chosen[i].size(); ++j) {
      if (j != 0) result.text += ", ";
      result.text += answer.chosen[i][j];
    }
  }
  return result;
}

void Translator::ReportModel(const std::string& model,
                             std::vector<agent::Event>* out) {
  if (model.empty() || model == model_) return;
  model_ = model;
  out->push_back(agent::ModelChanged{model});
}

bool Translator::FindCall(const std::string& itemId,
                          agent::ToolCall* out) const {
  auto found = running_.find(itemId);
  if (found == running_.end()) return false;
  *out = found->second;
  return true;
}

std::vector<agent::Event> Translator::Translate(const Json& message) {
  std::vector<agent::Event> events;
  if (KindOf(message) != MessageKind::Notification) return events;

  const std::string method = StringField(message, "method");
  const Json& params = Field(message, "params");

  if (method == "item/started") {
    const Json& item = Field(params, "item");
    if (IsToolItem(item)) {
      agent::ToolCall call = ToolCallFromItem(item);
      running_[call.id] = call;
      events.push_back(agent::ToolCallStarted{std::move(call)});
    } else if (StringField(item, "type") == "reasoning") {
      // The earliest sign that the model is thinking, and with low effort
      // the item does not come at all -- then nothing is said, which is true.
      events.push_back(agent::ThinkingTick{});
    }
  } else if (method == "item/completed") {
    const Json& item = Field(params, "item");
    if (IsToolItem(item)) {
      const std::string id = StringField(item, "id");
      auto found = running_.find(id);
      if (found == running_.end()) {
        // Finished without having been announced.  Not seen, but the result
        // has to have a call to sit behind.
        events.push_back(agent::ToolCallStarted{ToolCallFromItem(item)});
      } else {
        running_.erase(found);
      }
      events.push_back(agent::ToolCallFinished{ToolResultFromItem(item)});
    } else if (!TranslateMessageItem(item, &events, true)) {
      events.push_back(
          agent::Unrecognised{"item/" + StringField(item, "type")});
    }
  } else if (method == "turn/completed") {
    // A call still running is NOT forgotten here.  Interrupting the turn does
    // not kill the command (Codex 0.160.0): a ping ran on to its end and its
    // item/completed arrived half a minute after this, and with the call
    // forgotten it was announced a second time at the end of the transcript
    // (live, 4. 10. 2026).  An item id is never reused, so keeping it cannot
    // pair a later approval with the wrong call; a command that never
    // finishes costs one entry.
    agent::TurnEnded ended;
    const std::string status = StringField(Field(params, "turn"), "status");
    if (status == "interrupted") {
      ended.outcome = agent::TurnOutcome::Interrupted;
    } else if (status == "failed") {
      ended.outcome = agent::TurnOutcome::Failed;
    }
    events.push_back(ended);
  } else if (method == "thread/started") {
    const Json& thread = Field(params, "thread");
    const std::string cwd = StringField(thread, "cwd");
    if (!cwd.empty()) events.push_back(agent::WorkingDirectory{cwd});
    ReportModel(StringField(thread, "model"), &events);
  } else if (method == "thread/settings/updated") {
    // The mode in it is the adapter's to follow: it has to be weighed
    // against the changes the adapter itself asked for.
    ReportModel(StringField(Field(params, "threadSettings"), "model"), &events);
  } else if (method == "model/rerouted") {
    ReportModel(StringField(params, "toModel"), &events);
  } else if (method == "thread/tokenUsage/updated") {
    const Json& usage = Field(params, "tokenUsage");
    const Json& last = Field(usage, "last");
    const Json& total = Field(usage, "total");
    const long long window = NumberField(usage, "modelContextWindow");
    // The newest request was sent everything before it, so its size is how
    // full the window is now.
    events.push_back(
        agent::ContextUsed{NumberField(last, "totalTokens"), window});
    agent::Usage out;
    out.inputTokens = NumberField(total, "inputTokens");
    out.outputTokens = NumberField(total, "outputTokens");
    out.cacheReadTokens = NumberField(total, "cachedInputTokens");
    out.cacheWriteTokens = NumberField(total, "cacheWriteInputTokens");
    out.reasoningTokens = NumberField(total, "reasoningOutputTokens");
    out.contextWindow = window;
    // No cost: Codex does not state one, and a zero would be read as free.
    events.push_back(agent::UsageChanged{out});
  } else if (method == "account/rateLimits/updated") {
    const Json& limits = Field(params, "rateLimits");
    agent::RateLimitChanged out;
    for (const char* name : {"primary", "secondary"}) {
      const Json& window = Field(limits, name);
      if (!window.is_object()) continue;
      agent::LimitWindow entry;
      entry.minutes = static_cast<int>(NumberField(window, "windowDurationMins"));
      const Json& used = Field(window, "usedPercent");
      if (used.is_number()) entry.used = used.get<double>() / 100.0;
      entry.resetsAt = NumberField(window, "resetsAt");
      out.windows.push_back(entry);
    }
    if (!Field(limits, "rateLimitReachedType").is_null()) {
      out.state = agent::LimitState::Exhausted;
    }
    events.push_back(out);
  } else if (IgnoredMethods().count(method) == 0) {
    events.push_back(agent::Unrecognised{method});
  }
  return events;
}

std::vector<agent::Event> TranslateHistory(const Json& resumeResult) {
  std::vector<agent::Event> events;
  const std::string cwd = StringField(resumeResult, "cwd");
  if (!cwd.empty()) events.push_back(agent::WorkingDirectory{cwd});

  const Json& turns = Field(Field(resumeResult, "thread"), "turns");
  if (turns.is_array()) {
    for (const Json& turn : turns) {
      const Json& items = Field(turn, "items");
      if (!items.is_array()) continue;
      for (const Json& item : items) {
        if (StringField(item, "type") == "userMessage") {
          std::string text = UserText(item);
          if (!text.empty()) events.push_back(agent::UserPrompt{std::move(text)});
        } else if (IsToolItem(item)) {
          events.push_back(agent::ToolCallStarted{ToolCallFromItem(item)});
          // A call that was still running when the turn was cut off has no
          // result, and is left without one -- as it was live.
          if (StringField(item, "status") != "inProgress") {
            events.push_back(agent::ToolCallFinished{ToolResultFromItem(item)});
          }
        } else if (!TranslateMessageItem(item, &events, false)) {
          events.push_back(
              agent::Unrecognised{"item/" + StringField(item, "type")});
        }
      }
      if (StringField(turn, "status") == "interrupted") {
        events.push_back(agent::Interrupted{});
      }
    }
  }

  const std::string model = StringField(resumeResult, "model");
  if (!model.empty()) events.push_back(agent::ModelChanged{model});
  return events;
}

}  // namespace proto::codex
