#include "ui/main_window.h"

namespace ui {

bool MainWindow::Open(HINSTANCE instance,
                      const proto::Session::Options& options) {
  if (!Create(L"ClaudeLensMain", L"ClaudeLens",
              WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN, 900, 700, nullptr)) {
    return false;
  }
  pane_ = std::make_unique<SessionPane>();
  if (!pane_->Create(hwnd_, instance)) return false;
  if (!status_.Create(hwnd_, instance)) return false;
  pane_->SetStatusBar(&status_);

  RECT client = {};
  GetClientRect(hwnd_, &client);
  Arrange(client.right, client.bottom);

  if (!pane_->Start(options)) return false;
  Show(SW_SHOW);
  pane_->FocusPrompt();
  WarnIfMute();
  return true;
}

void MainWindow::WarnIfMute() {
  if (!pane_ || pane_->speechInstalled()) return;
  // A dialog, and not the status bar or the title, because this is the one
  // message the application cannot say itself: without the library it has no
  // voice, and every Announce falls back to a beep.  A dialog is read out by
  // NVDA's own machinery, so it arrives even here.
  //
  // Worth the interruption because the symptom is unreadable.  Beep already
  // means "the turn is over" (SpeakAnswer), so a beep to every keypress reads
  // as "this key does nothing" -- the copy of the .exe looks broken rather
  // than incomplete, and Ctrl+Enter sounds refused while it is in fact
  // sending.
  MessageBoxW(hwnd_,
              L"Vedľa ClaudeLens.exe chýba nvdaControllerClient.dll, takže "
              L"aplikácia nemá ako hovoriť: klávesy fungujú, ale namiesto "
              L"hlásení pípajú.\n\nSkopírujte knižnicu z bin\\ vedľa .exe "
              L"a spustite ClaudeLens znova.",
              L"ClaudeLens — bez reči", MB_OK | MB_ICONWARNING);
}

void MainWindow::Arrange(int width, int height) {
  // The bar first: it decides its own height from the font, and what is left
  // is what the pane may have.
  const int barHeight = status_.Resize(width);
  if (pane_) pane_->Layout(width, height - barHeight);
}

LRESULT MainWindow::HandleMessage(UINT message, WPARAM wParam, LPARAM lParam) {
  switch (message) {
    case WM_SIZE:
      Arrange(LOWORD(lParam), HIWORD(lParam));
      return 0;

    case WM_SETFOCUS:
      // The window itself is never a useful place for focus to sit; a screen
      // reader would announce the window and then nothing.  Where it goes is
      // the pane's business: back to the box the reader left, not always the
      // prompt -- coming back from another application used to cost them
      // their place in the transcript.
      if (pane_) pane_->RestoreFocus();
      return 0;

    case kMsgDrain:
      if (pane_) pane_->OnDrain();
      return 0;

    case kMsgPermission:
      return pane_ ? pane_->OnPermission(lParam) : 0;

    case WM_CLOSE:
      // Let the pane's Session shut the child down in the right order -- the
      // turn first, the pipe after.  Destroying the window first would take
      // the message queue away while the reader thread still wants it.
      pane_.reset();
      DestroyWindow(hwnd_);
      return 0;

    case WM_DESTROY:
      PostQuitMessage(0);
      return 0;

    default:
      break;
  }
  return Default(message, wParam, lParam);
}

}  // namespace ui
