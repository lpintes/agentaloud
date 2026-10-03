#include "proto/claude/events.h"

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
  if (subtype == "thinking_tokens") return EventKind::SystemThinkingTokens;
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

bool ParseUsage(const Json& record, const std::string& model, Usage* out) {
  if (StringField(record, "type") != "result") return false;

  Usage usage;
  bool anything = false;
  auto billed = record.find("total_cost_usd");
  if (billed != record.end() && billed->is_number()) {
    usage.billedUsd = billed->get<double>();
    anything = true;
  }

  auto models = record.find("modelUsage");
  if (models != record.end() && models->is_object()) {
    // Summed over models, not taken from one: a session may switch models,
    // and then there are two entries and neither is the whole of it.
    const std::pair<const char*, long long*> counts[] = {
        {"inputTokens", &usage.inputTokens},
        {"outputTokens", &usage.outputTokens},
        {"cacheReadInputTokens", &usage.cacheReadTokens},
        {"cacheCreationInputTokens", &usage.cacheCreationTokens},
        {"thinkingTokens", &usage.thinkingTokens},
    };
    bool namedWindow = false;
    for (const auto& entry : models->items()) {
      const Json& spent = entry.value();
      if (!spent.is_object()) continue;
      anything = true;
      auto cost = spent.find("costUSD");
      if (cost != spent.end() && cost->is_number()) {
        usage.listUsd += cost->get<double>();
      }
      for (const auto& [name, into] : counts) {
        auto value = spent.find(name);
        if (value != spent.end() && value->is_number()) {
          *into += value->get<long long>();
        }
      }

      // Not summed, unlike everything else here: a window is a ceiling, not an
      // amount.  The named model's own is taken when it is there; failing
      // that, the largest, which with one model in the session is the same
      // number and with two is at least not a made-up one.
      auto window = spent.find("contextWindow");
      if (window != spent.end() && window->is_number()) {
        const long long size = window->get<long long>();
        if (!model.empty() && entry.key() == model) {
          usage.contextWindow = size;
          namedWindow = true;
        } else if (!namedWindow && size > usage.contextWindow) {
          usage.contextWindow = size;
        }
      }
    }
  }

  if (!anything) return false;
  *out = usage;
  return true;
}

bool ParseContextTokens(const Json& record, long long* out) {
  if (StringField(record, "type") != "assistant") return false;
  auto message = record.find("message");
  if (message == record.end() || !message->is_object()) return false;
  auto usage = message->find("usage");
  if (usage == message->end() || !usage->is_object()) return false;

  // The three ways context reaches the model.  Output is not among them: what
  // the model wrote counts against the window only once it has been sent back
  // in, and by then it is in one of these three.
  const char* const parts[] = {"input_tokens", "cache_read_input_tokens",
                               "cache_creation_input_tokens"};
  long long total = 0;
  bool anything = false;
  for (const char* name : parts) {
    auto value = usage->find(name);
    if (value != usage->end() && value->is_number()) {
      total += value->get<long long>();
      anything = true;
    }
  }
  if (!anything) return false;
  *out = total;
  return true;
}

bool ParseAnsweringModel(const Json& record, std::string* out) {
  if (StringField(record, "type") != "assistant") return false;
  auto parent = record.find("parent_tool_use_id");
  if (parent != record.end() && !parent->is_null()) return false;
  auto message = record.find("message");
  if (message == record.end() || !message->is_object()) return false;
  const std::string model = StringField(*message, "model");
  // "<synthetic>" is how the CLI marks a message it made up itself -- an API
  // error, an interrupted turn.  Any name in angle brackets is that, not a model.
  if (model.empty() || model.front() == '<') return false;
  *out = model;
  return true;
}

const char* KindName(EventKind kind) {
  switch (kind) {
    case EventKind::SystemInit: return "system/init";
    case EventKind::SystemPermissionDenied: return "system/permission_denied";
    case EventKind::SystemHook: return "system/hook";
    case EventKind::SystemThinkingTokens: return "system/thinking_tokens";
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
