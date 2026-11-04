#include "recent_folders.h"

#include <algorithm>
#include <map>

namespace app {

std::vector<RecentFolder> MergeRecentFolders(
    const std::vector<FolderReport>& reports) {
  std::vector<RecentFolder> merged;
  std::map<std::wstring, size_t> index;
  for (const auto& report : reports) {
    const auto found = index.find(report.key);
    if (found == index.end()) {
      index.emplace(report.key, merged.size());
      merged.push_back({report.path, report.lastActive, report.backend});
      continue;
    }
    RecentFolder& folder = merged[found->second];
    if (report.lastActive > folder.lastActive)
      folder = {report.path, report.lastActive, report.backend};
  }
  std::stable_sort(merged.begin(), merged.end(),
                   [](const RecentFolder& a, const RecentFolder& b) {
                     return a.lastActive > b.lastActive;
                   });
  return merged;
}

}  // namespace app
