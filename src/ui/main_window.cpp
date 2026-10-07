#include "ui/main_window.h"

#include <algorithm>
#include <string>

#include "app_name.h"
#include "i18n/i18n.h"
#include "session_names.h"

namespace ui {
namespace {

// Menu commands, and the accelerators that send the same ids.  Far below the
// MDI client's window list, which counts up from kFirstChild.
enum : UINT {
  kIdNewSession = 40001,
  kIdExit,
  kIdNextSession,
  kIdPreviousSession,
  kIdCloseSession,
};
constexpr UINT kFirstChild = 50000;

// A session window has gone and its object can be dropped.  Posted by the
// child from WM_DESTROY -- it is still inside its own window procedure then.
constexpr UINT kMsgReap = WM_APP + 20;

HMENU BuildMenu(HMENU* windowMenu) {
  using i18n::Str;
  HMENU file = CreatePopupMenu();
  AppendMenuW(file, MF_STRING, kIdNewSession, i18n::Text(Str::kMenuNewSession));
  AppendMenuW(file, MF_SEPARATOR, 0, nullptr);
  AppendMenuW(file, MF_STRING, kIdExit, i18n::Text(Str::kMenuExit));

  // The client appends the list of open sessions below these, by their
  // titles -- which is why a session's title is its folder.
  HMENU window = CreatePopupMenu();
  AppendMenuW(window, MF_STRING, kIdNextSession,
              i18n::Text(Str::kMenuNextSession));
  AppendMenuW(window, MF_STRING, kIdPreviousSession,
              i18n::Text(Str::kMenuPreviousSession));
  AppendMenuW(window, MF_STRING, kIdCloseSession,
              i18n::Text(Str::kMenuCloseSession));

  HMENU bar = CreateMenu();
  AppendMenuW(bar, MF_POPUP, reinterpret_cast<UINT_PTR>(file),
              i18n::Text(Str::kMenuFile));
  AppendMenuW(bar, MF_POPUP, reinterpret_cast<UINT_PTR>(window),
              i18n::Text(Str::kMenuWindow));
  *windowMenu = window;
  return bar;
}

// The keys of the menu.  Ctrl+F4 and Ctrl+F6 are MDI's own; Ctrl+Tab is what
// other MDI and tabbed programs use for the same thing, and it reaches the
// table before either box would see it -- the prompt used to take it as Tab.
HACCEL BuildAccelerators() {
  ACCEL table[] = {
      {FCONTROL | FVIRTKEY, 'N', kIdNewSession},
      {FCONTROL | FVIRTKEY, VK_TAB, kIdNextSession},
      {FCONTROL | FSHIFT | FVIRTKEY, VK_TAB, kIdPreviousSession},
      {FCONTROL | FVIRTKEY, VK_F6, kIdNextSession},
      {FCONTROL | FSHIFT | FVIRTKEY, VK_F6, kIdPreviousSession},
      {FCONTROL | FVIRTKEY, VK_F4, kIdCloseSession},
  };
  return CreateAcceleratorTableW(table, ARRAYSIZE(table));
}

}  // namespace

MainWindow::~MainWindow() {
  if (accelerators_) DestroyAcceleratorTable(accelerators_);
}

bool MainWindow::Open(HINSTANCE instance,
                      std::unique_ptr<agent::Backend> backend,
                      const agent::StartOptions& options,
                      NewSessionFactory factory) {
  instance_ = instance;
  factory_ = std::move(factory);
  HMENU windowMenu = nullptr;
  HMENU menu = BuildMenu(&windowMenu);
  if (!Create(L"" APP_NAME L"Main", L"" APP_NAME,
              WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN, 900, 700, menu)) {
    DestroyMenu(menu);
    return false;
  }
  accelerators_ = BuildAccelerators();
  if (!status_.Create(hwnd_, instance)) return false;

  CLIENTCREATESTRUCT create = {};
  create.hWindowMenu = windowMenu;
  create.idFirstChild = kFirstChild;
  // MDIS_ALLCHILDSTYLES: the child's style is the one SessionWindow asks
  // for, every bit of it chosen for a reason, not the client's default.
  client_ = CreateWindowExW(0, L"MDICLIENT", nullptr,
                            WS_CHILD | WS_CLIPCHILDREN | WS_VISIBLE |
                                MDIS_ALLCHILDSTYLES,
                            0, 0, 0, 0, hwnd_, nullptr, instance, &create);
  if (!client_) return false;
  speech_.Open();

  RECT client = {};
  GetClientRect(hwnd_, &client);
  Arrange(client.right, client.bottom);

  if (!OpenSession(std::move(backend), options)) return false;
  Show(SW_SHOW);
  if (SessionWindow* active = Active()) active->pane()->FocusPrompt();
  WarnIfMute();
  return true;
}

bool MainWindow::OpenSession(std::unique_ptr<agent::Backend> backend,
                             const agent::StartOptions& options) {
  // Owned before it is opened: a session that fails to start destroys its
  // window on the way out, and the reap that follows has to find it.
  sessions_.push_back(std::make_unique<SessionWindow>());
  SessionWindow* session = sessions_.back().get();
  if (!session->Open(client_, instance_, &status_, std::move(backend), options,
                     [this] { PostMessageW(hwnd_, kMsgReap, 0, 0); })) {
    return false;
  }
  Renumber();
  return true;
}

void MainWindow::Renumber() {
  // Only the running ones: a session being closed has let go of its pane
  // and is about to be reaped, and counting it would leave a gap.
  std::vector<SessionWindow*> open;
  std::vector<std::wstring> keys;
  for (const auto& session : sessions_) {
    if (session->pane() == nullptr) continue;
    open.push_back(session.get());
    keys.push_back(session->folderKey());
  }
  const std::vector<int> ordinals = app::SessionOrdinals(keys);
  for (size_t i = 0; i < open.size(); ++i) open[i]->SetOrdinal(ordinals[i]);
}

void MainWindow::NewSession() {
  std::unique_ptr<agent::Backend> backend;
  agent::StartOptions options;
  if (!factory_ || !factory_(hwnd_, &backend, &options)) return;
  if (!OpenSession(std::move(backend), options)) {
    MessageBoxW(hwnd_, i18n::Text(i18n::Str::kSessionStartFailed),
                L"" APP_NAME, MB_OK | MB_ICONERROR);
    return;
  }
  // The focus follows the new session into its prompt.  The dialog that
  // asked for it has just closed, so this is the change of focus NVDA
  // announces -- and the window title it reads with it names the folder.
  if (SessionWindow* active = Active()) active->pane()->FocusPrompt();
}

SessionWindow* MainWindow::Active() const {
  if (!client_) return nullptr;
  const HWND active =
      reinterpret_cast<HWND>(SendMessageW(client_, WM_MDIGETACTIVE, 0, 0));
  for (const auto& session : sessions_) {
    if (session->handle() == active && session->pane()) return session.get();
  }
  return nullptr;
}

void MainWindow::StepSession(bool backwards) {
  SessionWindow* active = Active();
  const size_t open = std::count_if(
      sessions_.begin(), sessions_.end(),
      [](const auto& session) { return session->pane() != nullptr; });
  // Said, not beeped: a key that does nothing has to say why (invariant 6),
  // and NVDA announces nothing when the focus does not move.
  if (active == nullptr) {
    Announce(i18n::Text(i18n::Str::kSayNoSession));
    return;
  }
  if (open < 2) {
    Announce(i18n::Text(i18n::Str::kSayOnlySession));
    return;
  }
  // The focus goes with the activation (SessionWindow's WM_SETFOCUS), and
  // NVDA reads the change: the frame's title with the folder in it, then the
  // box the reader left in that session.
  SendMessageW(client_, WM_MDINEXT, reinterpret_cast<WPARAM>(active->handle()),
               backwards ? 1 : 0);
}

void MainWindow::CloseSession() {
  SessionWindow* active = Active();
  if (active == nullptr) {
    Announce(i18n::Text(i18n::Str::kSayNoSession));
    return;
  }
  SendMessageW(active->handle(), WM_CLOSE, 0, 0);
}

void MainWindow::Reap() {
  std::erase_if(sessions_,
                [](const auto& session) { return session->handle() == nullptr; });
  if (!sessions_.empty()) {
    Renumber();
    return;
  }
  // The last session's facts would otherwise stay in the bar and be read on
  // NVDA+End as if it were still running.
  for (int field = 0; field < StatusBar::kFieldCount; ++field) {
    status_.Set(static_cast<StatusBar::Field>(field), L"");
  }
}

void MainWindow::Announce(const std::wstring& text) {
  if (speech_.available()) {
    speech_.Say(text, true);
    return;
  }
  MessageBeep(MB_ICONASTERISK);
}

void MainWindow::WarnIfMute() {
  if (speech_.loaded()) return;
  // A dialog, and not the status bar or the title, because this is the one
  // message the application cannot say itself: without the library it has no
  // voice, and every Announce falls back to a beep.  A dialog is read out by
  // NVDA's own machinery, so it arrives even here.
  //
  // Worth the interruption because the symptom is unreadable.  Beep already
  // means "the turn is over and nothing was said" (SignalTurnEnd) -- and
  // without a voice every turn ends that way -- so a beep to every keypress
  // reads as "this key does nothing": the copy of the .exe looks broken rather
  // than incomplete, and Ctrl+Enter sounds refused while it is in fact
  // sending.
  const std::wstring text =
      i18n::Format(i18n::Str::kNoSpeechDll, {L"" APP_EXE, L"" APP_NAME});
  const std::wstring title = std::wstring(L"" APP_NAME L" — ") +
                             i18n::Text(i18n::Str::kNoSpeechTitle);
  MessageBoxW(hwnd_, text.c_str(), title.c_str(), MB_OK | MB_ICONWARNING);
}

void MainWindow::Arrange(int width, int height) {
  // The bar first: it decides its own height from the font, and what is left
  // is what the sessions may have.
  const int barHeight = status_.Resize(width);
  if (client_) MoveWindow(client_, 0, 0, width, height - barHeight, TRUE);
}

LRESULT MainWindow::Default(UINT message, WPARAM wParam, LPARAM lParam) const {
  return DefFrameProcW(hwnd_, client_, message, wParam, lParam);
}

LRESULT MainWindow::HandleMessage(UINT message, WPARAM wParam, LPARAM lParam) {
  switch (message) {
    case WM_SIZE:
      // Not passed on: DefFrameProcW would stretch the client over the whole
      // window, status bar included.
      Arrange(LOWORD(lParam), HIWORD(lParam));
      return 0;

    case WM_COMMAND:
      switch (LOWORD(wParam)) {
        case kIdNewSession:
          NewSession();
          return 0;
        case kIdExit:
          SendMessageW(hwnd_, WM_CLOSE, 0, 0);
          return 0;
        case kIdNextSession:
          StepSession(false);
          return 0;
        case kIdPreviousSession:
          StepSession(true);
          return 0;
        case kIdCloseSession:
          CloseSession();
          return 0;
        default:
          // The window list in the menu is DefFrameProcW's.
          break;
      }
      break;

    case WM_DPICHANGED: {
      // Windows hands us the rectangle this window should take on the screen
      // it just moved to.  A per-monitor aware process that ignores it keeps
      // the size it had at the old scaling, which is the whole cost of asking
      // for the awareness in the first place (see wWinMain).
      //
      // The fonts first: the SetWindowPos below sends WM_SIZE, and the layout
      // it triggers has to measure against the font it will actually draw.
      for (const auto& session : sessions_) {
        if (session->pane()) session->pane()->OnDpiChanged();
      }
      const RECT* suggested = reinterpret_cast<const RECT*>(lParam);
      SetWindowPos(hwnd_, nullptr, suggested->left, suggested->top,
                   suggested->right - suggested->left,
                   suggested->bottom - suggested->top,
                   SWP_NOZORDER | SWP_NOACTIVATE);
      return 0;
    }

    case WM_ACTIVATE:
      // Coming back to a window that is waiting for an answer.  A modal box
      // DISABLES its owner, and focus cannot be put on a child of a disabled
      // window -- SetFocus just fails, and it fails silently.  Measured
      // 2026-09-05: the reader came back to a permission box, heard the window
      // title and nothing else, and had to press Tab to reach the box at all.
      //
      // The box is where the focus belongs then, and Windows knows which one
      // it is: GW_ENABLEDPOPUP exists for exactly this question.  Handled on
      // WM_ACTIVATE rather than WM_SETFOCUS because a disabled window is not
      // where the focus lands in the first place.
      if (LOWORD(wParam) != WA_INACTIVE && !IsWindowEnabled(hwnd_)) {
        if (HWND waiting = GetWindow(hwnd_, GW_ENABLEDPOPUP)) {
          SetForegroundWindow(waiting);
        }
      }
      return 0;

    case WM_SETFOCUS:
      // The frame itself is never a useful place for focus to sit; a screen
      // reader would announce the window and then nothing.  The active
      // session decides where it goes (SessionWindow's WM_SETFOCUS): back to
      // the box the reader left, not always the prompt.
      //
      // Not while a modal box is up: see WM_ACTIVATE above.
      if (IsWindowEnabled(hwnd_)) {
        if (SessionWindow* active = Active()) SetFocus(active->handle());
      }
      return 0;

    case kMsgReap:
      Reap();
      return 0;

    case WM_CLOSE:
      // Every session's backend shut down in the right order -- the turn
      // first, the pipe after -- while the windows and the queue they post to
      // are still there.
      for (const auto& session : sessions_) session->ShutDown();
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
