#ifndef UI_MAIN_WINDOW_H
#define UI_MAIN_WINDOW_H

// The top-level window.  For now it holds exactly one SessionPane and does
// little but forward to it; when tabs arrive (claude-gui-lkk.7) this is where
// they go, and the pane will not have to change.

#include <memory>
#include <string>

#include "ui/session_pane.h"
#include "ui/status_bar.h"
#include "win/window.h"

namespace ui {

class MainWindow : public win::Window {
 public:
  // Takes the backend over and hands it on to the pane.
  bool Open(HINSTANCE instance, std::unique_ptr<agent::Backend> backend,
            const agent::StartOptions& options);

 protected:
  LRESULT HandleMessage(UINT message, WPARAM wParam, LPARAM lParam) override;

 private:
  // Status bar along the bottom, pane above it.
  void Arrange(int width, int height);
  // Says out loud -- through a dialog, the only way left -- that the speech
  // library is missing.  Once, at startup.
  void WarnIfMute();

 private:
  // Below the pane and outside it: with tabs there will be several panes and
  // still one bar.
  StatusBar status_;
  std::unique_ptr<SessionPane> pane_;
};

}  // namespace ui

#endif
