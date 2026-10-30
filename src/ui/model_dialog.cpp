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
    for (const agent::ModelChoice& choice : catalog) {
      const std::wstring row = RowFor(choice);
      SendMessageW(list, LB_ADDSTRING, 0, reinterpret_cast<LPARAM>(row.c_str()));
    }
    const agent::ModelChoice* current = agent::FindModel(catalog, pick_.current);
    if (current != nullptr) sentModel_ = current - catalog.data();
    shownModel_ = current != nullptr ? sentModel_ : 0;
    SendMessageW(list, LB_SETCURSEL, static_cast<WPARAM>(shownModel_), 0);
    ShowEfforts(pick_.effort);
    return false;  // the list is the first tab stop
  }

  bool OnCommand(int id, int notification) override {
    if (id == IDC_MODELS_LIST && notification == LBN_SELCHANGE) {
      const LRESULT selected = SendMessageW(Item(id), LB_GETCURSEL, 0, 0);
      if (selected == LB_ERR) return true;
      const std::vector<agent::ModelChoice>& catalog = *pick_.catalog;
      const std::string stage = agent::NearestEffort(
          catalog[static_cast<size_t>(shownModel_)].efforts,
          catalog[static_cast<size_t>(selected)].efforts, SelectedEffort());
      shownModel_ = selected;
      ShowEfforts(stage);
      return true;
    }
    // A double click is Enter for whoever uses a mouse.
    if ((id == IDC_MODELS_LIST || id == IDC_MODELS_EFFORT) &&
        notification == LBN_DBLCLK) {
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
    if (selected != sentModel_) {
      const std::string& id = (*pick_.catalog)[static_cast<size_t>(selected)].id;
      if (!pick_.set(id)) {
        pick_.announce(i18n::Text(i18n::Str::kModelNotSent));
        return false;
      }
      // Remembered at once: a failure of the effort below leaves the dialog
      // open, and this model has been sent whatever is done with it then.
      chosen_ = id;
      sentModel_ = selected;
    }
    const std::string stage = SelectedEffort();
    if (pick_.setEffort && stage != pick_.effort && !pick_.setEffort(stage)) {
      pick_.announce(i18n::Text(i18n::Str::kEffortNotSent));
      return false;
    }
    return true;
  }

 private:
  // Fills the second list with the stages of the highlighted model, `stage`
  // highlighted.  The first row is the model's default, which is no stage.
  void ShowEfforts(const std::string& stage) {
    HWND list = Item(IDC_MODELS_EFFORT);
    SendMessageW(list, LB_RESETCONTENT, 0, 0);
    const std::vector<std::string>& efforts =
        (*pick_.catalog)[static_cast<size_t>(shownModel_)].efforts;
    const bool any = pick_.setEffort && !efforts.empty();
    EnableWindow(list, any);
    if (!any) {
      SetText(IDC_MODELS_EFFORT_LABEL, i18n::Text(i18n::Str::kEffortNone));
      return;
    }
    SetText(IDC_MODELS_EFFORT_LABEL,
            i18n::Format(pick_.effortNextTurnOnly
                             ? i18n::Str::kEffortListLabelNextTurn
                             : i18n::Str::kEffortListLabel,
                         {std::to_wstring(efforts.size() + 1)}));
    const std::wstring first = i18n::Text(i18n::Str::kEffortDefault);
    SendMessageW(list, LB_ADDSTRING, 0, reinterpret_cast<LPARAM>(first.c_str()));
    int selected = 0;
    for (size_t i = 0; i < efforts.size(); ++i) {
      const std::wstring row = model::Utf16FromUtf8(efforts[i]);
      SendMessageW(list, LB_ADDSTRING, 0, reinterpret_cast<LPARAM>(row.c_str()));
      if (efforts[i] == stage) selected = static_cast<int>(i) + 1;
    }
    SendMessageW(list, LB_SETCURSEL, static_cast<WPARAM>(selected), 0);
  }

  // Empty for the default row and for a model without stages.
  std::string SelectedEffort() {
    const std::vector<std::string>& efforts =
        (*pick_.catalog)[static_cast<size_t>(shownModel_)].efforts;
    const LRESULT row =
        SendMessageW(Item(IDC_MODELS_EFFORT), LB_GETCURSEL, 0, 0);
    if (row == LB_ERR || row < 1 || static_cast<size_t>(row) > efforts.size()) {
      return {};
    }
    return efforts[static_cast<size_t>(row) - 1];
  }

  const ModelPick& pick_;
  std::string chosen_;
  // The row of the model in force, -1 when the catalog does not have it: then
  // whichever row is confirmed is a change.
  LRESULT sentModel_ = -1;
  // The row whose stages the second list holds.
  LRESULT shownModel_ = 0;
};

}  // namespace

std::string PickModel(HWND owner, const ModelPick& pick) {
  if (pick.catalog == nullptr || pick.catalog->empty()) return {};
  ModelDialog dialog(pick);
  dialog.ShowModal(owner, IDD_MODELS);
  return dialog.chosen();
}

}  // namespace ui
