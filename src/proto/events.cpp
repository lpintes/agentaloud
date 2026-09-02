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

bool ParseRateLimit(const Json& record, RateLimit* out) {
  if (StringField(record, "type") != "rate_limit_event") return false;
  auto info = record.find("rate_limit_info");
  if (info == record.end() || !info->is_object()) return false;

  RateLimit limit;
  limit.status = StringField(*info, "status");
  limit.limitType = StringField(*info, "rateLimitType");
  if (info->contains("utilization") && (*info)["utilization"].is_number()) {
    limit.utilization = (*info)["utilization"].get<double>();
  }
  if (info->contains("resetsAt") && (*info)["resetsAt"].is_number()) {
    limit.resetsAt = (*info)["resetsAt"].get<long long>();
  }

  auto windows = info->find("unifiedWindows");
  if (windows != info->end() && windows->is_object()) {
    const std::pair<const char*, double*> wanted[] = {
        {"five_hour", &limit.fiveHourUtilization},
        {"seven_day", &limit.sevenDayUtilization},
    };
    for (const auto& [name, into] : wanted) {
      auto window = windows->find(name);
      if (window == windows->end() || !window->is_object()) continue;
      auto value = window->find("utilization");
      if (value != window->end() && value->is_number()) {
        *into = value->get<double>();
      }
      // The window's own resetsAt is more precise than the top-level one when
      // both are there, and the top level may not be there at all.
      auto resets = window->find("resetsAt");
      if (limit.resetsAt == 0 && resets != window->end() &&
          resets->is_number()) {
        limit.resetsAt = resets->get<long long>();
      }
    }
  }

  if (limit.status.empty()) return false;
  *out = limit;
  return true;
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
