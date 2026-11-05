#include "proto/claude/sessions.h"

#include <algorithm>
#include <chrono>
#include <cstdint>
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

// The CLI's own nudge when a turn produced nothing visible.  Machinery, and
// named in model/transcript.h as such; it is here because this is the layer
// that knows what the CLI writes in the user's name.
constexpr std::string_view kNoOutputNudge =
    "[Your previous response had no visible output";
constexpr std::string_view kInterruptMark = "[Request interrupted by user";

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

namespace {

// <home>/projects, where the CLI files every project's sessions.  Empty when
// the home is unknown.
std::wstring ProjectsDir(const std::wstring& configDir) {
  std::wstring home;
  if (!configDir.empty()) {
    home = configDir;
  } else if (const wchar_t* configured = _wgetenv(L"CLAUDE_CONFIG_DIR");
             configured && *configured) {
    home = configured;
  } else if (const wchar_t* profile = _wgetenv(L"USERPROFILE");
             profile && *profile) {
    home = profile;
    home += L"\\.claude";
  } else {
    return {};
  }
  return home + L"\\projects";
}

}  // namespace

std::wstring ProjectSessionDir(const std::wstring& projectPath,
                              const std::wstring& configDir) {
  const std::wstring projects = ProjectsDir(configDir);
  if (projects.empty()) return {};
  return projects + L"\\" + ProjectKey(projectPath);
}

std::wstring SessionFilePath(const std::wstring& projectPath,
                             const std::wstring& id,
                             const std::wstring& configDir) {
  const std::wstring directory = ProjectSessionDir(projectPath, configDir);
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

namespace {

// A prompt sent during a turn that the CLI folded into that same turn is never
// filed as a `user` record: the model got it as a system-reminder beside the
// next tool result, and the file keeps only this attachment, at that very
// place (claude-gui-lkk.83, tools/probe_midturn.notes.md).  Without it a
// resumed transcript shows the answer and not what it answered.
//
// The same attachment also carries a subagent's hand-back (origin "peer",
// isMeta) and background task notifications (commandMode "task-notification");
// neither was typed by the reader.  A prompt from stdin has no origin at all,
// one typed in the terminal has "human".  Measured over the corpus 2026-10-20:
// 52 such prompts, not one of them also present as a `user` record.
bool AbsorbedPrompt(const Json& record, Json* out) {
  if (record.value("type", std::string()) != "attachment") return false;
  if (record.value("isMeta", false)) return false;
  const auto attachment = record.find("attachment");
  if (attachment == record.end() || !attachment->is_object()) return false;
  if (attachment->value("type", std::string()) != "queued_command") return false;
  if (attachment->value("commandMode", std::string()) != "prompt") return false;
  if (const auto origin = attachment->find("origin");
      origin != attachment->end() &&
      (!origin->is_object() ||
       origin->value("kind", std::string()) != "human")) {
    return false;
  }
  const auto prompt = attachment->find("prompt");
  if (prompt == attachment->end()) return false;
  *out = Json{{"type", "user"},
              {"message", Json{{"role", "user"}, {"content", *prompt}}}};
  if (const auto time = record.find("timestamp"); time != record.end()) {
    (*out)["timestamp"] = *time;
  }
  return true;
}

}  // namespace

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
    if (record.value("isSidechain", false)) continue;
    if (Json prompt; AbsorbedPrompt(record, &prompt)) {
      out->push_back(std::move(prompt));
      continue;
    }
    const std::string kind = record.value("type", std::string());
    if (kind != "user" && kind != "assistant") continue;
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

  // Backwards for the title too, since the last one wins.  A line is parsed
  // only when it starts the way a title record does -- the CLI writes the type
  // first, in all 2881 of them measured 2026-11-05 -- because searching every
  // line whole for the name doubled the time the list takes.
  constexpr std::string_view kCustomTitle = R"({"type":"custom-title")";
  constexpr std::string_view kAiTitle = R"({"type":"ai-title")";
  const auto TitleOf = [&](std::string_view line, const char* type,
                           const char* field) {
    if (!ParseLine(line, &record, &error)) return std::string();
    if (record.value("type", std::string()) != type) return std::string();
    const auto title = record.find(field);
    if (title == record.end() || !title->is_string()) return std::string();
    return OneLine(title->get_ref<const std::string&>());
  };
  std::string aiTitle;
  for (size_t i = lines.size(); i-- > 0;) {
    const std::string_view line = lines[i];
    if (line.starts_with(kCustomTitle)) {
      summary.title = TitleOf(line, "custom-title", "customTitle");
      if (!summary.title.empty()) break;
    } else if (aiTitle.empty() && line.starts_with(kAiTitle)) {
      aiTitle = TitleOf(line, "ai-title", "aiTitle");
    }
  }
  if (summary.title.empty()) summary.title = std::move(aiTitle);

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

bool ProjectSessions(const std::wstring& projectPath,
                     std::vector<SessionSummary>* out,
                     const std::wstring& configDir) {
  out->clear();
  const std::wstring directory = ProjectSessionDir(projectPath, configDir);
  if (directory.empty()) return false;
  std::error_code code;
  std::filesystem::directory_iterator entries(
      std::filesystem::path(directory), code);
  if (code) return true;  // no directory means no session, which is an answer

  for (const std::filesystem::directory_entry& entry : entries) {
    if (entry.path().extension() != L".jsonl") continue;
    SessionSummary summary;
    if (ReadSessionSummary(entry.path().wstring(), &summary)) {
      out->push_back(std::move(summary));
    }
  }
  // Text order is time order for ISO 8601 with a Z on the end, so the stamp
  // never has to be parsed to sort by it.  A session with no stamp at all
  // goes after every session that has one, which is what an empty string
  // does.  Ties go by id, so that a list read twice is the same list.
  std::sort(out->begin(), out->end(),
            [](const SessionSummary& a, const SessionSummary& b) {
              if (a.lastStamp != b.lastStamp) return a.lastStamp > b.lastStamp;
              return a.id < b.id;
            });
  return true;
}

bool LatestSession(const std::wstring& projectPath, SessionSummary* out,
                   const std::wstring& configDir) {
  std::vector<SessionSummary> sessions;
  if (!ProjectSessions(projectPath, &sessions, configDir) || sessions.empty()) {
    return false;
  }
  *out = std::move(sessions.front());
  return true;
}

bool ProjectConversations(const std::wstring& projectPath,
                          std::vector<agent::Conversation>* out,
                          const std::wstring& configDir) {
  out->clear();
  std::vector<SessionSummary> sessions;
  if (!ProjectSessions(projectPath, &sessions, configDir)) return false;
  for (SessionSummary& session : sessions) {
    agent::Conversation conversation;
    conversation.id = std::move(session.id);
    conversation.title = std::move(session.title);
    conversation.firstPrompt = std::move(session.firstPrompt);
    conversation.lastActive = UnixSeconds(session.lastStamp);
    out->push_back(std::move(conversation));
  }
  return true;
}

namespace {

// The last `size` bytes of the file, or the whole of it when it is no longer;
// `whole` says which.
bool ReadFileTail(const std::filesystem::path& path, std::uintmax_t size,
                  std::string* out, bool* whole) {
  std::ifstream file(path, std::ios::binary | std::ios::ate);
  if (!file) return false;
  const std::streamoff length = file.tellg();
  if (length < 0) return false;
  const std::streamoff start =
      static_cast<std::uintmax_t>(length) > size
          ? length - static_cast<std::streamoff>(size)
          : 0;
  *whole = start == 0;
  out->resize(static_cast<size_t>(length - start));
  file.seekg(start);
  file.read(out->data(), length - start);
  out->resize(static_cast<size_t>(file.gcount()));
  return true;
}

// Where and when the conversation in this file was last spoken in: the newest
// user or assistant record, each of which carries both `cwd` and `timestamp`.
// False for a file that has no such record -- not a conversation.
//
// From the tail, unlike ReadSessionSummary, because this runs over every
// session the machine has (677 MB in 810 files here, 2026-11-04) and not one
// project's.  The danger ReadWholeFile names is real -- one tool result can be
// bigger than any window -- so a window that holds no complete record of the
// kind is not an answer: it grows until it does, or until it is the file.
bool LastSpoken(const std::filesystem::path& path, std::string* cwd,
                std::string* stamp) {
  std::uintmax_t window = 64 * 1024;
  std::string text;
  Json record;
  std::string error;
  for (;;) {
    bool whole = false;
    if (!ReadFileTail(path, window, &text, &whole)) return false;
    std::vector<std::string_view> lines = SplitLines(text);
    // The first line of a window that starts mid-file is a piece of one.
    if (!whole && !lines.empty()) lines.erase(lines.begin());
    for (size_t i = lines.size(); i-- > 0;) {
      if (!ParseLine(lines[i], &record, &error)) continue;
      const std::string kind = record.value("type", std::string());
      if (kind != "user" && kind != "assistant") continue;
      *cwd = record.value("cwd", std::string());
      *stamp = record.value("timestamp", std::string());
      if (!cwd->empty() && !stamp->empty()) return true;
    }
    if (whole) return false;
    window *= 4;
  }
}

long long UnixSecondsOf(std::filesystem::file_time_type time) {
  return std::chrono::duration_cast<std::chrono::seconds>(
             std::chrono::file_clock::to_sys(time).time_since_epoch())
      .count();
}

}  // namespace

bool ProjectFolders(std::vector<agent::Project>* out,
                    const std::wstring& configDir) {
  namespace fs = std::filesystem;
  out->clear();
  const std::wstring projects = ProjectsDir(configDir);
  if (projects.empty()) return false;

  struct Candidate {
    fs::path file;
    long long modified;
    size_t folder;
  };
  std::vector<Candidate> candidates;
  size_t folders = 0;
  std::error_code code;
  fs::directory_iterator top(fs::path(projects), code);
  if (code) return true;  // no directory means no project yet
  for (const fs::directory_entry& folder : top) {
    std::error_code inner;
    if (!folder.is_directory(inner)) continue;
    // Top level only: a session's own subdirectory holds its subagents'
    // conversations and tool results, not conversations of the project.
    for (const fs::directory_entry& entry :
         fs::directory_iterator(folder.path(), inner)) {
      if (entry.path().extension() != L".jsonl") continue;
      const fs::file_time_type modified = entry.last_write_time(inner);
      if (inner) continue;
      candidates.push_back({entry.path(), UnixSecondsOf(modified), folders});
    }
    ++folders;
  }

  // A record is never written after its file was last modified, so once a
  // folder has an answer newer than a file's mtime, nothing in that file can
  // beat it.  Newest mtime first, then, and nearly every folder is decided by
  // its first file.  The second of slack is for the stamp's milliseconds.
  std::sort(candidates.begin(), candidates.end(),
            [](const Candidate& a, const Candidate& b) {
              return a.modified > b.modified;
            });
  std::vector<long long> newest(folders, 0);
  std::vector<std::string> where(folders);
  for (const Candidate& candidate : candidates) {
    if (newest[candidate.folder] > candidate.modified + 1) continue;
    std::string cwd, stamp;
    if (!LastSpoken(candidate.file, &cwd, &stamp)) continue;
    const long long when = UnixSeconds(stamp);
    if (when > newest[candidate.folder]) {
      newest[candidate.folder] = when;
      where[candidate.folder] = std::move(cwd);
    }
  }
  // The folder as the records name it, not as the directory does: the
  // directory name is the path with every non-alphanumeric made a dash, and
  // that cannot be undone.
  for (size_t i = 0; i < folders; ++i) {
    if (newest[i] == 0) continue;
    const std::u8string utf8(where[i].begin(), where[i].end());
    out->push_back({fs::path(utf8).wstring(), newest[i]});
  }
  return true;
}

long long UnixSeconds(const std::string& isoStamp) {
  int year = 0, month = 0, day = 0, hour = 0, minute = 0, second = 0;
  if (std::sscanf(isoStamp.c_str(), "%d-%d-%dT%d:%d:%d", &year, &month, &day,
                  &hour, &minute, &second) != 6) {
    return 0;
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
  if (moment == static_cast<std::time_t>(-1)) return 0;
  return static_cast<long long>(moment);
}

}  // namespace proto
