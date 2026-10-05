#include "ui/status_bar.h"

#include <commctrl.h>
#include <uxtheme.h>

namespace ui {
namespace {

constexpr int kIdStatusBar = 1005;

}  // namespace

bool StatusBar::Create(HWND host, HINSTANCE instance) {
  // SBARS_SIZEGRIP is left off: the grip is a mouse affordance and it eats the
  // right-hand end of the last field, which here holds text.
  bar_ = CreateWindowExW(0, STATUSCLASSNAMEW, L"", WS_CHILD | WS_VISIBLE, 0, 0,
                         0, 0, host, reinterpret_cast<HMENU>(kIdStatusBar),
                         instance, nullptr);
  if (bar_ == nullptr) return false;
  // Unthemed, and not for looks.  NVDA reads a part whose text is empty by its
  // display model -- what it last saw drawn there -- and the turn field is
  // empty between turns, so NVDA+End must find nothing there and start with
  // the model.  The classic bar wipes a part with FillRect, which NVDA hooks;
  // since the Common Controls 6 manifest (app.manifest, for the updater) the
  // bar is drawn through the theme instead, the old "pracujem" stayed in the
  // model and was read after every turn while the control itself was empty
  // (measured with WM_GETTEXT, 5. 10. 2026).  Fails silently: the bar looks
  // right and is read wrong.
  SetWindowTheme(bar_, L"", L"");
  return true;
}

int StatusBar::Resize(int width) {
  if (!bar_) return 0;
  SendMessageW(bar_, WM_SIZE, 0, 0);

  // Widths are the right edge of each part, so they accumulate; the last part
  // ends at -1, which the control reads as "the rest of the bar".
  //
  // Not four equal quarters: the turn field holds one word and the rate limit
  // holds a sentence, and a field too narrow shows its text cut off.  It is
  // read out in full either way, but the cut-off form is what a sighted user
  // reports as a bug.
  const int unit = width > 0 ? width / 20 : 0;
  int edges[kFieldCount] = {unit * 3, unit * 8, unit * 12, -1};
  SendMessageW(bar_, SB_SETPARTS, kFieldCount,
               reinterpret_cast<LPARAM>(edges));
  return height();
}

int StatusBar::height() const {
  if (!bar_) return 0;
  RECT rect = {};
  GetWindowRect(bar_, &rect);
  return rect.bottom - rect.top;
}

void StatusBar::Set(Field field, const std::wstring& text) {
  if (!bar_) return;
  SendMessageW(bar_, SB_SETTEXTW, static_cast<WPARAM>(field),
               reinterpret_cast<LPARAM>(text.c_str()));
}

}  // namespace ui
