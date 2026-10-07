#ifndef WIN_WINDOW_H
#define WIN_WINDOW_H

// A thin object wrapper over the Win32 window API.  Deliberately thin: it owns
// the class registration, the HWND lifetime and the WndProc thunk, and it maps
// menu command ids onto callables.  Everything else stays a plain Win32 call,
// because the code that needs this most -- the keyboard -- wants the message,
// not somebody's interpretation of it.
//
// Nothing in win/ knows about AgentAloud, so it can be lifted into another
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
  // An MDI child of the given MDICLIENT.  Made through WM_MDICREATE, which is
  // the one way the client keeps its list of children -- and its window menu
  // -- right.  A class whose children have to go through DefMDIChildProcW
  // says so by overriding Default().
  bool CreateMdiChild(const wchar_t* className, HWND mdiClient,
                      const std::wstring& title, DWORD style);

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
  // DefWindowProcW.  An MDI frame overrides it with DefFrameProcW and an MDI
  // child with DefMDIChildProcW; both insist on seeing messages the window
  // has handled itself, so those windows call it more often than others do.
  virtual LRESULT Default(UINT message, WPARAM wParam, LPARAM lParam) const;

  HWND hwnd_ = nullptr;

 private:
  static LRESULT CALLBACK WndProc(HWND window, UINT message, WPARAM wParam,
                                  LPARAM lParam);
  static bool Register(const wchar_t* className);
  bool Dispatch(int id);

  std::unordered_map<int, std::function<void()>> commands_;
};

// GetMessage, not PeekMessage: nothing is paced by this loop.  Whatever has to
// keep going runs on its own thread, so that a dropped-down menu or a modal
// dialog -- each of which spins its own message loop in here -- cannot stall
// it.
//
// Two tables rather than one, asked about per message: `always` is in force
// whatever the window is doing, `conditional` only while `conditionalActive`
// says so.  A window whose shortcut set depends on its state cannot express
// that any other way, because TranslateAccelerator eats the press before the
// window gets a say.  Either table may be null, and so may the predicate --
// then `conditional` never applies.
int RunMessageLoop(HWND window, HACCEL always, HACCEL conditional = nullptr,
                   std::function<bool()> conditionalActive = nullptr);

// The same loop for an MDI frame, with one table of accelerators.  Its own
// function rather than another parameter, because RunMessageLoop is shared
// with another project and must not change underneath it.
//
// TranslateMDISysAccel is deliberately not called.  What it gives -- Ctrl+F4,
// Ctrl+F6 -- goes through the child's system menu, which a child need not
// have, and it would answer a key with nothing where the frame has to say
// why.  The frame binds those keys in its own table instead, where they are
// listed with everything else.
int RunMdiMessageLoop(HWND frame, HACCEL accelerators);

}  // namespace win

#endif
