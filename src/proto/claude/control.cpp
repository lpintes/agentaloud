#include "proto/claude/control.h"

#include <algorithm>

#include "proto/claude/sessions.h"
#include "proto/elicitation.h"

namespace proto {
namespace {

std::string StringField(const Json& object, const char* name) {
  auto found = object.find(name);
  if (found == object.end() || !found->is_string()) return {};
  return found->get<std::string>();
}

Json Envelope(const std::string& requestId, Json inner) {
  return Json{{"type", "control_response"},
              {"response", Json{{"subtype", "success"},
                                {"request_id", requestId},
                                {"response", std::move(inner)}}}};
}

}  // namespace

bool ParsePermissionRequest(const Json& record, PermissionRequest* out) {
  auto request = record.find("request");
  if (request == record.end() || !request->is_object()) return false;
  if (StringField(*request, "subtype") != "can_use_tool") return false;

  out->requestId = StringField(record, "request_id");
  out->toolName = StringField(*request, "tool_name");
  out->displayName = StringField(*request, "display_name");
  out->description = StringField(*request, "description");
  out->toolUseId = StringField(*request, "tool_use_id");
  out->agentId = StringField(*request, "agent_id");
  out->decisionReasonType = StringField(*request, "decision_reason_type");
  out->blockedPath = StringField(*request, "blocked_path");
  auto interaction = request->find("requires_user_interaction");
  out->requiresUserInteraction = interaction != request->end() &&
                                 interaction->is_boolean() &&
                                 interaction->get<bool>();
  auto input = request->find("input");
  out->input = input != request->end() ? *input : Json::object();
  auto suggestions = request->find("permission_suggestions");
  out->suggestions = suggestions != request->end() ? *suggestions : Json();
  return true;
}

bool ParseInitializeResponse(const Json& record, InitializeInfo* out) {
  if (StringField(record, "type") != "control_response") return false;
  auto outer = record.find("response");
  if (outer == record.end() || !outer->is_object()) return false;
  if (StringField(*outer, "subtype") != "success") return false;
  // Two nestings of the same word, and they are not the same object: the outer
  // one is the envelope every control_response has, the inner one is what the
  // initialize handshake in particular answered with.
  auto inner = outer->find("response");
  if (inner == outer->end() || !inner->is_object()) return false;

  InitializeInfo info;
  info.requestId = StringField(*outer, "request_id");
  info.permissionMode = StringField(*inner, "current_permission_mode");
  // The mode decides whether this was an answer at all: it is the one field
  // the handshake always carries.  The account is read after that test and not
  // as part of it -- a response without it is still a response.
  if (info.permissionMode.empty()) return false;
  auto account = inner->find("account");
  if (account != inner->end() && account->is_object()) {
    info.accountEmail = StringField(*account, "email");
    info.subscriptionType = StringField(*account, "subscriptionType");
    info.apiProvider = StringField(*account, "apiProvider");
  }
  // An entry without a name is dropped rather than kept empty: the name is the
  // only part of it that can be typed, so a nameless one is a row in a list
  // that cannot be chosen.
  auto commands = inner->find("commands");
  if (commands != inner->end() && commands->is_array()) {
    for (const Json& entry : *commands) {
      if (!entry.is_object()) continue;
      SlashCommand command;
      command.name = StringField(entry, "name");
      if (command.name.empty()) continue;
      command.description = StringField(entry, "description");
      command.argumentHint = StringField(entry, "argumentHint");
      auto aliases = entry.find("aliases");
      if (aliases != entry.end() && aliases->is_array()) {
        for (const Json& alias : *aliases) {
          if (alias.is_string()) command.aliases.push_back(alias.get<std::string>());
        }
      }
      info.commands.push_back(std::move(command));
    }
  }
  // Nameless the same way: without a value there is nothing to send.
  auto models = inner->find("models");
  if (models != inner->end() && models->is_array()) {
    for (const Json& entry : *models) {
      if (!entry.is_object()) continue;
      ModelEntry model;
      model.value = StringField(entry, "value");
      if (model.value.empty()) continue;
      model.displayName = StringField(entry, "displayName");
      model.description = StringField(entry, "description");
      model.resolvedModel = StringField(entry, "resolvedModel");
      auto levels = entry.find("supportedEffortLevels");
      if (levels != entry.end() && levels->is_array()) {
        for (const Json& level : *levels) {
          if (level.is_string()) {
            model.supportedEffortLevels.push_back(level.get<std::string>());
          }
        }
      }
      info.models.push_back(std::move(model));
    }
  }
  *out = info;
  return true;
}

Json MakeInitialize(const std::string& requestId) {
  return Json{{"type", "control_request"},
              {"request_id", requestId},
              {"request", Json{{"subtype", "initialize"},
                               {"hooks", Json::object()}}}};
}

Json MakeSetPermissionMode(const std::string& requestId,
                           const std::string& mode) {
  return Json{{"type", "control_request"},
              {"request_id", requestId},
              {"request", Json{{"subtype", "set_permission_mode"},
                               {"mode", mode}}}};
}

Json MakeSetModel(const std::string& requestId, const std::string& model) {
  return Json{{"type", "control_request"},
              {"request_id", requestId},
              {"request", Json{{"subtype", "set_model"},
                               {"model", model == "default" ? Json()
                                                            : Json(model)}}}};
}

Json MakeSetEffort(const std::string& requestId, const std::string& level) {
  return Json{{"type", "control_request"},
              {"request_id", requestId},
              {"request",
               Json{{"subtype", "apply_flag_settings"},
                    {"settings", Json{{"effortLevel", level.empty()
                                                          ? Json()
                                                          : Json(level)}}}}}};
}

std::vector<std::wstring> OptionValues(
    const std::vector<std::wstring>& extraArgs, const std::wstring& longName,
    const std::wstring& shortName) {
  std::vector<std::wstring> values;
  for (size_t i = 0; i < extraArgs.size(); ++i) {
    const std::wstring& argument = extraArgs[i];
    if (argument == longName || (!shortName.empty() && argument == shortName)) {
      values.push_back(i + 1 < extraArgs.size() ? extraArgs[++i]
                                                : std::wstring());
    } else if (argument.size() > longName.size() &&
               argument.compare(0, longName.size(), longName) == 0 &&
               argument[longName.size()] == L'=') {
      values.push_back(argument.substr(longName.size() + 1));
    } else if (!shortName.empty() && argument.size() > shortName.size() &&
               argument.compare(0, shortName.size(), shortName) == 0) {
      // Glued "-rVALUE"; "-r=VALUE" lands here too and keeps its "=",
      // because that is the value the CLI then looks for.
      values.push_back(argument.substr(shortName.size()));
    }
  }
  return values;
}

bool ResumesConversation(const std::vector<std::wstring>& extraArgs) {
  return !OptionValues(extraArgs, L"--resume", L"-r").empty() ||
         !OptionValues(extraArgs, L"--continue", L"-c").empty();
}

namespace {

// The last one wins, for the same reason the CLI's own parser takes it: two
// --resume on one command line is somebody correcting themselves.  A bare one
// at the end of the line has nothing to give and does not erase the one
// before it.
std::wstring LastValue(const std::vector<std::wstring>& values) {
  for (auto it = values.rbegin(); it != values.rend(); ++it) {
    if (!it->empty()) return *it;
  }
  return std::wstring();
}

}  // namespace

std::wstring ResumedConversation(const std::vector<std::wstring>& extraArgs) {
  // --continue has no value to give; it leaves this empty, which is the
  // honest answer.
  return LastValue(OptionValues(extraArgs, L"--resume", L"-r"));
}

std::string StartupEffort(const std::vector<std::wstring>& extraArgs) {
  // The stages are ASCII words; anything else the CLI itself refuses.
  std::string level;
  for (wchar_t c : LastValue(OptionValues(extraArgs, L"--effort")))
    level += static_cast<char>(c);
  return level;
}

bool ParseSetPermissionModeResponse(const Json& record, std::string* requestId,
                                    std::string* mode) {
  if (StringField(record, "type") != "control_response") return false;
  auto outer = record.find("response");
  if (outer == record.end() || !outer->is_object()) return false;
  if (StringField(*outer, "subtype") != "success") return false;
  *requestId = StringField(*outer, "request_id");
  // The body echoes the mode for a headless host and is {} for others; both
  // are success.  Nested under `response` like the initialize answer.
  mode->clear();
  auto inner = outer->find("response");
  if (inner != outer->end() && inner->is_object()) {
    *mode = StringField(*inner, "mode");
  }
  return true;
}

std::string NextPermissionMode(const std::string& current) {
  if (current == "default") return "acceptEdits";
  if (current == "acceptEdits") return "plan";
  if (current == "plan") return "auto";
  if (current == "auto") return "default";
  // Off-cycle -- bypassPermissions, dontAsk, unknown, or not yet known.
  return "acceptEdits";
}

bool ParsePermissionModeReport(const Json& record, std::string* mode) {
  if (StringField(record, "type") != "system") return false;
  const std::string subtype = StringField(record, "subtype");
  if (subtype != "status" && subtype != "init") return false;
  const std::string said = StringField(record, "permissionMode");
  if (said.empty()) return false;
  *mode = said;
  return true;
}

const char kModeRequestPrefix[] = "mode-";

bool IsModeAnswer(const Json& record) {
  if (StringField(record, "type") != "control_response") return false;
  auto outer = record.find("response");
  if (outer == record.end() || !outer->is_object()) return false;
  return StringField(*outer, "request_id").rfind(kModeRequestPrefix, 0) == 0;
}

void PermissionModeTracker::Seed(const std::string& mode) {
  current_ = mode;
  reported_ = mode;
}

void PermissionModeTracker::SetReported(const std::string& mode) {
  if (mode == "plan" && !reported_.empty() && reported_ != "plan") {
    beforePlan_ = reported_;
  }
  reported_ = mode;
}

void PermissionModeTracker::Requested(const std::string& requestId,
                                      const std::string& mode) {
  pendingId_ = requestId;
  asked_[requestId] = mode;
  current_ = mode;
}

void PermissionModeTracker::Report(const std::string& mode) {
  if (mode.empty()) return;
  SetReported(mode);
  if (pendingId_.empty()) current_ = mode;
}

void PermissionModeTracker::Observe(const Json& record) {
  std::string mode;
  if (ParsePermissionModeReport(record, &mode)) {
    Report(mode);
    return;
  }
  if (StringField(record, "type") != "control_response") return;

  InitializeInfo info;
  if (ParseInitializeResponse(record, &info)) {
    Report(info.permissionMode);
    return;
  }

  auto outer = record.find("response");
  if (outer == record.end() || !outer->is_object()) return;
  const std::string id = StringField(*outer, "request_id");
  if (id.rfind(kModeRequestPrefix, 0) != 0) return;
  const std::string subtype = StringField(*outer, "subtype");
  if (subtype != "success" && subtype != "error") return;

  std::string echoed;
  std::string ignore;
  if (subtype == "success" &&
      ParseSetPermissionModeResponse(record, &ignore, &echoed) &&
      !echoed.empty()) {
    SetReported(echoed);
  }
  auto asked = asked_.find(id);
  if (asked != asked_.end()) {
    if (subtype == "error" &&
        std::find(refused_.begin(), refused_.end(), asked->second) ==
            refused_.end()) {
      refused_.push_back(asked->second);
    }
    asked_.erase(asked);
  }
  if (id != pendingId_) return;
  pendingId_.clear();
  // A success acked with {} leaves the requested mode standing -- nothing
  // better has been said.  Everything else takes the CLI's last word.
  if (subtype == "error" || !echoed.empty()) current_ = reported_;
}

Json MakeInterrupt(const std::string& requestId) {
  return Json{{"type", "control_request"},
              {"request_id", requestId},
              {"request", Json{{"subtype", "interrupt"},
                               {"reason", "interrupt"}}}};
}

Json MakeStopTask(const std::string& requestId, const std::string& taskId) {
  return Json{{"type", "control_request"},
              {"request_id", requestId},
              {"request", Json{{"subtype", "stop_task"},
                               {"task_id", taskId}}}};
}

Json MakeBackgroundTasks(const std::string& requestId) {
  return Json{{"type", "control_request"},
              {"request_id", requestId},
              {"request", Json{{"subtype", "background_tasks"}}}};
}

std::vector<std::string> ShellTaskIds(const Json& record) {
  std::vector<std::string> ids;
  if (StringField(record, "type") != "system" ||
      StringField(record, "subtype") != "background_tasks_changed") {
    return ids;
  }
  auto tasks = record.find("tasks");
  if (tasks == record.end() || !tasks->is_array()) return ids;
  for (const Json& task : *tasks) {
    if (!task.is_object() || StringField(task, "task_type") != "local_bash") {
      continue;
    }
    const std::string id = StringField(task, "task_id");
    if (!id.empty()) ids.push_back(id);
  }
  return ids;
}

const char kBackgroundRequestPrefix[] = "bg-";

bool IsBackgroundAnswer(const Json& record) {
  if (StringField(record, "type") != "control_response") return false;
  auto outer = record.find("response");
  if (outer == record.end() || !outer->is_object()) return false;
  return StringField(*outer, "request_id").rfind(kBackgroundRequestPrefix, 0) ==
         0;
}

bool IsMovedToBackground(const Json& record) {
  if (StringField(record, "type") != "system" ||
      StringField(record, "subtype") != "task_updated") {
    return false;
  }
  auto patch = record.find("patch");
  if (patch == record.end() || !patch->is_object()) return false;
  auto backgrounded = patch->find("is_backgrounded");
  return backgrounded != patch->end() && backgrounded->is_boolean() &&
         backgrounded->get<bool>();
}

Json MakeGetUsage(const std::string& requestId) {
  return Json{{"type", "control_request"},
              {"request_id", requestId},
              {"request", Json{{"subtype", "get_usage"},
                               {"skip_behaviors", true}}}};
}

const char kUsageRequestPrefix[] = "usage-";

bool ParseUsageResponse(const Json& record, agent::RateLimitChanged* out) {
  if (StringField(record, "type") != "control_response") return false;
  auto outer = record.find("response");
  if (outer == record.end() || !outer->is_object()) return false;
  if (StringField(*outer, "request_id").rfind(kUsageRequestPrefix, 0) != 0 ||
      StringField(*outer, "subtype") != "success") {
    return false;
  }
  auto inner = outer->find("response");
  if (inner == outer->end() || !inner->is_object()) return false;
  auto limits = inner->find("rate_limits");
  if (limits == inner->end() || !limits->is_object()) return false;

  agent::RateLimitChanged result;
  const struct {
    const char* name;
    int minutes;
  } kWindows[] = {{"five_hour", 300}, {"seven_day", 7 * 24 * 60}};
  for (const auto& known : kWindows) {
    auto window = limits->find(known.name);
    if (window == limits->end() || !window->is_object()) continue;
    auto used = window->find("utilization");
    if (used == window->end() || !used->is_number()) continue;
    agent::LimitWindow entry;
    entry.minutes = known.minutes;
    entry.used = used->get<double>() / 100.0;
    entry.resetsAt = UnixSeconds(StringField(*window, "resets_at"));
    if (entry.used >= 1) result.state = agent::LimitState::Exhausted;
    result.windows.push_back(entry);
  }
  if (result.windows.empty()) return false;
  *out = std::move(result);
  return true;
}

bool IsUsageResponse(const Json& record) {
  if (StringField(record, "type") != "control_response") return false;
  auto outer = record.find("response");
  return outer != record.end() && outer->is_object() &&
         StringField(*outer, "request_id").rfind(kUsageRequestPrefix, 0) == 0;
}

Json MakeAllow(const PermissionRequest& request, const Json& updatedInput,
               const Json& updatedPermissions) {
  Json body{{"behavior", "allow"},
            {"updatedInput",
             updatedInput.is_null() ? request.input : updatedInput}};
  if (!updatedPermissions.is_null()) {
    body["updatedPermissions"] = updatedPermissions;
  }
  return Envelope(request.requestId, body);
}

Json SessionPermissions(const Json& suggestions) {
  if (!suggestions.is_array()) return nullptr;
  Json kept = Json::array();
  for (const Json& suggestion : suggestions) {
    if (!suggestion.is_object()) continue;
    Json each = suggestion;
    each["destination"] = "session";
    kept.push_back(std::move(each));
  }
  if (kept.empty()) return nullptr;
  return kept;
}

Json ProjectPermissions(const Json& suggestions) {
  if (!suggestions.is_array()) return nullptr;
  bool local = false;
  for (const Json& suggestion : suggestions) {
    if (!suggestion.is_object()) continue;
    const std::string destination = StringField(suggestion, "destination");
    if (destination == "localSettings") {
      local = true;
    } else if (destination != "session") {
      return nullptr;
    }
  }
  if (!local) return nullptr;
  return suggestions;
}

Json MakeDeny(const PermissionRequest& request, const std::string& message,
              bool interrupt) {
  Json body{{"behavior", "deny"}, {"message", message}};
  if (interrupt) body["interrupt"] = true;
  return Envelope(request.requestId, body);
}

bool ParseElicitation(const Json& record, ElicitationRequest* out) {
  auto request = record.find("request");
  if (request == record.end() || !request->is_object()) return false;
  if (StringField(*request, "subtype") != "elicitation") return false;

  ElicitationRequest parsed;
  parsed.requestId = StringField(record, "request_id");
  parsed.agentId = StringField(*request, "agent_id");
  parsed.form.server = StringField(*request, "mcp_server_name");
  parsed.form.message = StringField(*request, "message");
  const std::string mode = StringField(*request, "mode");
  if (mode == "url") {
    parsed.form.url = StringField(*request, "url");
    if (parsed.form.url.empty()) return false;
  } else if (mode == "form") {
    static const Json null;
    auto schema = request->find("requested_schema");
    auto order = request->find("property_order");
    parsed.form.fields = ReadFormFieldsInOrder(
        schema != request->end() ? *schema : null,
        order != request->end() ? *order : null);
  } else {
    return false;
  }
  *out = std::move(parsed);
  return true;
}

Json MakeElicitationAnswer(const std::string& requestId,
                           const agent::FormRequest& form,
                           const agent::FormAnswer& answer) {
  switch (answer.action) {
    case agent::FormAnswer::Action::Accept: {
      Json body{{"action", "accept"}};
      if (form.url.empty()) {
        body["content"] = FormContent(form.fields, answer.values);
      }
      return Envelope(requestId, std::move(body));
    }
    case agent::FormAnswer::Action::Decline:
      return Envelope(requestId, Json{{"action", "decline"}});
    case agent::FormAnswer::Action::Cancel:
      break;
  }
  return Envelope(requestId, Json{{"action", "cancel"}});
}

Json MakeError(const std::string& requestId, const std::string& message) {
  return Json{{"type", "control_response"},
              {"response", Json{{"subtype", "error"},
                                {"request_id", requestId},
                                {"error", message}}}};
}

void PendingPrompts::Sent(const std::string& utf8Text) {
  const size_t first = utf8Text.find_first_not_of(" \t\r\n");
  if (first != std::string::npos && utf8Text[first] != '/') ++count_;
}

void PendingPrompts::Observe(const Json& record) {
  // "[Request interrupted by user]" is a `user` record too, but not a replay.
  if (count_ == 0 || !record.is_object()) return;
  auto type = record.find("type");
  auto replay = record.find("isReplay");
  if (type == record.end() || *type != "user" || replay == record.end() ||
      *replay != true) {
    return;
  }
  // The echo of a set_model is a replay too, and no prompt of ours.
  // Counting it would end the turn with the prompt still waiting behind it.
  auto message = record.find("message");
  if (message != record.end() && message->is_object()) {
    auto content = message->find("content");
    if (content != message->end() && content->is_string() &&
        content->get<std::string>().rfind("<local-command-stdout>", 0) == 0) {
      return;
    }
  }
  --count_;
}

}  // namespace proto
