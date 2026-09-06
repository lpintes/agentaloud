// ClaudeLens.  See CLAUDE.md for the shape of the thing; this file only
// starts it.

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <commctrl.h>
#include <objbase.h>
#include <shellapi.h>

#include <string>
#include <vector>

#include "model/utf.h"
#include "proto/sessions.h"
#include "ui/main_window.h"
#include "win/dialog.h"

namespace {

// The working directory is the project: it decides CLAUDE.md, the git repo and
// what the tools may touch.  It is never guessed from where the .exe happens
// to sit, because that is how a session ends up rooted somewhere harmless
// looking and wrong.
// The project, and the permission mode if one was asked for.  The mode is not
// guessed either: a session that quietly ran with bypassPermissions because
// the last one did is worse than one that asks too much.
//
// The model is here for a plainer reason: without it the CLI takes the one
// from the settings, which is the expensive one, and there is no way from
// inside the application to say "this piece of work is worth a cheaper model".
// A terminal session has /model for that; a headless one is told once, at
// startup, and never again.
//
// Everything else the CLI is to be told goes into extraArgs, but only by name:
// "--resume" is spelled out here rather than forwarded generically, because
// "--foo bar" cannot be told apart from "--foo" followed by the project.
struct Arguments {
  std::wstring project;
  std::wstring permissionMode;
  std::wstring model;
  std::vector<std::wstring> extraArgs;
  // What the window is to say about how this session came to be open.  Empty
  // for the ordinary case, where there is nothing to say: a fresh session
  // asked for on the command line is exactly what the empty transcript looks
  // like.  It is filled in by -c, which decided something on the reader's
  // behalf and therefore owes them the decision.
  std::wstring openingNote;
};

// "." is a perfectly good thing to type and a useless thing to read back: the
// title bar answers "which checkout is this", and the status bar wants the
// folder's name, and neither can be had from a relative path.  Expanded here
// rather than in the window, because the working directory of THIS process is
// what the dot means, and by the time the pane sees the string that context is
// gone.  A path that cannot be expanded is passed through untouched -- the CLI
// will complain about it better than a second check here would.
std::wstring Expand(const std::wstring& path) {
  if (path.empty()) return path;
  const DWORD needed = GetFullPathNameW(path.c_str(), 0, nullptr, nullptr);
  if (needed == 0) return path;
  std::wstring full(needed, L'\0');
  const DWORD written =
      GetFullPathNameW(path.c_str(), needed, full.data(), nullptr);
  if (written == 0 || written >= needed) return path;
  full.resize(written);
  return full;
}

// A prompt is a whole turn's worth of text and this one goes into a single
// sentence.  Cut on a space where there is one, so the sentence ends on a word
// rather than mid-syllable -- it is read aloud.
std::wstring Shorten(const std::wstring& text, size_t limit = 70) {
  if (text.size() <= limit) return text;
  size_t cut = text.rfind(L' ', limit);
  if (cut == std::wstring::npos || cut < limit / 2) cut = limit;
  return text.substr(0, cut) + L"…";
}

// `-c` means "carry on the last conversation in this folder".  Which one that
// is gets decided here and turned into a plain --resume, rather than passed to
// the CLI as --continue: the CLI answers that question out of
// ~/.claude/history.jsonl, where only interactively typed prompts are written,
// so for a folder used from both a terminal and ClaudeLens it would carry on
// the terminal's conversation and call it ours.  See proto/sessions.h.
//
// Which one it picked has to be said out loud.  Otherwise the reader carries
// on in something other than what they had in mind and has no way to find out
// -- the transcript is empty either way.
void ContinueLatest(Arguments* arguments) {
  // `-c --resume <id>` is not a contradiction to argue about: the one that
  // names a conversation wins, because it was typed by someone who knew which
  // one they wanted.  Two --resume on one command line would be the CLI's
  // problem to report, and it reports it on a stderr this process has not got.
  if (proto::ResumesConversation(arguments->extraArgs)) return;
  proto::SessionSummary latest;
  if (!proto::LatestSession(arguments->project, &latest)) {
    // Not an error and not a reason to refuse: a folder nobody has worked in
    // yet has nothing to carry on, and a new session is what was wanted.
    arguments->openingNote =
        L"V tomto projekte zatiaľ žiadny rozhovor nie je, začínam nový.";
    return;
  }
  arguments->extraArgs.push_back(L"--resume");
  arguments->extraArgs.push_back(latest.id);
  std::wstring note = L"Pokračujem v poslednom rozhovore projektu z ";
  note += proto::LocalTimeText(latest.lastStamp);
  if (!latest.firstPrompt.empty()) {
    note += L", začínal sa slovami „";
    note += Shorten(model::Utf16FromUtf8(latest.firstPrompt));
    note += L"“";
  }
  note += L".";
  arguments->openingNote = note;
}

Arguments ReadArguments() {
  Arguments arguments;
  bool continueLatest = false;
  int argc = 0;
  wchar_t** argv = CommandLineToArgvW(GetCommandLineW(), &argc);
  if (argv) {
    for (int i = 1; i < argc; ++i) {
      const std::wstring argument = argv[i];
      // The CLI's own spelling, and its own values -- acceptEdits, auto,
      // bypassPermissions, manual, dontAsk, plan.  Not validated here: the
      // CLI rejects what it does not know, and a second list of legal values
      // in this file would be a list that goes stale.
      if (argument == L"--permission-mode" && i + 1 < argc) {
        arguments.permissionMode = argv[++i];
      // An alias -- sonnet, haiku, opus -- or a full model id.  Not validated
      // here for the same reason: the list of what the CLI takes is the CLI's,
      // and a copy of it here would be a copy that goes stale.
      } else if (argument == L"--model" && i + 1 < argc) {
        arguments.model = argv[++i];
      // Carry on an earlier conversation.  Passed straight through, value and
      // all, exactly like the two above -- so `ClaudeLens --resume <id> .`
      // works and `ClaudeLens . --resume <id>` works too.
      //
      // The value is not checked against the shape of a UUID, because the CLI
      // takes a session title there as well ("--resume requires a valid
      // session ID or session title when used with --print"), and a check
      // here would be a second, narrower idea of what is legal.  A --resume
      // with nothing after it is passed on bare and the CLI says so itself:
      // measured 2026-09-06, it does NOT open the interactive picker under
      // --print, it refuses with that message and ends the turn with a
      // `result` carrying is_error.
      } else if (argument == L"--resume" || argument == L"-r") {
        arguments.extraArgs.push_back(argument);
        if (i + 1 < argc) arguments.extraArgs.push_back(argv[++i]);
      // Not forwarded and not remembered as itself: it is a question about
      // this folder, and the folder may still be several arguments away, so
      // the answer waits until the whole line has been read.
      } else if (argument == L"--continue" || argument == L"-c") {
        continueLatest = true;
      } else if (arguments.project.empty()) {
        arguments.project = argument;
      }
    }
    LocalFree(argv);
  }
  if (arguments.project.empty()) {
    arguments.project = win::PickFolder(nullptr, L"Vyberte priečinok projektu");
  }
  arguments.project = Expand(arguments.project);
  // After the expansion, because the folder is what the list of conversations
  // is keyed by and "." is not a key.
  if (continueLatest && !arguments.project.empty()) ContinueLatest(&arguments);
  return arguments;
}

}  // namespace

// wWinMain, not wmain, and linked with -mwindows.  A console-subsystem
// executable is given a console by Windows whether it wants one or not, so
// the first version put a black window on screen beside the application --
// which is precisely the thing this program exists to get away from.
int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int) {
  // Per-monitor DPI awareness, and the reason is not sharp text.  A
  // DPI-unaware process is scaled by Windows, and the coordinates another
  // process reads back are then rounded: the status bar's rectangle says it
  // reaches the last row of the client area while a hit test on that very row
  // lands on the frame window instead.  NVDA finds a status bar by asking
  // what object sits at the bottom left of the client area
  // (api.getStatusBar), so that rounding is the whole difference between
  // NVDA+End reading the bar and NVDA+End reading whatever text is on screen.
  // Measured at 150%: unaware fails, aware finds the bar.  Must be the first
  // thing done, before any window exists.
  SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);

  // Required by the shell folder picker, and by anything else that later
  // wants the shell.
  CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);

  INITCOMMONCONTROLSEX controls = {};
  controls.dwSize = sizeof(controls);
  controls.dwICC = ICC_STANDARD_CLASSES;
  InitCommonControlsEx(&controls);

  // RichEdit 4.1 comes from this library and the window class does not exist
  // until it is loaded.  Deliberately not freed: the class must outlive every
  // window that uses it, and that is the whole run.
  if (!LoadLibraryW(L"Msftedit.dll")) {
    MessageBoxW(nullptr, L"Nepodarilo sa načítať Msftedit.dll.", L"ClaudeLens",
                MB_OK | MB_ICONERROR);
    return 1;
  }

  const Arguments arguments = ReadArguments();
  if (arguments.project.empty()) return 0;  // cancelled, which is an answer

  proto::Session::Options options;
  options.workingDir = arguments.project;
  options.permissionMode = arguments.permissionMode;
  options.model = arguments.model;
  options.extraArgs = arguments.extraArgs;

  ui::MainWindow window;
  if (!window.Open(instance, options, arguments.openingNote)) {
    MessageBoxW(nullptr, L"Nepodarilo sa spustiť session.", L"ClaudeLens",
                MB_OK | MB_ICONERROR);
    return 1;
  }

  const int code = win::RunMessageLoop(window.handle(), nullptr);
  CoUninitialize();
  return code;
}
