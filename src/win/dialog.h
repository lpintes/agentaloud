#ifndef WIN_DIALOG_H
#define WIN_DIALOG_H

// A real dialog, from a resource template, through DialogBoxParamW -- not a
// window that merely looks like one.  The difference is not cosmetic and it is
// the whole reason this file exists:
//
//   * MSAA/UIA reports the role "dialog", so NVDA announces it on open and
//     reads the title and the focused control.  A plain WS_POPUP window --
//     what Delphi forms were -- announces nothing and has to be explored.
//   * Tab order, WS_GROUP arrow navigation, Esc for cancel, Enter for the
//     default button and the "&" mnemonics all come from the dialog manager.
//   * The layout is in dialog units in the .rc, so no coordinate is computed
//     here and the whole thing scales with the dialog font.
//
// Nothing in win/ knows about the emulator.

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <string>

namespace win {

class Dialog {
 public:
  Dialog() = default;
  virtual ~Dialog() = default;
  Dialog(const Dialog&) = delete;
  Dialog& operator=(const Dialog&) = delete;

  // Returns IDOK, IDCANCEL, or -1 if the template could not be loaded.
  INT_PTR ShowModal(HWND owner, int templateId);

  HWND handle() const { return hwnd_; }

 protected:
  // Set the controls up from the caller's state.  Return false to let the
  // dialog manager pick the first tab stop for focus, which is nearly always
  // what is wanted; return true only after focusing something else yourself.
  virtual bool OnInit() { return false; }
  // Return true when handled.  IDOK and IDCANCEL are handled by the base.
  virtual bool OnCommand(int id, int notification) { return false; }
  // Read the controls back before the dialog closes with IDOK.  Return false
  // to keep it open, having said why.
  virtual bool OnOk() { return true; }

  HWND Item(int id) const { return GetDlgItem(hwnd_, id); }
  void SetChecked(int id, bool on) const;
  bool IsChecked(int id) const;
  void SetText(int id, const std::wstring& text) const;
  std::wstring GetText(int id) const;
  void SetEnabled(int id, bool on) const;

  HWND hwnd_ = nullptr;

 private:
  static INT_PTR CALLBACK DlgProc(HWND dialog, UINT message, WPARAM wParam,
                                  LPARAM lParam);
};

// The shell's own folder picker.  Returns an empty string when cancelled,
// which is an answer and not an error.  Requires COM to be initialised on the
// calling thread.
std::wstring PickFolder(HWND owner, const wchar_t* title);

// The same, but for choosing where something is to be *written*: the folder
// named need not exist yet, and the caller creates it.  The picker above
// insists on an existing one, which turns "save this here" into two chores --
// go and make a folder somewhere else, then come back and pick it.
//
// It is a save dialog in folder mode, and that is the one combination the
// shell offers with a name field in it.  A name field is also the accessible
// half of this: typing a name is one edit box a screen reader reads out,
// where "New folder" on a toolbar has to be hunted for.
//
// suggestedName pre-fills that field and may be null.
std::wstring PickFolderToCreate(HWND owner, const wchar_t* title,
                                const wchar_t* suggestedName);

}  // namespace win

#endif
