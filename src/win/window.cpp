#include "win/window.h"

namespace win {
namespace {

// UNICODE is not defined project-wide, so IDC_ARROW and IDI_APPLICATION expand
// to the ANSI form of MAKEINTRESOURCE and will not go into a ...W call.  The
// numbers are the ones winuser.h uses; spelling them out here is cheaper than
// switching the whole project's A/W macros over for two constants.
const LPCWSTR kArrowCursor = MAKEINTRESOURCEW(32512);
const LPCWSTR kApplicationIcon = MAKEINTRESOURCEW(32512);

}  // namespace

Window::~Window() {
  // The thunk clears hwnd_ on WM_NCDESTROY, so a window already gone leaves
  // nothing to do here.
  if (hwnd_) DestroyWindow(hwnd_);
}

LRESULT CALLBACK Window::WndProc(HWND window, UINT message, WPARAM wParam,
                                 LPARAM lParam) {
  Window* self = nullptr;
  if (message == WM_NCCREATE) {
    auto* create = reinterpret_cast<CREATESTRUCTW*>(lParam);
    self = static_cast<Window*>(create->lpCreateParams);
    self->hwnd_ = window;
    SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
  } else {
    self = reinterpret_cast<Window*>(GetWindowLongPtrW(window, GWLP_USERDATA));
  }
  if (!self) return DefWindowProcW(window, message, wParam, lParam);

  const LRESULT result = self->HandleMessage(message, wParam, lParam);
  // After this the HWND is gone; keeping the pointer would let the destructor
  // destroy a window that belongs to somebody else by then.
  if (message == WM_NCDESTROY) {
    SetWindowLongPtrW(window, GWLP_USERDATA, 0);
    self->hwnd_ = nullptr;
  }
  return result;
}

bool Window::Create(const wchar_t* className, const std::wstring& title,
                    DWORD style, int clientWidth, int clientHeight,
                    HMENU menu) {
  const HINSTANCE instance = GetModuleHandleW(nullptr);
  WNDCLASSEXW existing{};
  existing.cbSize = sizeof(existing);
  if (!GetClassInfoExW(instance, className, &existing)) {
    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = &Window::WndProc;
    wc.hInstance = instance;
    wc.hCursor = LoadCursorW(nullptr, kArrowCursor);
    wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
    wc.lpszClassName = className;
    wc.hIcon = LoadIconW(nullptr, kApplicationIcon);
    if (!RegisterClassExW(&wc)) return false;
  }

  RECT rect{0, 0, clientWidth, clientHeight};
  AdjustWindowRect(&rect, style, menu != nullptr);
  CreateWindowExW(0, className, title.c_str(), style, CW_USEDEFAULT,
                  CW_USEDEFAULT, rect.right - rect.left, rect.bottom - rect.top,
                  nullptr, menu, instance, this);
  return hwnd_ != nullptr;
}

void Window::Show(int cmdShow) const {
  if (!hwnd_) return;
  ShowWindow(hwnd_, cmdShow);
  UpdateWindow(hwnd_);
}

void Window::SetTitle(const std::wstring& title) const {
  if (hwnd_) SetWindowTextW(hwnd_, title.c_str());
}

void Window::Destroy() {
  if (hwnd_) DestroyWindow(hwnd_);
}

void Window::OnCommand(int id, std::function<void()> action) {
  commands_[id] = std::move(action);
}

bool Window::Dispatch(int id) {
  const auto found = commands_.find(id);
  if (found == commands_.end()) return false;
  found->second();
  return true;
}

LRESULT Window::Default(UINT message, WPARAM wParam, LPARAM lParam) const {
  return DefWindowProcW(hwnd_, message, wParam, lParam);
}

LRESULT Window::HandleMessage(UINT message, WPARAM wParam, LPARAM lParam) {
  switch (message) {
    case WM_COMMAND:
      if (Dispatch(LOWORD(wParam))) return 0;
      break;
    case WM_DESTROY:
      PostQuitMessage(0);
      return 0;
    default:
      break;
  }
  return Default(message, wParam, lParam);
}

int RunMessageLoop(HWND window, HACCEL always, HACCEL conditional,
                   std::function<bool()> conditionalActive) {
  MSG message{};
  BOOL result = 0;
  while ((result = GetMessageW(&message, nullptr, 0, 0)) != 0) {
    if (result == -1) break;
    if (always && TranslateAcceleratorW(window, always, &message)) continue;
    // Asked afresh every message: the answer changes while the program runs.
    if (conditional && conditionalActive && conditionalActive() &&
        TranslateAcceleratorW(window, conditional, &message))
      continue;
    TranslateMessage(&message);
    DispatchMessageW(&message);
  }
  return static_cast<int>(message.wParam);
}

}  // namespace win
