#ifndef UI_MAIN_WINDOW_H
#define UI_MAIN_WINDOW_H

// The top-level window: an MDI frame with a menu bar, one status bar, and a
// session in each child (claude-gui-lkk.7.9).  MDI rather than tabs:
// Ctrl+Tab, Ctrl+F4 and a window menu listing what is open are what a reader
// already knows from other MDI programs, and NVDA reads them well.
//
// The frame does not know which CLIs exist.  A new session is asked for
// through NewSessionFactory, which main.cpp provides -- it is the one place
// that knows the adapters.

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <functional>
#include <memory>
#include <vector>

#include "agent/backend.h"
#include "ui/session_window.h"
#include "ui/speech.h"
#include "ui/status_bar.h"
#include "win/window.h"

namespace ui {

// Asks the reader what to start -- the dialog Nová session -- and makes it.
// False when they cancelled, or when what they chose was refused; the
// factory has said why then.
using NewSessionFactory =
    std::function<bool(HWND owner, std::unique_ptr<agent::Backend>* backend,
                       agent::StartOptions* options)>;

// Pomocník → Skontrolovať aktualizácie.  From main.cpp, which holds the
// settings the answer may write (a skipped version); the frame does not.
using UpdateCheck = std::function<void(HWND owner)>;

class MainWindow : public win::Window {
 public:
  ~MainWindow() override;

  // Takes the backend over and hands it on to the first session.  On false,
  // *failure is why the session did not start, or empty when that is not
  // known.
  bool Open(HINSTANCE instance, std::unique_ptr<agent::Backend> backend,
            const agent::StartOptions& options, NewSessionFactory factory,
            UpdateCheck checkForUpdates, std::wstring* failure);

  // For win::RunMdiMessageLoop.
  HACCEL accelerators() const { return accelerators_; }

 protected:
  LRESULT HandleMessage(UINT message, WPARAM wParam, LPARAM lParam) override;
  LRESULT Default(UINT message, WPARAM wParam, LPARAM lParam) const override;

 private:
  bool OpenSession(std::unique_ptr<agent::Backend> backend,
                   const agent::StartOptions& options, std::wstring* failure);
  // The menu items and their keys.
  void NewSession();
  void StepSession(bool backwards);
  void CloseSession();
  // F1.  The list is the active session's, because what Shift+Tab and F4 do
  // depends on its agent; without a session there is nothing true to list.
  void ShowKeys();
  // The session the reader is in, or null when none is open.
  SessionWindow* Active() const;
  // Numbers the sessions that share a folder, so that their titles differ.
  // After every open and close: closing the second of three makes the third
  // the second.
  void Renumber();
  // Drops the sessions whose windows are gone.  Posted, never called from a
  // child's own window procedure.
  void Reap();
  // Status bar along the bottom, the MDI client above it.
  void Arrange(int width, int height);
  // Says out loud -- through a dialog, the only way left -- that the speech
  // library is missing.  Once, at startup.
  void WarnIfMute();
  // The frame's own answer to a key -- with no session open there is no pane
  // to speak through.  Interrupts, being an answer to a key (invariant 7),
  // and beeps without a reader, like the pane does (invariant 6).
  void Announce(const std::wstring& text);

 private:
  Speech speech_;
  HINSTANCE instance_ = nullptr;
  HWND client_ = nullptr;
  HACCEL accelerators_ = nullptr;
  NewSessionFactory factory_;
  UpdateCheck checkForUpdates_;
  // Below the client and outside it: one bar for all the sessions, showing
  // the active one.
  StatusBar status_;
  std::vector<std::unique_ptr<SessionWindow>> sessions_;
};

}  // namespace ui

#endif
