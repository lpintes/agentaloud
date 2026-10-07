#include "ui/session_window.h"

#include <oleacc.h>

#include <string>

#include "app_name.h"

namespace ui {
namespace {

// The two properties Annotate puts on the child's client area.
const MSAAPROPID kAnnotated[] = {PROPID_ACC_ROLE, PROPID_ACC_NAME};

// The last component of the path: the same name the status bar gives the
// project.  The whole path is in the frame's title, read on request.
std::wstring FolderName(std::wstring path) {
  while (!path.empty() && (path.back() == L'\\' || path.back() == L'/')) {
    path.pop_back();
  }
  const size_t slash = path.find_last_of(L"\\/");
  return slash == std::wstring::npos ? path : path.substr(slash + 1);
}

IAccPropServices* PropServices() {
  IAccPropServices* services = nullptr;
  CoCreateInstance(CLSID_AccPropServices, nullptr, CLSCTX_INPROC_SERVER,
                   IID_IAccPropServices, reinterpret_cast<void**>(&services));
  return services;
}

}  // namespace

void SessionWindow::Annotate(const std::wstring& name) {
  // NVDA says the containers the focus has entered, when they are worth
  // saying: a grouping with a name is, a plain window's client area is not
  // (NVDAObjects presentationType).  So the client area of each session is
  // made a grouping named after its folder, and Ctrl+Tab is heard as the
  // folder and then the box -- where before it was the box alone, the same
  // for every session (2026-10-07, from use).
  //
  // Dynamic annotation and not an IAccessible of our own: two properties on
  // the proxy Windows already gives the window, and everything else about it
  // stays as it was.
  IAccPropServices* services = PropServices();
  if (services == nullptr) return;
  // Zeroed rather than VariantInit, which is the same thing and would pull
  // in oleaut32 for one call.
  VARIANT role = {};
  role.vt = VT_I4;
  role.lVal = ROLE_SYSTEM_GROUPING;
  services->SetHwndProp(hwnd_, OBJID_CLIENT, CHILDID_SELF, PROPID_ACC_ROLE,
                        role);
  services->SetHwndPropStr(hwnd_, OBJID_CLIENT, CHILDID_SELF, PROPID_ACC_NAME,
                           name.c_str());
  services->Release();
}

bool SessionWindow::Open(HWND mdiClient, HINSTANCE instance, StatusBar* bar,
                         std::unique_ptr<agent::Backend> backend,
                         const agent::StartOptions& options, Closed closed) {
  bar_ = bar;
  closed_ = std::move(closed);
  // Maximized, always: one session fills the frame and Ctrl+Tab goes to the
  // next one.  Overlapping windows are something to be seen.
  //
  // No WS_SYSMENU.  A maximized child with a system menu puts its icon and
  // three buttons into the frame's menu bar, and the first thing a reader
  // lands on after Alt is then an unnamed item before "Súbor".  Nor would
  // it buy what it seems to: NVDA names a window the focus enters only when
  // it has a system menu, but the client takes WS_SYSMENU away from the
  // maximized child -- measured, it was on the restored ones only.  Which
  // session this is, Annotate says instead.  The client has
  // MDIS_ALLCHILDSTYLES, or it would add the menu back.
  //
  // WS_MAXIMIZEBOX, or the client refuses to maximize the child: only the
  // first one came up maximized -- WS_MAXIMIZE at creation -- and Ctrl+Tab,
  // Ctrl+F4 and WM_MDIMAXIMIZE all left the next one restored, with the
  // folder gone from the frame's title (measured 2026-10-07).  With the bit
  // the client carries the maximized state over by itself.
  if (!CreateMdiChild(L"" APP_NAME L"Session", mdiClient, L"",
                      WS_MAXIMIZE | WS_MAXIMIZEBOX | WS_CLIPCHILDREN |
                          WS_CAPTION | WS_THICKFRAME | WS_VISIBLE)) {
    return false;
  }
  Annotate(FolderName(options.projectDir));
  pane_ = std::make_unique<SessionPane>();
  if (!pane_->Create(hwnd_, instance)) {
    SendMessageW(mdiClient, WM_MDIDESTROY, reinterpret_cast<WPARAM>(hwnd_), 0);
    return false;
  }
  RECT client = {};
  GetClientRect(hwnd_, &client);
  pane_->Layout(client.right, client.bottom);
  // WM_MDIACTIVATE came while the child was being made, before there was a
  // pane to tell.  A child just made is the active one.
  Activated(true);
  if (!pane_->Start(std::move(backend), options)) {
    pane_.reset();
    SendMessageW(mdiClient, WM_MDIDESTROY, reinterpret_cast<WPARAM>(hwnd_), 0);
    return false;
  }
  return true;
}

void SessionWindow::Activated(bool active) {
  if (!pane_) return;
  pane_->SetActive(active);
  pane_->SetStatusBar(active ? bar_ : nullptr);
}

LRESULT SessionWindow::Default(UINT message, WPARAM wParam,
                               LPARAM lParam) const {
  return DefMDIChildProcW(hwnd_, message, wParam, lParam);
}

LRESULT SessionWindow::HandleMessage(UINT message, WPARAM wParam,
                                     LPARAM lParam) {
  switch (message) {
    case WM_SIZE:
      if (pane_) pane_->Layout(LOWORD(lParam), HIWORD(lParam));
      // DefMDIChildProcW must see it too: it keeps the frame's maximized
      // state and its menu bar in step.
      break;

    case WM_MDIACTIVATE:
      // Sent to both: the one losing it has its handle in wParam, the one
      // gaining it in lParam.
      if (reinterpret_cast<HWND>(lParam) == hwnd_) Activated(true);
      if (reinterpret_cast<HWND>(wParam) == hwnd_) Activated(false);
      return 0;

    case WM_SETFOCUS: {
      // DefMDIChildProcW first -- it makes this the active child -- and then
      // where the focus really belongs.  Not while a modal box has the frame
      // disabled: see MainWindow's WM_ACTIVATE.
      const LRESULT result = Default(message, wParam, lParam);
      if (pane_ && IsWindowEnabled(GetAncestor(hwnd_, GA_ROOT))) {
        pane_->RestoreFocus();
      }
      return result;
    }

    case kMsgDrain:
      if (pane_) pane_->OnDrain();
      return 0;

    case kMsgPermission:
    case kMsgQuestion:
      // A session behind another one asks too, and the box has to say which
      // session it is about.  It cannot, so the session comes to the front
      // first and the box goes up over it: what is behind the box is what it
      // is asking about.
      SendMessageW(GetParent(hwnd_), WM_MDIACTIVATE,
                   reinterpret_cast<WPARAM>(hwnd_), 0);
      if (!pane_) return 0;
      return message == kMsgPermission ? pane_->OnPermission(lParam)
                                       : pane_->OnQuestion(lParam);

    case kMsgHistory:
      if (pane_) pane_->OnHistoryPosted();
      return 0;

    case kMsgQuestionByPrompt:
      if (pane_) pane_->OnQuestionByPrompt();
      return 0;

    case WM_CLOSE:
      // The backend first, while there is still a window for its reader
      // thread to post to; then the client destroys the child, which is the
      // one way it keeps its list and its window menu right.
      pane_.reset();
      SendMessageW(GetParent(hwnd_), WM_MDIDESTROY,
                   reinterpret_cast<WPARAM>(hwnd_), 0);
      return 0;

    case WM_DESTROY:
      if (closed_) closed_();
      return 0;

    case WM_NCDESTROY:
      // Annotations are kept by handle, and a handle is reused.
      if (IAccPropServices* services = PropServices()) {
        services->ClearHwndProps(hwnd_, OBJID_CLIENT, CHILDID_SELF,
                                 const_cast<MSAAPROPID*>(kAnnotated),
                                 ARRAYSIZE(kAnnotated));
        services->Release();
      }
      break;

    default:
      break;
  }
  return Default(message, wParam, lParam);
}

}  // namespace ui
