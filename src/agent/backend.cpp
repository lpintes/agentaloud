#include "agent/backend.h"

#include <algorithm>

namespace agent {

std::string NextMode(const Capabilities& capabilities,
                     const std::string& current,
                     const std::vector<std::string>& refused) {
  if (current.empty()) return {};
  std::vector<const Mode*> cycle;
  for (const Mode& mode : capabilities.modes) {
    if (mode.inCycle) cycle.push_back(&mode);
  }
  if (cycle.empty()) return {};
  auto open = [&](const Mode* mode) {
    return mode->id != current &&
           std::find(refused.begin(), refused.end(), mode->id) ==
               refused.end();
  };
  // Off the cycle, the walk starts on the second mode, not the first: that
  // is where one usually starts, so stepping onto it from somewhere else
  // would read as the key doing nothing.
  size_t start = cycle.size() > 1 ? 1 : 0;
  for (size_t i = 0; i < cycle.size(); ++i) {
    if (cycle[i]->id == current) start = i + 1;
  }
  for (size_t step = 0; step < cycle.size(); ++step) {
    const Mode* mode = cycle[(start + step) % cycle.size()];
    if (open(mode)) return mode->id;
  }
  return {};
}

const std::wstring* Program::Find(const std::wstring& name) const {
  auto lower = [](wchar_t c) {
    return c >= L'A' && c <= L'Z' ? static_cast<wchar_t>(c + (L'a' - L'A')) : c;
  };
  const std::wstring* found = nullptr;
  for (const auto& [key, value] : environment) {
    if (key.size() == name.size() &&
        std::equal(key.begin(), key.end(), name.begin(),
                   [&](wchar_t a, wchar_t b) { return lower(a) == lower(b); })) {
      found = &value;
    }
  }
  return found;
}

ConfigHome ResolveConfigHome(const Program& program,
                             const std::wstring& variable,
                             const std::wstring& profileFolder,
                             bool emptyIsUnset) {
  ConfigHome home;
  home.variable = variable;
  const std::wstring* given = program.Find(variable);
  std::wstring value;
  bool set = given != nullptr;
  if (set) {
    value = *given;
  } else if (const wchar_t* inherited = _wgetenv(variable.c_str())) {
    set = true;
    value = inherited;
  }
  if (set && (!value.empty() || !emptyIsUnset)) {
    home.path = value;
    home.fromVariable = true;
  } else if (const wchar_t* profile = _wgetenv(L"USERPROFILE");
             profile != nullptr && *profile) {
    home.path = std::wstring(profile) + L'\\' + profileFolder;
  }
  return home;
}

const Mode* FindMode(const Capabilities& capabilities, const std::string& id) {
  for (const Mode& mode : capabilities.modes) {
    if (mode.id == id) return &mode;
  }
  return nullptr;
}

long long ResetToSay(const RateLimitChanged& limit) {
  const LimitWindow* shortest = nullptr;
  const LimitWindow* spent = nullptr;  // the longest exhausted window
  for (const LimitWindow& window : limit.windows) {
    if (window.resetsAt <= 0) continue;
    if (shortest == nullptr || window.minutes < shortest->minutes) {
      shortest = &window;
    }
    if (window.used >= 1 && (spent == nullptr || window.minutes > spent->minutes)) {
      spent = &window;
    }
  }
  if (limit.state == LimitState::Exhausted) {
    // Claude names the window that refused; Codex only says that one did.
    if (limit.resetsAt > 0) return limit.resetsAt;
    if (spent != nullptr) return spent->resetsAt;
  }
  if (shortest != nullptr) return shortest->resetsAt;
  return limit.resetsAt;
}

const LimitWindow* LongResetToSay(const RateLimitChanged& limit) {
  const LimitWindow* longest = nullptr;
  for (const LimitWindow& window : limit.windows) {
    if (window.resetsAt <= 0 || window.minutes <= 0) continue;
    if (longest == nullptr || window.minutes > longest->minutes) {
      longest = &window;
    }
  }
  if (longest == nullptr || longest->resetsAt == ResetToSay(limit)) {
    return nullptr;
  }
  return longest;
}

}  // namespace agent
