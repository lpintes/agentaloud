#ifndef UI_FIND_DIALOG_H
#define UI_FIND_DIALOG_H

// Ctrl+F: what to look for in the transcript.
//
// Enter searches and the dialog stays open, the way Notepad's does, and that
// is about speech more than habit.  A dialog that closed on Enter answered
// with NVDA's announcement of the focus coming back -- the frame's title, the
// session, the field's name, and only then the line with the match -- and
// nothing of ours could be said in its place: behind a closing dialog speech
// dies (invariant 6).  With the focus staying put, the caller says the line
// itself, and the next Enter goes on to the next match.  The long
// announcement is left to Esc, where the reader is the one leaving.

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <functional>
#include <string>

#include "win/dialog.h"

namespace ui {

class FindDialog : public win::Dialog {
 public:
  // `text` is in and out: the last search, and what was searched for.
  // `search` moves the caret to the next match and says what happened.
  FindDialog(std::wstring* text,
             std::function<void(const std::wstring&)> search);

 protected:
  bool OnInit() override;
  bool OnOk() override;

 private:
  std::wstring* text_;
  std::function<void(const std::wstring&)> search_;
};

}  // namespace ui

#endif
