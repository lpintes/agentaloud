#ifndef UPDATE_H
#define UPDATE_H

// Updates from the project's GitHub releases (claude-gui-lkk.53): the part
// that needs no network and no windows.h -- reading what GitHub answered,
// deciding whether to offer anything, and which files of a downloaded package
// may replace which.  Kept apart from updater.* so the tests can hold the
// rules without WinHTTP and without a connection.
//
// The check runs before the session starts, so taking an update is swapping
// files and starting again: no CLI is running yet and nothing is lost.

#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "version.h"

namespace update {

inline constexpr wchar_t kHost[] = L"github.com";
inline constexpr wchar_t kLatestPath[] = L"/lpintes/agentaloud/releases/latest";
inline constexpr wchar_t kReleasePage[] =
    L"https://github.com/lpintes/agentaloud/releases/latest";
// Asset names without a version in them, so a download path can be built from
// the tag alone.  vydanie.yml publishes exactly these.
//
// The whole package and not the bare EXE: whoever downloaded it once and from
// then on only updates must end up with the same folder as someone who
// downloads it today.  An EXE alone would leave the DLLs from the first
// download beside a program that may need newer ones -- and a screen reader
// library of the wrong version fails silently.
inline constexpr wchar_t kPackageAsset[] = L"AgentAloud.zip";
inline constexpr wchar_t kSumsAsset[] = L"SHA256SUMS.txt";

// github.com/.../releases/latest answers with a redirect to the newest
// release's page, ".../releases/tag/v2026.10.1".  Reading the tag from that
// Location header needs no API, no JSON and no rate limit.  Anything that is
// not a release tag -- with no release yet GitHub redirects to ".../releases"
// -- yields nothing.
std::optional<std::wstring> TagFromLocation(std::wstring_view location);

// The SHA-256 that SHA256SUMS.txt gives for one file, in lower case, or
// nothing when the file is not listed or its line is malformed.  The format is
// sha256sum's: "<64 hex>  <name>", or "<64 hex> *<name>" in binary mode.
std::optional<std::string> HashFor(std::string_view sums, std::string_view file);

// Whether the automatic check should offer latestTag.  Only a release build
// offers anything (a development build has no number and must not replace
// itself with an older one), only a newer version, and never the one the
// reader said to skip -- though a release newer than that one is offered
// again.
bool ShouldOffer(std::optional<version::Number> current,
                 std::wstring_view latestTag, std::wstring_view skipped);

// Once a day: the check is due unless it already ran today.  Dates are
// "YYYY-MM-DD" in local time, compared as they are written.
bool CheckDue(std::wstring_view lastCheck, std::wstring_view today);

// Which files of an unpacked package go where.  `entries` are the files found
// in it, relative to the folder it was unpacked into, with either separator.
//
// The package has one folder on top, "AgentAloud-2026.10.1/", because that is
// how vydanie.yml zips it; when every entry sits in the same top folder, the
// folder is cut off.  Anything under config/ is left out -- that folder is the
// reader's settings, and a package must never overwrite it.  An entry that
// could land outside the application's folder -- absolute, with a drive, with
// ".." or a ':' (an alternate stream) in it -- refuses the whole package:
// a package with one such name is not one this project published.  So does a
// package without the EXE in it, which is the wrong download, not an update.
struct Plan {
  struct File {
    std::wstring from;  // as in `entries`
    std::wstring to;    // relative to the application's folder, '\' separated
  };
  std::vector<File> files;
  std::wstring error;  // non-empty: replace nothing
};
Plan PlanReplace(const std::vector<std::wstring>& entries,
                 std::wstring_view exeName);

}  // namespace update

#endif
