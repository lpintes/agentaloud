#include "ui/about_dialog.h"

#include <shellapi.h>

#include <string>

#include "app_name.h"
#include "i18n/i18n.h"
#include "ui/dialog_texts.h"
#include "ui/resource.h"
#include "version.h"
#include "win/dialog.h"

namespace ui {
namespace {

class AboutDialog : public win::Dialog {
 protected:
  bool OnInit() override {
    LocalizeDialog(hwnd_, IDD_ABOUT);
    SetTextLines(IDC_ABOUT_TEXT,
                 i18n::Format(i18n::Str::kAboutText,
                              {std::wstring(L"" APP_NAME),
                               std::wstring(version::Current()),
                               std::wstring(L"" APP_URL)}));
    return false;  // the box is the first tab stop
  }

  bool OnCommand(int id, int) override {
    if (id != IDC_ABOUT_PAGE) return false;
    // Stays open, like Kopírovať in the session details: the browser takes
    // the foreground, and coming back should find the dialog where it was.
    ShellExecuteW(nullptr, L"open", L"" APP_URL, nullptr, nullptr,
                  SW_SHOWNORMAL);
    return true;
  }
};

}  // namespace

void ShowAbout(HWND owner) {
  AboutDialog dialog;
  dialog.ShowModal(owner, IDD_ABOUT);
}

}  // namespace ui
