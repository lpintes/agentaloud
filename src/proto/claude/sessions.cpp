#include "proto/claude/sessions.h"

#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <string_view>
#include <vector>

#include "proto/jsonl.h"

namespace proto {
namespace {

// Whole file at once.  The alternative -- a window on the head and another on
// the tail -- saves most of the bytes and costs a case that fails silently: a
// single tool result of half a megabyte is an ordinary record here, so a tail
// window can hold no complete line at all, and the session would then look
// timeless and sort last.  Reading it all is a few tens of milliseconds per
// project and cannot be wrong.
// One read into one buffer, and not istreambuf_iterator: that one goes through
// the stream buffer a character at a time.  Measured over this project's 27
// sessions (24 MB, warm cache): 171 ms against 59 ms with -O2, and 1.4 s with
// -O0.  It is spent before the window appears, so it is the one place here
// where the difference would be felt.
bool ReadWholeFile(const std::wstring& path, std::string* out) {
  std::ifstream file(std::filesystem::path(path),
                     std::ios::binary | std::ios::ate);
  if (!file) return false;
  const std::streamoff size = file.tellg();
  if (size < 0) return false;
  out->resize(static_cast<size_t>(size));
  file.seekg(0);
  file.read(out->data(), size);
  out->resize(static_cast<size_t>(file.gcount()));
  return true;
}

// Views into `text`, so this costs pointers and no copying.  Empty lines are
// dropped: a file ending in a newline would otherwise have a last line that
// parses as nothing.
std::vector<std::string_view> SplitLines(const std::string& text) {
  std::vector<std::string_view> lines;
  size_t start = 0;
  while (start <= text.size()) {
    size_t end = text.find('\n', start);
    if (end == std::string::npos) end = text.size();
    std::string_view line(text.data() + start, end - start);
    while (!line.empty() && (line.back() == '\r' || line.back() == ' ')) {
      line.remove_suffix(1);
    }
    if (!line.empty()) lines.push_back(line);
    if (end == text.size()) break;
    start = end + 1;
  }
  return lines;
}

// Whatever text a `user` record carries, in whichever of the two shapes the
// content came in -- a bare string (1278 of the records measured here) or an
// array with a `text` part in it.  Empty for a record that holds only tool
// results, which is most of them.
//
// The type is checked here and not left to the caller: an `assistant` record
// has text in exactly the same place, and read as a prompt it would put the
// answer into the transcript twice -- once in Claude's name and once in the
// reader's.  Which is what it did, until this line.
std::string UserText(const Json& record) {
  if (record.value("type", std::string()) != "user") return {};
  const auto message = record.find("message");
  if (message == record.end() || !message->is_object()) return {};
  const auto content = message->find("content");
  if (content == message->end()) return {};
  if (content->is_string()) return content->get<std::string>();
  if (!content->is_array()) return {};
  for (const Json& part : *content) {
    if (part.is_object() && part.value("type", std::string()) == "text") {
      return part.value("text", std::string());
    }
  }
  return {};
}

// Where the text starts, past the whitespace, or npos when there is none.
size_t FirstWord(const std::string& text) {
  return text.find_first_not_of(" \t\r\n");
}

// One line, whitespace collapsed.  What goes into a sentence read out in one
// breath -- a prompt of twenty lines there would bury everything after it.
std::string OneLine(const std::string& text) {
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

// The CLI's own nudge when a turn produced nothing visible.  Machinery, and
// named in model/transcript.h as such; it is here because this is the layer
// that knows what the CLI writes in the user's name.
constexpr std::string_view kNoOutputNudge =
    "[Your previous response had no visible output";
constexpr std::string_view kInterruptMark = "[Request interrupted by user";

std::wstring Widen(const std::string& ascii) {
  return std::wstring(ascii.begin(), ascii.end());
}

}  // namespace

std::wstring ProjectKey(const std::wstring& projectPath) {
  std::wstring path = projectPath;
  // "C:\vcs\x\" and "C:\vcs\x" are the same project and the CLI knows only the
  // second spelling; a trailing separator would add a dash and send the lookup
  // to a directory that does not exist.  The root itself keeps its slash --
  // "C:\" without it is not a path at all.
  while (path.size() > 3 && (path.back() == L'\\' || path.back() == L'/')) {
    path.pop_back();
  }
  std::wstring key;
  key.reserve(path.size());
  for (const wchar_t c : path) {
    const bool plain = (c >= L'A' && c <= L'Z') || (c >= L'a' && c <= L'z') ||
                       (c >= L'0' && c <= L'9');
    key += plain ? c : L'-';
  }
  return key;
}

std::wstring ProjectSessionDir(const std::wstring& projectPath) {
  std::wstring home;
  if (const wchar_t* configured = _wgetenv(L"CLAUDE_CONFIG_DIR");
      configured && *configured) {
    home = configured;
  } else if (const wchar_t* profile = _wgetenv(L"USERPROFILE");
             profile && *profile) {
    home = profile;
    home += L"\\.claude";
  } else {
    return {};
  }
  return home + L"\\projects\\" + ProjectKey(projectPath);
}

std::wstring SessionFilePath(const std::wstring& projectPath,
                             const std::wstring& id) {
  const std::wstring directory = ProjectSessionDir(projectPath);
  if (directory.empty() || id.empty()) return {};
  return directory + L"\\" + id + L".jsonl";
}

bool IsInterruptMark(const Json& record) {
  const std::string text = UserText(record);
  const size_t first = FirstWord(text);
  if (first == std::string::npos) return false;
  return text.compare(first, kInterruptMark.size(), kInterruptMark) == 0;
}

std::string HumanPromptText(const Json& record) {
  if (record.value("isMeta", false)) return {};
  const std::string text = UserText(record);
  const size_t first = FirstWord(text);
  if (first == std::string::npos) return {};
  if (text[first] == '<') return {};
  if (text.compare(first, kInterruptMark.size(), kInterruptMark) == 0) {
    return {};
  }
  if (text.compare(first, kNoOutputNudge.size(), kNoOutputNudge) == 0) {
    return {};
  }
  return text;
}

bool ReadSessionRecords(const std::wstring& path, std::vector<Json>* out) {
  std::string text;
  if (!ReadWholeFile(path, &text)) return false;
  out->clear();
  Json record;
  std::string error;
  for (const std::string_view line : SplitLines(text)) {
    // A line that does not parse is skipped rather than fatal: the file is
    // being appended to by another process while we read it, so the last line
    // of it can legitimately be half written.
    if (!ParseLine(line, &record, &error)) continue;
    const std::string kind = record.value("type", std::string());
    if (kind != "user" && kind != "assistant") continue;
    if (record.value("isSidechain", false)) continue;
    out->push_back(std::move(record));
  }
  return !out->empty();
}

bool ReadSessionSummary(const std::wstring& path, SessionSummary* out) {
  std::string text;
  if (!ReadWholeFile(path, &text)) return false;
  const std::vector<std::string_view> lines = SplitLines(text);

  SessionSummary summary;
  const std::filesystem::path file(path);
  summary.id = file.stem().wstring();

  // Backwards for the time.  The records written when a session closes --
  // last-prompt, atis-latch -- carry none, so the answer is rarely on the very
  // last line and always within a few of it.
  Json record;
  std::string error;
  for (size_t i = lines.size(); i-- > 0;) {
    if (!ParseLine(lines[i], &record, &error)) continue;
    const std::string stamp = record.value("timestamp", std::string());
    if (!stamp.empty()) {
      summary.lastStamp = stamp;
      break;
    }
  }

  // Forwards for the prompt, and for the one thing that decides whether this
  // file is a conversation at all.
  bool spoken = false;
  for (const std::string_view line : lines) {
    if (!ParseLine(line, &record, &error)) continue;
    const std::string kind = record.value("type", std::string());
    if (kind != "user" && kind != "assistant") continue;
    spoken = true;
    if (kind == "user") {
      summary.firstPrompt = OneLine(HumanPromptText(record));
      if (!summary.firstPrompt.empty()) break;
    }
  }
  if (!spoken) return false;
  *out = std::move(summary);
  return true;
}

bool LatestSession(const std::wstring& projectPath, SessionSummary* out) {
  const std::wstring directory = ProjectSessionDir(projectPath);
  if (directory.empty()) return false;
  std::error_code code;
  std::filesystem::directory_iterator entries(
      std::filesystem::path(directory), code);
  if (code) return false;  // no directory means no session, which is an answer

  SessionSummary best;
  bool found = false;
  for (const std::filesystem::directory_entry& entry : entries) {
    if (entry.path().extension() != L".jsonl") continue;
    SessionSummary summary;
    if (!ReadSessionSummary(entry.path().wstring(), &summary)) continue;
    // Text order is time order for ISO 8601 with a Z on the end, so the stamp
    // never has to be parsed to sort by it.  A session with no stamp at all
    // loses to every session that has one, which is what an empty string does.
    if (!found || summary.lastStamp > best.lastStamp) {
      best = std::move(summary);
      found = true;
    }
  }
  if (found) *out = std::move(best);
  return found;
}

std::wstring LocalTimeText(const std::string& isoStamp) {
  int year = 0, month = 0, day = 0, hour = 0, minute = 0, second = 0;
  if (std::sscanf(isoStamp.c_str(), "%d-%d-%dT%d:%d:%d", &year, &month, &day,
                  &hour, &minute, &second) != 6) {
    return Widen(isoStamp);
  }
  std::tm utc = {};
  utc.tm_year = year - 1900;
  utc.tm_mon = month - 1;
  utc.tm_mday = day;
  utc.tm_hour = hour;
  utc.tm_min = minute;
  utc.tm_sec = second;
  utc.tm_isdst = 0;
  const std::time_t moment = _mkgmtime(&utc);
  if (moment == static_cast<std::time_t>(-1)) return Widen(isoStamp);
  // localtime and not localtime_r: this is asked once, at startup, from the
  // thread that has not started the reader yet.  The result is copied out
  // before anything else can be asked.
  const std::tm* local = std::localtime(&moment);
  if (!local) return Widen(isoStamp);
  wchar_t buffer[64];
  std::swprintf(buffer, 64, L"%d. %d. %d %d:%02d", local->tm_mday,
                local->tm_mon + 1, local->tm_year + 1900, local->tm_hour,
                local->tm_min);
  return buffer;
}

}  // namespace proto
