#include "proto/codex/steers.h"

#include <algorithm>

namespace proto::codex {

namespace {

std::string StringField(const Json& object, const char* name) {
  if (!object.is_object()) return {};
  auto found = object.find(name);
  if (found == object.end() || !found->is_string()) return {};
  return found->get<std::string>();
}

const Json& Field(const Json& object, const char* name) {
  static const Json null;
  if (!object.is_object()) return null;
  auto found = object.find(name);
  return found == object.end() ? null : *found;
}

}  // namespace

void Steers::Sent(const std::string& utf8Text) {
  waiting_.push_back(utf8Text);
}

void Steers::Observe(const Json& message) {
  if (waiting_.empty()) return;
  // item/started and item/completed come together; one of them is enough.
  if (StringField(message, "method") != "item/completed") return;
  const Json& item = Field(Field(message, "params"), "item");
  if (StringField(item, "type") != "userMessage") return;
  std::string text;
  const Json& content = Field(item, "content");
  if (content.is_array()) {
    for (const Json& part : content) {
      if (StringField(part, "type") == "text") text += StringField(part, "text");
    }
  }
  Forget(text);
}

bool Steers::Forget(const std::string& utf8Text) {
  auto found = std::find(waiting_.begin(), waiting_.end(), utf8Text);
  if (found == waiting_.end()) return false;
  waiting_.erase(found);
  return true;
}

std::vector<std::string> Steers::TakeDropped() {
  std::vector<std::string> dropped;
  dropped.swap(waiting_);
  return dropped;
}

}  // namespace proto::codex
