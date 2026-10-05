#include "settings.h"

#include "arguments.h"
#include "model/utf.h"

namespace app {

namespace {

std::wstring Wide(std::string_view text) { return model::Utf16FromUtf8(text); }

std::string_view Trim(std::string_view text) {
  const auto space = [](char ch) { return ch == ' ' || ch == '\t' || ch == '\r'; };
  while (!text.empty() && space(text.front())) text.remove_prefix(1);
  while (!text.empty() && space(text.back())) text.remove_suffix(1);
  return text;
}

std::wstring Joined(const std::vector<std::string>& words) {
  std::wstring out;
  for (const std::string& word : words) {
    if (!out.empty()) out += L", ";
    out += Wide(word);
  }
  return out;
}

}  // namespace

void Settings::Complain(int line, const std::wstring& text) {
  if (error_.empty()) error_ = L"riadok " + std::to_wstring(line) + L": " + text;
}

Settings Settings::Parse(std::string_view text) {
  Settings settings;
  constexpr std::string_view kBom = "\xef\xbb\xbf";
  if (text.substr(0, kBom.size()) == kBom) text.remove_prefix(kBom.size());
  int number = 0;
  while (!text.empty()) {
    ++number;
    const size_t end = text.find('\n');
    const std::string_view raw = text.substr(0, end);
    text.remove_prefix(end == std::string_view::npos ? text.size() : end + 1);
    const std::string_view line = Trim(raw);
    if (line.empty() || line.front() == '#') continue;
    const size_t equals = line.find('=');
    const std::string key(Trim(line.substr(0, equals == std::string_view::npos
                                                   ? line.size()
                                                   : equals)));
    if (equals == std::string_view::npos || key.empty()) {
      settings.Complain(number, L"očakáva sa kľúč=hodnota, je tam „" +
                                    Wide(line) + L"“");
      continue;
    }
    // Twice is refused rather than "the last one wins": a reader who adds a
    // line at the end and does not see the old one further up would get
    // whichever one this rule happens to prefer, and not know there was a
    // choice.
    const auto known = settings.entries_.find(key);
    if (known != settings.entries_.end()) {
      settings.Complain(number, L"kľúč " + Wide(key) + L" je už na riadku " +
                                    std::to_wstring(known->second.line));
      continue;
    }
    settings.entries_[key] = {std::string(Trim(line.substr(equals + 1))), number};
  }
  return settings;
}

std::string Settings::Get(const std::string& key) const {
  const auto found = entries_.find(key);
  return found == entries_.end() ? std::string() : found->second.value;
}

void Settings::Check(const std::map<std::string, agent::Capabilities>& backends) {
  std::vector<std::string> names;
  for (const auto& [name, capabilities] : backends) names.push_back(name);
  for (const auto& [key, entry] : entries_) {
    // An empty value is a template line (see Get) and has nothing to check --
    // but its key does: a misspelt key left empty is still a misspelt key, and
    // would stay one the day a value is typed after it.
    const bool empty = entry.value.empty();
    if (key == kBackendKey) {
      if (!empty && backends.count(entry.value) == 0) {
        Complain(entry.line, L"neznámy backend " + Wide(entry.value) +
                                 L"; známe sú: " + Joined(names));
      }
      continue;
    }
    const size_t dot = key.find('.');
    const auto backend = dot == std::string::npos
                             ? backends.end()
                             : backends.find(key.substr(0, dot));
    const std::string name = dot == std::string::npos ? "" : key.substr(dot + 1);
    if (backend == backends.end() ||
        (name != kPermissionModeKey && name != kModelKey)) {
      Complain(entry.line, L"neznámy kľúč " + Wide(key));
      continue;
    }
    // The model is not checked, for the reason --model is not: the list is
    // the CLI's and changes with every release.
    if (!empty && name == kPermissionModeKey &&
        agent::FindMode(backend->second, entry.value) == nullptr) {
      std::vector<std::string> valid;
      for (const agent::Mode& mode : backend->second.modes) {
        valid.push_back(mode.id);
      }
      Complain(entry.line, L"režim " + Wide(entry.value) + L" " +
                               Wide(backend->second.agentName) +
                               L" nepozná; platné sú: " + Joined(valid));
    }
  }
}

void ApplySettings(Arguments* arguments, const Settings& settings) {
  if (arguments->backend.empty()) arguments->backend = settings.Get(kBackendKey);
  const std::string backend =
      arguments->backend.empty() ? kDefaultBackend : arguments->backend;
  if (arguments->permissionMode.empty()) {
    arguments->permissionMode =
        Wide(settings.Get(backend + "." + kPermissionModeKey));
  }
  if (arguments->model.empty()) {
    arguments->model = Wide(settings.Get(backend + "." + kModelKey));
  }
}

}  // namespace app
