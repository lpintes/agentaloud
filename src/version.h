#ifndef VERSION_H
#define VERSION_H

// How two versions of AgentAloud compare (claude-gui-lkk.53).
//
// The version is not written in any source file: the Makefile asks git for
// the tag the build stands on and generates build/app_version.h from it.  A
// release is year.month.serial, "2026.10.1", tagged "v2026.10.1", and the
// serial starts again at 1 every month.  Anything built off a tag carries
// git's suffix ("2026.10.1-5-gabc1234", "-dirty", "0.0.0-<hash>") and is a
// development build: it has no number, so it never takes itself for older
// than a release and never offers to replace itself with one.
//
// No generated header here.  Which version THIS build is lives in
// version_current.cpp, which only the application links -- included here, the
// tests would be relinked by every commit.

#include <compare>
#include <optional>
#include <string_view>

namespace version {

struct Number {
  int year = 0;
  int month = 0;
  int serial = 0;
  // Members in this order, so the defaulted comparison is year first.
  // Compared as numbers, not as text: as text "2026.9.5" would beat
  // "2026.10.1".
  auto operator<=>(const Number&) const = default;
};

// "2026.10.1" or "v2026.10.1" and nothing else: exactly three decimal
// numbers, no suffix.  Everything else is not a release and yields nothing.
std::optional<Number> Parse(std::wstring_view text);

// The version as the reader is told it, with git's suffix for a development
// build.  Defined in version_current.cpp, application only.
std::wstring_view Current();

}  // namespace version

#endif
