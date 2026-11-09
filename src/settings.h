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
  // comments, and a line in brackets starts a project's section
  // (ForProject).  What cannot be read as key=value, and a key given twice
  // in one section or before any, end
  // up in error(); the rest is read regardless, so the complaint names the
  // first bad line and not whatever happens to come after it.
  static Settings Parse(std::string_view text);

  // The value of `key`, empty when it is not there.  An empty value in the
  // file means the same as no line at all: a line like `claude.model=` is a
  // template waiting to be filled in, not a request for an empty model.
  // The profile's value comes first (Overlay), and an empty one there lets
  // this file's through, for the same reason.
  std::string Get(const std::string& key) const;

  // This file's own lines, without the profile: what Set and Serialize work
  // on.  Whatever reads a setting asks Get or Keys.
  const std::map<std::string, Entry>& entries() const { return entries_; }

  // Every key Get can answer, the profile's included.  A `<backend>.env.`
  // key of this file is left out when the profile has the same one in
  // another case: Windows takes the two for one variable, and the one that
  // got set last would win by an order nobody wrote.
  std::vector<std::string> Keys() const;

  // Lays a profile over this file (claude-gui-lkk.90): another settings file,
  // named by --settings, which says only how it differs -- two shortcuts on
  // the desktop, one for work and one for home, share everything else.  The
  // profile only answers Get; Set and Serialize stay with this file, so what
  // the application writes (the day of the update check) lands where it is
  // read the next time whatever profile is used.  Both are Checked apart
  // first, each complaining about its own lines.
  void Overlay(const Settings& profile);

  // How two spellings of one folder are found to be the same: win::FolderKey
  // in the application.  Handed in because this file has no windows.h, and
  // not a rule of its own because the folder the reader typed, the folder of
  // the recent list and the folder of a section must all be compared by one
  // rule -- C:\x\ and c:/X written by two rules would quietly be two
  // projects.  Null compares the text as written.
  using FolderKeyFn = std::wstring (*)(std::wstring);

  // These settings with the section of `folder` laid over them
  // (claude-gui-lkk.56): a project may want another mode or model than the
  // rest.  A section is headed by the full path in brackets, [C:\vcs\x], and
  // what follows it up to the next one is its; it says only
  // <backend>.permission-mode and <backend>.model (Check).  The narrower rule
  // wins: the profile's section, then this file's, then the profile, then
  // this file.  A copy for reading, not for Set.
  Settings ForProject(const std::wstring& folder, FolderKeyFn folderKey) const;

  // The first thing wrong with the file, without its name, "riadok 3: ...".
  // Empty when there is nothing wrong.
  const std::wstring& error() const { return error_; }

  // Sets `key` to `value`, in place: the line that holds the key now gets the
  // new value and every other line -- comments, blank lines, their order --
  // stays as it was, so a file written by hand survives being written by the
  // application.  A key not yet in the file goes on a new line at the end.
  // The caller saves Serialize().  The new line goes before the first
  // section, not at the end, which would be inside the last project's.
  void Set(const std::string& key, const std::string& value);

  // The file again, with whatever Set changed.  Byte for byte what Parse got
  // when nothing was set: the BOM, CRLF and a missing last newline included.
  std::string Serialize() const;

  // Whether the start-up checks GitHub for a newer release.  On unless the
  // file says 0 -- a file from before the key existed must not quietly stop
  // the updates.
  bool CheckUpdates() const;

  // Whether the turn's end and a waiting question are signalled by our own
  // tones (claude-gui-lkk.75).  On unless the file says 0, for the reason
  // CheckUpdates is.
  bool Sounds() const;

  // Checks every key against what the backends are and what each of them can
  // do: `backend` must name one of them, `<backend>.permission-mode` must be
  // one of that backend's modes, check-updates and sounds are 0 or 1,
  // `<backend>.env.` is followed by a name, given once whatever its case, and
  // nothing else may be there.  Every
  // backend's lines are checked, not just the chosen one's -- a typo in the
  // codex lines would otherwise wait to be found the day codex is used.
  // A section must be headed by a full path, a folder only once by
  // `folderKey`, and hold only the mode and the model: the CLI and its
  // environment also list the conversations, before any folder is chosen,
  // and the rest belongs to the whole process.
  // Sets error() and does nothing when there is one already.
  void Check(const std::map<std::string, agent::Capabilities>& backends,
             FolderKeyFn folderKey = nullptr);

  // What a profile must not say, on top of Check: the language, because it
  // is chosen before the command line that names the profile is read, and
  // the keys the application writes, because it writes them into the main
  // file and a profile's copy would hide every later write.  Refused rather
  // than ignored -- a line that silently does not apply is the one failure
  // this file exists to avoid.
  void CheckProfile();

 private:
  struct Section {
    std::wstring folder;  // as written between the brackets
    int line = 0;         // of the heading
    std::map<std::string, Entry> entries;
  };

  void Complain(int line, const std::wstring& text);
  static const Section* FindSection(const std::vector<Section>& sections,
                                    const std::wstring& folder,
                                    FolderKeyFn folderKey);

  std::map<std::string, Entry> entries_;  // the lines before any section
  std::map<std::string, Entry> overlay_;
  std::vector<Section> sections_;  // in the order of the file
  std::vector<Section> overlaySections_;
  // The sections ForProject chose, of this file and of the profile.
  std::map<std::string, Entry> project_;
  std::map<std::string, Entry> projectOverlay_;
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
// "sk" or "en"; without it, the language of Windows (claude-gui-lkk.52).
// main.cpp reads it out of the bytes before anything else, because the
// complaints this file makes are already text in some language.
inline constexpr char kLanguageKey[] = "language";
// Updates (claude-gui-lkk.53).  The last two are written by the application:
// the day of the last check that got an answer, "YYYY-MM-DD", and the release
// the reader answered "Preskočiť túto verziu" to, "2026.10.1".
inline constexpr char kCheckUpdatesKey[] = "check-updates";
inline constexpr char kLastUpdateCheckKey[] = "last-update-check";
inline constexpr char kSkippedVersionKey[] = "skipped-version";
// "0" turns our own tones off (claude-gui-lkk.75).
inline constexpr char kSoundsKey[] = "sounds";
// The CLI and its environment, per backend (claude-gui-lkk.72):
// codex.executable=bin\codex-x86_64-pc-windows-msvc.exe and
// codex.env.CODEX_HOME=E:\codex.  For a portable installation, whose CLI is
// neither on PATH nor called what the adapter calls it, and whose
// configuration does not live in the profile.
inline constexpr char kExecutableKey[] = "executable";
inline constexpr char kEnvPrefix[] = "env.";
// The roots the list of recent folders is limited to, full paths separated
// by ';' as in PATH -- one line, because a key given twice is refused
// (claude-gui-lkk.90).  Meant for a profile, but the main file may say it too.
inline constexpr char kProjectsKey[] = "projects";

// The roots of kProjectsKey, as written, without the empty ones.  None means
// the list is not limited.
std::vector<std::wstring> ProjectRoots(const Settings& settings);

// Fills in what the command line left empty: the command line wins, the file
// comes next -- with a profile laid over it first (Overlay) -- and the
// built-in default, which CheckBackend supplies, last.  The mode and the model are read for the backend that will actually run,
// whichever of the two said which one that is.  Call after Parse and before
// CheckBackend and CheckMode.
void ApplySettings(Arguments* arguments, const Settings& settings);

// The CLI `backend` runs: `typedExecutable` when there is one -- the command
// line wins here too, and main.cpp has made it absolute already -- or
// `<backend>.executable`, and `<backend>.env.<NAME>` for its environment.
//
// A relative path in the file is relative to `appDirectory`, not to the
// working directory: the file of a portable installation travels with the
// package, and the working directory is whatever the shortcut said.  An
// environment value is taken as it is written -- whether it is a path, only
// the CLI knows.
agent::Program ProgramFor(const Settings& settings, const std::string& backend,
                          const std::wstring& typedExecutable,
                          const std::wstring& appDirectory);

}  // namespace app

#endif
