#ifndef UI_STATUS_BAR_H
#define UI_STATUS_BAR_H

// The strip along the bottom of the window.
//
// It is a standard msctls_statusbar32 on purpose.  NVDA does not read a status
// bar when it changes -- it reads it on NVDA+End, on request -- and that is
// exactly the property wanted here.  Everything that a terminal keeps shouting
// at you goes in this bar: the state of the turn, the model, the project, how
// close the rate limit is.  None of it is worth a sentence read aloud in the
// middle of an answer, and all of it is worth being able to ask for.
//
// What does NOT go here: cost and token counts.  They belong in the session
// details dialog, because they are not something you glance at.

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <string>

namespace ui {

class StatusBar {
 public:
  // The four fields, in the order they stand on screen.
  enum Field {
    kTurn = 0,     // pripravene / pracujem / hotove
    kModel = 1,    // model, and the permission mode it is running under
    kProject = 2,  // which directory this session is in
    kLimit = 3,    // rate limit, empty until the CLI says something about one
    kFieldCount = 4,
  };

  bool Create(HWND host, HINSTANCE instance);
  void Set(Field field, const std::wstring& text);

  // Called from the host's WM_SIZE.  The bar sizes itself, but the parts have
  // to be laid out again, and the height it took is what the rest of the
  // window has to keep clear of.
  int Resize(int width);
  int height() const;

  HWND handle() const { return bar_; }

 private:
  HWND bar_ = nullptr;
};

}  // namespace ui

#endif
