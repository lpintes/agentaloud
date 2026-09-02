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

  RECT client = {};
  GetClientRect(hwnd_, &client);
  pane_->Layout(client.right, client.bottom);

  if (!pane_->Start(options)) return false;
  Show(SW_SHOW);
  pane_->FocusPrompt();
  return true;
}

LRESULT MainWindow::HandleMessage(UINT message, WPARAM wParam, LPARAM lParam) {
  switch (message) {
    case WM_SIZE:
      if (pane_) pane_->Layout(LOWORD(lParam), HIWORD(lParam));
      return 0;

    case WM_SETFOCUS:
      // The window itself is never a useful place for focus to sit; a screen
      // reader would announce the window and then nothing.
      if (pane_) pane_->FocusPrompt();
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
