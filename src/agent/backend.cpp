#include "agent/backend.h"

namespace agent {

std::string NextMode(const Capabilities& capabilities,
                     const std::string& current) {
  if (current.empty()) return {};
  std::vector<const Mode*> cycle;
  for (const Mode& mode : capabilities.modes) {
    if (mode.inCycle) cycle.push_back(&mode);
  }
  if (cycle.empty()) return {};
  for (size_t i = 0; i < cycle.size(); ++i) {
    if (cycle[i]->id == current) return cycle[(i + 1) % cycle.size()]->id;
  }
  // Off the cycle.  Not the first mode: that is where one usually starts, so
  // stepping onto it from somewhere else would read as the key doing nothing.
  return cycle[cycle.size() > 1 ? 1 : 0]->id;
}

const Mode* FindMode(const Capabilities& capabilities, const std::string& id) {
  for (const Mode& mode : capabilities.modes) {
    if (mode.id == id) return &mode;
  }
  return nullptr;
}

}  // namespace agent
