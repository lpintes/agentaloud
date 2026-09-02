#ifndef UI_MAIN_WINDOW_H
#define UI_MAIN_WINDOW_H

// The top-level window.  For now it holds exactly one SessionPane and does
// little but forward to it; when tabs arrive (claude-gui-lkk.7) this is where
// they go, and the pane will not have to change.

#include <memory>

#include "ui/session_pane.h"
#include "win/window.h"

namespace ui {

class MainWindow : public win::Window {
 public:
  bool Open(HINSTANCE instance, const proto::Session::Options& options);

 protected:
  LRESULT HandleMessage(UINT message, WPARAM wParam, LPARAM lParam) override;

 private:
  std::unique_ptr<SessionPane> pane_;
};

}  // namespace ui

#endif
