#include "proto/jsonl.h"

namespace proto {

void LineAssembler::Feed(std::string_view bytes) {
  while (!bytes.empty()) {
    const size_t newline = bytes.find('\n');
    if (newline == std::string_view::npos) {
      pending_.append(bytes);
      return;
    }
    std::string_view line = bytes.substr(0, newline);
    bytes.remove_prefix(newline + 1);

    if (pending_.empty()) {
      // The common case: a whole record arrived in one read and never needs
      // to touch the pending buffer at all.
      if (!line.empty() && line.back() == '\r') line.remove_suffix(1);
      if (!line.empty() && onLine_) onLine_(line);
      continue;
    }
    pending_.append(line);
    if (!pending_.empty() && pending_.back() == '\r') pending_.pop_back();
    if (!pending_.empty() && onLine_) onLine_(pending_);
    pending_.clear();
  }
}

void LineAssembler::Flush() {
  if (pending_.empty()) return;
  if (pending_.back() == '\r') pending_.pop_back();
  if (!pending_.empty() && onLine_) onLine_(pending_);
  pending_.clear();
}

bool ParseLine(std::string_view line, Json* out, std::string* error) {
  Json parsed = Json::parse(line, nullptr, false);  // no throw
  if (parsed.is_discarded()) {
    if (error) *error = "not JSON";
    return false;
  }
  *out = std::move(parsed);
  return true;
}

}  // namespace proto
