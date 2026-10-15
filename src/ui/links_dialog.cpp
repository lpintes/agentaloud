#include "ui/links_dialog.h"

#include <shellapi.h>

#include <utility>

#include "i18n/i18n.h"
#include "ui/dialog_texts.h"
#include "ui/resource.h"
#include "win/dialog.h"

namespace ui {

namespace {

bool IsWebAddress(const std::wstring& url) {
  auto starts = [&url](const wchar_t* prefix) {
    const size_t length = wcslen(prefix);
    return url.size() > length &&
           CompareStringOrdinal(url.c_str(), static_cast<int>(length), prefix,
                                static_cast<int>(length), TRUE) == CSTR_EQUAL;
  };
  return starts(L"https://") || starts(L"http://");
}

class LinksDialog : public win::Dialog {
 public:
  LinksDialog(std::vector<model::Link> links,
              const std::function<void(const std::wstring&)>& announce)
      : links_(std::move(links)), announce_(announce) {}

 protected:
  bool OnInit() override {
    LocalizeDialog(hwnd_, IDD_LINKS);
    HWND list = Item(IDC_LINKS_LIST);
    for (const model::Link& link : links_) {
      // The address stays in the row even with a title: it is what says
      // where the browser will go, and a model's title can be wrong.
      const std::wstring row =
          link.title.empty()
              ? link.url
              : i18n::Format(i18n::Str::kLinkRow, {link.title, link.url});
      SendMessageW(list, LB_ADDSTRING, 0, reinterpret_cast<LPARAM>(row.c_str()));
    }
    SendMessageW(list, LB_SETCURSEL, 0, 0);
    return false;  // the list is the first tab stop
  }

  bool OnCommand(int id, int notification) override {
    // A double click is Enter for whoever uses a mouse.
    if (id == IDC_LINKS_LIST && notification == LBN_DBLCLK) {
      if (OnOk()) EndDialog(hwnd_, IDOK);
      return true;
    }
    return false;
  }

  bool OnOk() override {
    const LRESULT selected =
        SendMessageW(Item(IDC_LINKS_LIST), LB_GETCURSEL, 0, 0);
    if (selected == LB_ERR || static_cast<size_t>(selected) >= links_.size()) {
      return false;
    }
    if (OpenLink(links_[static_cast<size_t>(selected)].url)) return true;
    announce_(i18n::Text(i18n::Str::kLinkOpenFailed));
    return false;
  }

 private:
  std::vector<model::Link> links_;
  const std::function<void(const std::wstring&)>& announce_;
};

}  // namespace

bool OpenLink(const std::wstring& url) {
  if (!IsWebAddress(url)) return false;
  const auto result = reinterpret_cast<INT_PTR>(ShellExecuteW(
      nullptr, L"open", url.c_str(), nullptr, nullptr, SW_SHOWNORMAL));
  return result > 32;
}

void ShowLinks(HWND owner, std::vector<model::Link> links,
               const std::function<void(const std::wstring&)>& announce) {
  LinksDialog dialog(std::move(links), announce);
  dialog.ShowModal(owner, IDD_LINKS);
}

}  // namespace ui
