// ClaudeLens.  See CLAUDE.md for the shape of the thing; this file only
// starts it.

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <commctrl.h>
#include <objbase.h>
#include <shellapi.h>

#include <string>

#include "ui/main_window.h"
#include "win/dialog.h"

namespace {

// The working directory is the project: it decides CLAUDE.md, the git repo and
// what the tools may touch.  It is never guessed from where the .exe happens
// to sit, because that is how a session ends up rooted somewhere harmless
// looking and wrong.
std::wstring ChooseProject() {
  int argc = 0;
  wchar_t** argv = CommandLineToArgvW(GetCommandLineW(), &argc);
  std::wstring project;
  if (argv) {
    if (argc > 1) project = argv[1];
    LocalFree(argv);
  }
  if (!project.empty()) return project;
  return win::PickFolder(nullptr, L"Vyberte priečinok projektu");
}

}  // namespace

// wWinMain, not wmain, and linked with -mwindows.  A console-subsystem
// executable is given a console by Windows whether it wants one or not, so
// the first version put a black window on screen beside the application --
// which is precisely the thing this program exists to get away from.
int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int) {
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

  const std::wstring project = ChooseProject();
  if (project.empty()) return 0;  // cancelled, which is an answer

  proto::Session::Options options;
  options.workingDir = project;

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
