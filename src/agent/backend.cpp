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

const Mode* FindMode(const Capabilities& capabilities, const std::string& id) {
  for (const Mode& mode : capabilities.modes) {
    if (mode.id == id) return &mode;
  }
  return nullptr;
}

}  // namespace agent
