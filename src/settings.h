#ifndef SETTINGS_H
#define SETTINGS_H

// What AgentAloud remembers between runs, read once at start-up
// (claude-gui-lkk.55).  Written by hand for now; a settings dialog comes after
// localisation.  Until it existed every run
// started from nothing, so a reader who always works in auto mode had to say
// so with Shift+Tab every time -- and forgot.
//
// Three things here are decisions rather than detail:
//
// A setting changes only when the reader changes it, never as a side effect.
// Shift+Tab changes the running session and nothing else: a mode remembered
// behind the reader's back is a mode that carries a slip -- plan pressed once
// too often -- into every session after it, silently.  Whatever writes here
// later -- the dialog, the update check -- must leave the lines it does not
// change as they were, comments included, because the file is also written
// by hand.
//
// The format is plain key=value in UTF-8, parsed by hand, and not the profile
// API: WritePrivateProfileStringW writes ANSI into a file without a UTF-16
// BOM, so the first value with diacritics in it would be corrupted, silently.
// Keys are English and flat, with the backend as a prefix --
// claude.permission-mode -- because the CLIs do not share their words: "auto"
// is a different mode in each of them, and one key for both would be a trap.
//
// A file that is missing is not a problem -- the first run has none -- but a
// line that is wrong IS, and it is refused, not skipped.  The one thing a
// skipped line can do is quietly not apply: a typo in a key looks exactly
// like a setting nobody made, and the reader is left asking why the session
// started in the wrong mode.  The command line refuses what it does not know
// for the same reason (invariant 16).
//
// No windows.h.  Where the file lives is main.cpp's question; this sees bytes.

#include <map>
#include <string>
#include <string_view>
#include <vector>

#include "agent/backend.h"

namespace app {

struct Arguments;

class Settings {
 public:
  struct Entry {
    std::string value;  // UTF-8, trimmed
    int line = 0;       // 1-based, for the complaint
  };

  // The text of the file.  A UTF-8 BOM is skipped -- Notepad writes one -- and
  // lines may end in CRLF.  Blank lines and lines starting with '#' are
  // comments.  What cannot be read as key=value, and a key given twice, end
  // up in error(); the rest is read regardless, so the complaint names the
  // first bad line and not whatever happens to come after it.
  static Settings Parse(std::string_view text);

  // The value of `key`, empty when it is not there.  An empty value in the
  // file means the same as no line at all: a line like `claude.model=` is a
  // template waiting to be filled in, not a request for an empty model.
  std::string Get(const std::string& key) const;

  const std::map<std::string, Entry>& entries() const { return entries_; }

  // The first thing wrong with the file, without its name, "riadok 3: ...".
  // Empty when there is nothing wrong.
  const std::wstring& error() const { return error_; }

  // Sets `key` to `value`, in place: the line that holds the key now gets the
  // new value and every other line -- comments, blank lines, their order --
  // stays as it was, so a file written by hand survives being written by the
  // application.  A key not yet in the file goes on a new line at the end.
  // The caller saves Serialize().
  void Set(const std::string& key, const std::string& value);

  // The file again, with whatever Set changed.  Byte for byte what Parse got
  // when nothing was set: the BOM, CRLF and a missing last newline included.
  std::string Serialize() const;

  // Whether the start-up checks GitHub for a newer release.  On unless the
  // file says 0 -- a file from before the key existed must not quietly stop
  // the updates.
  bool CheckUpdates() const;

  // Checks every key against what the backends are and what each of them can
  // do: `backend` must name one of them, `<backend>.permission-mode` must be
  // one of that backend's modes, check-updates is 0 or 1, and nothing else
  // may be there.  Every
  // backend's lines are checked, not just the chosen one's -- a typo in the
  // codex lines would otherwise wait to be found the day codex is used.
  // Sets error() and does nothing when there is one already.
  void Check(const std::map<std::string, agent::Capabilities>& backends);

 private:
  void Complain(int line, const std::wstring& text);

  std::map<std::string, Entry> entries_;
  std::wstring error_;
  // The file as it came, a line per element, without the '\n' but with any
  // '\r' -- so Serialize gives back what was read.
  std::vector<std::string> lines_;
  bool bom_ = false;
  bool finalNewline_ = true;
};

// The keys, in one place, so the help and the check cannot disagree about
// them.
inline constexpr char kBackendKey[] = "backend";
inline constexpr char kPermissionModeKey[] = "permission-mode";
inline constexpr char kModelKey[] = "model";
// Updates (claude-gui-lkk.53).  The last two are written by the application:
// the day of the last check that got an answer, "YYYY-MM-DD", and the release
// the reader answered "Preskočiť túto verziu" to, "2026.10.1".
inline constexpr char kCheckUpdatesKey[] = "check-updates";
inline constexpr char kLastUpdateCheckKey[] = "last-update-check";
inline constexpr char kSkippedVersionKey[] = "skipped-version";

// Fills in what the command line left empty: the command line wins, the file
// comes next, and the built-in default -- which CheckBackend supplies -- last.
// The mode and the model are read for the backend that will actually run,
// whichever of the two said which one that is.  Call after Parse and before
// CheckBackend and CheckMode.
void ApplySettings(Arguments* arguments, const Settings& settings);

}  // namespace app

#endif
