#include "proto/claude/control.h"

#include <algorithm>

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
  out->decisionReasonType = StringField(*request, "decision_reason_type");
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

void PermissionModeTracker::Requested(const std::string& requestId,
                                      const std::string& mode) {
  pendingId_ = requestId;
  asked_[requestId] = mode;
  current_ = mode;
}

void PermissionModeTracker::Report(const std::string& mode) {
  if (mode.empty()) return;
  reported_ = mode;
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
    reported_ = echoed;
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

Json MakeDeny(const PermissionRequest& request, const std::string& message) {
  return Envelope(request.requestId,
                  Json{{"behavior", "deny"}, {"message", message}});
}

Json MakeError(const std::string& requestId, const std::string& message) {
  return Json{{"type", "control_response"},
              {"response", Json{{"subtype", "error"},
                                {"request_id", requestId},
                                {"error", message}}}};
}

}  // namespace proto
