// ClaudeLens.  See CLAUDE.md for the shape of the thing; this file only
// starts it.

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <commctrl.h>
#include <objbase.h>
#include <shellapi.h>

#include <string>
#include <vector>

#include "arguments.h"
// Generated from git describe into build/ -- see the Makefile.
#include "claudelens_version.h"
#include "proto/claude/claude_backend.h"
#include "proto/codex/codex_backend.h"
#include "ui/main_window.h"
#include "win/console.h"
#include "win/dialog.h"

namespace {

// The CLIs there is an adapter for, in the order --help names them.  This
// file is the one place that knows them: everything else holds an
// agent::Backend and asks it what it can do (claude-gui-lkk.44).
const std::vector<std::string> kBackends = {"claude", "codex"};

// From the git tag, so no copy of it can go stale (Makefile).  ASCII.
const wchar_t kVersion[] = L"" CLAUDELENS_VERSION;

std::unique_ptr<agent::Backend> MakeBackend(const std::string& name) {
  if (name == "claude") return std::make_unique<proto::ClaudeBackend>();
  if (name == "codex") return std::make_unique<proto::codex::CodexBackend>();
  return nullptr;
}

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

std::vector<std::wstring> CommandLineWords() {
  std::vector<std::wstring> words;
  int argc = 0;
  wchar_t** argv = CommandLineToArgvW(GetCommandLineW(), &argc);
  if (!argv) return words;
  // Without argv[0]: the program's own name is not a word of the command.
  for (int i = 1; i < argc; ++i) words.emplace_back(argv[i]);
  LocalFree(argv);
  return words;
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

  // Read before anything is loaded and long before anything is shown, because
  // --help must end without a window ever existing -- and Msftedit.dll below
  // is the first thing that would put one on screen if it failed.
  app::Arguments arguments = app::Parse(CommandLineWords());
  app::CheckBackend(&arguments, kBackends);
  // Made now, before the folder picker: its capabilities are what the mode is
  // checked against, and a refused mode must not be answered with "which
  // folder?".  Making it starts nothing -- the process comes with Start.
  std::unique_ptr<agent::Backend> backend =
      MakeBackend(arguments.backend.empty() ? app::kDefaultBackend
                                            : arguments.backend);
  if (backend) app::CheckMode(&arguments, backend->capabilities());
  if (arguments.help) {
    // The dialog is the wrong answer here and it is the fallback anyway: it is
    // reached only when there is no console up the tree and no redirection,
    // which means Explorer or a shortcut.  AllocConsole would be worse -- the
    // window it makes dies with the process, so the text would appear and
    // vanish, which is the same as not printing it.
    const std::wstring help = app::HelpText(
        kVersion, kBackends, MakeBackend(app::kDefaultBackend)->capabilities());
    if (!win::WriteToParentConsole(help)) {
      MessageBoxW(nullptr, help.c_str(), L"ClaudeLens — nápoveda",
                  MB_OK | MB_ICONINFORMATION);
    }
    CoUninitialize();
    return 0;
  }
  if (arguments.version) {
    const std::wstring text = std::wstring(L"ClaudeLens ") + kVersion + L"\n";
    if (!win::WriteToParentConsole(text)) {
      MessageBoxW(nullptr, text.c_str(), L"ClaudeLens", MB_OK | MB_ICONINFORMATION);
    }
    CoUninitialize();
    return 0;
  }
  if (!arguments.error.empty()) {
    // Out the same way as the help, and for the same reason: whoever typed the
    // option typed it at a prompt.  The whole text of the help is not repeated
    // here -- a complaint that scrolls the offending line off the screen is a
    // complaint nobody reads -- but where to get it is.
    const std::wstring text =
        L"ClaudeLens: " + arguments.error + L".\n" +
        L"Zoznam volieb vypíše ClaudeLens --help.\n";
    if (!win::WriteToParentConsole(text)) {
      MessageBoxW(nullptr, text.c_str(), L"ClaudeLens", MB_OK | MB_ICONERROR);
    }
    CoUninitialize();
    return 2;
  }
  // After the checks, or --help typed on its own -- or a refused option --
  // would be answered with a dialog asking which project.
  if (arguments.project.empty()) {
    arguments.project = win::PickFolder(nullptr, L"Vyberte priečinok projektu");
  }
  if (arguments.project.empty()) return 0;  // cancelled, which is an answer
  // Expanded, because the folder is also what the list of conversations is
  // keyed by for -c, and "." is not a key.
  arguments.project = Expand(arguments.project);

  // RichEdit 4.1 comes from this library and the window class does not exist
  // until it is loaded.  Deliberately not freed: the class must outlive every
  // window that uses it, and that is the whole run.
  if (!LoadLibraryW(L"Msftedit.dll")) {
    MessageBoxW(nullptr, L"Nepodarilo sa načítať Msftedit.dll.", L"ClaudeLens",
                MB_OK | MB_ICONERROR);
    return 1;
  }

  agent::StartOptions options;
  options.projectDir = arguments.project;
  // The CLI's own word, which is ASCII: a copy, not a conversion.
  options.mode.assign(arguments.permissionMode.begin(),
                      arguments.permissionMode.end());
  options.model = arguments.model;
  // `-c --resume <id>` is not a contradiction to argue about: the one that
  // names a conversation wins, because it was typed by someone who knew which
  // one they wanted.
  if (arguments.resume) {
    options.resume = agent::StartOptions::Resume::ById;
    options.resumeId = arguments.resumeId;
  } else if (arguments.continueLatest) {
    options.resume = agent::StartOptions::Resume::Latest;
  }
  options.extraArgs = arguments.cliArgs;

  ui::MainWindow window;
  if (!window.Open(instance, std::move(backend), options)) {
    MessageBoxW(nullptr, L"Nepodarilo sa spustiť session.", L"ClaudeLens",
                MB_OK | MB_ICONERROR);
    return 1;
  }

  const int code = win::RunMessageLoop(window.handle(), nullptr);
  CoUninitialize();
  return code;
}
