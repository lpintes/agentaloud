// AgentAloud.  See CLAUDE.md for the shape of the thing; this file only
// starts it.

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <commctrl.h>
#include <objbase.h>
#include <shellapi.h>

#include <map>
#include <string>
#include <vector>

#include "app_name.h"
#include "arguments.h"
#include "model/utf.h"
#include "settings.h"
#include "update.h"
#include "updater.h"
#include "version.h"
// Generated from git describe into build/ -- see the Makefile.
#include "app_version.h"
#include "proto/claude/claude_backend.h"
#include "proto/codex/codex_backend.h"
#include "ui/main_window.h"
#include "ui/new_session_dialog.h"
#include "ui/resource.h"
#include "win/console.h"
#include "win/dialog.h"
#include "win/paths.h"

namespace {

// The CLIs there is an adapter for, in the order --help names them.  This
// file is the one place that knows them: everything else holds an
// agent::Backend and asks it what it can do (claude-gui-lkk.44).
const std::vector<std::string> kBackends = {"claude", "codex"};

// From the git tag, so no copy of it can go stale (Makefile).  ASCII.
const wchar_t kVersion[] = L"" APP_VERSION;

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

// A `config` folder beside the .exe when there is one, %APPDATA%\AgentAloud
// otherwise.  A folder and not a file, because portable mode has to be a
// deliberate act: an unpacked archive can leave a stray file behind, and a
// mode that switches itself on unnoticed is exactly the silent state this
// project keeps getting caught by.  Never created here, for the same reason --
// and nothing writes into it yet, so there is nothing to create it for.
// Empty when the shell will not say where roaming data goes.
std::wstring SettingsFile() {
  const std::wstring portable = win::ExecutableDirectory() + L"\\config";
  const DWORD attributes = GetFileAttributesW(portable.c_str());
  if (attributes != INVALID_FILE_ATTRIBUTES &&
      (attributes & FILE_ATTRIBUTE_DIRECTORY)) {
    return portable + L"\\settings.txt";
  }
  const std::wstring roaming = win::RoamingAppData();
  if (roaming.empty()) return {};
  return roaming + L"\\" APP_NAME L"\\settings.txt";
}

// A file that is not there is no settings at all, which is how every first
// run starts; a file that is there is checked against every backend.
app::Settings ReadSettings(const std::wstring& file) {
  std::string bytes;
  if (file.empty() || !win::ReadFileBytes(file, &bytes)) return {};
  app::Settings settings = app::Settings::Parse(bytes);
  std::map<std::string, agent::Capabilities> backends;
  for (const std::string& name : kBackends) {
    backends[name] = MakeBackend(name)->capabilities();
  }
  settings.Check(backends);
  return settings;
}

// A failed save is not said: the check runs before anything else on every
// start, and a complaint about a file at that moment would be about something
// the reader did not do.  What it costs is a check again tomorrow -- or a
// skipped version offered once more.
void SaveSettings(const std::wstring& file, const app::Settings& settings) {
  std::wstring error;
  if (!file.empty()) win::WriteFileBytes(file, settings.Serialize(), &error);
}

// The automatic update check (claude-gui-lkk.53).  Before the folder picker
// and before the session, so taking an update is only swapping files and
// starting again: no CLI runs yet, and the reader is not asked for a folder
// twice.  True means the new version has been started and this process
// should leave.
//
// The answer is remembered only when there was one.  A network that is down
// or slow tries again at the next start rather than tomorrow -- the cap of
// three seconds is what keeps that cheap.
bool UpdateBeforeStart(app::Settings& settings, const std::wstring& file) {
  if (!settings.CheckUpdates()) return false;
  const std::wstring today = updater::Today();
  const std::wstring last =
      model::Utf16FromUtf8(settings.Get(app::kLastUpdateCheckKey));
  if (!update::CheckDue(last, today)) return false;

  const updater::Latest latest = updater::FetchLatestWithin(3000);
  if (latest.kind == updater::Latest::Kind::kFailed) return false;
  settings.Set(app::kLastUpdateCheckKey, model::Utf8FromUtf16(today));
  SaveSettings(file, settings);

  if (latest.kind != updater::Latest::Kind::kFound) return false;
  const std::wstring skipped =
      model::Utf16FromUtf8(settings.Get(app::kSkippedVersionKey));
  if (!update::ShouldOffer(version::Parse(version::Current()), latest.tag,
                           skipped)) {
    return false;
  }
  switch (updater::AskToUpdate(nullptr, latest.tag, /*restartsItself=*/true)) {
    case updater::Choice::kSkip: {
      // Without the v: the key says a version, not a tag.
      const std::wstring bare =
          latest.tag.starts_with(L'v') ? latest.tag.substr(1) : latest.tag;
      settings.Set(app::kSkippedVersionKey, model::Utf8FromUtf16(bare));
      SaveSettings(file, settings);
      return false;
    }
    case updater::Choice::kLater:
      return false;
    case updater::Choice::kUpdate:
      // A failure has been said by the updater; the start then goes on with
      // the version there is.
      return updater::DownloadAndInstall(nullptr, latest.tag, true) ==
             updater::Installed::kRestarted;
  }
  return false;
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
  // Kept as typed: Nová session may choose another backend, and then the
  // file's mode and model have to be read again for that one, under what the
  // line said and not under what the file said for the first.
  const app::Arguments typed = arguments;
  // Between Parse and the checks: what the file says fills in only what the
  // line left out, and then goes through the same checks as if it had been
  // typed.  A file with a bad line is not applied at all -- half a file is a
  // setting nobody wrote.
  const std::wstring settingsFile = SettingsFile();
  app::Settings settings = ReadSettings(settingsFile);
  if (settings.error().empty()) app::ApplySettings(&arguments, settings);
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
    const std::wstring help =
        app::HelpText(kVersion, kBackends,
                      MakeBackend(app::kDefaultBackend)->capabilities(),
                      settingsFile);
    if (!win::WriteToParentConsole(help)) {
      MessageBoxW(nullptr, help.c_str(), L"" APP_NAME L" — nápoveda",
                  MB_OK | MB_ICONINFORMATION);
    }
    CoUninitialize();
    return 0;
  }
  if (arguments.version) {
    const std::wstring text = std::wstring(L"" APP_NAME L" ") + kVersion + L"\n";
    if (!win::WriteToParentConsole(text)) {
      MessageBoxW(nullptr, text.c_str(), L"" APP_NAME, MB_OK | MB_ICONINFORMATION);
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
        L"" APP_NAME L": " + arguments.error + L".\n" +
        L"Zoznam volieb vypíše " APP_NAME L" --help.\n";
    if (!win::WriteToParentConsole(text)) {
      MessageBoxW(nullptr, text.c_str(), L"" APP_NAME, MB_OK | MB_ICONERROR);
    }
    CoUninitialize();
    return 2;
  }
  // Refused, not skipped, and out the same way as a bad option: the file was
  // written by hand for a reason, and a session started without it would be
  // the wrong mode with nothing said about why.  The file is named in full --
  // the reader has to open it, and %APPDATA% is not a place anyone finds by
  // guessing.
  if (!settings.error().empty()) {
    const std::wstring text = L"" APP_NAME L": nastavenia " + settingsFile +
                              L", " + settings.error() + L".\n";
    if (!win::WriteToParentConsole(text)) {
      MessageBoxW(nullptr, text.c_str(), L"" APP_NAME, MB_OK | MB_ICONERROR);
    }
    CoUninitialize();
    return 2;
  }
  // A previous update's folders go first, so they are gone whether or not this
  // start checks; then the check, which may end this process.
  updater::RemoveLeftover();
  if (UpdateBeforeStart(settings, settingsFile)) {
    CoUninitialize();
    return 0;
  }
  // After the checks, or --help typed on its own -- or a refused option --
  // would be answered with a dialog asking which project.
  if (arguments.project.empty()) {
    ui::NewSession choice;
    choice.backend = arguments.backend;
    for (const std::string& name : kBackends) {
      ui::NewSessionBackend entry;
      entry.name = name;
      entry.models = MakeBackend(name)->capabilities().models;
      entry.model = !typed.model.empty()
                        ? typed.model
                        : model::Utf16FromUtf8(settings.Get(
                              name + "." + app::kModelKey));
      choice.backends.push_back(std::move(entry));
    }
    ui::NewSessionDialog dialog(&choice);
    if (dialog.ShowModal(nullptr, IDD_NEW_SESSION) != IDOK) {
      CoUninitialize();
      return 0;  // cancelled, which is an answer
    }
    // The file read again for the backend chosen, then the model overwritten
    // with the field: an emptied field means the CLI's default, and
    // ApplySettings would fill it back in.
    arguments.backend = choice.backend;
    arguments.permissionMode = typed.permissionMode;
    app::ApplySettings(&arguments, settings);
    arguments.model = choice.model;
    arguments.project = choice.project;
    backend = MakeBackend(arguments.backend);
    app::CheckMode(&arguments, backend->capabilities());
    // Only a mode typed for one backend and refused by the other gets here.
    // A dialog, because the console this was started from -- if any -- has
    // been left behind by a dialog in between.
    if (!arguments.error.empty()) {
      const std::wstring text = L"" APP_NAME L": " + arguments.error + L".";
      MessageBoxW(nullptr, text.c_str(), L"" APP_NAME, MB_OK | MB_ICONERROR);
      CoUninitialize();
      return 2;
    }
  }
  // Expanded, because the folder is also what the list of conversations is
  // keyed by for -c, and "." is not a key.
  arguments.project = Expand(arguments.project);

  // RichEdit 4.1 comes from this library and the window class does not exist
  // until it is loaded.  Deliberately not freed: the class must outlive every
  // window that uses it, and that is the whole run.
  if (!LoadLibraryW(L"Msftedit.dll")) {
    MessageBoxW(nullptr, L"Nepodarilo sa načítať Msftedit.dll.", L"" APP_NAME,
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
    MessageBoxW(nullptr, L"Nepodarilo sa spustiť session.", L"" APP_NAME,
                MB_OK | MB_ICONERROR);
    return 1;
  }

  const int code = win::RunMessageLoop(window.handle(), nullptr);
  CoUninitialize();
  return code;
}
