// ClaudeLens.  See CLAUDE.md for the shape of the thing; this file only
// starts it.

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <commctrl.h>
#include <objbase.h>
#include <shellapi.h>

#include <string>
#include <vector>

#include "proto/claude_backend.h"
#include "proto/sessions.h"
#include "ui/main_window.h"
#include "win/console.h"
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
  // --help was asked for, so nothing else on the line matters and no window is
  // to be opened.  A separate flag rather than an empty project, because an
  // empty project means "ask which folder" and that is a dialog -- the one
  // answer --help must not give.
  bool help = false;
  // Something on the command line was not understood.  Non-empty means no
  // window either: an option that was refused was typed for a reason, and
  // opening a session without it would be doing something other than what was
  // asked, silently.  The text goes out the same way the help does.
  std::wstring error;
};

// Whoever types --help types it at a prompt, so this is the one place the
// application answers in text rather than in a window.  Written out here, and
// not read from anywhere: a help text in a resource or a file is a help text
// that gets out of step with ReadArguments below, and the two are ten lines
// apart precisely so that they do not.
//
// It names every option this process understands, and then says what happens
// to the ones it does not, because that is the failure nobody would guess:
// --fork-session does not reach the CLI, it becomes a path.
std::wstring HelpText() {
  return
      L"ClaudeLens — okno namiesto terminálu pre Claude Code.\n"
      L"\n"
      L"Použitie:\n"
      L"  ClaudeLens [voľby] [priečinok]\n"
      L"\n"
      L"  priečinok\n"
      L"      Pracovný adresár session: rozhoduje o tom, ktoré CLAUDE.md\n"
      L"      a ktorý git repozitár platia a čoho sa smú dotknúť nástroje.\n"
      L"      Keď sa neuvedie, ClaudeLens sa naň spýta dialógom.\n"
      L"\n"
      L"Voľby:\n"
      L"  --permission-mode <režim>\n"
      L"      Režim povolení, v pravopise CLI: acceptEdits, auto,\n"
      L"      bypassPermissions, manual, dontAsk, plan.  Bez neho platí to,\n"
      L"      čo má nastavené CLI.\n"
      L"\n"
      L"  --model <alias|id>\n"
      L"      sonnet, haiku, opus alebo úplné id modelu.  Bez neho platí\n"
      L"      model z nastavení, teda ten drahý.\n"
      L"\n"
      L"  --resume <id|titul>, -r <id|titul>\n"
      L"      Pokračuje v pomenovanom rozhovore.  Predchádzajúce ťahy sa\n"
      L"      prečítajú z disku a kurzor stojí za nimi, na mieste, kde sa\n"
      L"      pokračuje; keď sa súbor nenájde — pod titulom sa nenájde\n"
      L"      nikdy — okno začne prázdne.\n"
      L"\n"
      L"  --continue, -c\n"
      L"      Pokračuje v poslednom rozhovore tohto priečinka.  Ktorý to je,\n"
      L"      vyberá ClaudeLens sám zo súborov v ~/.claude/projects — nie\n"
      L"      CLI, ktoré o headless session nevie.  Priečinok bez jediného\n"
      L"      rozhovoru začne novú session.  Spolu s --resume vyhráva\n"
      L"      --resume.\n"
      L"\n"
      L"  --help, -h\n"
      L"      Tento text.\n"
      L"\n"
      L"Nič iné sa CLI neposiela.  Voľba, ktorú ClaudeLens nepozná — napríklad\n"
      L"--fork-session — sa neprepošle a ani sa z nej nestane cesta: povie to\n"
      L"a skončí.  Priečinok projektu je prvý argument, ktorý sa\n"
      L"nezačína pomlčkou, takže priečinok s pomlčkou na začiatku mena sa\n"
      L"takto zadať nedá.\n"
      L"\n"
      L"Čo vie klávesnica, povie ClaudeLens sám: F1 vypíše všetky klávesy, F2\n"
      L"podrobnosti session a F4 otvorí zoznam slash príkazov.\n";
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

// `-c` means "carry on the last conversation in this folder".  Which one that
// is gets decided here and turned into a plain --resume, rather than passed to
// the CLI as --continue: the CLI answers that question out of
// ~/.claude/history.jsonl, where only interactively typed prompts are written,
// so for a folder used from both a terminal and ClaudeLens it would carry on
// the terminal's conversation and call it ours.  See proto/sessions.h.
//
// It says nothing about which one it picked.  The restored transcript is the
// answer: the first prompt of that conversation is its first block, and
// Ctrl+Home leads to it.  A sentence in front of it would be the application
// answering a question nobody asked -- resuming is a deliberate act.
void ContinueLatest(Arguments* arguments) {
  // `-c --resume <id>` is not a contradiction to argue about: the one that
  // names a conversation wins, because it was typed by someone who knew which
  // one they wanted.  Two --resume on one command line would be the CLI's
  // problem to report, and it reports it on a stderr this process has not got.
  if (proto::ResumesConversation(arguments->extraArgs)) return;
  proto::SessionSummary latest;
  // Nothing to carry on is not an error and not a reason to refuse: a folder
  // nobody has worked in yet gets a new session, which is what was wanted, and
  // it is left looking exactly like one -- an empty project behaving like an
  // empty project.
  if (!proto::LatestSession(arguments->project, &latest)) return;
  arguments->extraArgs.push_back(L"--resume");
  arguments->extraArgs.push_back(latest.id);
}

Arguments ReadArguments() {
  Arguments arguments;
  bool continueLatest = false;
  int argc = 0;
  wchar_t** argv = CommandLineToArgvW(GetCommandLineW(), &argc);
  if (argv) {
    // The first complaint is the one that gets said, and the reading carries
    // on regardless: --help typed after a typo is still a request for help,
    // and the help text is the better answer to both.
    auto complain = [&arguments](const std::wstring& text) {
      if (arguments.error.empty()) arguments.error = text;
    };
    for (int i = 1; i < argc; ++i) {
      const std::wstring argument = argv[i];
      // An option that takes a value and stands last on the line would
      // otherwise fall through to the branches below and be taken for the
      // project folder -- the same silent swap this whole check exists to
      // stop, only one argument further along.
      const bool wantsValue = argument == L"--permission-mode" ||
                              argument == L"--model";
      if (wantsValue && i + 1 >= argc) {
        complain(L"voľba " + argument + L" potrebuje hodnotu");
        break;
      }
      // The CLI's own spelling, and its own values -- acceptEdits, auto,
      // bypassPermissions, manual, dontAsk, plan.  Not validated here: the
      // CLI rejects what it does not know, and a second list of legal values
      // in this file would be a list that goes stale.
      if (argument == L"--permission-mode") {
        arguments.permissionMode = argv[++i];
      // An alias -- sonnet, haiku, opus -- or a full model id.  Not validated
      // here for the same reason: the list of what the CLI takes is the CLI's,
      // and a copy of it here would be a copy that goes stale.
      } else if (argument == L"--model") {
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
      // Read wherever it stands and nothing after it is looked at: `ClaudeLens
      // . --help` is a request for help, not a session in this folder.
      } else if (argument == L"--help" || argument == L"-h") {
        arguments.help = true;
        break;
      // Anything else that starts with a dash is an option this process does
      // not have, and the one thing it must not become is the project folder:
      // that is how --fork-session used to end up as a path and the session
      // started somewhere that does not exist, with nothing said about why.
      // A bare "-" is caught by the same rule rather than by an exception --
      // it is not a folder anybody means on Windows, and a rule with one
      // exception is a rule nobody remembers.
      } else if (!argument.empty() && argument[0] == L'-') {
        complain(L"neznáma voľba " + argument);
      } else if (arguments.project.empty()) {
        arguments.project = argument;
      }
    }
    LocalFree(argv);
  }
  // Before the folder picker, or --help typed on its own would answer with a
  // dialog asking which project -- and then, once cancelled, with nothing.
  // A refused option gets out ahead of the picker for the same reason: being
  // asked which folder is a strange answer to "there is no such option".
  if (arguments.help) return arguments;
  if (!arguments.error.empty()) return arguments;
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

  // Read before anything is loaded and long before anything is shown, because
  // --help must end without a window ever existing -- and Msftedit.dll below
  // is the first thing that would put one on screen if it failed.
  const Arguments arguments = ReadArguments();
  if (arguments.help) {
    // The dialog is the wrong answer here and it is the fallback anyway: it is
    // reached only when there is no console up the tree and no redirection,
    // which means Explorer or a shortcut.  AllocConsole would be worse -- the
    // window it makes dies with the process, so the text would appear and
    // vanish, which is the same as not printing it.
    if (!win::WriteToParentConsole(HelpText())) {
      MessageBoxW(nullptr, HelpText().c_str(), L"ClaudeLens — nápoveda",
                  MB_OK | MB_ICONINFORMATION);
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
  if (arguments.project.empty()) return 0;  // cancelled, which is an answer

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
  // --resume travels here, value and all, until the port takes it over
  // (claude-gui-lkk.44.4, step d).
  options.extraArgs = arguments.extraArgs;

  // The one place that knows which CLI is behind the window.
  ui::MainWindow window;
  if (!window.Open(instance, std::make_unique<proto::ClaudeBackend>(),
                   options)) {
    MessageBoxW(nullptr, L"Nepodarilo sa spustiť session.", L"ClaudeLens",
                MB_OK | MB_ICONERROR);
    return 1;
  }

  const int code = win::RunMessageLoop(window.handle(), nullptr);
  CoUninitialize();
  return code;
}
