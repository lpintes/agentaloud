#ifndef WIN_WINDOW_H
#define WIN_WINDOW_H

// A thin object wrapper over the Win32 window API.  Deliberately thin: it owns
// the class registration, the HWND lifetime and the WndProc thunk, and it maps
// menu command ids onto callables.  Everything else stays a plain Win32 call,
// because the code that needs this most -- the keyboard -- wants the message,
// not somebody's interpretation of it.
//
// Nothing in win/ knows about the emulator, so it can be lifted into another
// project as it stands.

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <functional>
#include <string>
#include <unordered_map>

namespace win {

class Window {
 public:
  Window() = default;
  virtual ~Window();
  Window(const Window&) = delete;
  Window& operator=(const Window&) = delete;

  // menu may be null.  Size is the wanted client area; the frame is added on
  // top, so the caller does not have to know how thick this Windows draws it.
  bool Create(const wchar_t* className, const std::wstring& title, DWORD style,
              int clientWidth, int clientHeight, HMENU menu);

  HWND handle() const { return hwnd_; }
  void Show(int cmdShow) const;
  void SetTitle(const std::wstring& title) const;
  void Destroy();

  // Menu items, accelerators and buttons all arrive as WM_COMMAND with an id,
  // so one map keyed by id is the whole event plumbing this needs.  A lambda
  // per command reads well and keeps the handler next to the thing it serves.
  void OnCommand(int id, std::function<void()> action);

 protected:
  // Return the result, or call Default() for anything not handled.
  virtual LRESULT HandleMessage(UINT message, WPARAM wParam, LPARAM lParam);
  LRESULT Default(UINT message, WPARAM wParam, LPARAM lParam) const;

  HWND hwnd_ = nullptr;

 private:
  static LRESULT CALLBACK WndProc(HWND window, UINT message, WPARAM wParam,
                                  LPARAM lParam);
  bool Dispatch(int id);

  std::unordered_map<int, std::function<void()>> commands_;
};

// GetMessage, not PeekMessage: nothing is paced by this loop any more.  The
// emulator runs on its own thread precisely so that a dropped-down menu or a
// modal dialog -- each of which spins its own message loop in here -- cannot
// stop the machine and dry the sound out.
//
// Two tables rather than one, asked about per message: `always` is in force
// whatever the window is doing, `conditional` only while `conditionalActive`
// says so.  A window whose shortcut set depends on its state cannot express
// that any other way, because TranslateAccelerator eats the press before the
// window gets a say.  Either table may be null, and so may the predicate --
// then `conditional` never applies.
int RunMessageLoop(HWND window, HACCEL always, HACCEL conditional = nullptr,
                   std::function<bool()> conditionalActive = nullptr);

}  // namespace win

#endif
