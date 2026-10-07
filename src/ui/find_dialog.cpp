#include "ui/find_dialog.h"

#include <utility>

#include "ui/dialog_texts.h"
#include "ui/resource.h"

namespace ui {

FindDialog::FindDialog(std::wstring* text,
                       std::function<void(const std::wstring&)> search)
    : text_(text), search_(std::move(search)) {}

bool FindDialog::OnInit() {
  LocalizeDialog(hwnd_, IDD_FIND);
  // The last search comes back, and the dialog manager selects it all when
  // it puts the focus there: Enter repeats it, typing replaces it.
  SetText(IDC_FIND_TEXT, *text_);
  return false;
}

// Never closes: see the header.
bool FindDialog::OnOk() {
  const std::wstring text = GetText(IDC_FIND_TEXT);
  if (text.empty()) return false;
  *text_ = text;
  search_(text);
  return false;
}

}  // namespace ui
