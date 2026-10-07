#include "session_names.h"

#include <map>

namespace app {

std::vector<int> SessionOrdinals(const std::vector<std::wstring>& keys) {
  std::map<std::wstring, int> total;
  for (const auto& key : keys) ++total[key];
  std::map<std::wstring, int> seen;
  std::vector<int> ordinals;
  ordinals.reserve(keys.size());
  for (const auto& key : keys) {
    const int number = ++seen[key];
    ordinals.push_back(total[key] > 1 ? number : 0);
  }
  return ordinals;
}

}  // namespace app
