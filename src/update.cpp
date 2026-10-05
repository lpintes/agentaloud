#include "update.h"

#include <cwctype>

namespace update {

namespace {

bool IsHex(char ch) {
  return (ch >= '0' && ch <= '9') || (ch >= 'a' && ch <= 'f') ||
         (ch >= 'A' && ch <= 'F');
}

// Path components, empty ones dropped -- "a//b" and "a/b/" are a/b.
std::vector<std::wstring> Components(std::wstring_view path) {
  std::vector<std::wstring> parts;
  std::wstring part;
  for (wchar_t ch : path) {
    if (ch == L'/' || ch == L'\\') {
      if (!part.empty()) parts.push_back(std::move(part));
      part.clear();
    } else {
      part += ch;
    }
  }
  if (!part.empty()) parts.push_back(std::move(part));
  return parts;
}

// Windows names are case-insensitive, and so is the test for config/ and the
// EXE: "Config\settings.txt" is the same file.
bool SameName(std::wstring_view left, std::wstring_view right) {
  if (left.size() != right.size()) return false;
  for (size_t i = 0; i < left.size(); ++i) {
    if (std::towlower(left[i]) != std::towlower(right[i])) return false;
  }
  return true;
}

}  // namespace

std::optional<std::wstring> TagFromLocation(std::wstring_view location) {
  constexpr std::wstring_view kMarker = L"/releases/tag/";
  const size_t at = location.find(kMarker);
  if (at == std::wstring_view::npos) return std::nullopt;
  std::wstring_view tag = location.substr(at + kMarker.size());
  tag = tag.substr(0, tag.find_first_of(L"?#/"));
  // Only a tag that is a release number.  Whatever else might sit there is not
  // something this project published, so it is not offered.
  if (!version::Parse(tag)) return std::nullopt;
  return std::wstring(tag);
}

std::optional<std::string> HashFor(std::string_view sums, std::string_view file) {
  while (!sums.empty()) {
    const size_t end = sums.find('\n');
    std::string_view line = sums.substr(0, end);
    sums = end == std::string_view::npos ? std::string_view{} : sums.substr(end + 1);
    if (!line.empty() && line.back() == '\r') line.remove_suffix(1);
    if (line.size() < 66) continue;
    const std::string_view hash = line.substr(0, 64);
    std::string_view name = line.substr(64);
    // Two spaces in text mode, space and '*' in binary mode.
    if (name.starts_with("  ") || name.starts_with(" *")) {
      name.remove_prefix(2);
    } else {
      continue;
    }
    if (name != file) continue;
    std::string result;
    for (char ch : hash) {
      if (!IsHex(ch)) return std::nullopt;
      result += ch >= 'A' && ch <= 'F' ? static_cast<char>(ch - 'A' + 'a') : ch;
    }
    return result;
  }
  return std::nullopt;
}

bool ShouldOffer(std::optional<version::Number> current,
                 std::wstring_view latestTag, std::wstring_view skipped) {
  const std::optional<version::Number> latest = version::Parse(latestTag);
  if (!current || !latest || *latest <= *current) return false;
  const std::optional<version::Number> declined = version::Parse(skipped);
  return !declined || *latest != *declined;
}

bool CheckDue(std::wstring_view lastCheck, std::wstring_view today) {
  return lastCheck != today;
}

Plan PlanReplace(const std::vector<std::wstring>& entries,
                 std::wstring_view exeName) {
  Plan plan;
  std::vector<std::vector<std::wstring>> split;
  for (const std::wstring& entry : entries) {
    // Checked on the raw text, before splitting can hide anything: a leading
    // separator is a rooted path, and ':' is a drive or a stream.
    if (entry.empty() || entry.front() == L'/' || entry.front() == L'\\' ||
        entry.find(L':') != std::wstring::npos) {
      plan.error = L"balík obsahuje neplatnú cestu " + entry;
      return plan;
    }
    std::vector<std::wstring> parts = Components(entry);
    for (const std::wstring& part : parts) {
      if (part == L"..") {
        plan.error = L"balík obsahuje neplatnú cestu " + entry;
        return plan;
      }
    }
    split.push_back(std::move(parts));
  }

  bool oneTop = !split.empty();
  for (const auto& parts : split) {
    if (parts.size() < 2 || parts[0] != split.front()[0]) oneTop = false;
  }

  bool hasExe = false;
  for (size_t i = 0; i < split.size(); ++i) {
    const size_t skip = oneTop ? 1 : 0;
    std::wstring to;
    for (size_t k = skip; k < split[i].size(); ++k) {
      if (!to.empty()) to += L'\\';
      to += split[i][k];
    }
    if (to.empty()) continue;
    // The reader's own folder, never touched -- see the header.
    if (SameName(split[i][skip], L"config")) continue;
    if (split[i].size() == skip + 1 && SameName(to, exeName)) hasExe = true;
    plan.files.push_back({entries[i], to});
  }
  if (!hasExe) {
    plan.files.clear();
    plan.error = L"v balíku chýba " + std::wstring(exeName);
  }
  return plan;
}

}  // namespace update
