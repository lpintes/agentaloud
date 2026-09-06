// ClaudeLens.  See CLAUDE.md for the shape of the thing; this file only
// starts it.

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <commctrl.h>
#include <objbase.h>
#include <shellapi.h>

#include <string>
#include <vector>

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

Arguments ReadArguments() {
  Arguments arguments;
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
  if (!window.Open(instance, options)) {
    MessageBoxW(nullptr, L"Nepodarilo sa spustiť session.", L"ClaudeLens",
                MB_OK | MB_ICONERROR);
    return 1;
  }

  const int code = win::RunMessageLoop(window.handle(), nullptr);
  CoUninitialize();
  return code;
}
