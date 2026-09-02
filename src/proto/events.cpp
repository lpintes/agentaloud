#include "proto/events.h"

namespace proto {
namespace {

std::string StringField(const Json& record, const char* name) {
  auto found = record.find(name);
  if (found == record.end() || !found->is_string()) return {};
  return found->get<std::string>();
}

EventKind ClassifySystem(const std::string& subtype) {
  if (subtype == "init") return EventKind::SystemInit;
  if (subtype == "permission_denied") return EventKind::SystemPermissionDenied;
  if (subtype == "hook_started" || subtype == "hook_response") {
    return EventKind::SystemHook;
  }
  return EventKind::SystemOther;
}

}  // namespace

Event Classify(Json record) {
  Event event;
  event.subtype = StringField(record, "subtype");
  event.sessionId = StringField(record, "session_id");

  const std::string type = StringField(record, "type");
  if (type == "system") {
    event.kind = ClassifySystem(event.subtype);
  } else if (type == "assistant") {
    event.kind = EventKind::Assistant;
  } else if (type == "user") {
    event.kind = EventKind::User;
  } else if (type == "control_request") {
    event.kind = EventKind::ControlRequest;
  } else if (type == "control_response") {
    event.kind = EventKind::ControlResponse;
  } else if (type == "rate_limit_event") {
    event.kind = EventKind::RateLimit;
  } else if (type == "result") {
    event.kind = EventKind::Result;
  }
  event.raw = std::move(record);
  return event;
}

const char* KindName(EventKind kind) {
  switch (kind) {
    case EventKind::SystemInit: return "system/init";
    case EventKind::SystemPermissionDenied: return "system/permission_denied";
    case EventKind::SystemHook: return "system/hook";
    case EventKind::SystemOther: return "system";
    case EventKind::Assistant: return "assistant";
    case EventKind::User: return "user";
    case EventKind::ControlRequest: return "control_request";
    case EventKind::ControlResponse: return "control_response";
    case EventKind::RateLimit: return "rate_limit";
    case EventKind::Result: return "result";
    case EventKind::Unknown: break;
  }
  return "unknown";
}

}  // namespace proto
