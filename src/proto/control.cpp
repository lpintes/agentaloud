#include "proto/control.h"

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
  auto input = request->find("input");
  out->input = input != request->end() ? *input : Json::object();
  return true;
}

Json MakeInitialize(const std::string& requestId) {
  return Json{{"type", "control_request"},
              {"request_id", requestId},
              {"request", Json{{"subtype", "initialize"},
                               {"hooks", Json::object()}}}};
}

Json MakeAllow(const PermissionRequest& request, const Json& updatedInput) {
  return Envelope(request.requestId,
                  Json{{"behavior", "allow"},
                       {"updatedInput", updatedInput.is_null()
                                            ? request.input
                                            : updatedInput}});
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
