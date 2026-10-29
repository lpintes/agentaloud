#include "ui/model_dialog.h"

#include "i18n/i18n.h"
#include "model/utf.h"
#include "ui/dialog_texts.h"
#include "ui/resource.h"
#include "win/dialog.h"

namespace ui {

namespace {

// The name first, because it is what is being looked for and what a screen
// reader reads first; the id is left out, it is the CLI's word for the same
// thing.  The descriptions are a few words each, so they go in whole.
std::wstring RowFor(const agent::ModelChoice& choice) {
  std::wstring row = model::Utf16FromUtf8(choice.name);
  if (!choice.description.empty()) {
    row += L" — " + model::Utf16FromUtf8(choice.description);
  }
  return row;
}

class ModelDialog : public win::Dialog {
 public:
  explicit ModelDialog(const ModelPick& pick) : pick_(pick) {}

  const std::string& chosen() const { return chosen_; }

 protected:
  bool OnInit() override {
    LocalizeDialog(hwnd_, IDD_MODELS);
    const std::vector<agent::ModelChoice>& catalog = *pick_.catalog;
    const std::wstring count = std::to_wstring(catalog.size());
    SetText(IDC_MODELS_LIST_LABEL,
            i18n::Format(pick_.nextTurnOnly ? i18n::Str::kModelListLabelNextTurn
                                            : i18n::Str::kModelListLabel,
                         {count}));
    HWND list = Item(IDC_MODELS_LIST);
    int selected = 0;
    for (size_t i = 0; i < catalog.size(); ++i) {
      const std::wstring row = RowFor(catalog[i]);
      SendMessageW(list, LB_ADDSTRING, 0, reinterpret_cast<LPARAM>(row.c_str()));
      if (catalog[i].id == pick_.current) selected = static_cast<int>(i);
    }
    SendMessageW(list, LB_SETCURSEL, static_cast<WPARAM>(selected), 0);
    return false;  // the list is the first tab stop
  }

  bool OnCommand(int id, int notification) override {
    // A double click is Enter for whoever uses a mouse.
    if (id == IDC_MODELS_LIST && notification == LBN_DBLCLK) {
      if (OnOk()) EndDialog(hwnd_, IDOK);
      return true;
    }
    return false;
  }

  bool OnOk() override {
    const LRESULT selected =
        SendMessageW(Item(IDC_MODELS_LIST), LB_GETCURSEL, 0, 0);
    if (selected == LB_ERR ||
        static_cast<size_t>(selected) >= pick_.catalog->size()) {
      return false;
    }
    const std::string& id = (*pick_.catalog)[static_cast<size_t>(selected)].id;
    if (!pick_.set(id)) {
      pick_.announce(i18n::Text(i18n::Str::kModelNotSent));
      return false;
    }
    chosen_ = id;
    return true;
  }

 private:
  const ModelPick& pick_;
  std::string chosen_;
};

}  // namespace

std::string PickModel(HWND owner, const ModelPick& pick) {
  if (pick.catalog == nullptr || pick.catalog->empty()) return {};
  ModelDialog dialog(pick);
  if (dialog.ShowModal(owner, IDD_MODELS) != IDOK) return {};
  return dialog.chosen();
}

}  // namespace ui
