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

std::string OneLine(std::string_view text) {
  std::string flat;
  bool space = false;
  for (const char c : text) {
    if (c == ' ' || c == '\t' || c == '\r' || c == '\n') {
      space = !flat.empty();
      continue;
    }
    if (space) flat += ' ';
    space = false;
    flat += c;
  }
  return flat;
}

namespace {

std::string Scalar(const Json& value) {
  if (value.is_string()) return value.get<std::string>();
  if (value.is_null()) return {};
  return value.dump();
}

// A value that fits behind its key on the same line: a plain value, a list of
// plain values, or nothing at all.
bool Inline(const Json& value, std::string* text) {
  if (value.is_object()) {
    if (!value.empty()) return false;
    text->clear();
    return true;
  }
  if (!value.is_array()) {
    *text = Scalar(value);
    return true;
  }
  std::string joined;
  for (const Json& item : value) {
    if (item.is_object() || item.is_array()) return false;
    if (!joined.empty()) joined += ", ";
    joined += Scalar(item);
  }
  *text = std::move(joined);
  return true;
}

void AddLine(std::string* out, const std::string& line) {
  if (!out->empty()) out->push_back('\n');
  *out += line;
}

void AppendLines(const Json& value, const std::string& pad, std::string* out) {
  std::string text;
  if (Inline(value, &text)) {
    AddLine(out, pad + text);
    return;
  }
  if (value.is_object()) {
    for (auto it = value.begin(); it != value.end(); ++it) {
      if (Inline(it.value(), &text)) {
        AddLine(out, pad + it.key() + (text.empty() ? ":" : ": " + text));
      } else {
        AddLine(out, pad + it.key() + ":");
        AppendLines(it.value(), pad + "  ", out);
      }
    }
    return;
  }
  // Numbered rather than marked: a '-' in front of every item is read out as
  // a word per item, and the number says where in the list the reader is.
  int number = 0;
  for (const Json& item : value) {
    const std::string label = std::to_string(++number) + ".";
    if (Inline(item, &text)) {
      AddLine(out, pad + label + (text.empty() ? "" : " " + text));
    } else {
      AddLine(out, pad + label);
      AppendLines(item, pad + "  ", out);
    }
  }
}

}  // namespace

std::string ReadableJson(const Json& value) {
  std::string text;
  AppendLines(value, std::string(), &text);
  return text;
}

std::string ReadableField(const Json& value) {
  std::string text;
  if (Inline(value, &text)) return text;
  AppendLines(value, "  ", &text);
  return "\n" + text;
}

std::string ReadableIfJson(const std::string& text) {
  const size_t first = text.find_first_not_of(" \t\r\n");
  if (first == std::string::npos) return text;
  if (text[first] != '{' && text[first] != '[') return text;
  Json parsed = Json::parse(text, nullptr, false);
  if (parsed.is_discarded() || !parsed.is_structured()) return text;
  return ReadableJson(parsed);
}

}  // namespace proto
