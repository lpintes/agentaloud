#include "proto/codex/threads.h"

namespace proto::codex {

Json ThreadListParams(const std::string& cwdUtf8, const std::string& cursor,
                      int limit) {
  // updated_at, because the default is created_at: the list would then put a
  // conversation carried on yesterday below one begun the day after it.
  Json params = {
      {"cwd", cwdUtf8}, {"limit", limit}, {"sortKey", "updated_at"}};
  if (!cursor.empty()) params["cursor"] = cursor;
  return params;
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
