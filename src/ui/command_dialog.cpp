#include "ui/command_dialog.h"

#include <commctrl.h>

#include <string>

#include "app_name.h"
#include "i18n/i18n.h"
#include "model/utf.h"
#include "ui/dialog_texts.h"
#include "ui/resource.h"
#include "win/dialog.h"

namespace ui {

using i18n::Str;

namespace {

// As much of a description as goes on one list line.  Read aloud, a line is
// one thing to listen to, and the longest description on this account is 1436
// characters -- nearly a page per arrow key.  The whole of it is one Tab away
// in the box below, for the one command being considered.
constexpr size_t kLineDescription = 90;

std::wstring Lowered(std::wstring text) {
  // CharLowerBuffW and not tolower: the filter is typed in whatever the reader
  // types, and "Č" has to match "č" the same way "C" matches "c".
  if (!text.empty()) {
    CharLowerBuffW(text.data(), static_cast<DWORD>(text.size()));
  }
  return text;
}

// A list box item is one line whatever is in it -- a lone newline comes out as
// a box glyph and reads aloud as nothing.
std::wstring OneLine(std::wstring text) {
  for (wchar_t& character : text) {
    if (character == L'\n' || character == L'\r' || character == L'\t') {
      character = L' ';
    }
  }
  return text;
}

std::wstring Shorten(const std::wstring& text, size_t limit) {
  if (text.size() <= limit) return text;
  size_t cut = text.rfind(L' ', limit);
  if (cut == std::wstring::npos || cut < limit / 2) cut = limit;
  return text.substr(0, cut) + L"…";
}

// What one command looks like in the list: the name first, because that is
// what is being looked for and what a screen reader reads first, then the
// argument hint, then as much description as fits.
std::wstring LineFor(const agent::SlashCommand& command) {
  std::wstring line = L"/" + model::Utf16FromUtf8(command.name);
  if (!command.argumentHint.empty()) {
    line += L" " + model::Utf16FromUtf8(command.argumentHint);
  }
  if (!command.description.empty()) {
    line += L" — " + Shorten(OneLine(model::Utf16FromUtf8(command.description)),
                             kLineDescription);
  }
  return OneLine(std::move(line));
}

// The filter matches the NAME and the aliases, and deliberately not the
// description.  A description is a paragraph, so matching in it means a word
// typed for one command pulls in a dozen others that merely mention it, and
// the list stops answering the question that was asked.  The aliases are in
// because a plugin's command is offered under its long name
// ("mattpocock-skills:tdd") and remembered under the short one.
bool Matches(const agent::SlashCommand& command, const std::wstring& filter) {
  if (filter.empty()) return true;
  if (Lowered(model::Utf16FromUtf8(command.name)).find(filter) !=
      std::wstring::npos) {
    return true;
  }
  for (const std::string& alias : command.aliases) {
    if (Lowered(model::Utf16FromUtf8(alias)).find(filter) !=
        std::wstring::npos) {
      return true;
    }
  }
  return false;
}

class CommandDialog : public win::Dialog {
 public:
  explicit CommandDialog(const std::vector<agent::SlashCommand>& commands)
      : commands_(commands) {}

  const agent::SlashCommand& chosen() const { return commands_[chosenIndex_]; }

 protected:
  bool OnInit() override;
  bool OnCommand(int id, int notification) override;
  bool OnOk() override;

 private:
  // Rebuilds the list from the filter box and says how many are left in the
  // list's label.  The count goes in the label because that is the accessible
  // name of the list, so a screen reader reads it on the way in -- filtering
  // happens in another control and nothing announces its effect.
  void Refill();
  // The index into commands_ of the item at a list position, or -1.
  int CommandAt(int position) const;
  // Puts the whole description of the highlighted command in the box below.
  void ShowDetail();
  static LRESULT CALLBACK FilterProc(HWND window, UINT message, WPARAM wParam,
                                     LPARAM lParam, UINT_PTR id,
                                     DWORD_PTR data);

  const std::vector<agent::SlashCommand>& commands_;
  size_t chosenIndex_ = 0;
};

bool CommandDialog::OnInit() {
  LocalizeDialog(hwnd_, IDD_COMMANDS);
  Refill();
  // The filter box is subclassed so that Down leads into the list.  Without it
  // the way from typing to choosing is Tab, which is one key more than the
  // habit every filtered list in Windows has taught.
  SetWindowSubclass(Item(IDC_CMD_FILTER), FilterProc, 1,
                    reinterpret_cast<DWORD_PTR>(this));
  return false;  // the dialog manager puts the focus in the filter box
}

int CommandDialog::CommandAt(int position) const {
  if (position < 0) return -1;
  const LRESULT data =
      SendDlgItemMessageW(hwnd_, IDC_CMD_LIST, LB_GETITEMDATA,
                          static_cast<WPARAM>(position), 0);
  if (data == LB_ERR) return -1;
  return static_cast<int>(data);
}

void CommandDialog::Refill() {
  const std::wstring filter = Lowered(GetText(IDC_CMD_FILTER));
  SendDlgItemMessageW(hwnd_, IDC_CMD_LIST, LB_RESETCONTENT, 0, 0);
  size_t shown = 0;
  for (size_t i = 0; i < commands_.size(); ++i) {
    if (!Matches(commands_[i], filter)) continue;
    const std::wstring line = LineFor(commands_[i]);
    const LRESULT at = SendDlgItemMessageW(
        hwnd_, IDC_CMD_LIST, LB_ADDSTRING, 0,
        reinterpret_cast<LPARAM>(line.c_str()));
    if (at == LB_ERR || at == LB_ERRSPACE) continue;
    // The list position is not the index into commands_ as soon as anything is
    // filtered out, so the index travels with the item rather than being
    // recomputed from where it landed.
    SendDlgItemMessageW(hwnd_, IDC_CMD_LIST, LB_SETITEMDATA,
                        static_cast<WPARAM>(at),
                        static_cast<LPARAM>(i));
    ++shown;
  }
  SetText(IDC_CMD_LIST_LABEL,
          shown == commands_.size()
              ? i18n::Format(Str::kCmdListLabel, {std::to_wstring(shown)})
              : i18n::Format(Str::kCmdListLabelFiltered,
                             {std::to_wstring(shown),
                              std::to_wstring(commands_.size())}));
  // Nothing is highlighted after a refill, so the box below has nothing to
  // show either.  Emptying it matters: a description left over from a command
  // the filter has since thrown out is a description of something that is no
  // longer on screen.
  ShowDetail();
}

void CommandDialog::ShowDetail() {
  const int index = CommandAt(static_cast<int>(
      SendDlgItemMessageW(hwnd_, IDC_CMD_LIST, LB_GETCURSEL, 0, 0)));
  if (index < 0) {
    SetText(IDC_CMD_DETAIL, L"");
    return;
  }
  const agent::SlashCommand& command = commands_[static_cast<size_t>(index)];
  std::wstring text;
  if (!command.argumentHint.empty()) {
    text += i18n::Format(Str::kCmdArguments,
                         {model::Utf16FromUtf8(command.argumentHint)}) +
            L"\n";
  }
  if (!command.aliases.empty()) {
    std::wstring aliases;
    for (size_t i = 0; i < command.aliases.size(); ++i) {
      if (i) aliases += L", ";
      aliases += L"/" + model::Utf16FromUtf8(command.aliases[i]);
    }
    text += i18n::Format(Str::kCmdAliases, {aliases}) + L"\n";
  }
  text += model::Utf16FromUtf8(command.description);
  SetTextLines(IDC_CMD_DETAIL, text);
}

bool CommandDialog::OnCommand(int id, int notification) {
  if (id == IDC_CMD_FILTER && notification == EN_CHANGE) {
    Refill();
    return true;
  }
  if (id == IDC_CMD_LIST && notification == LBN_SELCHANGE) {
    ShowDetail();
    return true;
  }
  // A double click is the mouse's Enter and has to mean the same thing.
  if (id == IDC_CMD_LIST && notification == LBN_DBLCLK) {
    if (OnOk()) EndDialog(hwnd_, IDOK);
    return true;
  }
  return false;
}

bool CommandDialog::OnOk() {
  int index = CommandAt(static_cast<int>(
      SendDlgItemMessageW(hwnd_, IDC_CMD_LIST, LB_GETCURSEL, 0, 0)));
  // Enter straight out of the filter box takes the first match, which is what
  // a filter box is for: type enough of the name, press Enter.  Only when
  // there is exactly nothing to take does the dialog refuse.
  if (index < 0) index = CommandAt(0);
  if (index < 0) {
    const std::wstring title =
        std::wstring(L"" APP_NAME L" — ") + i18n::Text(Str::kDlgCmdCaption);
    MessageBoxW(hwnd_, i18n::Text(Str::kCmdNoMatch), title.c_str(),
                MB_OK | MB_ICONINFORMATION);
    SetFocus(Item(IDC_CMD_FILTER));
    return false;
  }
  chosenIndex_ = static_cast<size_t>(index);
  return true;
}

LRESULT CALLBACK CommandDialog::FilterProc(HWND window, UINT message,
                                           WPARAM wParam, LPARAM lParam,
                                           UINT_PTR id, DWORD_PTR data) {
  CommandDialog* dialog = reinterpret_cast<CommandDialog*>(data);
  // The dialog manager asks every control which keys it wants before handing
  // one over, and an arrow it is not asked for it keeps for group navigation
  // -- which in a group of one control means the key does nothing at all and
  // says nothing about it.  So the box asks for the arrows explicitly rather
  // than relying on what a plain EDIT answers.
  if (message == WM_GETDLGCODE) {
    return DefSubclassProc(window, message, wParam, lParam) | DLGC_WANTARROWS;
  }
  if (message == WM_KEYDOWN && wParam == VK_DOWN) {
    const HWND list = dialog->Item(IDC_CMD_LIST);
    if (SendMessageW(list, LB_GETCOUNT, 0, 0) > 0) {
      // Highlight the first entry on the way in, so that the list speaks the
      // moment it takes the focus rather than announcing an empty selection.
      if (SendMessageW(list, LB_GETCURSEL, 0, 0) == LB_ERR) {
        SendMessageW(list, LB_SETCURSEL, 0, 0);
        dialog->ShowDetail();
      }
      SetFocus(list);
    }
    return 0;
  }
  if (message == WM_DESTROY) RemoveWindowSubclass(window, FilterProc, id);
  return DefSubclassProc(window, message, wParam, lParam);
}

}  // namespace

bool PickCommand(HWND owner, const std::vector<agent::SlashCommand>& commands,
                 agent::SlashCommand* chosen) {
  if (commands.empty()) return false;
  CommandDialog dialog(commands);
  if (dialog.ShowModal(owner, IDD_COMMANDS) != IDOK) return false;
  *chosen = dialog.chosen();
  return true;
}

}  // namespace ui
