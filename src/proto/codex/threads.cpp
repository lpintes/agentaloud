#include "proto/codex/threads.h"

#include <algorithm>
#include <filesystem>
#include <map>

namespace proto::codex {

Json ThreadListParams(const std::string& cwdUtf8, const std::string& cursor,
                      int limit) {
  // updated_at, because the default is created_at: the list would then put a
  // conversation carried on yesterday below one begun the day after it.
  Json params = {{"limit", limit}, {"sortKey", "updated_at"}};
  if (!cwdUtf8.empty()) params["cwd"] = cwdUtf8;
  if (!cursor.empty()) params["cursor"] = cursor;
  return params;
}

std::string AppendThreadFolders(const Json& result,
                                std::vector<agent::Project>* out) {
  if (!result.is_object()) return {};
  const auto data = result.find("data");
  if (data != result.end() && data->is_array()) {
    for (const Json& thread : *data) {
      if (!thread.is_object()) continue;
      const auto cwd = thread.find("cwd");
      if (cwd == thread.end() || !cwd->is_string()) continue;
      const std::string& utf8 = cwd->get_ref<const std::string&>();
      if (utf8.empty()) continue;
      agent::Project project;
      // UTF-8 to UTF-16 through filesystem::path, which does it without
      // windows.h -- and proto/ must not reach into model/ for its own.
      project.path =
          std::filesystem::path(std::u8string(utf8.begin(), utf8.end()))
              .wstring();
      const auto updated = thread.find("updatedAt");
      if (updated != thread.end() && updated->is_number_integer()) {
        project.lastActive = updated->get<long long>();
      }
      out->push_back(std::move(project));
    }
  }
  const auto next = result.find("nextCursor");
  if (next == result.end() || !next->is_string()) return {};
  return next->get<std::string>();
}

std::vector<agent::Project> FoldersOfThreads(
    std::vector<agent::Project> threads) {
  std::map<std::wstring, long long> newest;
  for (const agent::Project& thread : threads) {
    long long& when = newest[thread.path];
    when = std::max(when, thread.lastActive);
  }
  std::vector<agent::Project> folders;
  folders.reserve(newest.size());
  for (auto& [path, when] : newest) folders.push_back({path, when});
  return folders;
}

std::string AppendThreadPage(const Json& result,
                             std::vector<agent::Conversation>* out) {
  if (!result.is_object()) return {};
  const auto data = result.find("data");
  if (data != result.end() && data->is_array()) {
    for (const Json& thread : *data) {
      if (!thread.is_object()) continue;
      const auto id = thread.find("id");
      if (id == thread.end() || !id->is_string() ||
          id->get_ref<const std::string&>().empty()) {
        continue;
      }
      agent::Conversation conversation;
      // A thread id is a UUID: ASCII, so a copy and not a conversion.
      const std::string& ascii = id->get_ref<const std::string&>();
      conversation.id.assign(ascii.begin(), ascii.end());
      // The first message whole, as the server keeps it; `name` would be
      // the better label and is null for nearly every thread (measured, 82
      // of 83), so it is not looked at.
      const auto preview = thread.find("preview");
      if (preview != thread.end() && preview->is_string()) {
        conversation.firstPrompt =
            OneLine(preview->get_ref<const std::string&>());
      }
      const auto updated = thread.find("updatedAt");
      if (updated != thread.end() && updated->is_number_integer()) {
        conversation.lastActive = updated->get<long long>();
      }
      out->push_back(std::move(conversation));
    }
  }
  const auto next = result.find("nextCursor");
  if (next == result.end() || !next->is_string()) return {};
  return next->get<std::string>();
}

}  // namespace proto::codex
