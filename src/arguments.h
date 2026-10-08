#ifndef ARGUMENTS_H
#define ARGUMENTS_H

// The command line of AgentAloud: what it says, what the help says it may
// say, and what is refused.  Out of main.cpp so that it can be tested -- the
// rules here are exactly the ones that fail silently (invariant 16), and a
// rule nobody can run is a rule nobody checks.
//
// No windows.h.  Reading argv off the process, the folder picker and turning
// "." into a full path stay in main.cpp; this sees a list of strings.

#include <string>
#include <vector>

#include "agent/backend.h"

namespace app {

// The working directory is the project: it decides CLAUDE.md, the git repo and
// what the tools may touch.  It is never guessed from where the .exe happens
// to sit, because that is how a session ends up rooted somewhere harmless
// looking and wrong.  Nor is the permission mode guessed: a session that
// quietly ran with bypassPermissions because the last one did is worse than
// one that asks too much.
//
// The model is here for a plainer reason: without it the CLI takes the one
// from the settings, which is the expensive one, and a headless session is
// told once, at startup, and never again.
struct Arguments {
  std::wstring project;
  // Which CLI.  Empty until Check fills in the default; see kDefaultBackend.
  std::string backend;
  std::wstring permissionMode;
  std::wstring model;
  // --resume was given, with what followed it.  The id may be empty: a bare
  // --resume at the end of the line is passed on as it is (invariant 14).
  bool resume = false;
  std::wstring resumeId;
  // -c: carry on the newest conversation of the project.  Which one that is,
  // is the backend's question.
  bool continueLatest = false;
  // Everything after "--", untouched, for the CLI itself.  Nothing in front
  // of "--" ever gets here: "--foo bar" cannot be told apart from "--foo"
  // followed by the project, so an option this process does not know is
  // refused rather than forwarded (invariant 16).
  std::vector<std::wstring> cliArgs;
  // --help was asked for, so nothing else on the line matters and no window is
  // to be opened.  A separate flag rather than an empty project, because an
  // empty project means "ask which folder" and that is a dialog -- the one
  // answer --help must not give.
  bool help = false;
  // --version: the same as help in everything but the text.
  bool version = false;
  // --check-updates: the check the reader asks for, in place of the daily one
  // at this start.  An act and not a setting -- written into the file it
  // would be forgotten there and check at every start.
  bool checkUpdates = false;
  // Something on the command line was not understood.  Non-empty means no
  // window either: an option that was refused was typed for a reason, and
  // opening a session without it would be doing something other than what was
  // asked, silently.
  std::wstring error;
};

// The backend when --backend is not given.  Not a required option: it would
// break every shortcut already made and tell the application nothing it does
// not know.
extern const char kDefaultBackend[];

// The words on the line, without the program's own name.
Arguments Parse(const std::vector<std::wstring>& words);

// What can only be checked once it is known which backends exist and what the
// chosen one can do: that the backend is one of `known`, and that the mode is
// one of its modes.  Fills in the default backend.  Sets `error` and leaves
// the rest alone; does nothing when there is an error or help already.
//
// The mode IS checked now, where it used to be passed on for the CLI to
// refuse.  With one CLI that was enough; with two the words differ, and a
// mode refused by the CLI is refused on a stderr this process has not got
// (invariant 16) -- a session that starts and says nothing.
void CheckBackend(Arguments* arguments, const std::vector<std::string>& known);
void CheckMode(Arguments* arguments, const agent::Capabilities& capabilities);

// The help, filled in from what is true: the version, the backends there are,
// the modes of the default one and where the settings file is looked for --
// %APPDATA% is not a place anyone finds by guessing.  The version comes in from main.cpp, the
// one file that includes the header generated from git describe -- were it
// included here, the tests would be relinked by every commit.
std::wstring HelpText(const std::wstring& version,
                      const std::vector<std::string>& backends,
                      const agent::Capabilities& defaultBackend,
                      const std::wstring& settingsFile);

}  // namespace app

#endif
