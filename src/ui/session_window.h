#ifndef UI_SESSION_WINDOW_H
#define UI_SESSION_WINDOW_H

// One session as an MDI child of the frame (claude-gui-lkk.7.9).
//
// The host the pane does not know about: it forwards the pane's kMsg*
// messages, tells it when it becomes the active session and when it stops
// being one, and hands it the frame's status bar for as long as it is.  The
// pane itself stays exactly what it was when it was the whole window.

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <functional>
#include <memory>

#include "agent/backend.h"
#include "ui/session_pane.h"
#include "ui/status_bar.h"
#include "win/window.h"

namespace ui {

class SessionWindow : public win::Window {
 public:
  // Called from WM_DESTROY, so that the frame can let go of this object --
  // later, never from inside the call: this window is still in its own
  // window procedure then.
  using Closed = std::function<void()>;

  // Makes the child, the pane in it, and starts the backend.  False when the
  // session did not start; the child is gone again by then.
  bool Open(HWND mdiClient, HINSTANCE instance, StatusBar* bar,
            std::unique_ptr<agent::Backend> backend,
            const agent::StartOptions& options, Closed closed);

  // Null once the session has been shut down.
  SessionPane* pane() const { return pane_.get(); }
  // Stops the backend in the right order -- the turn first, the pipe after
  // -- while the window and its message queue are still there.
  void ShutDown() { pane_.reset(); }

 protected:
  LRESULT HandleMessage(UINT message, WPARAM wParam, LPARAM lParam) override;
  LRESULT Default(UINT message, WPARAM wParam, LPARAM lParam) const override;

 private:
  void Activated(bool active);
  // Gives the client area a role and a name a screen reader says when the
  // focus enters it, so that switching sessions says which one it came to.
  void Annotate(const std::wstring& name);

  StatusBar* bar_ = nullptr;
  Closed closed_;
  std::unique_ptr<SessionPane> pane_;
};

}  // namespace ui

#endif
